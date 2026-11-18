# _test_comui_chain_image2d.py —— 2D 节点级链路冒烟（模拟 ComUI 宿主串链，ctypes）
# 链 A：image2d_read → image2d_binarize →（分叉）
#         ├─ image2d_connected_components（组件数/质心与构造数据一致）
#         └─ image2d_fit_circles（圆数/圆心/半径与构造数据一致）
#       注：CC 节点成功输出为"输入克隆叠加绿框"，叠加框非零像素会污染下游
#       二值语义节点，故 fit_circles 从 binarize 输出分叉取数（宿主图结构的
#       正常形态）；两跳输出图尺寸均与输入一致。
# 链 B：image2d_read → image2d_canny → image2d_hough_lines（线数 >=1 且角度合理）
# 新节点烟测（覆盖矩阵 A 类补缺，正/错路径）：
#   warp_affine / warp_perspective / find_matches_roi / match_template_pyramid /
#   convexity_defects（经 contour_cloud → convexity_defects 真实链路验证）
# 每跳输出缓冲（images/clouds 数组与内部缓冲）用 algo_free 释放，不重复释放。
# 用法：python _test_comui_chain_image2d.py [DLL 目录]（默认 <repo>/build/output/Release）
import ctypes as ct
import math
import os
import re
import struct
import sys
import tempfile
import binascii
import zlib

sys.stdout.reconfigure(encoding="utf-8", errors="replace")   # GBK 控制台打中文

# DLL directory: argv[1] overrides; default = canonical <repo>/build/output/Release
DLL_DIR = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "build", "output", "Release"))
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

class AlgoCImageOut(ct.Structure):
    _fields_ = [("width", ct.c_int32), ("height", ct.c_int32), ("bgra", ct.POINTER(ct.c_uint8))]

class AlgoCCloudOut(ct.Structure):
    _fields_ = [("count", ct.c_int32), ("xyz", ct.POINTER(ct.c_float)), ("rgb", ct.POINTER(ct.c_uint8))]

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

dll = ct.CDLL(os.path.join(DLL_DIR, "Image2DAlg.dll"))
def entry(name):
    f = getattr(dll, name)
    f.argtypes = [ct.POINTER(AlgoCInput), ct.POINTER(AlgoCOutput)]
    f.restype = ct.c_int
    return f
dll.algo_free.argtypes = [ct.c_void_p]

E = {n: entry(n) for n in [
    "Image2DAlg_ComuiRead", "Image2DAlg_ComuiBinarize",
    "Image2DAlg_ComuiConnectedComponents", "Image2DAlg_ComuiFitCircles",
    "Image2DAlg_ComuiCanny", "Image2DAlg_ComuiHoughLines",
    "Image2DAlg_ComuiContourCloud",
    "Image2DAlg_ComuiWarpAffine", "Image2DAlg_ComuiWarpPerspective",
    "Image2DAlg_ComuiFindMatchesRoi", "Image2DAlg_ComuiMatchTemplatePyramid",
    "Image2DAlg_ComuiConvexityDefects",
]}

# ---- 参数构造（kind：0=double 1=int32 2=text；宿主 marshal 按声明顺序）----
def Pd(v): return AlgoCParam(0, float(v), 0, None)
def Pi(v): return AlgoCParam(1, 0.0, int(v), None)
def Pt(s):
    b = s.encode("utf-8") if isinstance(s, str) else s
    return AlgoCParam(2, 0.0, 0, b)

def call(fn, params=(), images=(), clouds=()):
    """组装 AlgoCInput 调用节点入口；返回 (rc, out)。"""
    parr = (AlgoCParam * len(params))(*params) if params else None
    iarr = (AlgoCImage * len(images))(*images) if images else None
    carr = (AlgoCCloud * len(clouds))(*clouds) if clouds else None
    inp = AlgoCInput(len(params), parr, len(images), iarr, len(clouds), carr)
    out = AlgoCOutput()
    rc = fn(ct.pointer(inp), ct.pointer(out))
    return rc, out

def free_out(out):
    """每跳输出缓冲 algo_free 释放（images/clouds 数组 + 内部缓冲），不重复。"""
    if out.images:
        imgs = ct.cast(out.images, ct.POINTER(AlgoCImageOut))
        for i in range(out.imageCount):
            if imgs[i].bgra:
                dll.algo_free(imgs[i].bgra)
        dll.algo_free(ct.cast(out.images, ct.c_void_p))
    if out.clouds:
        for i in range(out.cloudCount):
            c = out.clouds[i]
            if c.xyz:
                dll.algo_free(c.xyz)
            if c.rgb:
                dll.algo_free(c.rgb)
        dll.algo_free(ct.cast(out.clouds, ct.c_void_p))

