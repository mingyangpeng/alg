import sys
import numpy as np
from scipy.spatial import cKDTree

path = sys.argv[1]
with open(path, 'rb') as f:
    header = b''
    while b'end_header' not in header:
        header += f.readline()
    n = int([l for l in header.split(b'\n') if l.startswith(b'element vertex')][0].split()[-1])
    body = f.read()
pts = np.frombuffer(body, dtype='<f4').reshape(-1, 3)[:n].astype(np.float64)
x, y, z = pts.T

A = np.c_[x, y, np.ones(len(x))]
sel = np.ones(len(x), dtype=bool)
for _ in range(5):
    coef, *_ = np.linalg.lstsq(A[sel], z[sel], rcond=None)
    r = z - A @ coef
    sel = r < np.median(r) + 3 * (np.median(np.abs(r - np.median(r))) + 1e-9)
zr = z - A @ coef
zr -= np.median(zr[sel])

def fit_sphere(Q):
    M = np.c_[2 * Q, np.ones(len(Q))]
    b = (Q ** 2).sum(1)
    sol, *_ = np.linalg.lstsq(M, b, rcond=None)
    c = sol[:3]
    R = np.sqrt(sol[3] + (c ** 2).sum())
    return c, R, np.linalg.norm(Q - c, axis=1) - R

def fit_plane(Q):
    M = np.c_[Q[:, 0], Q[:, 1], np.ones(len(Q))]
    sol, *_ = np.linalg.lstsq(M, Q[:, 2], rcond=None)
    res = Q[:, 2] - M @ sol
    return sol, res

hi = zr > 0.02
P = np.c_[x[hi], y[hi], zr[hi]]
t = cKDTree(P[:, :2])
pairs = t.query_pairs(r=0.12, output_type='ndarray')
parent = np.arange(len(P))
def find(a):
    while parent[a] != a:
        parent[a] = parent[parent[a]]; a = parent[a]
    return a
for a, b in pairs:
    ra, rb = find(a), find(b)
    if ra != rb: parent[rb] = ra
roots = np.array([find(i) for i in range(len(P))])
labels, counts = np.unique(roots, return_counts=True)

rows = []
tree_all = cKDTree(np.c_[x, y])
for k in range(len(labels)):
    mm = roots == labels[k]
    if counts[k] < 8:
        continue
    Q = P[mm]
    cxy = Q[:, :2].mean(0)
    # 局部基面：环带 0.18~0.30mm 内的低点
    idx = tree_all.query_ball_point(cxy, 0.30)
    ring = np.c_[x[idx], y[idx], zr[idx]]
    rr = np.linalg.norm(ring[:, :2] - cxy, axis=1)
    basepts = ring[(rr > 0.16) & (rr < 0.28) & (ring[:, 2] < 0.05)]
    if len(basepts) < 6:
        continue
    bcoef, _ = fit_plane(basepts)
    base_z = bcoef[0] * cxy[0] + bcoef[1] * cxy[1] + bcoef[2]
    c, R, sres = fit_sphere(Q)
    apex = c[2] + R
    pcoef, pres = fit_plane(Q)
    rows.append((len(Q), Q[:, 2].max(), apex - base_z, Q[:, 2].max() - base_z,
                 R, np.sqrt((sres ** 2).mean()), np.sqrt((pres ** 2).mean()),
                 np.linalg.norm(Q[:, :2] - cxy, axis=1).max()))

R = np.array(rows)
print('bumps analyzed:', len(R))
names = ['n', 'peak_abs', 'apex_h', 'peak_h', 'R', 'sphere_rms', 'plane_rms', 'foot_r']
for i, nm in enumerate(names):
    print('%-11s p5/25/50/75/95 = %s' % (nm, np.round(np.percentile(R[:, i], [5, 25, 50, 75, 95]), 5)))
print('sphere_rms < plane_rms 的比例: %.1f%%' % (100.0 * np.mean(R[:, 5] < R[:, 6])))
print('apex_h - peak_h  p50/p95/P100 =', np.round(np.percentile(R[:, 2] - R[:, 3], [50, 95, 100]), 5))
