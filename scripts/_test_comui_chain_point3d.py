# _test_comui_chain_point3d.py —— 3D 节点级链路冒烟（模拟 ComUI 宿主把节点串成链，ctypes）
# 链 A：point3d.read_sick(Z:\ScanImage\3D, frame=0) → point3d.voxel_downsample
#       → point3d.region_plane（ROI 取质心附近区域）——断言每跳点数递减合理、
#         region_plane 消息含平面参数
# 链 B：point3d.load_cloud(脚本内写临时 PLY 到 %TEMP%) → point3d.filter_cloud(voxel)
#       → point3d.measure_cloud(AABB)——断言 AABB ≈ 构造数据的已知包围盒
# Part 0：SICK 深层 3 节点（sick_quality / sick_intensity / sick_raw）正/错路径
# 约定：Z: 盘不可用时链 A 与 Part 0 的真实数据用例打印 SKIP 不算失败；
#       每跳输出缓冲用 algo_free 释放且不重复释放（释放后置 None 防误用）。
import ctypes as ct
import os
import re
import sys
import tempfile

sys.stdout.reconfigure(encoding="utf-8", errors="replace")   # GBK 控制台打中文

# DLL directory: argv[1] overrides; default = canonical <repo>/build/output/Release
DLL_DIR = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "build", "output", "Release")
os.add_dll_directory(DLL_DIR)

SICK_DIR = r"Z:\ScanImage\3D"
SICK_DIR2 = r"Z:\ScanImage\3D-2"

def sick_available():
    """Z: 盘 SICK 数据可用（目录存在且含 .dat）"""
    try:
        return os.path.isdir(SICK_DIR) and any(f.endswith(".dat") for f in os.listdir(SICK_DIR))
    except OSError:
        return False

# ---- ABI 结构（comui_native_abi.h，MSVC 默认 8 字节对齐）----
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

class AlgoCCloudOut(ct.Structure):
    _fields_ = [("count", ct.c_int32), ("xyz", ct.POINTER(ct.c_float)), ("rgb", ct.POINTER(ct.c_uint8))]

class AlgoCImageOut(ct.Structure):
    _fields_ = [("width", ct.c_int32), ("height", ct.c_int32), ("bgra", ct.POINTER(ct.c_uint8))]

class AlgoCOutput(ct.Structure):
    _fields_ = [("imageCount", ct.c_int32), ("images", ct.c_void_p),
                ("cloudCount", ct.c_int32), ("clouds", ct.POINTER(AlgoCCloudOut)),
                ("error", ct.c_char_p)]

fails = 0
def check(ok, name):
    global fails
    print(("  [PASS] " if ok else "  [FAIL] ") + name)
    if not ok:
        fails += 1

def skip(name):
    print("  [SKIP] " + name)

dll = ct.CDLL(os.path.join(DLL_DIR, "Point3DAlg.dll"))
dll.algo_free.argtypes = [ct.c_void_p]

def entry(name):
    fn = getattr(dll, name)
    fn.argtypes = [ct.POINTER(AlgoCInput), ct.POINTER(AlgoCOutput)]
    fn.restype = ct.c_int
    return fn

E_READ_SICK = entry("Point3DAlg_ComuiReadSick")
E_VOXEL = entry("Point3DAlg_ComuiVoxel")
E_REGION_PLANE = entry("Point3DAlg_ComuiRegionPlane")
E_LOAD_CLOUD = entry("Point3DAlg_ComuiLoadCloud")
E_FILTER = entry("Point3DAlg_ComuiFilterCloud")
E_MEASURE = entry("Point3DAlg_ComuiMeasureCloud")
E_SICK_QUALITY = entry("Point3DAlg_ComuiSickQuality")
E_SICK_INTENSITY = entry("Point3DAlg_ComuiSickIntensity")
E_SICK_RAW = entry("Point3DAlg_ComuiSickRaw")
E_SAVE_EX = entry("Point3DAlg_ComuiSaveCloudEx")
E_FIT_MODEL = entry("Point3DAlg_ComuiFitModel")
E_KNN = entry("Point3DAlg_ComuiKnnSearch")
E_RADIUS = entry("Point3DAlg_ComuiRadiusSearch")
E_THICKNESS = entry("Point3DAlg_ComuiThicknessMap")

# ---- marshal 辅助（宿主按 node.json params 声明序 + kind 组装，不带参数名）----
def P(kind, val):
    """单个节点参数：kind 0=number 1=int 2=text/path（UTF-8）"""
    if kind == 0:
        return AlgoCParam(0, float(val), 0, None)
    if kind == 1:
        return AlgoCParam(1, 0.0, int(val), None)
    return AlgoCParam(2, 0.0, 0, str(val).encode("utf-8"))

