# _test_comui_profile.py —— 模拟 ComUI 宿主调用 alg.point3d.profile 节点（ctypes）
# 验证：返回 0 / 剖面点云 / 台阶落差定位 / 边界
import ctypes as ct
import os
import re
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
random.seed(31)

fails = 0


def check(ok, name):
    global fails
    print(("  [PASS] " if ok else "  [FAIL] ") + name)
    if not ok:
        fails += 1


# 合成：台阶（y<2 → 1.0；y≥2 → 1.5），σ=3µm，线沿 +Y 穿过台阶
xyz = []
for i in range(0, 101):
    for j in range(0, 101):
        x, y = i * 0.04, j * 0.04
        z = (1.0 if y < 2.0 else 1.5) + random.gauss(0, 0.003)
        xyz += [x, y, z]
buf = (ct.c_float * len(xyz))(*xyz)

dll = ct.CDLL(os.path.join(DLL_DIR, "Point3DAlg.dll"))
dll.Point3DAlg_ComuiProfile.argtypes = [ct.POINTER(AlgoCInput), ct.POINTER(AlgoCOutput)]
dll.Point3DAlg_ComuiProfile.restype = ct.c_int
dll.algo_free.argtypes = [ct.c_void_p]

params = (AlgoCParam * 6)(
    AlgoCParam(0, 2.0, 0, None),    # x0
    AlgoCParam(0, 0.0, 0, None),    # y0
    AlgoCParam(0, 2.0, 0, None),    # x1
    AlgoCParam(0, 4.0, 0, None),    # y1
    AlgoCParam(0, 0.2, 0, None),    # band
    AlgoCParam(0, 0.1, 0, None),    # bin_size
)
cloud = AlgoCCloud(len(xyz) // 3, buf, None)
inp = AlgoCInput(6, params, 0, None, 1, ct.pointer(cloud))
out = AlgoCOutput()

rc = dll.Point3DAlg_ComuiProfile(ct.pointer(inp), ct.pointer(out))
msg = (out.error or b"").decode("utf-8", "replace")
print("return=%d msg=%s" % (rc, msg))
check(rc == 0, "返回 0")
m = re.search(r"剖面 (\d+) 格.*z \[([\d.-]+) @s([\d.-]+) ~ ([\d.-]+) @s([\d.-]+)\] "
              r"max_step ([\d.-]+) @s([\d.-]+)", msg)
check(m is not None, "消息解析")
if m:
    pts, zmin, smin, zmax, smax, step, sstep = (int(m.group(1)), float(m.group(2)),
                                                float(m.group(3)), float(m.group(4)),
                                                float(m.group(5)), float(m.group(6)),
                                                float(m.group(7)))
    check(pts >= 38, f"格数 {pts} ≈ 40")
    check(abs(zmin - 1.0) < 0.003 and abs(zmax - 1.5) < 0.003, "z 范围 = 两台阶高度")
    check(abs(step - 0.5) < 0.01, f"台阶落差 {step} = 0.5 ± 10µm")
    check(abs(sstep - 2.0) < 0.11, f"跳变位置 {sstep} ≈ 台阶线 s=2.0")

if out.clouds:
    arr = out.clouds[0]
    check(arr.count >= 38, "剖面点数合理")
    # 剖面两段高度抽查（点 xy 在线上，z=中位高度）
    lo = any(abs(arr.xyz[i * 3 + 2] - 1.0) < 0.005 and arr.xyz[i * 3 + 1] < 1.8
             for i in range(arr.count))
    hi = any(abs(arr.xyz[i * 3 + 2] - 1.5) < 0.005 and arr.xyz[i * 3 + 1] > 2.2
             for i in range(arr.count))
    check(lo and hi, "剖面两段高度独立正确")
    dll.algo_free(arr.xyz)
    if arr.rgb:
        dll.algo_free(arr.rgb)
    dll.algo_free(ct.cast(out.clouds, ct.c_void_p))

# 边界：线段在云外
params2 = (AlgoCParam * 6)(
    AlgoCParam(0, 50.0, 0, None), AlgoCParam(0, 50.0, 0, None),
    AlgoCParam(0, 54.0, 0, None), AlgoCParam(0, 50.0, 0, None),
    AlgoCParam(0, 0.2, 0, None), AlgoCParam(0, 0.1, 0, None))
inp2 = AlgoCInput(6, params2, 0, None, 1, ct.pointer(cloud))
out2 = AlgoCOutput()
rc2 = dll.Point3DAlg_ComuiProfile(ct.pointer(inp2), ct.pointer(out2))
check(rc2 == 4 and out2.error, "云外线段返回 4: %s" % (out2.error or b"").decode("utf-8", "replace"))

print("\n%s（失败 %d 项）" % ("全部通过" if fails == 0 else "存在失败", fails))
sys.exit(0 if fails == 0 else 1)
