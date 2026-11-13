# _test_comui_bump_report.py —— 模拟 ComUI 宿主调用 alg.point3d.bump_report 节点（ctypes）
# 验证：返回 0 / bump 点云 / 统计消息（高度+直径+NG）/ 高度 NG 检出 / 边界
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
random.seed(51)

fails = 0


def check(ok, name):
    global fails
    print(("  [PASS] " if ok else "  [FAIL] ") + name)
    if not ok:
        fails += 1


# 合成：9 bump 阵列（同高同径）+ 中心 bump 高度 NG（+0.06）且位置偏移 +0.08（位置度 NG）
positions = [(1.0 + (i % 3) * 1.5, 1.0 + (i // 3) * 1.5) for i in range(9)]
# NG 定义先落到 bump 列表（勿放进逐格点内层循环：条件命中一次后期望位置被改写，
# 其余格点会退化生成普通 bump——NG/位移完全进不了云）
bumpDefs = []
for k, (cx, cy) in enumerate(positions):
    h = 0.19
    if k == 4:
        h = 0.25
        cx += 0.08
    bumpDefs.append((cx, cy, h))
ngPos = (bumpDefs[4][0], bumpDefs[4][1])   # 位移后期望位置 (2.58, 2.5)
xyz = []
for i in range(0, 126):
    for j in range(0, 126):
        x, y = i * 0.04, j * 0.04
        z = 1.0 + random.gauss(0, 0.002)
        for (cx, cy, h) in bumpDefs:
            R = 0.13
            dx, dy = x - cx, y - cy
            d2 = dx * dx + dy * dy
            zc = 1.0 + h - R
            dMax = R if zc >= 1.0 else math.sqrt(max(0.0, R * R - (1.0 - zc) ** 2))
            if d2 < dMax * dMax:
                zb = zc + math.sqrt(R * R - d2)
                if zb > z:
                    z = zb + random.gauss(0, 0.001)
        xyz += [x, y, z]
buf = (ct.c_float * len(xyz))(*xyz)

dll = ct.CDLL(os.path.join(DLL_DIR, "Point3DAlg.dll"))
dll.Point3DAlg_ComuiBumpReport.argtypes = [ct.POINTER(AlgoCInput), ct.POINTER(AlgoCOutput)]
dll.Point3DAlg_ComuiBumpReport.restype = ct.c_int
dll.algo_free.argtypes = [ct.c_void_p]

params = (AlgoCParam * 11)(
    AlgoCParam(0, 0.02, 0, None),    # height_threshold
    AlgoCParam(0, 0.25, 0, None),    # cluster_tolerance
    AlgoCParam(0, 0.15, 0, None),    # height_min
    AlgoCParam(0, 0.22, 0, None),    # height_max（0.25 的中心 bump NG）
    AlgoCParam(0, 0.0, 0, None),     # diameter_min（不判）
    AlgoCParam(0, 0.0, 0, None),     # diameter_max（不判）
    AlgoCParam(0, 1.5, 0, None),     # nominal_pitch_x（启用位置度；与阵列实际间距一致）
    AlgoCParam(0, 1.5, 0, None),     # nominal_pitch_y
    AlgoCParam(0, 1.0, 0, None),     # nominal_origin_x（首个 bump 位置）
    AlgoCParam(0, 1.0, 0, None),     # nominal_origin_y
    AlgoCParam(0, 0.05, 0, None),    # pos_threshold
)
cloud = AlgoCCloud(len(xyz) // 3, buf, None)
inp = AlgoCInput(11, params, 0, None, 1, ct.pointer(cloud))
out = AlgoCOutput()

rc = dll.Point3DAlg_ComuiBumpReport(ct.pointer(inp), ct.pointer(out))
msg = (out.error or b"").decode("utf-8", "replace")
print("return=%d msg=%s" % (rc, msg))
check(rc == 0, "返回 0")
check(out.cloudCount == 1 and out.clouds, "输出 bump 点云")
arr = out.clouds[0]
check(arr.count == 9, f"检出 9 个 bump（实测 {arr.count}）")
import re
m = re.search(r"NG 高度 (\d+)", msg)
check(m is not None and int(m.group(1)) == 1, "高度 NG = 1（+0.06 中心 bump）")
check("位置度 1（漏测 0）" in msg, "消息含位置度 NG=1（同 bump 双 NG）")
# NG bump 高度 ≈ 0.25（apex 落 0.04 格点，期望位置偏差可到 0.02）
ngH = [arr.xyz[i * 3 + 2] for i in range(arr.count)
       if abs(arr.xyz[i * 3] - ngPos[0]) < 0.03 and abs(arr.xyz[i * 3 + 1] - ngPos[1]) < 0.03]
check(len(ngH) == 1 and abs(ngH[0] - 0.25) < 0.005, f"NG bump 高度 ≈ 0.25（实测 {ngH}）")
check("共面性σ" in msg, "消息含共面性统计")
dll.algo_free(arr.xyz)
if arr.rgb:
    dll.algo_free(arr.rgb)
dll.algo_free(ct.cast(out.clouds, ct.c_void_p))

# 边界：纯平面
xyzFlat = []
for i in range(0, 51):
    for j in range(0, 51):
        xyzFlat += [i * 0.04, j * 0.04, 1.0 + random.gauss(0, 0.002)]
bufF = (ct.c_float * len(xyzFlat))(*xyzFlat)
cloudF = AlgoCCloud(len(xyzFlat) // 3, bufF, None)
inp2 = AlgoCInput(6, params, 0, None, 1, ct.pointer(cloudF))
out2 = AlgoCOutput()
rc2 = dll.Point3DAlg_ComuiBumpReport(ct.pointer(inp2), ct.pointer(out2))
check(rc2 == 4 and out2.error, "纯平面返回 4: %s" % (out2.error or b"").decode("utf-8", "replace"))

print("\n%s（失败 %d 项）" % ("全部通过" if fails == 0 else "存在失败", fails))
sys.exit(0 if fails == 0 else 1)
