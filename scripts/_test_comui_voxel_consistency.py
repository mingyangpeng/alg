# _test_comui_voxel_consistency.py —— FilterCloud(voxel) vs ComuiVoxel 一致性直查（ctypes）
# 来源：规划文档 §12 遗留检查项——C API Point3DAlg_FilterCloud(method=0，PCL VoxelGrid)
#   与 ComUI 适配层 Point3DAlg_ComuiVoxel（哈希格网质心，节点 alg.point3d.voxel_downsample）
#   须"同输入同语义输出"，避免语义漂移。
#
# 两实现口径（读码结论，src/ShapelyAlg/CloudProcessor.cpp filterVoxelGrid +
# thrid/PCL-1.9.1 voxel_grid.hpp + integration/comui/Point3DAlg_comui.cpp）：
#   FilterCloud(0): 分格索引 = floor(x * (float)(1/leaf))（float 倒数相乘）；
#                   is_dense=false → 非有限点剔除；质心 = float(Vector4f) 累加 / 点数；
#                   输出按体素键升序（x 最快、z 最慢）；可选 min_points_per_voxel。
#   ComuiVoxel:     分格索引 = (int64)floor(x / leaf)（double 除法）；21 位打包键
#                   （任一轴 |idx| ≥ 2^21 会碰撞）；质心 = double 累加 / 点数；
#                   颜色 = uint 截断均值；unordered_map 输出顺序任意；leaf<1e-6 钳到
#                   1e-6；无 min_points 参数、无非有限点防护。
# 两者同义（每格质心），数值路径不同 → 期望分格一致、质心差 ≤ 数值噪声。
# 本脚本用 float64 真值（与 ComuiVoxel 同算术：double 除法分格 + double 质心）
# 按"格键"配对（序无关），任何差异如实归属并报告，不做硬凑。
import ctypes as ct
import os
import sys

import numpy as np

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
# DLL 目录：argv[1] 覆盖；默认 <repo>/build/output/Release（与链路脚本同口径）
DLL_DIR = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "build", "output", "Release"))
os.environ["PATH"] = DLL_DIR + os.pathsep + os.environ.get("PATH", "")
os.add_dll_directory(DLL_DIR)


# ---------------- ctypes 结构（comui_native_abi.h / Point3DAlg_api.h 权威） ----------------

class AlgoCParam(ct.Structure):
    _fields_ = [("kind", ct.c_int32), ("d", ct.c_double), ("i", ct.c_int32), ("s", ct.c_char_p)]


class AlgoCCloud(ct.Structure):
    _fields_ = [("count", ct.c_int32), ("xyz", ct.POINTER(ct.c_float)), ("rgb", ct.POINTER(ct.c_uint8))]


class AlgoCImage(ct.Structure):
    _fields_ = [("width", ct.c_int32), ("height", ct.c_int32), ("bgra", ct.POINTER(ct.c_uint8))]


class AlgoCInput(ct.Structure):
    _fields_ = [("paramCount", ct.c_int32), ("params", ct.POINTER(AlgoCParam)),
                ("imageCount", ct.c_int32), ("images", ct.POINTER(AlgoCImage)),
                ("cloudCount", ct.c_int32), ("clouds", ct.POINTER(AlgoCCloud))]


class AlgoCOutput(ct.Structure):
    _fields_ = [("imageCount", ct.c_int32), ("images", ct.c_void_p),
                ("cloudCount", ct.c_int32), ("clouds", ct.c_void_p),
                ("error", ct.c_char_p)]


class AlgoCCloudOut(ct.Structure):
    _fields_ = [("count", ct.c_int32), ("xyz", ct.POINTER(ct.c_float)), ("rgb", ct.POINTER(ct.c_uint8))]


class PCloud(ct.Structure):  # Point3DAlg_PointCloud { float* xyz; int count; }
    _fields_ = [("xyz", ct.POINTER(ct.c_float)), ("count", ct.c_int32)]


