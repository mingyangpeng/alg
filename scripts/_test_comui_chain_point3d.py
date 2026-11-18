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

print("\n%s（失败 %d 项）" % ("全部通过" if fails == 0 else "存在失败", fails))
sys.exit(0 if fails == 0 else 1)