def out_image(out):
    """输出单图像 → (w, h, bytes BGRA)。"""
    assert out.imageCount == 1 and out.images, "expected 1 image out"
    img = ct.cast(out.images, ct.POINTER(AlgoCImageOut))[0]
    n = img.width * img.height * 4
    return img.width, img.height, bytes(ct.cast(img.bgra, ct.POINTER(ct.c_uint8 * n)).contents)

def out_cloud(out):
    assert out.cloudCount == 1 and out.clouds, "expected 1 cloud out"
    c = out.clouds[0]
    return c.count, [c.xyz[i] for i in range(c.count * 3)]

def msg(out):
    return (out.error or b"").decode("utf-8", "replace")

def msg_count(out):
    m = re.search(r"count=(\d+)", msg(out))
    return int(m.group(1)) if m else -1

def img_view(w, h, data):
    """BGRA bytes → 常驻 ctypes 缓冲 + AlgoCImage 视图（返回 (buf, AlgoCImage)）。"""
    buf = (ct.c_uint8 * (w * h * 4)).from_buffer_copy(data)
    return buf, AlgoCImage(w, h, buf)

# ---- PNG 写入（stdlib：zlib + CRC；8-bit RGB，ReadImage 支持 png）----
def write_png(path, w, h, rows):
    """rows: 每行 bytearray（RGB 3 字节/像素）。"""
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + \
               struct.pack(">I", binascii.crc32(tag + data) & 0xffffffff)
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)   # 8bit, color type 2 (RGB)
    raw = b"".join(b"\x00" + bytes(r) for r in rows)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
                chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))

# =====================================================================
# 构造图（%TEMP%，白底 + 黑色规则几何，保证可检出）
# =====================================================================
W, H = 240, 240
TMP = tempfile.mkdtemp(prefix="comui_chain_img2d_")

def make_canvas():
    return [bytearray(b"\xff" * (W * 3)) for _ in range(H)]   # 白底

def fill_disk(rows, cx, cy, r):
    for y in range(cy - r - 1, cy + r + 2):
        for x in range(cx - r - 1, cx + r + 2):
            if 0 <= x < W and 0 <= y < H and (x - cx) ** 2 + (y - cy) ** 2 <= r * r:
                rows[y][x * 3:x * 3 + 3] = b"\x00\x00\x00"

# 图 A：4 个黑色圆盘 r=12（链 A：二值化反转后为前景）
DISKS = [(50, 50), (150, 50), (50, 150), (150, 150)]
R = 12
rowsA = make_canvas()
for (cx, cy) in DISKS:
    fill_disk(rowsA, cx, cy, R)
pathA = os.path.join(TMP, "chain_a_disks.png")
write_png(pathA, W, H, rowsA)

# 图 B：黑色矩形 (60,60)-(140,120) + 3px 粗 45° 斜线（链 B：边缘/直线检出）
rowsB = make_canvas()
for y in range(60, 120):
    for x in range(60, 140):
        rowsB[y][x * 3:x * 3 + 3] = b"\x00\x00\x00"
for x in range(170, 221):
    y = 40 + (x - 170)
    for dy in (-1, 0, 1):
        if 0 <= y + dy < H:
            rowsB[y + dy][x * 3:x * 3 + 3] = b"\x00\x00\x00"
pathB = os.path.join(TMP, "chain_b_lines.png")
write_png(pathB, W, H, rowsB)

# 模板：图 A 在 (34,34) 处 33x33 裁块（含圆盘 0 全貌 + 4px 白边）——find_matches_roi /
# match_template_pyramid 用。模板与搜索图同源，保证像素级可命中。
T_SIZE = 33
rowsT = [rowsA[34 + dy][34 * 3:34 * 3 + T_SIZE * 3] for dy in range(T_SIZE)]
pathT = os.path.join(TMP, "chain_tpl_disk.png")
write_png(pathT, T_SIZE, T_SIZE, rowsT)
TPL_C = [(50, 50), (150, 50), (50, 150), (150, 150)]      # 期望命中中心
TPL_TL = [(cx - 16, cy - 16) for (cx, cy) in TPL_C]       # 期望命中左上角

print("temp dir:", TMP)

