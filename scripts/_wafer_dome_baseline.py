# _wafer_dome_baseline.py —— Point3DAlg_DomeFit 的 Python 基准（数值验收对照）。
# 与 src/Point3DAlg/tools/Point3DAlg_DomeFit.cpp 的算法链路逐行等价：
#   稳健基面（迭代修剪 LSQ，统计在全点上）→ 内点中位归零
#   → 高度阈值 0.02 → 3D 欧式聚类（tol=0.12, min=8）
#   → Kåsa 球拟合 → med+3MAD 残差修剪重拟合 → 守卫（apex-peak ≤ tol、rms ≤ 5µm）
# 用法：python -I scripts/_wafer_dome_baseline.py output/moi_wafer.ply
# 基准输出（moi_wafer.ply）：n=2019, h p50=0.18674, std=0.00197, rms 中位≈1.4µm
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

# ---- 稳健基面（与 C++ DomeFitBase 一致：统计在全点、每轮从全点重建内点）----
A = np.c_[x, y, np.ones(len(x))]
sel = np.ones(len(x), dtype=bool)
for _ in range(5):
    coef, *_ = np.linalg.lstsq(A[sel], z[sel], rcond=None)
    r = z - A @ coef
    sel = r < np.median(r) + 3 * (np.median(np.abs(r - np.median(r))) + 1e-9)
zr = z - A @ coef
zr -= np.median(zr[sel])          # 内点中位归零
print('base inliers=%d  sigma=%.5f' % (sel.sum(), zr[sel].std()))

# ---- 高度阈值 + 3D 欧式聚类 ----
hi = zr > 0.02
P = np.c_[x[hi], y[hi], zr[hi]]
t3 = cKDTree(P)
pairs = t3.query_pairs(r=0.12, output_type='ndarray')
parent = np.arange(len(P))
def find(a):
    while parent[a] != a:
        parent[a] = parent[parent[a]]; a = parent[a]
    return a
for a, b in pairs:
    ra, rb = find(a), find(b)
    if ra != rb:
        parent[rb] = ra
roots = np.array([find(i) for i in range(len(P))])
labels, counts = np.unique(roots, return_counts=True)

# ---- 每簇 Kåsa 球拟合 + 修剪重拟合 + 守卫 ----
def kasa(Q):
    M = np.c_[2 * Q, np.ones(len(Q))]
    b = (Q ** 2).sum(1)
    s, *_ = np.linalg.lstsq(M, b, rcond=None)
    c = s[:3]
    R = np.sqrt(s[3] + (c ** 2).sum())
    return c, R

hs, rmsAll, validCnt, invalid = [], [], 0, 0
for k in range(len(labels)):
    mm = roots == labels[k]
    if counts[k] < 8:
        continue
    Q = P[mm]
    m = len(Q)
    c, R = kasa(Q)
    rms = 0.0
    if np.isfinite(R):
        sres = np.linalg.norm(Q - c, axis=1) - R
        med = np.median(sres)
        mad = np.median(np.abs(sres - med))
        keep = sres <= med + 3.0 * mad + 1e-12
        if keep.sum() >= 4 and keep.sum() < m:
            c, R = kasa(Q[keep])
            sres = np.linalg.norm(Q[keep] - c, axis=1) - R
            rms = np.sqrt((sres ** 2).mean())
        else:
            rms = np.sqrt((sres ** 2).mean())
    if not np.isfinite(R) or R < 0.01 or R > 1.0:
        continue
    h = c[2] + R
    if h <= 0:
        continue
    if h - Q[:, 2].max() > 0.12 or rms > 0.005:   # 守卫：apex 悬空 / 劣质拟合
        invalid += 1
        continue
    validCnt += 1
    hs.append(h)
    rmsAll.append(rms)

hs = np.array(hs)
print('clusters(valid/invalid) = %d / %d' % (validCnt, invalid))
print('h: p1/5/50/95/99 = %s  std=%.5f  mean=%.5f'
      % (np.round(np.percentile(hs, [1, 5, 50, 95, 99]), 5), hs.std(), hs.mean()))
print('rms: p50=%.5f max=%.5f' % (np.median(rmsAll), np.max(rmsAll)))
