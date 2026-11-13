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

# 基面：稳健平面（迭代剔除高区）
A = np.c_[x, y, np.ones(len(x))]
sel = np.ones(len(x), dtype=bool)
for _ in range(5):
    coef, *_ = np.linalg.lstsq(A[sel], z[sel], rcond=None)
    r = z - A @ coef
    s = np.median(r) + 3 * (np.median(np.abs(r - np.median(r))) + 1e-9)
    sel = r < s
zr = z - A @ coef
zr -= np.median(zr[sel])
print('base: z = %.4f*x + %.4f*y + %.5f  (used %d/%d pts)' % (coef[0], coef[1], coef[2], sel.sum(), len(x)))

uniq_y = np.unique(np.round(y, 6))
print('unique y values:', len(uniq_y), ' -> 扫描行数?')

hi = zr > 0.02
print('raised(>0.02):', hi.sum())
P = pts[hi]
tree = cKDTree(P)
pairs = tree.query_pairs(r=0.08, output_type='ndarray')
print('pairs:', len(pairs))

parent = np.arange(len(P))
def find(a):
    while parent[a] != a:
        parent[a] = parent[parent[a]]
        a = parent[a]
    return a
def union(a, b):
    ra, rb = find(a), find(b)
    if ra != rb:
        parent[rb] = ra
for a, b in pairs:
    union(a, b)
roots = np.array([find(i) for i in range(len(P))])
labels, counts = np.unique(roots, return_counts=True)
order = np.argsort(counts)[::-1]
print('clusters:', len(labels), 'sizes top:', counts[order][:10])

def fit_sphere(Q):
    # algebraic least squares: x^2+y^2+z^2 = 2cx*x+2cy*y+2cz*z + (R^2-c^2)
    M = np.c_[2 * Q, np.ones(len(Q))]
    b = (Q ** 2).sum(axis=1)
    sol, *_ = np.linalg.lstsq(M, b, rcond=None)
    c = sol[:3]
    R = np.sqrt(sol[3] + (c ** 2).sum())
    resid = np.linalg.norm(Q - c, axis=1) - R
    return c, R, resid

print('\ntop clusters:')
for k in order[:8]:
    m = roots == labels[k]
    Q = P[m]
    cx, cy, cz = Q.mean(axis=0)
    foot = np.sqrt(((Q[:, :2] - Q[:, :2].mean(0)) ** 2).sum(1).max())
    c, R, resid = fit_sphere(Q)
    apex = c[2] + R
    print('  n=%4d  peak=%.4f  centroid=(%.3f,%.3f,%.3f)  footprint_r=%.4f' % (len(Q), zr[hi][m].max(), cx, cy, cz, foot))
    print('        sphere c=(%.4f,%.4f,%.4f) R=%.4f apex_z(base rel)=%.4f resid_rms=%.5f' % (c[0], c[1], c[2], R, apex, np.sqrt((resid ** 2).mean())))

# 取一个 bump 做径向剖面（相对局部基面）
m = roots == labels[order[0]]
Q = P[m]
cxy = Q[:, :2].mean(0)
r = np.linalg.norm(Q[:, :2] - cxy, axis=1)
print('\nradial profile (bump #1):')
for lo in np.arange(0, 0.16, 0.02):
    sel = (r >= lo) & (r < lo + 0.02)
    if sel.sum():
        print('  r=%.2f-%.2f  n=%3d  z_mean=%.4f z_max=%.4f' % (lo, lo + 0.02, sel.sum(), Q[sel, 2].mean(), Q[sel, 2].max()))