def call(fn, params, clouds=None):
    """调用节点入口；clouds = [(count, xyz_list)] → AlgoCCloud 数组。返回 (rc, out)。"""
    holders = []                      # 保住 bytes 生命周期
    arr = None
    if params:
        arr = (AlgoCParam * len(params))()
        for k, (kind, val) in enumerate(params):
            if kind == 2:
                holders.append(str(val).encode("utf-8"))
                arr[k] = AlgoCParam(2, 0.0, 0, holders[-1])
            elif kind == 1:
                arr[k] = AlgoCParam(1, 0.0, int(val), None)
            else:
                arr[k] = AlgoCParam(0, float(val), 0, None)
    carr = None
    if clouds:
        carr = (AlgoCCloud * len(clouds))()
        for k, (count, xyz) in enumerate(clouds):
            cxyz = (ct.c_float * len(xyz))(*xyz)
            holders.append(cxyz)
            carr[k] = AlgoCCloud(count, ct.cast(cxyz, ct.POINTER(ct.c_float)), None)
    inp = AlgoCInput(len(params) if params else 0, arr,
                     0, None,
                     len(clouds) if clouds else 0, carr)
    out = AlgoCOutput()
    rc = fn(ct.byref(inp), ct.byref(out))
    return rc, out

def out_cloud(out):
    """取第 0 片输出点云副本并立即释放节点缓冲（algo_free 一次性，防重复释放）"""
    assert out.cloudCount == 1 and out.clouds, "输出点云缺失"
    c = out.clouds[0]
    assert c.count > 0 and c.xyz, "输出点云缓冲为空"
    pts = [(c.xyz[i * 3], c.xyz[i * 3 + 1], c.xyz[i * 3 + 2]) for i in range(c.count)]
    dll.algo_free(ct.cast(c.xyz, ct.c_void_p))          # xyz 归还（宿主 free 配对）
    if c.rgb:
        dll.algo_free(ct.cast(c.rgb, ct.c_void_p))
    dll.algo_free(ct.cast(out.clouds, ct.c_void_p))     # 容器归还
    out.clouds = None                                   # 置空防二次释放
    out.cloudCount = 0
    return pts

def out_image(out):
    """取第 0 张输出图副本并立即释放节点缓冲（algo_free 一次性）"""
    assert out.imageCount == 1 and out.images, "输出图像缺失"
    im = ct.cast(out.images, ct.POINTER(AlgoCImageOut))[0]
    assert im.width > 0 and im.height > 0 and im.bgra, "输出图像缓冲为空"
    n = im.width * im.height
    px = [(im.bgra[i * 4], im.bgra[i * 4 + 1], im.bgra[i * 4 + 2], im.bgra[i * 4 + 3])
          for i in range(n)]
    dll.algo_free(ct.cast(im.bgra, ct.c_void_p))        # bgra 归还
    dll.algo_free(out.images)                           # 容器归还
    out.images = None                                   # 置空防二次释放
    out.imageCount = 0
    return im.width, im.height, px

def out_msg(out):
    return (out.error or b"").decode("utf-8", "replace")

def msg_of(out):
    """成功节点的结果消息（manual_regist/measure 先例：rc=0 时 error 通道回传结果文本）；
    顺带释放该节点可能存在的输出缓冲。"""
    m = out_msg(out)
    if out.clouds:
        out_cloud(out)
    if out.images:
        out_image(out)
    return m

def bbox_of(pts):
    xs = [p[0] for p in pts]; ys = [p[1] for p in pts]; zs = [p[2] for p in pts]
    return min(xs), min(ys), min(zs), max(xs), max(ys), max(zs)

# =====================================================================
# Part 0. SICK 深层 3 节点：正/错路径
# =====================================================================
print("---- Part 0. SICK 深层节点（sick_quality / sick_intensity / sick_raw）----")

# 错误路径不依赖 Z: 数据（或 rc 不受配置读取影响），恒跑
print("[错误路径]")
rc, out = call(E_SICK_QUALITY, [(2, "")])
check(rc == 1, "sick_quality 未设路径 → 1: %s" % out_msg(out))

rc, out = call(E_SICK_QUALITY, [(2, SICK_DIR), (1, 0), (0, 0.0)])
check(rc == 3, "sick_quality diff_threshold<=0 → 3: %s" % out_msg(out))

rc, out = call(E_SICK_INTENSITY, [(2, "no_such_dir_xyz_0"), (1, -1)])
check(rc == 2, "sick_intensity 文件夹无效 → 2: %s" % out_msg(out))