# =====================================================================
# 链 A：read → binarize →（分叉）connected_components / fit_circles
# =====================================================================
print("\n[链 A] read → binarize → connected_components / fit_circles")

rc, outRead = call(E["Image2DAlg_ComuiRead"], [Pt(pathA)])
check(rc == 0, "read 返回 0")
w, h, bgraA = out_image(outRead)
check((w, h) == (W, H), "read 输出尺寸 %dx%d" % (w, h))
bufA, imgA = img_view(w, h, bgraA)

# binarize：mode 0 固定二值 + invert=1（白底→0，黑盘→255 前景）
paramsBin = [Pi(0), Pd(128), Pd(200), Pd(255), Pd(128), Pi(3), Pd(0), Pi(1)]
rc, outBin = call(E["Image2DAlg_ComuiBinarize"], paramsBin, images=[imgA])
check(rc == 0, "binarize 返回 0")
wb, hb, bgraBin = out_image(outBin)
check((wb, hb) == (W, H), "binarize 输出尺寸不变 %dx%d" % (wb, hb))
bufBin, binImg = img_view(wb, hb, bgraBin)

# 前景像素 = 4 个圆盘面积和（构造数据一致性检查）
fg = sum(1 for i in range(0, len(bgraBin), 4) if bgraBin[i])
expect_fg = sum(sum(1 for y in range(cy - R, cy + R + 1)
                    for x in range(cx - R, cx + R + 1)
                    if (x - cx) ** 2 + (y - cy) ** 2 <= R * R) for (cx, cy) in DISKS)
check(abs(fg - expect_fg) <= 40, "binarize 前景像素 %d ≈ 构造面积 %d" % (fg, expect_fg))

# 分叉 1：connected_components（8 邻域，min_area=100 → 恰 4 个圆盘组件）
rc, outCC = call(E["Image2DAlg_ComuiConnectedComponents"],
                 [Pi(8), Pd(100), Pd(0)], images=[binImg])
check(rc == 0, "connected_components 返回 0")
wc, hc, _ = out_image(outCC)
check((wc, hc) == (W, H), "CC 输出尺寸不变 %dx%d" % (wc, hc))
nCC = msg_count(outCC)
check(nCC == len(DISKS), "CC 组件数 = 构造圆盘数（%d）" % nCC)
cents = [(float(a), float(b)) for a, b in re.findall(r"c=\(([\d.]+),([\d.]+)\)", msg(outCC))]
check(len(cents) == nCC, "CC 消息含每组件质心（%d 条）" % len(cents))
if len(cents) == len(DISKS):
    errs = [min(abs(px - cx) + abs(py - cy) for (cx, cy) in DISKS) for (px, py) in cents]
    check(max(errs) <= 2.0, "CC 质心与构造圆心偏差 <= 2px（max %.2f）" % max(errs))
free_out(outCC)

# 分叉 2：fit_circles（min_area=100，非亚像素——纯二值 Kåsa，圆参数与构造一致）
rc, outFit = call(E["Image2DAlg_ComuiFitCircles"],
                  [Pd(100), Pd(0), Pd(0), Pd(0), Pi(0)], images=[binImg])
check(rc == 0, "fit_circles 返回 0")
wf, hf, _ = out_image(outFit)
check((wf, hf) == (W, H), "fit_circles 输出尺寸不变 %dx%d" % (wf, hf))
nFit = msg_count(outFit)
check(nFit == len(DISKS), "fit_circles 圆数 = 构造圆盘数（%d）" % nFit)
fits = [(float(a), float(b), float(r)) for a, b, r in
        re.findall(r"c=\(([-\d.]+),([-\d.]+)\) r=([\d.]+)", msg(outFit))]
check(len(fits) == nFit, "fit_circles 消息含每圆参数（%d 条）" % len(fits))
if len(fits) == len(DISKS):
    cerr = max(min(abs(fx - cx) + abs(fy - cy) for (cx, cy) in DISKS) for (fx, fy, _) in fits)
    rerr = max(abs(r - R) for (_, _, r) in fits)
    check(cerr <= 3.0, "圆心与构造圆心偏差 <= 3px（max %.2f）" % cerr)
    check(rerr <= 1.5, "半径与构造半径偏差 <= 1.5px（max %.2f）" % rerr)
free_out(outFit)

free_out(outBin)
free_out(outRead)

# =====================================================================
# 链 B：read → canny → hough_lines
# =====================================================================
print("\n[链 B] read → canny → hough_lines")

