# _test_comui_cloud_diff.py —— 模拟 ComUI 宿主调用 alg.point3d.cloud_diff 节点（ctypes）
# 验证：返回 0 / 差值点云 / 统计消息（全局抬升 + 局部凸起）/ 边界
import ctypes as ct
import os
import re
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
random.seed(11)

fails = 0


def check(ok, name):
    global fails
    print(("  [PASS] " if ok else "  [FAIL] ") + name)
    if not ok:
        fails += 1


# 合成：基准 z=1.0（σ=2µm）；被测 = 基准 + 局部凸起（[1,2]×[1,2]mm +0.05）+ 全局 +0.002
xyzA, xyzB = [], []
for i in range(0, 101):
    for j in range(0, 101):
        x, y = i * 0.04, j * 0.04
        za = 1.0 + random.gauss(0, 0.002)
        bump = 0.05 if (1.0 <= x <= 2.0 and 1.0 <= y <= 2.0) else 0.0
        zb = za + 0.002 + bump + random.gauss(0, 0.001)
        xyzA += [x, y, za]
        xyzB += [x, y, zb]
bufA = (ct.c_float * len(xyzA))(*xyzA)
bufB = (ct.c_float * len(xyzB))(*xyzB)

dll = ct.CDLL(os.path.join(DLL_DIR, "Point3DAlg.dll"))
dll.Point3DAlg_ComuiCloudDiff.argtypes = [ct.POINTER(AlgoCInput), ct.POINTER(AlgoCOutput)]
dll.Point3DAlg_ComuiCloudDiff.restype = ct.c_int
dll.algo_free.argtypes = [ct.c_void_p]

cloudA = AlgoCCloud(len(xyzA) // 3, bufA, None)
cloudB = AlgoCCloud(len(xyzB) // 3, bufB, None)
params = (AlgoCParam * 3)(AlgoCParam(0, 0.1, 0, None), AlgoCParam(1, 0.0, 3, None),
                          AlgoCParam(0, 0.02, 0, None))
inp = AlgoCInput(3, params, 0, None, 2, (AlgoCCloud * 2)(cloudA, cloudB))
out = AlgoCOutput()

rc = dll.Point3DAlg_ComuiCloudDiff(ct.pointer(inp), ct.pointer(out))
msg = (out.error or b"").decode("utf-8", "replace")
print("return=%d msg=%s" % (rc, msg))
check(rc == 0, "返回 0")
check(out.cloudCount == 1 and out.clouds, "输出差值点云")
m = re.search(r"cells=(\d+) mean=([-\d.+]+) .*超差\(>[\d.]+\) (\d+) \(([\d.]+)%\)", msg)
if m:
    cells, mean, over, ratio = int(m.group(1)), float(m.group(2)), int(m.group(3)), float(m.group(4))
    # 云 4×4mm=16mm²；凸起 [1,2]²=1mm²（占 6.25%）；全局抬升 0.002
    check(cells >= 900, f"cells={cells} ≈ 900+")
    # mean = 0.002 全局 + 0.05×6.25% 凸起贡献 ≈ 0.0051
    check(0.004 <= mean <= 0.006, f"mean={mean} ≈ 0.0051（全局 0.002 + 凸起贡献）")
    # 凸起 1mm² / 0.01mm²格 = 100 格（边缘格部分被 min_points 过滤）
    check(70 <= over <= 130, f"超差格 {over} ≈ 100（凸起 1mm²）")
    check(4.0 <= ratio <= 9.0, f"超差占比 {ratio}% ≈ 面积比 6.25%")
else:
    check(False, "消息解析失败")

if out.clouds:
    # 解引用差值点云（布局：count + float* xyz + rgb）
    class AlgoCCloudOut(ct.Structure):
        _fields_ = [("count", ct.c_int32), ("xyz", ct.POINTER(ct.c_float)), ("rgb", ct.POINTER(ct.c_uint8))]
    arr = ct.cast(out.clouds, ct.POINTER(AlgoCCloudOut)).contents
check(arr.count >= 900, "差值点数合理")
hasBump = any(abs(arr.xyz[i + 2] - 0.052) < 0.006 for i in range(0, arr.count * 3, 3))
check(hasBump, "差值点云含凸起 z≈0.052 的点（NG 可视化）")
dll.algo_free(arr.xyz)
if arr.rgb:
    dll.algo_free(arr.rgb)
dll.algo_free(ct.cast(out.clouds, ct.c_void_p))

# 对齐路径：十字台阶形貌（平面对 XY 平移不可观测——得分全平解任意），
# B 错位 (0.3, -0.2)，align_search=0.5 → 对齐量 ≈ (-0.3, 0.2)
surf = lambda x, y: 1.0 + (0.5 if x >= 2.0 else 0.0) + (0.3 if y >= 2.0 else 0.0)
xyzA2, xyzB2 = [], []
for i in range(0, 101):
    for j in range(0, 101):
        x, y = i * 0.04, j * 0.04
        xyzA2 += [x, y, surf(x, y) + random.gauss(0, 0.002)]
        xyzB2 += [x + 0.3, y - 0.2, surf(x, y) + 0.02 + random.gauss(0, 0.002)]
bufA2 = (ct.c_float * len(xyzA2))(*xyzA2)
bufB2 = (ct.c_float * len(xyzB2))(*xyzB2)
params3 = (AlgoCParam * 4)(AlgoCParam(0, 0.1, 0, None), AlgoCParam(1, 0.0, 3, None),
                           AlgoCParam(0, 0.02, 0, None), AlgoCParam(0, 0.5, 0, None))
cloudA2 = AlgoCCloud(len(xyzA2) // 3, bufA2, None)
cloudB2 = AlgoCCloud(len(xyzB2) // 3, bufB2, None)
inp3 = AlgoCInput(4, params3, 0, None, 2, (AlgoCCloud * 2)(cloudA2, cloudB2))
out3 = AlgoCOutput()
rc3 = dll.Point3DAlg_ComuiCloudDiff(ct.pointer(inp3), ct.pointer(out3))
msg3 = (out3.error or b"").decode("utf-8", "replace")
print("align rc=%d msg=%s" % (rc3, msg3))
check(rc3 == 0, "对齐差分返回 0")
m3 = re.search(r"mean=([+\-\d.]+).*对齐([+\-\d.]+),([+\-\d.]+)", msg3)
check(m3 is not None, "消息含对齐量")
if m3:
    mean3 = float(m3.group(1))
    adx, ady = float(m3.group(2)), float(m3.group(3))
    check(abs(adx + 0.3) < 0.06 and abs(ady - 0.2) < 0.06,
          f"对齐量 ({adx:.3f},{ady:.3f}) ≈ (-0.3,0.2)（±cell/2）")
    check(abs(mean3 - 0.02) < 0.004, f"对齐后 mean {mean3} = 真实 z 差 0.02")

# 边界：只连一片云
inp2 = AlgoCInput(3, params, 0, None, 1, (AlgoCCloud * 1)(cloudA))
out2 = AlgoCOutput()
rc2 = dll.Point3DAlg_ComuiCloudDiff(ct.pointer(inp2), ct.pointer(out2))
check(rc2 == 1 and out2.error, "单云输入返回 1: %s" % (out2.error or b"").decode("utf-8", "replace"))

print("\n%s（失败 %d 项）" % ("全部通过" if fails == 0 else "存在失败", fails))
sys.exit(0 if fails == 0 else 1)