rc, out = call(E_SICK_RAW, [(2, "no_such_dir_xyz_0"), (1, -1)])
check(rc == 2, "sick_raw 文件夹无效 → 2: %s" % out_msg(out))

if sick_available():
    multi = SICK_DIR + ";" + SICK_DIR2
    print("[正路径]")
    # sick_quality：多相机自检（产线每日标定漂移自检定位）
    rc, out = call(E_SICK_QUALITY, [(2, multi), (1, 0), (0, 0.02)])
    qmsg = msg_of(out)
    check(rc == 0, "sick_quality 多相机 rc=0")
    check(rc == 0 and "配对格" in qmsg and "设备 2 台" in qmsg and "拼接" in qmsg,
          "sick_quality 消息含配对/设备数/拼接尺寸: %s" % qmsg)

    # sick_quality：单相机无衔接带 → 4（量测失败口径）
    rc, out = call(E_SICK_QUALITY, [(2, SICK_DIR), (1, 0), (0, 0.02)])
    check(rc == 4, "sick_quality 单相机 → 4: %s" % out_msg(out))

    # sick_quality：帧号越界 → 3
    rc, out = call(E_SICK_QUALITY, [(2, SICK_DIR), (1, 999999), (0, 0.02)])
    check(rc == 3, "sick_quality 帧号越界 → 3: %s" % out_msg(out))

    # sick_intensity：真实数据（有强度 → rc=0 出图；无强度 → rc=4 明确报，均合法）
    rc, out = call(E_SICK_INTENSITY, [(2, SICK_DIR), (1, 0)])
    imsg = out_msg(out)
    if rc == 0:
        w, hgt, px = out_image(out)
        check(w > 0 and hgt > 0, "sick_intensity 图尺寸 %dx%d" % (w, hgt))
        check(any(p[3] == 255 for p in px), "sick_intensity BGRA alpha=255")
        check("强度图" in imsg, "sick_intensity 消息: %s" % imsg)
    else:
        check(rc == 4 and "无强度通道" in imsg, "sick_intensity 无强度 → 4 明确报: %s" % imsg)

    # sick_raw：正路径 + 帧号越界
    rc, out = call(E_SICK_RAW, [(2, SICK_DIR), (1, 0)])
    rmsg = out_msg(out)
    check(rc == 0, "sick_raw rc=0")
    if rc == 0:
        w, hgt, px = out_image(out)
        check(w == 2560 and hgt > 0, "sick_raw 尺寸 w=2560 h=%d" % hgt)
        check("原始" in rmsg and "编码器行数" in rmsg, "sick_raw 消息: %s" % rmsg)
        # 归一化口径抽检：有像素灰度 255（raw 最大），无效格黑
        check(max(p[0] for p in px) == 255, "sick_raw 归一化含灰度 255")
    rc, out = call(E_SICK_RAW, [(2, SICK_DIR), (1, 999999)])
    check(rc == 3, "sick_raw 帧号越界 → 3: %s" % out_msg(out))
else:
    print("[SKIP] Z: 盘 SICK 数据不可用——真实数据用例跳过")
    skip("sick_quality 多相机/单相机/帧号越界")
    skip("sick_intensity 正路径")
    skip("sick_raw 正路径/帧号越界")

# =====================================================================
# 链 A. read_sick → voxel_downsample → region_plane（真实 SICK 数据）
# =====================================================================
print("---- 链 A. read_sick → voxel_downsample → region_plane ----")
if sick_available():
    rc, out = call(E_READ_SICK, [(2, SICK_DIR), (1, 0)])
    check(rc == 0, "read_sick rc=0（Z:\\ScanImage\\3D, frame=0）")
    pts0 = out_cloud(out) if rc == 0 else []
    n0 = len(pts0)
    check(n0 > 0, "read_sick 输出点数 %d > 0" % n0)

    if rc == 0 and n0 > 0:
        # 跳 1：体素下采样（leaf 取跨度/300，保证输出点数明显递减）
        x0, y0, z0, x1, y1, z1 = bbox_of(pts0)
        span = max(x1 - x0, y1 - y0, 1e-6)
        leaf = span / 300.0
        rc, out = call(E_VOXEL, [(0, leaf)], clouds=[(n0, [v for p in pts0 for v in p])])
        check(rc == 0, "voxel_downsample rc=0（leaf=%.4g）" % leaf)
        pts1 = out_cloud(out) if rc == 0 else []
        n1 = len(pts1)
        check(0 < n1 <= n0, "voxel 点数递减合理：%d -> %d" % (n0, n1))
        check(n1 < n0, "voxel 严格降采样（%d < %d）" % (n1, n0))

        # 跳 2：ROI 平面量测（ROI 取质心附近区域 = 质心 ± 跨度 5%）
        cx = sum(p[0] for p in pts1) / n1
        cy = sum(p[1] for p in pts1) / n1
        w = max((x1 - x0) * 0.1, leaf * 4)
        h = max((y1 - y0) * 0.1, leaf * 4)
        rc, out = call(E_REGION_PLANE, [(0, cx - w / 2), (0, cy - h / 2), (0, w), (0, h)],
                       clouds=[(n1, [v for p in pts1 for v in p])])
        pmsg = msg_of(out)
        check(rc == 0, "region_plane rc=0（ROI 质心附近 %.2f×%.2f）" % (w, h))
        check(rc == 0 and "z=" in pmsg and "内点" in pmsg,
              "region_plane 消息含平面参数: %s" % pmsg)
    elif rc == 0:
        skip("链 A 后两跳（read_sick 输出为空）")