rc, outRead2 = call(E["Image2DAlg_ComuiRead"], [Pt(pathB)])
check(rc == 0, "read 返回 0")
_, _, bgraB = out_image(outRead2)
bufB, imgB = img_view(W, H, bgraB)

rc, outCanny = call(E["Image2DAlg_ComuiCanny"], [Pd(50), Pd(150)], images=[imgB])
check(rc == 0, "canny 返回 0")
wcn, hcn, bgraEdges = out_image(outCanny)
check((wcn, hcn) == (W, H), "canny 输出尺寸不变 %dx%d" % (wcn, hcn))
edges = sum(1 for i in range(0, len(bgraEdges), 4) if bgraEdges[i])
check(edges > 200, "canny 边缘像素充足（%d）" % edges)
bufE, imgE = img_view(wcn, hcn, bgraEdges)

rc, outHough = call(E["Image2DAlg_ComuiHoughLines"],
                    [Pd(1), Pd(1), Pi(50), Pi(40), Pi(5)], images=[imgE])
check(rc == 0, "hough_lines 返回 0")
_, _, _ = out_image(outHough)   # 输出为叠加图（尺寸同输入）
nLines = msg_count(outHough)
check(nLines >= 1, "检出线数 >= 1（%d）" % nLines)
segs = [(int(a), int(b), int(c), int(d)) for a, b, c, d in
        re.findall(r"\((\d+),(\d+)\)-\((\d+),(\d+)\)", msg(outHough))]
check(len(segs) == nLines, "hough_lines 消息含每线段端点（%d 条）" % len(segs))
# 角度合理性：构造几何仅 0/45/90 度线（矩形边 + 对角线），容差 15 度
def ang_ok(x1, y1, x2, y2):
    a = math.degrees(math.atan2(y2 - y1, x2 - x1)) % 180.0
    return min(abs(a - t) if abs(a - t) <= 90 else 180 - abs(a - t)
               for t in (0.0, 45.0, 90.0)) <= 15.0
check(all(ang_ok(*s) for s in segs), "全部线段角度 ∈ {0,45,90}±15°")
free_out(outHough)
free_out(outCanny)
free_out(outRead2)

# =====================================================================
# 新节点烟测（覆盖矩阵 A 类补缺，正/错路径）
# =====================================================================
print("\n[新节点] warp_affine / warp_perspective / find_matches_roi / "
      "match_template_pyramid / convexity_defects")

# imgA：复用链 A 的 read 输出视图（bufA 缓冲常驻，未随 free_out 释放——
# free_out 只释放节点 C 侧输出缓冲，img_view 的是独立 ctypes 拷贝）。

# ---- 模板图 BGRA（read 模板 PNG）----
rc, outReadT = call(E["Image2DAlg_ComuiRead"], [Pt(pathT)])
check(rc == 0, "read 模板图返回 0")
wt, ht, bgraT = out_image(outReadT)
check((wt, ht) == (T_SIZE, T_SIZE), "模板尺寸 %dx%d" % (wt, ht))
bufT, imgT = img_view(wt, ht, bgraT)

# ---- warp_affine：恒等矩阵 → 输出与输入逐字节一致 ----
rc, outW = call(E["Image2DAlg_ComuiWarpAffine"],
                [Pt("1,0,0,0,1,0"), Pi(0), Pi(0), Pi(1)], images=[imgA])
check(rc == 0, "warp_affine(恒等) 返回 0")
ww, wh, bgraW = out_image(outW)
check((ww, wh) == (W, H), "warp_affine 输出尺寸 = 输入尺寸（0=同输入）")
check(bgraW == bgraA, "恒等矩阵输出与输入逐字节一致")
check("out=240x240" in msg(outW), "消息回显输出尺寸: %s" % msg(outW))
free_out(outW)

# ---- warp_affine：平移 (20,10) → 圆盘整体右下移动；错路径：矩阵值个数不足 ----
rc, outW2 = call(E["Image2DAlg_ComuiWarpAffine"],
                 [Pt("1,0,20,0,1,10"), Pi(0), Pi(0), Pi(1)], images=[imgA])
check(rc == 0, "warp_affine(平移) 返回 0")
_, _, bgraW2 = out_image(outW2)
def px(b, x, y):
    o = (y * W + x) * 4
    return b[o]
check(px(bgraW2, 70, 60) < 100 and px(bgraA, 70, 60) > 200,
      "平移 (20,10)：圆盘移到 (70,60)（黑）")
