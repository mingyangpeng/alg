import sys
import numpy as np

path = sys.argv[1]
with open(path, 'rb') as f:
    header = b''
    while b'end_header' not in header:
        line = f.readline()
        if not line:
            break
        header += line
    n = 0
    for line in header.split(b'\n'):
        if line.startswith(b'element vertex'):
            n = int(line.split()[-1])
    body = f.read()

pts = np.frombuffer(body, dtype='<f4')
print('header:', header.decode('ascii', 'replace').strip().replace('\n', ' | '))
print('points:', n, 'floats:', pts.size)
pts = pts.reshape(-1, 3)[:n]
print('dtype range:', pts.min(axis=0), pts.max(axis=0))
print('bbox size  :', pts.max(axis=0) - pts.min(axis=0))

# 用最小二乘拟合基准平面 z = a*x + b*y + c（先粗看，未做稳健剔除）
A = np.c_[pts[:, 0], pts[:, 1], np.ones(len(pts))]
coef, *_ = np.linalg.lstsq(A, pts[:, 2], rcond=None)
print('plane least-squares a,b,c =', coef)
resid = pts[:, 2] - A @ coef
print('resid percentiles(mm):', np.percentile(resid, [0, 1, 5, 25, 50, 75, 95, 99, 99.9, 100]))

# 以中位数为基面，看抬升分布
zr = resid - np.median(resid)
print('z above base percentiles:', np.percentile(zr, [50, 90, 95, 99, 99.5, 99.9, 100]))

# 2D 网格连通域找 bump（阈值取温和值，先看有多少隆起）
for thr in (0.005, 0.01, 0.02, 0.05):
    mask = zr > thr
    print('above %.3f mm: %d points (%.2f%%)' % (thr, mask.sum(), 100.0 * mask.sum() / len(zr)))

# 网格分辨率与连通域（纯 numpy 4 邻域 flood fill 太慢，用 scipy 若有）
try:
    from scipy import ndimage
    have_scipy = True
except Exception:
    have_scipy = False
print('scipy:', have_scipy)

if have_scipy:
    x, y = pts[:, 0], pts[:, 1]
    res = 0.02
    nx = int((x.max() - x.min()) / res) + 3
    ny = int((y.max() - y.min()) / res) + 3
    ix = ((x - x.min()) / res).astype(np.int32) + 1
    iy = ((y - y.min()) / res).astype(np.int32) + 1
    gridded = np.zeros((ny, nx), dtype=np.float32)
    np.maximum.at(gridded, (iy, ix), zr.astype(np.float32))
    occ = gridded > 0.005
    lab, nlab = ndimage.label(occ, structure=np.ones((3, 3)))
    print('grid', nx, ny, 'labels above 5um:', nlab)
    sizes = ndimage.sum(occ, lab, range(1, nlab + 1))
    order = np.argsort(sizes)[::-1]
    print('top clusters (cells, peak mm):')
    for k in order[:20]:
        m = lab == (k + 1)
        print('  #%d cells=%d peak=%.4f' % (k + 1, int(sizes[k]), float(gridded[m].max())))