else:
    print("[SKIP] Z: 盘 SICK 数据不可用")
    skip("链 A 全部")

# =====================================================================
# 链 B. load_cloud(临时 PLY) → filter_cloud(voxel) → measure_cloud(AABB)
# =====================================================================
print("---- 链 B. load_cloud → filter_cloud(voxel) → measure_cloud(AABB) ----")
tmp_ply = os.path.join(tempfile.gettempdir(), "_comui_chain_point3d_box.ply")
NX, NY = 41, 41                       # x,y ∈ [0,10] 步 0.25；z = 2.0 + 0.5·(x/10)·(y/10)
STEP = 0.25
verts = []
for i in range(NX):
    for j in range(NY):
        x, y = i * STEP, j * STEP
        z = 2.0 + 0.5 * (x / 10.0) * (y / 10.0)
        verts.append((x, y, z))
with open(tmp_ply, "w", encoding="ascii") as f:
    f.write("ply\nformat ascii 1.0\nelement vertex %d\n" % len(verts))
    f.write("property float x\nproperty float y\nproperty float z\nend_header\n")
    for v in verts:
        f.write("%.4f %.4f %.4f\n" % v)
EXPECT = (0.0, 0.0, 2.0, 10.0, 10.0, 2.5)   # 构造数据已知包围盒 min/max
LEAF_B = 0.5                                 # voxel 边长（质心偏移 ≤ leaf/2）

rc, out = call(E_LOAD_CLOUD, [(2, tmp_ply), (1, 1)])
check(rc == 0, "load_cloud rc=0（PLY ascii，%d 点）" % len(verts))
ptsB = out_cloud(out) if rc == 0 else []      # 先取缓冲再读消息（防重复释放）
nB = len(ptsB)
check(nB == len(verts), "load_cloud 点数 = %d" % len(verts))

if rc == 0 and nB > 0:
    rc, out = call(E_FILTER, [(1, 0), (0, LEAF_B)], clouds=[(nB, [v for p in ptsB for v in p])])
    check(rc == 0, "filter_cloud(voxel) rc=0: %s" % out_msg(out))
    ptsF = out_cloud(out) if rc == 0 else []   # 先取缓冲
    nF = len(ptsF)
    check(0 < nF < nB, "filter 点数递减合理：%d -> %d" % (nB, nF))

    if rc == 0 and nF > 0:
        rc, out = call(E_MEASURE, [(1, 0)], clouds=[(nF, [v for p in ptsF for v in p])])
        amsg = msg_of(out)
        check(rc == 0 and "AABB" in amsg, "measure_cloud(AABB) rc=0: %s" % amsg)
        m = re.search(r"min=\(([^)]*)\) max=\(([^)]*)\)", amsg)
        if m:
            got = [float(t) for t in m.group(1).split(",")] + \
                  [float(t) for t in m.group(2).split(",")]
            tol = LEAF_B / 2 + 0.05     # 体素质心把包围盒向内收缩 ≤ leaf/2
            ok = all(abs(g - e) <= tol for g, e in zip(got, EXPECT))
            check(ok, "AABB ≈ 已知包围盒（容差 %.2f）: got=%s expect=%s" % (tol, got, list(EXPECT)))
        else:
            check(False, "AABB 消息解析失败: %s" % amsg)

try:
    os.remove(tmp_ply)
except OSError:
    pass

