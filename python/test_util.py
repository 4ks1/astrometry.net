from util import *
import numpy as np
import time
from starutil_numpy import *

x = np.random.uniform(size=10000) * 10
y = np.random.uniform(size=10000) * 100
xlo,xhi = 0., 9.
ylo,yhi = 5., 105.
nx,ny = 10,12
H = np.zeros((ny,nx), np.int32)
an_hist2d(x, y, H, xlo, xhi, ylo, yhi)

print H.sum()

H2,xe,ye = np.histogram2d(x, y, range=((xlo,xhi),(ylo,yhi)), bins=(nx,ny))

assert(np.all(H == H2.T))

x2 = np.append(x, np.array([xlo,xlo,xhi,xhi]))
y2 = np.append(y, np.array([ylo,yhi,ylo,yhi]))

Hb = np.zeros((ny,nx), np.int32)
an_hist2d(x2, y2, Hb, xlo, xhi, ylo, yhi)

H2b,xe,ye = np.histogram2d(x2, y2, range=((xlo,xhi),(ylo,yhi)), bins=(nx,ny))

assert(np.all(Hb == H2b.T))

an_hist2d(x, y, Hb, xlo, xhi, ylo, yhi)

assert(np.all(Hb == (H2b.T + H2.T)))


import sys
sys.exit(0)




sip = Sip('dec095705.01.p.w.wcs')
x = np.random.uniform(2000, size=100)
y = np.random.uniform(4000, size=100)
ra,dec = sip.pixelxy2radec(x, y)

xx = x + np.random.normal(scale=0.1, size=x.shape)
yy = y + np.random.normal(scale=0.1, size=y.shape)

xyz = radectoxyz(ra, dec)
xy = np.vstack((xx,yy)).T
print 'xyz', xyz.shape
print 'xy', xy.shape

tan = Tan('dec095705.01.p.w.wcs')

sip2 = fit_sip_wcs_2(xyz, xy, None, tan, 2,2)

print 'Got', sip2
print 'Vs truth', sip

sip2.write_to('sip2.wcs')

xy = np.vstack((x,y)).T
sip3 = fit_sip_wcs_2(xyz, xy, None, tan, 2,2)
sip3.write_to('sip3.wcs')

import sys
sys.exit(0)


X = np.random.uniform(1000., size=1001).astype(np.float32)

med = flat_median_f(X)
print 'Median', med
print 'vs np ', np.median(X)

for pct in [0., 1., 10., 25., 50., 75., 99., 100.]:
    p1 = flat_percentile_f(X, pct)
    p2 = np.percentile(X, pct)
    print 'Percentile', pct
    print '  ', p1
    print 'vs', p2


import fitsio
#X = fitsio.read('nasty.fits')
X = fitsio.read('dsky.fits')
assert(np.all(np.isfinite(X)))
print 'med', np.median(X)
print 'flat...'
f = flat_median_f(X)
print 'flat', f

for seed in range(42, 100):
    np.random.seed(seed)

    #X = np.random.normal(scale=10.0, size=(1016,1016)).astype(np.float32)
    X = np.random.normal(scale=10.0, size=(1015,1015)).astype(np.float32)
    # X = np.random.normal(scale=10.0, size=(10,10)).astype(np.float32)

    for i in range(3):
        t0 = time.clock()
        m = np.median(X)
        t1 = time.clock() - t0
        print 'np.median:', t1
    print 'value:', m

    I = np.argsort(X.ravel())
    m = X.flat[I[len(I)/2]]
    print 'element[N/2] =', m

    for i in range(3):
        t0 = time.clock()
        pym = flat_median_f(X)
        t1 = time.clock() - t0
        print 'flat_median:', t1
    print 'value:', pym
    assert(pym == m)
        
wcs = Tan()

wcs.crval = (1.,2.)
print 'crval', wcs.crval
(cr0,cr1) = wcs.crval

wcs.crpix = (50,100)
print 'crpix', wcs.crpix

wcs.crpix[0] = 500
print 'crpix', wcs.crpix

y = wcs.crpix[1]
wcs.crval[0] = 1.

wcs.cd = [1e-4,2e-4,-3e-4,4e-4]
print 'cd', wcs.cd

print 'wcs:', wcs

#wcs = tan_t()
wcs.pixel_scale()
xyz = wcs.pixelxy2xyz(0, 0)
print 'xyz', xyz
rd = wcs.pixelxy2radec(0, 0)
print 'rd', rd
xy = wcs.radec2pixelxy(rd[0], rd[1])
print 'xy', xy

X,Y = np.array([1,2,3]), np.array([4,5,6])
print 'X,Y', X,Y
R,D = wcs.pixelxy2radec(X, Y)
print 'R,D', R,D


wcs = Tan(0., 0., 0., 0., 1e-3, 0., 0., 1e-3, 100., 100.)

x1 = np.arange(3.)
x2 = np.arange(3)
x3 = x1[np.newaxis,:]
x4 = x1[:,np.newaxis]
x5 = x1.astype(np.float32)
x6 = x1.astype(np.dtype(np.float64).newbyteorder('S'))
x7 = 3.
x8 = np.array(3.)

import gc
gc.collect()
gc.set_debug(gc.DEBUG_LEAK)

for r,d in [(1., 2.),
            (1., x1),
            (1, x2),
            (1, x1),
            (x1, x1),
            (x1, x3),
            (x3, x4),
            (1., x5),
            (1., x6),
            (1., x7),
            (1., x8),
            ]:
    print
    print 'testing radec2pixelxy'
    print '  r', type(r),
    if hasattr(r, 'dtype'):
       print r.dtype,
    print r

    print '  d', type(d),
    if hasattr(d, 'dtype'):
        print d.dtype,
    print d
    
    ok,x,y = wcs.radec2pixelxy(r, d)
    print 'ok,x,y =', ok,x,y
    print '  x', type(x),
    if hasattr(x, 'dtype'):
        print x.dtype,
    print x
    print '  y', type(y),
    if hasattr(y, 'dtype'):
        print y.dtype,
    print y

    
gc.collect()

print 'Garbage:', len(gc.garbage)
for x in gc.garbage:
    print '  ', x
    
