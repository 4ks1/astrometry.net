/*
  This file is part of the Astrometry.net suite.
  Copyright 2009 Dustin Lang.

  The Astrometry.net suite is free software; you can redistribute
  it and/or modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation, version 2.

  The Astrometry.net suite is distributed in the hope that it will be
  useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with the Astrometry.net suite ; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
*/

#include <stdint.h>

#include "uniformize-catalog.h"
#include "intmap.h"
#include "healpix.h"
#include "healpix-utils.h"
#include "permutedsort.h"
#include "starutil.h"
#include "an-bool.h"
#include "mathutil.h"
#include "errors.h"
#include "log.h"
#include "boilerplate.h"
#include "fitsioutils.h"

struct oh_token {
	int hp;
	int nside;
	int finenside;
};

// Return 1 if the given "hp" is outside the healpix described in "oh_token".
static int outside_healpix(int hp, void* vtoken) {
	struct oh_token* token = vtoken;
	int bighp;
	healpix_convert_nside(hp, token->finenside, token->nside, &bighp);
	return (bighp == token->hp ? 0 : 1);
}

static bool is_duplicate(int hp, double ra, double dec, int Nside,
						 intmap_t* starlists,
						 double* ras, double* decs, double dedupr2) {
	double xyz[3];
	int neigh[9];
	int nn;
	double xyz2[3];
	int j, k;
	radecdeg2xyzarr(ra, dec, xyz);
	// Check this healpix...
	neigh[0] = hp;
	// Check neighbouring healpixes... (+1 is to skip over this hp)
	nn = 1 + healpix_get_neighbours(hp, neigh+1, Nside);
	for (k=0; k<nn; k++) {
		int otherhp = neigh[k];
		bl* lst = intmap_find(starlists, otherhp, FALSE);
		if (!lst)
			continue;
		for (j=0; j<bl_size(lst); j++) {
			int otherindex;
			bl_get(lst, j, &otherindex);
			radecdeg2xyzarr(ras[otherindex], decs[otherindex], xyz2);
			if (!distsq_exceeds(xyz, xyz2, 3, dedupr2))
				return TRUE;
		}
	}
	return FALSE;
}