check(px(bgraW2, 50, 50) > 200, "原圆心处变为白底")
free_out(outW2)
rc, _ = call(E["Image2DAlg_ComuiWarpAffine"],
             [Pt("1,0,0,0,1"), Pi(0), Pi(0), Pi(1)], images=[imgA])
check(rc == 3, "warp_affine 错路径：矩阵非 6 值 → 3")
rc, outE = call(E["Image2DAlg_ComuiWarpAffine"], [], images=[])
check(rc == 1, "warp_affine 错路径：输入未连线 → 1")

# ---- warp_perspective：恒等 3x3 → 逐字节一致；错路径：8 值 → 3 ----
rc, outP = call(E["Image2DAlg_ComuiWarpPerspective"],
                [Pt("1,0,0,0,1,0,0,0,1"), Pi(0), Pi(0), Pi(1)], images=[imgA])
check(rc == 0, "warp_perspective(恒等) 返回 0")
_, _, bgraP = out_image(outP)
check(bgraP == bgraA, "恒等单应输出与输入逐字节一致")
free_out(outP)
rc, _ = call(E["Image2DAlg_ComuiWarpPerspective"],
             [Pt("1,0,0,0,1,0,0,0"), Pi(0), Pi(0), Pi(1)], images=[imgA])
check(rc == 3, "warp_perspective 错路径：矩阵非 9 值 → 3")

# ---- find_matches_roi：全图 4 命中 → ROI 内恰 1 命中（坐标平移回全图系）----
paramsFMR = [Pi(0), Pi(0), Pi(0), Pi(0), Pi(5), Pd(0.8), Pi(10), Pi(5)]
rc, outFMR = call(E["Image2DAlg_ComuiFindMatchesRoi"], paramsFMR, images=[imgA, imgT])
check(rc == 0, "find_matches_roi(全图) 返回 0")
nFMR = msg_count(outFMR)
check(nFMR == 4, "全图命中数 = 圆盘数（%d）" % nFMR)
locs = [(int(a), int(b), float(s)) for a, b, s in
        re.findall(r"\((\d+),(\d+)\) score=([\d.]+)", msg(outFMR))]
if nFMR == 4 and len(locs) == 4:
    perr = max(min(abs(x - tx) + abs(y - ty) for (tx, ty) in TPL_TL) for (x, y, _) in locs)
    check(perr <= 2, "命中左上角与构造位置偏差 <= 2px（max %.2f）" % perr)
    check(all(s >= 0.8 for (_, _, s) in locs), "全部得分 >= 0.8（min %.3f）" %
          min(s for (_, _, s) in locs))
free_out(outFMR)

rc, outFMR2 = call(E["Image2DAlg_ComuiFindMatchesRoi"],
                   [Pi(20), Pi(20), Pi(60), Pi(60), Pi(5), Pd(0.8), Pi(10), Pi(5)],
                   images=[imgA, imgT])
check(rc == 0, "find_matches_roi(ROI 60x60) 返回 0")
nFMR2 = msg_count(outFMR2)
check(nFMR2 == 1, "ROI 内恰 1 命中（%d）" % nFMR2)
locs2 = re.findall(r"\((\d+),(\d+)\) score=([\d.]+)", msg(outFMR2))
if locs2:
    x, y, _ = locs2[0]
    check(abs(int(x) - 34) <= 2 and abs(int(y) - 34) <= 2,
          "ROI 命中坐标平移回全图系 ≈ (34,34)：(%s,%s)" % (x, y))
free_out(outFMR2)
rc, _ = call(E["Image2DAlg_ComuiFindMatchesRoi"],
             [Pi(0), Pi(0), Pi(20), Pi(20), Pi(5), Pd(0.8), Pi(10), Pi(5)],
             images=[imgA, imgT])
check(rc == 3, "find_matches_roi 错路径：模板大于 ROI → 3")
rc, _ = call(E["Image2DAlg_ComuiFindMatchesRoi"], paramsFMR, images=[imgA])
check(rc == 1, "find_matches_roi 错路径：缺模板输入 → 1")

# ---- match_template_pyramid：单尺度 ±3° 搜索，命中圆盘 0 中心 ----
paramsPy = [Pi(5), Pd(1.0), Pd(1.0), Pd(0.1), Pd(-3), Pd(3), Pd(3), Pi(2)]
rc, outPy = call(E["Image2DAlg_ComuiMatchTemplatePyramid"], paramsPy, images=[imgA, imgT])
check(rc == 0, "match_template_pyramid 返回 0")
mPy = re.search(r"score=([\d.]+) c=\(([-\d.]+),([-\d.]+)\) scale=([\d.]+) "
                r"angle=([-\d.]+) levels=(\d+)", msg(outPy))