# =====================================================================
# Part 4. A 类补缺 4 节点：save_cloud_ex / knn_search / radius_search / thickness_map
#（错误路径恒跑；正路径用链 B 的 verts 合成云，不依赖 Z: 盘）
# =====================================================================
print("---- Part 4. A 类补缺节点（save_cloud_ex / knn_search / radius_search / thickness_map）----")

print("[错误路径]")
rc, out = call(E_SAVE_EX, [], clouds=[(len(verts), [v for p in verts for v in p])])
check(rc == 1, "save_cloud_ex 未设路径 → 1: %s" % out_msg(out))

rc, out = call(E_SAVE_EX, [(2, os.path.join(tempfile.gettempdir(), "_no_such_dir_xyz_0", "c.ply")),
                           (1, 1), (1, 1)], clouds=[(len(verts), [v for p in verts for v in p])])
check(rc == 2, "save_cloud_ex 目录不存在 → 2: %s" % out_msg(out))

rc, out = call(E_SAVE_EX, [(2, os.path.join(tempfile.gettempdir(), "_c_bad.pcd")), (1, 9), (1, 0)],
               clouds=[(len(verts), [v for p in verts for v in p])])
check(rc == 3, "save_cloud_ex type=9 → 3: %s" % out_msg(out))

rc, out = call(E_KNN, [(0, 5.0), (0, 5.0), (0, 2.125), (1, 0)],
               clouds=[(len(verts), [v for p in verts for v in p])])
check(rc == 3, "knn_search k=0 → 3: %s" % out_msg(out))

rc, out = call(E_KNN, [(0, 5.0), (0, 5.0), (0, 2.125), (1, 10)])
check(rc == 1, "knn_search 输入未连线 → 1: %s" % out_msg(out))

rc, out = call(E_KNN, [(0, 0.0), (0, 0.0), (0, 0.0), (1, 5)], clouds=[(0, [])])
check(rc == 2, "knn_search 空点云 → 2: %s" % out_msg(out))

rc, out = call(E_RADIUS, [(0, 5.0), (0, 5.0), (0, 2.125), (0, 0.0)], clouds=[(len(verts), [v for p in verts for v in p])])
check(rc == 3, "radius_search radius=0 → 3: %s" % out_msg(out))

rc, out = call(E_RADIUS, [(0, 5.0), (0, 5.0), (0, 2.125), (0, 0.5)])
check(rc == 1, "radius_search 输入未连线 → 1: %s" % out_msg(out))

rc, out = call(E_THICKNESS, [(0, 0.25)])
check(rc == 1, "thickness_map 未连线 → 1: %s" % out_msg(out))

rc, out = call(E_THICKNESS, [(0, 0.25)], clouds=[(len(verts), [v for p in verts for v in p])])
check(rc == 1, "thickness_map 只连一片云 → 1: %s" % out_msg(out))

rc, out = call(E_THICKNESS, [(0, 0.0)],
               clouds=[(len(verts), [v for p in verts for v in p]),
                       (len(verts), [v for p in verts for v in p])])
check(rc == 3, "thickness_map cell=0 → 3: %s" % out_msg(out))

print("[正路径]（合成云，不依赖 Z: 盘）")

# knn_search：query 落在格网点 (5,5,2.125) 上 → 最近邻距离 0；k=5 全列
QK = (5.0, 5.0, 2.0 + 0.5 * 0.5 * 0.5)
rc, out = call(E_KNN, [(0, QK[0]), (0, QK[1]), (0, QK[2]), (1, 5)],
               clouds=[(len(verts), [v for p in verts for v in p])])
kmsg = out_msg(out)
check(rc == 0, "knn_search k=5 rc=0: %s" % kmsg)
m = re.search(r"knn k=(\d+) hits=(\d+)", kmsg)
check(m is not None and m.group(1) == "5" and m.group(2) == "5", "knn 消息头恒为 k=5 hits=5")
groups = re.findall(r"(\d+):([0-9.eE+-]+)", kmsg)
check(len(groups) == 5, "knn 消息列 5 组 idx:dist")
check(len(groups) == 5 and float(groups[0][1]) < 1e-6, "knn 最近邻距离 ≈ 0（query 与格网点重合）")
d5 = [float(g[1]) for g in groups]
check(d5 == sorted(d5), "knn 距离升序（欧氏口径）")

# knn_search：k 超上限 999 → 按 30 截断（消息头部仍是总 k）
rc, out = call(E_KNN, [(0, QK[0]), (0, QK[1]), (0, QK[2]), (1, 999)],
               clouds=[(len(verts), [v for p in verts for v in p])])
kmsg = out_msg(out)
check(rc == 0 and "knn k=30 hits=30" in kmsg, "knn k=999 截断为 30: %s" % kmsg[:48])

