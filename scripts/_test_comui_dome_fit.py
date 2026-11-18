# _test_comui_dome_fit.py —— 模拟 ComUI 宿主调用 alg.point3d.dome_fit 节点（ctypes）
# 验证：返回 0 / 输出点数 = 合成 bump 数 / 高度接近真值 / 消息含统计
import ctypes as ct
import math
import os
import sys

sys.stdout.reconfigure(encoding="utf-8", errors="replace")   # GBK 控制台打 µ/中文

# DLL directory: argv[1] overrides; default = canonical <repo>/build/output/Release
DLL_DIR = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "build", "output", "Release")
os.add_dll_directory(DLL_DIR)

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

class AlgoCOutput(ct.Structure):
    _fields_ = [("imageCount", ct.c_int32), ("images", ct.c_void_p),
                ("cloudCount", ct.c_int32), ("clouds", ct.POINTER(AlgoCCloudOut)),
                ("error", ct.c_char_p)]

import random
random.seed(2026)
def noise(sigma=0.002):
    return random.gauss(0.0, sigma)

# ---- 合成点云：10x10mm 基面 + 4 个 bump（h/R=1.45，与实测形态一致）----
truth = []
for (i, j), h in zip([(0, 0), (0, 1), (1, 0), (1, 1)], [0.15, 0.19, 0.22, 0.19]):
    truth.append((2.5 + i * 3.0, 2.5 + j * 3.0, h, h / 1.45))

xyz = []
step = 0.036
n = int(10.0 / step) + 1
for i in range(n):
    for j in range(n):
        x, y = i * step, j * step
        z = noise()
        for (cx, cy, h, R) in truth:
            dx, dy = x - cx, y - cy
            r2 = dx * dx + dy * dy
            capR2 = 2.0 * R * h - h * h
            if capR2 > 0 and r2 < capR2:
                zb = (h - R) + math.sqrt(R * R - r2)
                if zb > z:
                    z = zb + noise(0.001)
        xyz += [x, y, z]
xyz = (ct.c_float * len(xyz))(*xyz)

fails = 0
def check(ok, name):
    global fails
    print(("  [PASS] " if ok else "  [FAIL] ") + name)
    if not ok:
        fails += 1

dll = ct.CDLL(os.path.join(DLL_DIR, "Point3DAlg.dll"))
dll.Point3DAlg_ComuiDomeFit.argtypes = [ct.POINTER(AlgoCInput), ct.POINTER(AlgoCOutput)]
dll.Point3DAlg_ComuiDomeFit.restype = ct.c_int
dll.algo_free.argtypes = [ct.c_void_p]

# ---- 参数按 node.json 声明顺序：number, number, int, number ----
params = (AlgoCParam * 4)(
    AlgoCParam(0, 0.02, 0, None),     # height_threshold
    AlgoCParam(0, 0.12, 0, None),     # cluster_tolerance
    AlgoCParam(1, 0.0, 8, None),      # min_points
    AlgoCParam(0, 0.005, 0, None),    # max_fit_rms
)
cloud = AlgoCCloud(len(xyz) // 3, xyz, None)
inp = AlgoCInput(4, params, 0, None, 1, ct.pointer(cloud))
out = AlgoCOutput()

rc = dll.Point3DAlg_ComuiDomeFit(ct.pointer(inp), ct.pointer(out))
print("return=%d error=%s" % (rc, (out.error or b"").decode("utf-8", "replace")))
check(rc == 0, "返回 0")
check(out.cloudCount == 1 and out.clouds, "输出 1 片点云")
if rc == 0 and out.clouds:
    o = out.clouds[0]
    print("输出点数=%d" % o.count)
    check(o.count == len(truth), "输出点数 = bump 数（%d）" % len(truth))
    # 高度与真值比对（按 xy 最近匹配）
    errs = []
    for k in range(o.count):
        px, py, pz = o.xyz[k * 3], o.xyz[k * 3 + 1], o.xyz[k * 3 + 2]
        best = min(truth, key=lambda t: (px - t[0]) ** 2 + (py - t[1]) ** 2)
        errs.append(abs(pz - best[2]))
    maxE = max(errs) if errs else 1.0
    print("高度误差 max=%.2fµm" % (maxE * 1000))
    check(maxE < 0.008, "高度误差 < 8µm")
    # 输出 z 均为高度（>0.1mm）
    check(all(o.xyz[k * 3 + 2] > 0.1 for k in range(o.count)), "输出 z 均为有效高度")
    # 消息含统计
    msg = (out.error or b"").decode("utf-8", "replace")
    check("bump" in msg and "个" in msg, "消息含统计: %s" % msg)
    dll.algo_free(out.clouds[0].xyz)
    if out.clouds[0].rgb:
        dll.algo_free(out.clouds[0].rgb)
    dll.algo_free(ct.cast(out.clouds, ct.c_void_p))

# ---- 边界：空输入 ----
inp2 = AlgoCInput(4, params, 0, None, 0, None)
rc2 = dll.Point3DAlg_ComuiDomeFit(ct.pointer(inp2), ct.pointer(out))
check(rc2 == 1 and out.error, "未连线返回 1: %s" % (out.error or b"").decode("utf-8", "replace"))

print("\n%s（失败 %d 项）" % ("全部通过" if fails == 0 else "存在失败", fails))
sys.exit(0 if fails == 0 else 1)