int uniformize_catalog(fitstable_t* intable, fitstable_t* outtable,
					   const char* racol, const char* deccol,
					   const char* sortcol, bool sort_ascending,
					   // ?  Or do this cut in a separate process?
					   int bighp, int bignside,
					   int nmargin,
					   // uniformization nside.
					   int Nside,
					   double dedup_radius,
					   int nsweeps,
					   char** args, int argc) {
	bool allsky;
	intmap_t* starlists;
	int NHP;
	bool dense = FALSE;
	double dedupr2 = 0.0;
	tfits_type dubl;
	int N;
	int* inorder = NULL;
	int* outorder = NULL;
	int outi;
	double *ra = NULL, *dec = NULL;
	il* myhps = NULL;
	int i,j,k;
	int nkeep = nsweeps;
	int noob = 0;
	int ndup = 0;
	struct oh_token token;
	int* npersweep = NULL;
	qfits_header* outhdr = NULL;

	if (bignside == 0)
		bignside = 1;
	allsky = (bighp == -1);

    if (Nside % bignside) {
        ERROR("Fine healpixelization Nside must be a multiple of the coarse healpixelization Nside");
        return -1;
    }
	if (Nside > HP_MAX_INT_NSIDE) {
		ERROR("Error: maximum healpix Nside = %i", HP_MAX_INT_NSIDE);
		return -1;
	}

	NHP = 12 * Nside * Nside;
	logverb("Healpix Nside: %i, # healpixes on the whole sky: %i\n", Nside, NHP);
	if (!allsky) {
		logverb("Creating index for healpix %i, nside %i\n", bighp, bignside);
		logverb("Number of healpixes: %i\n", ((Nside/bignside)*(Nside/bignside)));
	}
	logverb("Healpix side length: %g arcmin.\n", healpix_side_length_arcmin(Nside));

	dubl = fitscolumn_double_type();
	if (!racol)
		racol = "RA";
	ra = fitstable_read_column(intable, racol, dubl);
	if (!ra) {
		ERROR("Failed to find RA column (%s) in table", racol);
		return -1;
	}
	if (!deccol)
		deccol = "DEC";
	dec = fitstable_read_column(intable, deccol, dubl);
	if (!dec) {
		ERROR("Failed to find DEC column (%s) in table", deccol);
		return -1;
	}

	N = fitstable_nrows(intable);

	// FIXME -- argsort and seek around the input table, and append to
	// starlists in order; OR read from the input table in sequence and
	// sort in the starlists?
	if (sortcol) {
		double *sortval;
		logverb("Sorting by %s...\n", sortcol);
		sortval = fitstable_read_column(intable, sortcol, dubl);
		inorder = permuted_sort(sortval, sizeof(double),
								sort_ascending ? compare_doubles_asc : compare_doubles_desc,
								NULL, N);
		free(sortval);
	}

	token.nside = bignside;
	token.finenside = Nside;
	token.hp = bighp;

	if (!allsky && nmargin) {
		int bigbighp, bighpx, bighpy;
		int ninside;
		il* seeds = il_new(256);
		logverb("Finding healpixes in range...\n");
        healpix_decompose_xy(bighp, &bigbighp, &bighpx, &bighpy, bignside);
		ninside = (Nside/bignside)*(Nside/bignside);
		// Prime the queue with the fine healpixes that are on the
		// boundary of the big healpix.
		// -1 prevents us from double-adding the corners.
		for (i=0; i<((Nside / bignside) - 1); i++) {
			// add (i,0), (i,max), (0,i), and (0,max) healpixes
            int xx = i + bighpx * (Nside / bignside);
            int yy = i + bighpy * (Nside / bignside);
            int y0 =     bighpy * (Nside / bignside);
            int y1 =(1 + bighpy)* (Nside / bignside) - 1;
            int x0 =     bighpx * (Nside / bignside);
            int x1 =(1 + bighpx)* (Nside / bignside) - 1;
            assert(xx < Nside);
            assert(yy < Nside);
            assert(x0 < Nside);
            assert(x1 < Nside);
            assert(y0 < Nside);
            assert(y1 < Nside);
			il_append(seeds, healpix_compose_xy(bigbighp, xx, y0, Nside));
			il_append(seeds, healpix_compose_xy(bigbighp, xx, y1, Nside));
			il_append(seeds, healpix_compose_xy(bigbighp, x0, yy, Nside));
			il_append(seeds, healpix_compose_xy(bigbighp, x1, yy, Nside));
		}
        logmsg("Number of boundary healpixes: %i (Nside/bignside = %i)\n", il_size(seeds), Nside/bignside);

		myhps = healpix_region_search(-1, seeds, Nside, NULL, NULL,
									  outside_healpix, &token, nmargin);
		logmsg("Number of margin healpixes: %i\n", il_size(myhps));
		il_free(seeds);

		il_sort(myhps, TRUE);
		// DEBUG
		il_check_consistency(myhps);
		il_check_sorted_ascending(myhps, TRUE);
	}

	dedupr2 = arcsec2distsq(dedup_radius);
	starlists = intmap_new(sizeof(int32_t), nkeep, 0, dense);

	logverb("Placing stars in grid cells...\n");
	for (i=0; i<N; i++) {
		int hp;
		bl* lst;
		int32_t j32;
		if (inorder)
			j = inorder[i];
		else
			j = i;
		
		hp = radecdegtohealpix(ra[j], dec[j], Nside);
		// in bounds?
		if (myhps) {
			if (outside_healpix(hp, &token) &&
				!il_sorted_contains(myhps, hp)) {
				noob++;
				continue;
			}
		} else if (!allsky) {
			if (outside_healpix(hp, &token)) {
				noob++;
				continue;
			}
		}

		lst = intmap_find(starlists, hp, TRUE);
		// is this list full?
		if (nkeep && (bl_size(lst) >= nkeep))
			// Here we assume we're working in sorted order: once the list is full we're done.
			continue;

		if ((dedupr2 > 0.0) &&
			is_duplicate(hp, ra[j], dec[j], Nside, starlists, ra, dec, dedupr2)) {
			ndup++;
			continue;
		}

		// Add the new star (by index)
		j32 = j;
		bl_append(lst, &j32);
	}
	logverb("%i outside the healpix\n", noob);
	logverb("%i duplicates\n", ndup);

	il_free(myhps);
	myhps = NULL;
	free(inorder);
	inorder = NULL;
	free(ra);
	ra = NULL;
	free(dec);
	dec = NULL;

	outorder = malloc(N * sizeof(int));
	outi = 0;

	npersweep = calloc(nsweeps, sizeof(int));

	for (k=0; k<nsweeps; k++) {
		int starti = outi;
		for (i=0;; i++) {
			bl* lst;
			int hp;
			if (!intmap_get_entry(starlists, i, &hp, &lst))
				break;
			if (bl_size(lst) <= k)
				continue;
			bl_get(lst, k, &j);
			outorder[outi] = j;
			outi++;
		}
		logmsg("Sweep %i: %i stars\n", k+1, outi - starti);
		npersweep[k] = outi - starti;
	}
	intmap_free(starlists);
	starlists = NULL;

	logmsg("Total: %i stars\n", outi);
	N = outi;

	outhdr = fitstable_get_primary_header(outtable);
    if (allsky)
        qfits_header_add(outhdr, "ALLSKY", "T", "All-sky catalog.", NULL);
    boilerplate_add_fits_headers(outhdr);
    qfits_header_add(outhdr, "HISTORY", "This file was generated by the command-line:", NULL, NULL);
    fits_add_args(outhdr, args, argc);
    qfits_header_add(outhdr, "HISTORY", "(end of command line)", NULL, NULL);

    //fits_header_add_double(outhdr, "JITTER", jitter, "cut-an: catalog positional error [arcsec]");
    fits_header_add_int(outhdr, "NSTARS", N, "Number of stars.");
    fits_header_add_int(outhdr, "HEALPIX", bighp, "Healpix covered by this catalog, with Nside=HPNSIDE");
    fits_header_add_int(outhdr, "HPNSIDE", bignside, "Nside of HEALPIX.");
	fits_header_add_int(outhdr, "CUTNSIDE", Nside, "uniformization scale (healpix nside)");
	fits_header_add_int(outhdr, "CUTMARG", nmargin, "margin size, in healpixels");
	//qfits_header_add(outhdr, "CUTBAND", cutband, "band on which the cut was made", NULL);
	fits_header_add_double(outhdr, "CUTDEDUP", dedup_radius, "deduplication radius [arcsec]");
	fits_header_add_int(outhdr, "CUTNSWEP", nsweeps, "number of sweeps");
	//fits_header_add_double(outhdr, "CUTMINMG", minmag, "minimum magnitude");
	//fits_header_add_double(outhdr, "CUTMAXMG", maxmag, "maximum magnitude");
	for (k=0; k<nsweeps; k++) {
		char key[64];
		sprintf(key, "SWEEP%i", (k+1));
        fits_header_add_int(outhdr, key, npersweep[k], "# stars added");
	}
	free(npersweep);

	// Write output.
	fitstable_add_fits_columns_as_struct(intable);
	fitstable_copy_columns(intable, outtable);
	if (fitstable_write_header(outtable)) {
		ERROR("Failed to write output table header");
		return -1;
	}
	logmsg("Writing output...\n");
	logverb("Row size: %i\n", fitstable_row_size(intable));
	if (fitstable_copy_rows_data(intable, outorder, N, outtable)) {
		ERROR("Failed to copy rows from input table to output");
		return -1;
	}
	if (fitstable_fix_header(outtable)) {
		ERROR("Failed to fix output table header");
		return -1;
	}
	free(outorder);
	return 0;
}