dll = ct.CDLL(os.path.join(DLL_DIR, "Point3DAlg.dll"))
dll.Point3DAlg_FilterCloud.argtypes = [ct.POINTER(PCloud), ct.c_int, ct.POINTER(ct.c_double),
                                       ct.c_int, ct.POINTER(PCloud)]
dll.Point3DAlg_FilterCloud.restype = ct.c_int
dll.Point3DAlg_FreeCloud.argtypes = [ct.POINTER(PCloud)]
dll.Point3DAlg_ComuiVoxel.argtypes = [ct.POINTER(AlgoCInput), ct.POINTER(AlgoCOutput)]
dll.Point3DAlg_ComuiVoxel.restype = ct.c_int
dll.algo_free.argtypes = [ct.c_void_p]

fails = 0        # 语义级失败（返回码/点数/分格）
tol_fails = 0    # 容差级失败（质心数值 >1e-6，语义仍可能一致）


def check(ok, name):
    global fails
    print(("  [PASS] " if ok else "  [FAIL] ") + name)
    if not ok:
        fails += 1


def check_tol(ok, name):
    global tol_fails
    print(("  [PASS] " if ok else "  [FAIL] ") + name)
    if not ok:
        tol_fails += 1


# ---------------- 调用封装 ----------------

def filter_voxel(xyz_f32, leaf, min_pts=None):
    """Point3DAlg_FilterCloud(method=0)；xyz_f32: np.float32 (N,3)。返回 (rc, (M,3) float32)。"""
    buf = np.ascontiguousarray(xyz_f32, dtype=np.float32)
    pin = PCloud(buf.ctypes.data_as(ct.POINTER(ct.c_float)), int(buf.shape[0]))
    params = [leaf] if min_pts is None else [leaf, float(min_pts)]
    parr = (ct.c_double * len(params))(*params)
    pout = PCloud()
    rc = dll.Point3DAlg_FilterCloud(ct.pointer(pin), 0, parr, len(params), ct.pointer(pout))
    if rc != 0 or pout.count <= 0:
        return rc, None
    out = np.ctypeslib.as_array(pout.xyz, shape=(pout.count, 3)).copy()
    dll.Point3DAlg_FreeCloud(ct.pointer(pout))
    return rc, out


