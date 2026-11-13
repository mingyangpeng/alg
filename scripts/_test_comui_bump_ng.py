# _test_comui_bump_ng.py —— 模拟 ComUI 宿主调用 business.bump_ng_check 节点（ctypes）
# 验证：基准/被测 bump 云 → DomeFit×2 + BumpCompare 编排 → NG 判定统计 + 差异点云
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
random.seed(41)

fails = 0


def check(ok, name):
    global fails
    print(("  [PASS] " if ok else "  [FAIL] ") + name)
    if not ok:
        fails += 1


# ---- 合成：基面 z=1.0 + bump 阵列（半球形球冠，DomeFit 可检出）----
# 基准：49 个 bump（7×7 网格 pitch 1.2mm，h=0.19 R=h/1.45）
# 被测：同位但 3 个 bump +0.05（NG>0.02）、2 个缺失；其余高度一致
def make_cloud(bump_heights, positions, bump=True):
    xyz = []
    for i in range(0, 101):
        for j in range(0, 101):
            x, y = i * 0.04, j * 0.04
            z = 1.0 + random.gauss(0, 0.002)
            for (cx, cy), h in zip(positions, bump_heights):
                dx, dy = x - cx, y - cy
                d2 = dx * dx + dy * dy
                R = h / 1.45
                capR2 = 2.0 * R * h - h * h
                if bump and capR2 > 0 and d2 < capR2:
                    zb = 1.0 + (h - R) + math.sqrt(R * R - d2)   # 绝对高度（基面 1.0）
                    if zb > z:
                        z = zb + random.gauss(0, 0.001)
            xyz += [x, y, z]
    return xyz


positions = [(0.7 + (i % 7) * 0.5, 0.7 + (i // 7) * 0.5) for i in range(49)]   # 7×7 pitch 0.5 ⊂ 4mm 网格
ref_h = [0.19] * 49
test_h = [0.19] * 49
test_pos = positions[:]
# 3 个 +0.05（NG）
for idx in (10, 20, 30):
    test_h[idx] = 0.24
# 2 个缺失（从尾部删）
for idx in (48, 47):
    test_pos.pop(idx)
    test_h.pop(idx)

bufA = (ct.c_float * len(ref_h * 0))(*[])  # placeholder
xyzA = make_cloud(ref_h, positions)
xyzB = make_cloud(test_h, test_pos)
bufA = (ct.c_float * len(xyzA))(*xyzA)
bufB = (ct.c_float * len(xyzB))(*xyzB)

dll = ct.CDLL(os.path.join(DLL_DIR, "BusinessAlg.dll"))
dll.BusinessAlg_ComuiBumpNgCheck.argtypes = [ct.POINTER(AlgoCInput), ct.POINTER(AlgoCOutput)]
dll.BusinessAlg_ComuiBumpNgCheck.restype = ct.c_int
dll.algo_free.argtypes = [ct.c_void_p]

params = (AlgoCParam * 4)(
    AlgoCParam(0, 0.02, 0, None),   # ng_threshold
    AlgoCParam(0, 0.3, 0, None),    # match_radius
    AlgoCParam(0, 0.02, 0, None),   # height_threshold
    AlgoCParam(0, 0.25, 0, None),   # cluster_tolerance（冠直径 0.23 > 默认 0.12 会分裂）
)
cloudA = AlgoCCloud(len(xyzA) // 3, bufA, None)
cloudB = AlgoCCloud(len(xyzB) // 3, bufB, None)
inp = AlgoCInput(4, params, 0, None, 2, (AlgoCCloud * 2)(cloudA, cloudB))
out = AlgoCOutput()

rc = dll.BusinessAlg_ComuiBumpNgCheck(ct.pointer(inp), ct.pointer(out))
msg = (out.error or b"").decode("utf-8", "replace")
print("return=%d msg=%s" % (rc, msg))
check(rc == 0, "返回 0")
import re
m = re.search(r"配对 (\d+)/(\d+)/(\d+)（.*NG (\d+) 个.*dh mean ([+\-\d.]+)", msg)
check(m is not None, "消息解析")
if m:
    matched, miss, extra, ng = (int(m.group(1)), int(m.group(2)),
                                int(m.group(3)), int(m.group(4)))
    dh_mean = float(m.group(5))
    # DomeFit 检出数可能略少于生成数（边缘/噪声），NG 数以实际配对为准：
    # 3 个 +0.05 bump 若都被检出并配对 → NG=3
    check(45 <= matched <= 49, f"配对 {matched} ≈ 45~49")
    check(ng == 3, f"NG = 3（实测 {ng}）")
    check(abs(dh_mean) < 0.01, f"dh_mean {dh_mean} ≈ 0（正负噪声抵消）")

if out.clouds:
    arr = out.clouds[0]
    # 差异点云含全部配对（47 点，z=高度差），其中 3 个 |z|>0.03 为 NG
    check(arr.count == matched, f"差异点云 = 配对数（{arr.count}）")
    ngPts = [arr.xyz[i * 3 + 2] for i in range(arr.count)
             if abs(arr.xyz[i * 3 + 2]) > 0.03]
    check(len(ngPts) == 3, f"差异点云含 3 个 |dh|>0.03 NG 点（实测 {len(ngPts)}）")
    dll.algo_free(arr.xyz)
    if arr.rgb:
        dll.algo_free(arr.rgb)
    dll.algo_free(ct.cast(out.clouds, ct.c_void_p))

# 边界：单云
inp2 = AlgoCInput(4, params, 0, None, 1, (AlgoCCloud * 1)(cloudA))
out2 = AlgoCOutput()
rc2 = dll.BusinessAlg_ComuiBumpNgCheck(ct.pointer(inp2), ct.pointer(out2))
check(rc2 == 1 and out2.error, "单云返回 1: %s" % (out2.error or b"").decode("utf-8", "replace"))

print("\n%s（失败 %d 项）" % ("全部通过" if fails == 0 else "存在失败", fails))
sys.exit(0 if fails == 0 else 1)
