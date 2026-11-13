import sys
import numpy as np
from scipy.spatial import cKDTree
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

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

# 取最高点周边 1.2x1.2mm 区域，画 3D
i0 = np.argmax(zr)
cx, cy = x[i0], y[i0]
m = (np.abs(x - cx) < 0.6) & (np.abs(y - cy) < 0.6)
print('crop center', cx, cy, 'points', m.sum())
fig = plt.figure(figsize=(12, 5))
ax = fig.add_subplot(121, projection='3d')
ax.scatter(x[m], y[m], zr[m], c=zr[m], s=6, cmap='viridis')
ax.set_title('1.2x1.2mm around highest point')
ax.set_zlim(-0.02, 0.22)

# 该区域内的点按高度分层
Q = np.c_[x[m], y[m], zr[m]]
print('crop z percentiles', np.percentile(Q[:, 2], [0, 10, 25, 50, 75, 90, 99, 100]))

# 找该区域最近的 bump 中心，打印所有点（按半径）
tree = cKDTree(Q[:, :2])
rr = np.linalg.norm(Q[:, :2] - [cx, cy], axis=1)
sel2 = rr < 0.25
print('bump points (r, z):')
for q, r_ in sorted(zip(Q[sel2], rr[sel2]), key=lambda t: t[1]):
    print('   r=%.4f  x=%8.3f y=%8.3f z=%+.4f' % (r_, q[0], q[1], q[2]))

ax2 = fig.add_subplot(122)
sc = ax2.scatter(Q[:, 0], Q[:, 1], c=Q[:, 2], s=8, cmap='viridis')
ax2.set_aspect('equal')
fig.colorbar(sc, ax=ax2)
ax2.set_title('top view of crop')
plt.tight_layout()
plt.savefig(sys.argv[2], dpi=120)
print('saved', sys.argv[2])

# 每个 bump 的峰值高度统计（用 0.02 阈值 + 0.08 连通）
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
peaks, cents = [], []
for k in range(len(labels)):
    mm = roots == labels[k]
    if counts[k] >= 6:
        peaks.append(P[mm, 2].max())
        cents.append(P[mm, :2].mean(0))
peaks = np.array(peaks); cents = np.array(cents)
print('bumps(count>=6):', len(peaks))
print('peak percentiles', np.percentile(peaks, [0, 5, 25, 50, 75, 95, 100]))
# 峰值与位置的相关（看基面弯曲）
for axis, name in ((0, 'x'), (1, 'y')):
    c = np.corrcoef(cents[:, axis], peaks)[0, 1]
    print('corr(peak, %s) = %.3f' % (name, c))
# 格点间距
if len(cents) > 100:
    tt = cKDTree(cents)
    d, _ = tt.query(cents, k=2)
    print('bump pitch percentiles', np.percentile(d[:, 1], [1, 25, 50, 75, 99]))