def comui_voxel_leaf(xyz_f32, leaf):
    """Point3DAlg_ComuiVoxel（leaf 是唯一参数，按 node.json 声明顺序；rgb=None）。
    返回 (rc, (M,3) float32)。"""
    buf = np.ascontiguousarray(xyz_f32, dtype=np.float32).ravel()
    cloud = AlgoCCloud(len(buf) // 3, buf.ctypes.data_as(ct.POINTER(ct.c_float)), None)
    params = (AlgoCParam * 1)(AlgoCParam(0, float(leaf), 0, None))
    inp = AlgoCInput(1, params, 0, None, 1, ct.pointer(cloud))
    out = AlgoCOutput()
    rc = dll.Point3DAlg_ComuiVoxel(ct.pointer(inp), ct.pointer(out))
    if rc != 0:
        return rc, None
    arr = ct.cast(out.clouds, ct.POINTER(AlgoCCloudOut)).contents
    res = None
    if arr.count > 0 and arr.xyz:
        res = np.ctypeslib.as_array(arr.xyz, shape=(arr.count, 3)).copy()
    dll.algo_free(arr.xyz)
    if arr.rgb:
        dll.algo_free(arr.rgb)
    dll.algo_free(out.clouds)
    return rc, res


# ---------------- float64 真值：与 ComuiVoxel 完全同算术（double 除法分格 + double 质心） ----------------

def cell_keys(arr_f32, leaf):
    """按 ComuiVoxel 同款算术（float32 坐标升 double 后除 leaf、floor）给点/质心归属格键。"""
    c = np.floor(arr_f32.astype(np.float64) / leaf).astype(np.int64)
    return (c[:, 0] << 42) + (c[:, 1] << 21) + c[:, 2]


def truth_centroids(xyz_f32, leaf):
    """真值：输入点按 double 分格 → 每格 float64 质心。返回 (格数, {key: centroid})。"""
    x64 = xyz_f32.astype(np.float64)
    k = cell_keys(x64, leaf)
    uniq = np.unique(k)
    cents = {int(kk): x64[k == kk].mean(axis=0) for kk in uniq}
    return int(uniq.size), cents


def rebin_map(out_f32, leaf):
    """输出质心按 double 除法回分格 → {key: centroid}。若某格质心恰落在格边界上，
    可能回错格（重复键+缺键）→ 由调用方识别并回退最近邻配对。"""
    k = cell_keys(out_f32, leaf)
    uniq, cnts = np.unique(k, return_counts=True)
    return {int(kk): out_f32[i].astype(np.float64) for i, kk in enumerate(k)}, \
        {int(kk): int(c) for kk, c in zip(uniq, cnts)}


def per_cell_maxdiff(m1, m2):
    """两个 {key: centroid} 的公共键逐点最大欧氏差。"""
    ds = [float(np.linalg.norm(m1[k] - m2[k])) for k in m1 if k in m2]
    return max(ds) if ds else float("inf")


def nearest_maxdiff(a, b):
    """序无关最近邻配对最大距离（a 每点到 b 全体的最近距离的最大值；b 同理取大）。"""
    da = np.linalg.norm(a[:, None, :].astype(np.float64) - b[None, :, :].astype(np.float64), axis=2)
    return float(max(da.min(axis=1).max(), da.min(axis=0).max()))


# ---------------- 合成点云：规则网格 + 随机噪声 + 离群点（≥500 点） ----------------

rng = np.random.default_rng(42)
g = np.arange(0.0, 3.0 + 1e-9, 0.1, dtype=np.float32)          # [0,3]³ 网格，间距 0.1 → 31³
gx, gy, gz = np.meshgrid(g, g, g, indexing="ij")
grid = np.stack([gx.ravel(), gy.ravel(), gz.ravel()], axis=1)
noise = rng.uniform(0.0, 3.0, size=(1500, 3)).astype(np.float32)   # 盒内随机噪声
outliers = rng.uniform(20.0, 25.0, size=(12, 3)) * rng.choice([-1.0, 1.0], size=(12, 3))
outliers = outliers.astype(np.float32)                             # 远离主体的离群点
xyz = np.vstack([grid, noise, outliers])
print("输入点数 = %d（网格 %d + 噪声 1500 + 离群 12）" % (len(xyz), len(grid)))
check(len(xyz) >= 500, "合成点云 ≥500 点")

# ---------------- 主对比：leaf = 0.5 / 1.0 / 2.0 ----------------

print("\n== 主对比（FilterCloud voxel vs ComuiVoxel，仅 leaf 参数）==")
for leaf in (0.5, 1.0, 2.0):
    rc1, out1 = filter_voxel(xyz, leaf)
    rc2, out2 = comui_voxel_leaf(xyz, leaf)
    n_truth, truth = truth_centroids(xyz, leaf)
    n1 = 0 if out1 is None else len(out1)
    n2 = 0 if out2 is None else len(out2)
    print(f"\n-- leaf={leaf}: FilterCloud rc={rc1} n={n1} | ComuiVoxel rc={rc2} n={n2} | 真值格数={n_truth}")
    check(rc1 == 0 and rc2 == 0, f"leaf={leaf} 两实现均返回 0")
    check(n1 == n2 == n_truth, f"leaf={leaf} 输出点数一致（PCL {n1} / Comui {n2} / 真值 {n_truth}）")
    if n1 == n_truth and n2 == n_truth and n1 > 0:
        # 分格一致性：两实现输出的质心按 double 除法回分格，应恰为真值格集合（每格一个质心）
        m1, c1 = rebin_map(out1, leaf)
        m2, c2 = rebin_map(out2, leaf)
        tkeys = set(truth.keys())
        ok_bin = (not any(v > 1 for v in c1.values())) and (not any(v > 1 for v in c2.values())) \
            and set(m1.keys()) == tkeys and set(m2.keys()) == tkeys
        check(ok_bin, f"leaf={leaf} 分格一致（两实现质心回分格 = 真值 {len(tkeys)} 格，无缺/多/重）")
        if ok_bin:
            d12 = per_cell_maxdiff(m1, m2)
            d1t = per_cell_maxdiff(m1, truth)
            d2t = per_cell_maxdiff(m2, truth)
            print(f"   按格配对最大偏差：PCL↔Comui = {d12:.3e}；PCL↔真值 = {d1t:.3e}（float 累加）；"
                  f"Comui↔真值 = {d2t:.3e}（≤输出 float 1 ulp）")
            check_tol(d12 < 1e-6, f"leaf={leaf} 逐格质心一致（<1e-6）")
        else:
            # 质心落在格边界的退化情形 → 回退序无关最近邻配对
            dnn = nearest_maxdiff(out1, out2)
            print(f"   回分格出现缺/多/重（质心落边界），回退最近邻配对：PCL↔Comui 最大 {dnn:.3e}")
            check_tol(dnn < 1e-6, f"leaf={leaf} 最近邻逐点一致（<1e-6）")

# ---------------- 差异归属仿真：float32(Vector4f) 顺序累加能否复现 PCL↔真值偏差 ----------------

print("\n== 差异归属仿真（PCL 质心为 float32 顺序累加，voxel_grid.hpp L409-415）==")
for leaf in (0.5, 1.0, 2.0):
    x64 = xyz.astype(np.float64)
    kk_all = cell_keys(x64, leaf)
    worst = 0.0
    for kk in np.unique(kk_all):
        pts = xyz[kk_all == kk]                      # float32 原始坐标
        truth = x64[kk_all == kk].mean(axis=0)       # float64 真值质心
        for trial in range(3):                       # 组内顺序不定（std::sort 不稳定），抽样多序
            order = rng.permutation(len(pts)) if trial else np.arange(len(pts))
            acc = np.zeros(3, dtype=np.float32)
            for i in order:
                acc += pts[i]                        # 逐点 float32 加（同 PCL Vector4f 路径）
            c = acc / np.float32(len(pts))
            worst = max(worst, float(np.linalg.norm(c.astype(np.float64) - truth)))
    print(f"   leaf={leaf}: float32 顺序累加仿真最大偏差 = {worst:.3e}（量级应与上方 PCL↔真值一致）")

# ---------------- FilterCloud 专属：min_points_per_voxel（ComuiVoxel 无此参数） ----------------

print("\n== FilterCloud min_points_per_voxel（ComuiVoxel 参数面差异验证）==")
rc0, out0 = filter_voxel(xyz, 0.5)
rc3, out3 = filter_voxel(xyz, 0.5, min_pts=5)
n_full = 0 if out0 is None else len(out0)
n3 = 0 if out3 is None else len(out3)
x64 = xyz.astype(np.float64)
k05 = cell_keys(x64, 0.5)
uniq05, cnt05 = np.unique(k05, return_counts=True)
n_truth5 = int((cnt05 >= 5).sum())
print(f"   FilterCloud leaf=0.5 minPts=5: rc={rc3} n={n3}（无 minPts 时 n={n_full}）；"
      f"真值 ≥5 点格数={n_truth5}")
check(rc3 == 0 and 0 < n3 < n_full, "minPts=5 剔除稀疏格（离群/孤立噪声格被滤除）")
check(n3 == n_truth5, f"minPts=5 输出格数 = 真值 ≥5 点格数（{n3} vs {n_truth5}）")
_, out_com_full = comui_voxel_leaf(xyz, 0.5)
print(f"   ComuiVoxel leaf=0.5: n={0 if out_com_full is None else len(out_com_full)}"
      "（无 min_points 参数，孤立格全保留——参数面差异，api.h 头注释已注明）")

# ---------------- 非有限点探针（口径差异，ComuiVoxel 侧为观察口径） ----------------

print("\n== 非有限点探针（FilterCloud 剔除 / ComuiVoxel 无防护）==")
probe = np.array([[0.1, 0.1, 0.1], [0.7, 0.1, 0.1], [1.3, 0.1, 0.1],
                  [float("nan"), 0.5, 0.5], [0.5, float("inf"), 0.5]], dtype=np.float32)
rcn, on = filter_voxel(probe, 0.5)
okn = rcn == 0 and on is not None and len(on) == 3 and not bool(np.isnan(on).any())
check(okn, f"FilterCloud 剔除 NaN/Inf 后剩 3 格且输出干净（rc={rcn} n={0 if on is None else len(on)}）")
rcm, om = comui_voxel_leaf(probe, 0.5)
has_nan = om is not None and bool(np.isnan(om).any())
print(f"   ComuiVoxel rc={rcm} n={0 if om is None else len(om)} 输出含 NaN={has_nan}"
      "（观察口径：floor(NaN)→UB，污染质心；生产输入应先 removeNonFinite）")

# ---------------- 边界分格探针：float 倒数 vs double 除法（informational） ----------------

print("\n== 边界分格探针（PCL: floor(x*(float)(1/leaf)) vs Comui: floor(x/leaf) double）==")
print("   坐标恰为 leaf 整数倍的边界点阵——两条索引路径可能把边界坐标分到不同格：")
for leafb in (0.1, 0.3, 0.7, 0.5):
    gb = np.arange(0.0, 2.1 + 1e-9, leafb, dtype=np.float32)  # 恰落在格边界上的坐标
    gx, gy, gz = np.meshgrid(gb, gb, gb, indexing="ij")
    xb = np.stack([gx.ravel(), gy.ravel(), gz.ravel()], axis=1)
    # Python 仿真两条索引路径（与 C++ 同序：float32 乘法→double floor / double 除法→floor）
    inv32 = np.float32(1.0) / np.float32(leafb)
    idx_pcl = np.floor((xb * inv32).astype(np.float32).astype(np.float64))
    idx_com = np.floor(xb.astype(np.float64) / leafb)
    n_flip = int((idx_pcl != idx_com).any(axis=1).sum())
    rcb1, ob1 = filter_voxel(xb, leafb)
    rcb2, ob2 = comui_voxel_leaf(xb, leafb)
    nb1 = 0 if ob1 is None else len(ob1)
    nb2 = 0 if ob2 is None else len(ob2)
    tag = "一致" if nb1 == nb2 else "分格不同（口径差异）"
    print(f"   leaf={leafb}: 边界点 {len(xb)}，索引路径分歧点 {n_flip}，"
          f"DLL 实测格数 PCL={nb1} Comui={nb2} → {tag}")

# ---------------- 结论 ----------------

print("\n== 结论 ==")
if fails == 0 and tol_fails == 0:
    verdict = "PASS：两实现同输入同语义同数值（分格一致且逐格质心 <1e-6）"
elif fails == 0:
    verdict = ("PASS（语义）/ 数值差异（容差级）：分格与'每格质心'语义完全一致；逐格质心差超 1e-6，"
               "差异全在 PCL 侧——其质心为 float32(Vector4f) 顺序累加（voxel_grid.hpp），"
               "ComuiVoxel 为 double 累加（与 float64 真值差 ≤1 输出 ulp）。另见边界分格探针："
               "非 dyadic leaf 且坐标恰在格边界时两实现分格口径不同。")
else:
    verdict = "FAIL：存在语义级差异（返回码/点数/分格不一致），见上方明细"
print(verdict + f"（语义级失败 {fails} 项，容差级失败 {tol_fails} 项）")
sys.exit(0 if fails == 0 else 1)
