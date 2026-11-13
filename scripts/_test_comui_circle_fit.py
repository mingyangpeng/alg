# _test_comui_circle_fit.py —— 模拟 ComUI 宿主调用 alg.point3d.circle_fit 节点（ctypes）
# 验证：返回 0 / 孔位点云（每孔一点 z=半径）/ 三孔检出 / 边界
import ctypes as ct
import math
import os
import sys

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
DLL_DIR = r"D:\pmy\alg\build\output\Release"
os.add_dll_directory(DLL_DIR)


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


class AlgoCOutput(ct.Structure):
    _fields_ = [("imageCount", ct.c_int32), ("images", ct.c_void_p),
                ("cloudCount", ct.c_int32), ("clouds", ct.POINTER(AlgoCCloudOut)),
                ("error", ct.c_char_p)]


import random
random.seed(21)

fails = 0


def check(ok, name):
    global fails
    print(("  [PASS] " if ok else "  [FAIL] ") + name)
    if not ok:
        fails += 1


# 合成：基面 z=1.0 + 三个孔壁（不同半径），z ∈ [1.0, 1.2]
xyz = []
holes = [(1.0, 1.0, 0.20), (3.0, 1.0, 0.30), (2.0, 3.0, 0.40)]


def noise(s):
    return random.gauss(0, s)


for i in range(0, 101):
    for j in range(0, 101):
        xyz += [i * 0.04, j * 0.04, 1.0 + noise(0.002)]
for (cx, cy, R) in holes:
    for z in [1.0 + 0.02 * k for k in range(11)]:
        for i in range(48):
            th = 2 * math.pi * i / 48
            xyz += [cx + R * math.cos(th) + noise(0.005),
                    cy + R * math.sin(th) + noise(0.005),
                    z]
buf = (ct.c_float * len(xyz))(*xyz)

dll = ct.CDLL(os.path.join(DLL_DIR, "Point3DAlg.dll"))
dll.Point3DAlg_ComuiCircleFit.argtypes = [ct.POINTER(AlgoCInput), ct.POINTER(AlgoCOutput)]
dll.Point3DAlg_ComuiCircleFit.restype = ct.c_int
dll.algo_free.argtypes = [ct.c_void_p]

params = (AlgoCParam * 6)(
    AlgoCParam(0, 1.05, 0, None),    # z_low（切掉基面）
    AlgoCParam(0, 1.15, 0, None),    # z_high
    AlgoCParam(0, 0.2, 0, None),     # cluster_tolerance
    AlgoCParam(1, 20, 0, None),      # min_points
    AlgoCParam(0, 0.05, 0, None),    # radius_min
    AlgoCParam(0, 1.0, 0, None),     # radius_max
)
cloud = AlgoCCloud(len(xyz) // 3, buf, None)
inp = AlgoCInput(6, params, 0, None, 1, ct.pointer(cloud))
out = AlgoCOutput()

rc = dll.Point3DAlg_ComuiCircleFit(ct.pointer(inp), ct.pointer(out))
msg = (out.error or b"").decode("utf-8", "replace")
print("return=%d msg=%s" % (rc, msg))
check(rc == 0, "返回 0")
check(out.cloudCount == 1 and out.clouds, "输出孔位点云")
arr = out.clouds[0]
print("输出点数=%d" % arr.count)
check(arr.count == 3, "三孔检出")
# 每孔：xy=圆心、z=半径，与真值匹配
for k in range(arr.count):
    px, py, pr = arr.xyz[k * 3], arr.xyz[k * 3 + 1], arr.xyz[k * 3 + 2]
    best = min(holes, key=lambda t: (px - t[0]) ** 2 + (py - t[1]) ** 2)
    check(math.hypot(px - best[0], py - best[1]) < 0.01, f"孔{k} 圆心匹配 ({best[0]},{best[1]})")
    check(abs(pr - best[2]) < 0.005, f"孔{k} 半径 = {best[2]} ± 5µm（实测 {pr:.4f}）")
dll.algo_free(arr.xyz)
if arr.rgb:
    dll.algo_free(arr.rgb)
dll.algo_free(ct.cast(out.clouds, ct.c_void_p))

# 边界：z 带无点
params2 = (AlgoCParam * 6)(
    AlgoCParam(0, 5.0, 0, None), AlgoCParam(0, 6.0, 0, None),
    AlgoCParam(0, 0.2, 0, None), AlgoCParam(1, 20, 0, None),
    AlgoCParam(0, 0.05, 0, None), AlgoCParam(0, 1.0, 0, None))
inp2 = AlgoCInput(6, params2, 0, None, 1, ct.pointer(cloud))
out2 = AlgoCOutput()
rc2 = dll.Point3DAlg_ComuiCircleFit(ct.pointer(inp2), ct.pointer(out2))
check(rc2 == 4 and out2.error, "z 带无点返回 4: %s" % (out2.error or b"").decode("utf-8", "replace"))

print("\n%s（失败 %d 项）" % ("全部通过" if fails == 0 else "存在失败", fails))
sys.exit(0 if fails == 0 else 1)