# radius_search：r=0.3 → 中心 + 4 个正邻格点（对角 0.354 > 0.3 不入）
rc, out = call(E_RADIUS, [(0, QK[0]), (0, QK[1]), (0, QK[2]), (0, 0.3)],
               clouds=[(len(verts), [v for p in verts for v in p])])
rmsg = out_msg(out)
check(rc == 0, "radius_search r=0.3 rc=0: %s" % rmsg)
m = re.search(r"radius=[0-9.eE+-]+ hits=(\d+)", rmsg)
check(m is not None and m.group(1) == "5", "radius r=0.3 命中 5（中心+4邻点）")

# radius_search：0 命中属正常成功（api.h 口径："半径内无点 → 0 成功 + *out_count=0"）
rc, out = call(E_RADIUS, [(0, 100.0), (0, 100.0), (0, 100.0), (0, 0.1)],
               clouds=[(len(verts), [v for p in verts for v in p])])
rmsg = out_msg(out)
check(rc == 0 and "hits=0" in rmsg, "radius 远离点云 → rc=0 hits=0: %s" % rmsg)

# thickness_map：顶面平面 z=2.0、底面 z=0.5+x/10 → 厚度 1.5~0.5（线性），cell=0.25 逐格对齐
top_pts = []
bot_pts = []
for i in range(41):
    for j in range(41):
        x, y = i * 0.25, j * 0.25
        top_pts.append((x, y, 2.0))
        bot_pts.append((x, y, 0.5 + x / 10.0))
rc, out = call(E_THICKNESS, [(0, 0.25)],
               clouds=[(len(top_pts), [v for p in top_pts for v in p]),
                       (len(bot_pts), [v for p in bot_pts for v in p])])
tmsg = out_msg(out)
check(rc == 0, "thickness_map rc=0: %s" % tmsg)
if rc == 0:
    check("thickness=[0.5~1.5]" in tmsg, "thickness min/max = [0.5~1.5]: %s" % tmsg)
    check("valid=1681/1681(100.0%)" in tmsg, "thickness 有效率 100%%: %s" % tmsg)
    w, hgt, px = out_image(out)
    check(w == 41 and hgt == 41, "thickness 图尺寸 41x41: %dx%d" % (w, hgt))
    check(any(p[3] == 255 for p in px), "thickness BGRA alpha=255")
    check(max(p[0] for p in px) == 255 and min(p[0] for p in px) == 0,
          "厚度归一化含灰度 0 与 255（tmin→0/tmax→255）")

# thickness_map：两云 XY 不重叠 → 无同格双有效对 → 4（量测失败口径）
bot_far = [(x + 100.0, y, z) for (x, y, z) in bot_pts]
rc, out = call(E_THICKNESS, [(0, 0.25)],
               clouds=[(len(top_pts), [v for p in top_pts for v in p]),
                       (len(bot_pts), [v for p in bot_far for v in p])])
check(rc == 4, "thickness 两云 XY 不重叠 → 4: %s" % out_msg(out))

# =====================================================================
# 链 C. load_cloud → filter_cloud(voxel) → save_cloud_ex 落盘 → load_cloud 回读
#（save_cloud_ex 反向闭环验证：PLY/TXT/PCD 三格式落盘并回读点数一致）
# =====================================================================
print("---- 链 C. load_cloud → filter(voxel) → save_cloud_ex → load_cloud 回读 ----")
tmp_in_ply = os.path.join(tempfile.gettempdir(), "_comui_chain_point3d_save_in.ply")
tmp_out_ply = os.path.join(tempfile.gettempdir(), "_comui_chain_point3d_saved.ply")
tmp_out_txt = os.path.join(tempfile.gettempdir(), "_comui_chain_point3d_saved.txt")
tmp_out_pcd = os.path.join(tempfile.gettempdir(), "_comui_chain_point3d_saved.pcd")
with open(tmp_in_ply, "w", encoding="ascii") as f:
    f.write("ply\nformat ascii 1.0\nelement vertex %d\n" % len(verts))
    f.write("property float x\nproperty float y\nproperty float z\nend_header\n")
    for v in verts:
        f.write("%.4f %.4f %.4f\n" % v)

rc, out = call(E_LOAD_CLOUD, [(2, tmp_in_ply), (1, 1)])
check(rc == 0, "链C load_cloud rc=0")
ptsC = out_cloud(out) if rc == 0 else []
nC = len(ptsC)
check(nC == len(verts), "链C load_cloud 点数 = %d" % len(verts))