check(mPy is not None, "消息含 score/c/scale/angle/levels: %s" % msg(outPy))
if mPy:
    score, cx, cy = float(mPy.group(1)), float(mPy.group(2)), float(mPy.group(3))
    check(score >= 0.7, "得分 >= 0.7（%.3f）" % score)
    check(abs(cx - 50) <= 3 and abs(cy - 50) <= 3, "中心落点 ≈ (50,50)：(%.1f,%.1f)" % (cx, cy))
    check(abs(float(mPy.group(5))) <= 4.0, "命中角度 ≈ 0（%s°）" % mPy.group(5))
free_out(outPy)
rc, _ = call(E["Image2DAlg_ComuiMatchTemplatePyramid"],
             [Pi(5), Pd(1.0), Pd(1.0), Pd(0.1), Pd(-3), Pd(3), Pd(0), Pi(2)],
             images=[imgA, imgT])
check(rc == 3, "match_template_pyramid 错路径：angle_step=0 → 3")

# ---- convexity_defects：真实链路 contour_cloud → convexity_defects ----
# 构造带凹口的白色前景块（前景非零语义）：方框 (20,20)-(100,100)，顶部中间
# 挖凹口 (50..70)x(20..50)——凸包缺口深度 = 30px，最深点落在凹口底 y=50。
# 注：输入点集取 contour_cloud 输出（findContours 点序，节点声明的上游）——
#     C API 的点序自适应仅覆盖该来源（手工密集点序实测会得到角落伪缺陷，
#     见覆盖矩阵审计"意外发现"）。
BW, BH = 128, 128
blob = bytearray()
for y in range(BH):
    for x in range(BW):
        inside = 20 <= x < 100 and 20 <= y < 100 and not (50 <= x < 70 and 20 <= y < 50)
        blob += b"\xff\xff\xff\xff" if inside else b"\x00\x00\x00\x00"
bufBlob, imgBlob = img_view(BW, BH, bytes(blob))

rc, outCloud = call(E["Image2DAlg_ComuiContourCloud"],
                    [Pi(1), Pd(1)], images=[imgBlob])
check(rc == 0, "contour_cloud 返回 0")
check("contours=1" in msg(outCloud), "contour_cloud 检出 1 条轮廓: %s" % msg(outCloud))
co = out_cloud(outCloud)          # (count, [x,y,z,...])
free_out(outCloud)
cloud = AlgoCCloud(co[0], (ct.c_float * (co[0] * 3))(*co[1]), None)

rc, outCvd = call(E["Image2DAlg_ComuiConvexityDefects"], [], clouds=[cloud])
check(rc == 0, "convexity_defects 返回 0")
nCvd = msg_count(outCvd)
check(nCvd >= 1, "检出凹陷数 >= 1（%d）" % nCvd)
depths = [float(d) for d in re.findall(r"depth=([\d.]+)", msg(outCvd))]
check(depths and max(depths) >= 25 and max(depths) <= 35,
      "凹口深度 ≈ 30px（%.1f）" % (max(depths) if depths else -1))
far = [(float(a), float(b)) for a, b in re.findall(r"pt=\(([-\d.]+),([-\d.]+)\)", msg(outCvd))]
if depths and far:
    i = depths.index(max(depths))
    check(abs(far[i][1] - 50) <= 2.0 and 48 <= far[i][0] <= 72,
          "最深点在凹口底 y≈50：(%0.1f,%0.1f)" % far[i])
cnt, xyz = out_cloud(outCvd)      # 缺陷最深点 z=0 点云
check(cnt == nCvd, "输出缺陷点云点数 = 缺陷数（%d）" % cnt)
check(all(abs(xyz[i * 3 + 2]) < 1e-6 for i in range(cnt)), "输出点云 z=0")
free_out(outCvd)
pts2 = (ct.c_float * 4)(0, 0, 10, 0)   # 2 点输入 → 点数不足
cloud2 = AlgoCCloud(2, pts2, None)
rc, _ = call(E["Image2DAlg_ComuiConvexityDefects"], [], clouds=[cloud2])
check(rc == 2, "convexity_defects 错路径：点数 < 3 → 2")

free_out(outReadT)

print("\n%s（失败 %d 项）" % ("全部通过" if fails == 0 else "存在失败", fails))
sys.exit(0 if fails == 0 else 1)
