import sys
import numpy as np
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
pts = np.frombuffer(body, dtype='<f4').reshape(-1, 3)[:n]
x, y, z = pts[:, 0].astype(np.float64), pts[:, 1].astype(np.float64), pts[:, 2].astype(np.float64)

# 去掉平面倾斜：用低半区（基面）点拟合平面
A = np.c_[x, y, np.ones(len(x))]
coef, *_ = np.linalg.lstsq(A, z, rcond=None)
zr = z - A @ coef
base = np.median(zr[np.abs(zr - np.median(zr)) < 0.005])
zr -= base
print('base plane offset removed; zr range', zr.min(), zr.max())

# 稀疏点云 -> 用 0.1mm 网格统计每格最大高度
res = 0.1
nx = int((x.max() - x.min()) / res) + 1
ny = int((y.max() - y.min()) / res) + 1
ix = ((x - x.min()) / res).astype(np.int32)
iy = ((y - y.min()) / res).astype(np.int32)
grid = np.full((ny, nx), np.nan, dtype=np.float32)
tmp = np.full((ny, nx), -1e9, dtype=np.float32)
np.maximum.at(tmp, (iy, ix), zr.astype(np.float32))
grid[tmp > -1e8] = tmp[tmp > -1e8]

fig, axes = plt.subplots(1, 2, figsize=(14, 6))
im = axes[0].imshow(grid, origin='lower', cmap='viridis')
axes[0].set_title('z above base (max per 0.1mm cell)')
fig.colorbar(im, ax=axes[0], label='mm')

# xy 散点按高度着色（抽稀）
sel = np.random.RandomState(0).choice(len(x), size=min(150000, len(x)), replace=False)
sc = axes[1].scatter(x[sel], y[sel], c=zr[sel], s=0.2, cmap='viridis')
axes[1].set_aspect('equal')
axes[1].set_title('top view colored by height')
fig.colorbar(sc, ax=axes[1], label='mm')
plt.tight_layout()
plt.savefig(sys.argv[2], dpi=130)
print('saved', sys.argv[2])

# 高程直方图
plt.figure(figsize=(10, 4))
plt.hist(zr, bins=200)
plt.yscale('log')
plt.xlabel('z above base (mm)')
plt.ylabel('count')
plt.title('height histogram (log)')
plt.tight_layout()
plt.savefig(sys.argv[3], dpi=120)

# 高区空间范围
hi = zr > 0.05
print('raised points:', hi.sum())
print('raised x range', x[hi].min(), x[hi].max())
print('raised y range', y[hi].min(), y[hi].max())
print('raised z range', zr[hi].min(), zr[hi].max())
print('raised z percentiles', np.percentile(zr[hi], [0, 1, 5, 50, 95, 99, 100]))