if rc == 0 and nC > 0:
    rc, out = call(E_FILTER, [(1, 0), (0, 0.5)], clouds=[(nC, [v for p in ptsC for v in p])])
    check(rc == 0, "链C filter_cloud(voxel) rc=0: %s" % out_msg(out))
    ptsF = out_cloud(out) if rc == 0 else []
    nF = len(ptsF)
    check(0 < nF < nC, "链C filter 点数递减合理：%d -> %d" % (nC, nF))
else:
    ptsF, nF = [], 0

if ptsF:
    flatF = [v for p in ptsF for v in p]
    for tname, tcode, tfile in (("PLY", 1, tmp_out_ply), ("TXT", 2, tmp_out_txt), ("PCD", 0, tmp_out_pcd)):
        rc, out = call(E_SAVE_EX, [(2, tfile), (1, tcode), (1, 1 if tcode == 1 else 0)],
                       clouds=[(nF, flatF)])
        smsg = msg_of(out)
        check(rc == 0, "链C save_cloud_ex %s rc=0: %s" % (tname, smsg))
        check("points=%d" % nF in smsg and "type=%s" % tname in smsg,
              "save %s 消息含点数/格式: %s" % (tname, smsg))
        rc, out = call(E_LOAD_CLOUD, [(2, tfile), (1, tcode)])
        ptsR = out_cloud(out) if rc == 0 else []
        check(rc == 0 and len(ptsR) == nF, "链C %s 落盘回读点数一致：%d == %d" % (tname, len(ptsR), nF))
else:
    skip("链C save/回读（filter 输出为空）")

# =====================================================================
# Part 5. 1.3.0 扩展节点：fit_model method 5 = line3D / filter_cloud method 10 = approxVoxel
#（错误路径恒跑；正路径用合成云，不依赖 Z: 盘）
# =====================================================================
print("---- Part 5. 1.3.0 扩展节点（fit_model line3D / filter_cloud approxVoxel）----")

# 空间直线拟合：沿方向 (1,2,3)/|·| 过 (1,1,1) 的直线 + 微垂直扰动（确定性伪噪声）
import math
DIRV = (1.0, 2.0, 3.0)
dn = math.sqrt(sum(c * c for c in DIRV))
DIRU = tuple(c / dn for c in DIRV)
a0 = (DIRU[1], -DIRU[0], 0.0)
a0n = math.sqrt(sum(c * c for c in a0))
A = tuple(c / a0n for c in a0)                      # 与 DIRU 正交的单位向量
B = (DIRU[1] * A[2] - DIRU[2] * A[1],
     DIRU[2] * A[0] - DIRU[0] * A[2],
     DIRU[0] * A[1] - DIRU[1] * A[0])               # u×a 第三个正交向量
line_pts = []
for i in range(60):
    t = -1.5 + i * 0.05
    jx = ((i * 37) % 7 - 3) * 0.002                 # ±0.006 内确定性垂直扰动
    jy = ((i * 53) % 7 - 3) * 0.002
    line_pts.append((1.0 + DIRU[0] * t + A[0] * jx + B[0] * jy,
                     1.0 + DIRU[1] * t + A[1] * jx + B[1] * jy,
                     1.0 + DIRU[2] * t + A[2] * jx + B[2] * jy))
flat_line = [v for p in line_pts for v in p]

# fit_model method 5 line3D（node.json 参数声明序：method/fit/distance_threshold）
rc, out = call(E_FIT_MODEL, [(1, 5), (1, 0), (0, 0.05)], clouds=[(len(line_pts), flat_line)])
check(rc == 0, "fit_model line3D(RANSAC) rc=0")
lmsg = msg_of(out)
m = re.search(r"line3D p=\(([-\d.eE+]+), ([-\d.eE+]+), ([-\d.eE+]+)\) "
              r"dir=\(([-\d.eE+]+), ([-\d.eE+]+), ([-\d.eE+]+)\)", lmsg)
check(m is not None, "line3D 消息含 p=/dir= 六系数: %s" % lmsg[:80])
if m:
    dv = [float(m.group(i)) for i in (4, 5, 6)]
    dot = sum(dv[k] * DIRU[k] for k in range(3))
    check(abs(abs(dot) - 1.0) < 1e-3, "line3D 方向与构造直线平行（|dot|=%.6f）" % abs(dot))
    check(abs(math.sqrt(sum(c * c for c in dv)) - 1.0) < 1e-3, "line3D 方向为单位向量")
    pv = [float(m.group(i)) for i in (1, 2, 3)]
    po = [pv[k] - 1.0 for k in range(3)]
    proj = sum(po[k] * DIRU[k] for k in range(3))
    perp = math.sqrt(max(0.0, sum(c * c for c in po) - proj * proj))
    check(perp < 0.05, "line3D 轴上点距构造直线 < 0.05（%.4f）" % perp)
