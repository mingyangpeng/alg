# _test_comui_region_plane.py —— 模拟 ComUI 宿主调用 alg.point3d.region_plane 节点（ctypes）
# 验证：返回 0 / 高度=真值 / 消息含 PV/σ/内点 / 台阶差可由两 ROI 相减 / ROI 无点报错
import ctypes as ct
import os
import sys

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
# DLL directory: argv[1] overrides; default = canonical <repo>/build/output/Release
DLL_DIR = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "build", "output", "Release")
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


class AlgoCOutput(ct.Structure):
    _fields_ = [("imageCount", ct.c_int32), ("images", ct.c_void_p),
                ("cloudCount", ct.c_int32), ("clouds", ct.c_void_p),
                ("error", ct.c_char_p)]


import random
import re

random.seed(7)

fails = 0


def _height_of(msg):
    m = re.search(r"高度 ([-\d.]+) mm", msg or "")
    return float(m.group(1)) if m else None


def check(ok, name):
    global fails
    print(("  [PASS] " if ok else "  [FAIL] ") + name)
    if not ok:
        fails += 1


# 合成：倾斜面 z = 0.002x + 0.001y + 2.0，σ=2µm
# ROI(1,1,2,2) 中心 (2,2) → 真值 2.006；ROI(3,3,2,2) 中心 (4,4) → 真值 2.012
xyz = []
for i in range(0, 101):
    for j in range(0, 101):
        x, y = i * 0.04, j * 0.04
        z = 0.002 * x + 0.001 * y + 2.0 + random.gauss(0, 0.002)
        xyz += [x, y, z]
xyz = (ct.c_float * len(xyz))(*xyz)

dll = ct.CDLL(os.path.join(DLL_DIR, "Point3DAlg.dll"))
dll.Point3DAlg_ComuiRegionPlane.argtypes = [ct.POINTER(AlgoCInput), ct.POINTER(AlgoCOutput)]
dll.Point3DAlg_ComuiRegionPlane.restype = ct.c_int


def fit(x, y, w, h):
    params = (AlgoCParam * 4)(AlgoCParam(0, x, 0, None), AlgoCParam(0, y, 0, None),
                              AlgoCParam(0, w, 0, None), AlgoCParam(0, h, 0, None))
    cloud = AlgoCCloud(len(xyz) // 3, xyz, None)
    inp = AlgoCInput(4, params, 0, None, 1, ct.pointer(cloud))
    out = AlgoCOutput()
    rc = dll.Point3DAlg_ComuiRegionPlane(ct.pointer(inp), ct.pointer(out))
    return rc, (out.error or b"").decode("utf-8", "replace")


rc, msg = fit(1.0, 1.0, 2.0, 2.0)
print("return=%d msg=%s" % (rc, msg))
check(rc == 0, "返回 0")
check("高度 2.006" in msg, "ROI1 中心高度 2.006")
check("平面度PV" in msg and "内点" in msg, "消息含 PV/σ/内点")

rc2, msg2 = fit(3.0, 3.0, 2.0, 2.0)
print("return=%d msg=%s" % (rc2, msg2))
h2 = _height_of(msg2)
check(rc2 == 0 and h2 is not None and abs(h2 - 2.012) < 0.001, "ROI2 中心高度 2.012（台阶差 = 0.006 可相减）")

rc3, msg3 = fit(50.0, 50.0, 1.0, 1.0)
check(rc3 == 4 and msg3, "ROI 无点返回 4: %s" % msg3)

print("\n%s（失败 %d 项）" % ("全部通过" if fails == 0 else "存在失败", fails))
sys.exit(0 if fails == 0 else 1)