mm = re.search(r"内点 (\d+)/(\d+)", lmsg)
check(mm is not None and int(mm.group(1)) >= 50, "line3D 内点 >= 50/60: %s" % (mm.group(1) if mm else "?"))

# fit_model line3D：MSAC / LMS 两种 fit 也支持（api.h：plane/line3D 三种 fit 均支持）
rc, out = call(E_FIT_MODEL, [(1, 5), (1, 1), (0, 0.05)], clouds=[(len(line_pts), flat_line)])
check(rc == 0, "fit_model line3D(MSAC) rc=0")
msg_of(out)
rc, out = call(E_FIT_MODEL, [(1, 5), (1, 2), (0, 0.05)], clouds=[(len(line_pts), flat_line)])
check(rc == 0, "fit_model line3D(LMS) rc=0")
msg_of(out)

# fit_model 错路径：method 6 越界 → 3；method 5 全同点（方向退化）→ 4；未连线 → 1
rc, out = call(E_FIT_MODEL, [(1, 6), (1, 0), (0, 0.05)], clouds=[(len(line_pts), flat_line)])
check(rc == 3, "fit_model method=6 越界 → 3: %s" % out_msg(out))
degen = [1.0, 2.0, 3.0] * 20
rc, out = call(E_FIT_MODEL, [(1, 5), (1, 0), (0, 0.05)], clouds=[(20, degen)])
check(rc == 4, "fit_model line3D 全同点退化 → 4: %s" % out_msg(out))
rc, out = call(E_FIT_MODEL, [(1, 5), (1, 0), (0, 0.05)], clouds=[])
check(rc == 1, "fit_model 输入未连线 → 1")

# filter_cloud method 10 approxVoxel：复用链 B 的 verts（41×41 格网 1681 点，间隔 0.25）
rc, out = call(E_FILTER, [(1, 10), (0, 0.5)], clouds=[(nB, [v for p in ptsB for v in p])])
check(rc == 0, "filter_cloud approxVoxel(leaf=0.5) rc=0: %s" % out_msg(out))
ptsV = out_cloud(out) if rc == 0 else []
nV = len(ptsV)
check(0 < nV < nB, "approxVoxel 点数递减合理：%d -> %d" % (nB, nV))
if ptsV:
    vb = bbox_of(ptsV)
    check(all(abs(vb[i] - EXPECT[i]) <= 0.75 for i in range(6)),
          "approxVoxel 包围盒 ≈ 构造包围盒（leaf/2 量级偏差）")
    check(all(0.0 <= p[0] <= 10.0 and 0.0 <= p[1] <= 10.0 and 2.0 <= p[2] <= 2.5
              for p in ptsV), "approxVoxel 输出点均落在构造范围内")
# approxVoxel 与 method 0 voxel 同 leaf：输出点数同量级（同为格网质心，格划分可有微小差异）
rc, out = call(E_FILTER, [(1, 0), (0, 0.5)], clouds=[(nB, [v for p in ptsB for v in p])])
ptsX = out_cloud(out) if rc == 0 else []
check(rc == 0 and abs(len(ptsX) - nV) <= max(4, 0.1 * nV),
      "approxVoxel 与 voxel 同 leaf 点数同量级：%d vs %d" % (nV, len(ptsX)))

# filter_cloud 错路径：leaf<=0 → 3；method 11 越界 → 3；未连线 → 1
rc, out = call(E_FILTER, [(1, 10), (0, 0.0)], clouds=[(nB, [v for p in ptsB for v in p])])
check(rc == 3, "filter_cloud approxVoxel leaf=0 → 3: %s" % out_msg(out))
rc, out = call(E_FILTER, [(1, 11), (0, 0.5)], clouds=[(nB, [v for p in ptsB for v in p])])
check(rc == 3, "filter_cloud method=11 越界 → 3: %s" % out_msg(out))
rc, out = call(E_FILTER, [(1, 10), (0, 0.5)], clouds=[])
check(rc == 1, "filter_cloud 输入未连线 → 1")

for t in (tmp_in_ply, tmp_out_ply, tmp_out_txt, tmp_out_pcd):
    try:
        os.remove(t)
    except OSError:
        pass

print("\n%s（失败 %d 项）" % ("全部通过" if fails == 0 else "存在失败", fails))
sys.exit(0 if fails == 0 else 1)
