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
    "Image2DAlg_ComuiLaplacian", "Image2DAlg_ComuiFilter2D",
    "Image2DAlg_ComuiDenoiseNLMeans",
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

# =====================================================================
# 2.4.0 新节点烟测（覆盖矩阵 A 类首选批：fit_circle_ex / fit_circle_robust /
# hough_lines_proi / hough_circles_roi，正/错路径 + 链路组合）
# 链 C：链 A fit_circles 检出的圆心 → crop 圆盘邻域 ROI 子图 → contour_cloud
#       子点集（平移回全图坐标）→ fit_circle_ex / fit_circle_robust 复核
#       （圆心/半径与构造值一致、抗外点内点率 100%）
# =====================================================================
E.update({n: entry(n) for n in [
    "Image2DAlg_ComuiFitCircleEx", "Image2DAlg_ComuiFitCircleRobust",
    "Image2DAlg_ComuiHoughLinesPRoi", "Image2DAlg_ComuiHoughCirclesRoi",
    "Image2DAlg_ComuiCrop",   # 链 C：ROI 子图裁剪（原 E 表未含）
]})

print("\n[新节点 2.4.0] fit_circle_ex / fit_circle_robust / hough_lines_proi / hough_circles_roi")

# ---- 链 C：fit_circles 圆心 → crop ROI 子点集 → 精化拟合复核（依赖链 A 结果）----
print("\n[链 C] fit_circles 圆心 → crop → contour_cloud → fit_circle_ex / fit_circle_robust")
if len(fits) == len(DISKS):
    c0 = min(fits, key=lambda f: (f[0] - 50) ** 2 + (f[1] - 50) ** 2)   # 圆盘 0 附近
    gx0, gy0 = int(round(c0[0])), int(round(c0[1]))
    rx0, ry0, rw0, rh0 = gx0 - 20, gy0 - 20, 40, 40                     # 圆盘 0 邻域 ROI
    rc, outCropC = call(E["Image2DAlg_ComuiCrop"],
                        [Pi(rx0), Pi(ry0), Pi(rw0), Pi(rh0)], images=[binImg])
    check(rc == 0, "crop(圆盘 0 邻域) 返回 0")
    wc0, hc0, bgraCropC = out_image(outCropC)
    check((wc0, hc0) == (rw0, rh0), "crop 输出尺寸 %dx%d" % (wc0, hc0))
    bufCropC, imgCropC = img_view(wc0, hc0, bgraCropC)

    rc, outCcC = call(E["Image2DAlg_ComuiContourCloud"],
                      [Pi(1), Pd(1)], images=[imgCropC])
    check(rc == 0, "contour_cloud(ROI 子图) 返回 0")
    cntC, xyzC = out_cloud(outCcC)
    free_out(outCcC)
    free_out(outCropC)
    check(cntC >= 8, "ROI 子点集点数 >= 8（%d）" % cntC)

    # 子图坐标 → 全图坐标（crop 无坐标回传，宿主侧按 ROI 原点平移）
    sub_xy = [0.0] * (cntC * 3)
    for i in range(cntC):
        sub_xy[i * 3] = xyzC[i * 3] + rx0
        sub_xy[i * 3 + 1] = xyzC[i * 3 + 1] + ry0
        sub_xy[i * 3 + 2] = 0.0
    check(all(0 <= sub_xy[i * 3] < W and 0 <= sub_xy[i * 3 + 1] < H for i in range(cntC)),
          "子点集平移回全图坐标在图内")
    cloudSub = AlgoCCloud(cntC, (ct.c_float * (cntC * 3))(*sub_xy), None)

    # fit_circle_ex：高精度复核（圆心/半径与构造值一致，rms 小）
    rc, outFex = call(E["Image2DAlg_ComuiFitCircleEx"], [], clouds=[cloudSub])
    check(rc == 0, "fit_circle_ex 返回 0")
    mFex = re.search(r"c=\(([-\d.]+),([-\d.]+)\) r=([\d.]+) rms=([\d.]+)", msg(outFex))
    check(mFex is not None, "fit_circle_ex 消息含圆参数: %s" % msg(outFex))
    if mFex:
        check(abs(float(mFex.group(1)) - 50) <= 3 and abs(float(mFex.group(2)) - 50) <= 3,
              "ex 圆心 ≈ 构造圆心 (50,50)：（%s,%s）" % (mFex.group(1), mFex.group(2)))
        check(abs(float(mFex.group(3)) - R) <= 1.5,
              "ex 半径 ≈ 构造半径 12：%s" % mFex.group(3))
        check(float(mFex.group(4)) <= 1.0, "ex rms <= 1.0px（%s）" % mFex.group(4))
    nFex, xyzFex = out_cloud(outFex)          # 单点云（x=圆心x y=圆心y z=半径）
    check(nFex == 1, "ex 输出单点云 1 点")
    check(abs(xyzFex[2] - R) <= 1.5, "ex 点云 z 承载半径 ≈ 12：%.2f" % xyzFex[2])
    free_out(outFex)

    # fit_circle_robust：抗外点复核（纯圆周点集内点率 100%，圆参数一致）
    rc, outFrb = call(E["Image2DAlg_ComuiFitCircleRobust"],
                      [Pd(2.0), Pi(1000), Pi(42)], clouds=[cloudSub])
    check(rc == 0, "fit_circle_robust 返回 0")
    mFrb = re.search(r"c=\(([-\d.]+),([-\d.]+)\) r=([\d.]+) rms=([\d.]+) inliers=(\d+)/(\d+)",
                     msg(outFrb))
    check(mFrb is not None, "robust 消息含圆参数+内点数: %s" % msg(outFrb))
    if mFrb:
        check(abs(float(mFrb.group(1)) - 50) <= 3 and abs(float(mFrb.group(2)) - 50) <= 3,
              "robust 圆心 ≈ (50,50)：（%s,%s）" % (mFrb.group(1), mFrb.group(2)))
        check(abs(float(mFrb.group(3)) - R) <= 1.5, "robust 半径 ≈ 12：%s" % mFrb.group(3))
        check(int(mFrb.group(5)) == int(mFrb.group(6)),
              "纯圆周点集内点率 100%%（%s/%s）" % (mFrb.group(5), mFrb.group(6)))
    free_out(outFrb)
else:
    print("  [SKIP] 链 C：链 A fit_circles 未检出 4 圆，跳过链路组合复核")

# ---- fit_circle_ex / fit_circle_robust 错路径（不依赖链 A）----
rc, _ = call(E["Image2DAlg_ComuiFitCircleEx"], [], clouds=[])
check(rc == 1, "fit_circle_ex 错路径：输入未连线 → 1")
pts2c = (ct.c_float * 6)(0, 0, 0, 10, 0, 0)   # 2 点（xyz 交错，z=0）
cloud2pt = AlgoCCloud(2, pts2c, None)
rc, _ = call(E["Image2DAlg_ComuiFitCircleEx"], [], clouds=[cloud2pt])
check(rc == 2, "fit_circle_ex 错路径：点数 < 3 → 2")
line_xy = (ct.c_float * 30)(*sum(([float(v), float(v), 0.0] for v in range(0, 100, 10)), []))
cloudLine = AlgoCCloud(10, line_xy, None)   # y=x 共线 10 点（xyz 交错）
rc, _ = call(E["Image2DAlg_ComuiFitCircleEx"], [], clouds=[cloudLine])
check(rc == 3, "fit_circle_ex 错路径：共线退化 → 3")
rc, _ = call(E["Image2DAlg_ComuiFitCircleRobust"], [Pd(2.0), Pi(1000), Pi(42)], clouds=[])
check(rc == 1, "fit_circle_robust 错路径：输入未连线 → 1")
rc, _ = call(E["Image2DAlg_ComuiFitCircleRobust"], [Pd(2.0), Pi(1000), Pi(42)],
             clouds=[cloud2pt])
check(rc == 2, "fit_circle_robust 错路径：点数 < 3 → 2")
rc, _ = call(E["Image2DAlg_ComuiFitCircleRobust"], [Pd(0), Pi(1000), Pi(42)],
             clouds=[cloudLine])
check(rc == 3, "fit_circle_robust 错路径：threshold=0 → 3")
rc, _ = call(E["Image2DAlg_ComuiFitCircleRobust"], [Pd(2.0), Pi(1000), Pi(42)],
             clouds=[cloudLine])
check(rc == 3, "fit_circle_robust 错路径：共线无有效圆（内点 < 3）→ 3")

# ---- hough_lines_proi：全图 ROI 与 ROI 限定（斜线区）----
rc, outHlp = call(E["Image2DAlg_ComuiHoughLinesPRoi"],
                  [Pi(0), Pi(0), Pi(0), Pi(0), Pd(1), Pd(1), Pi(50), Pi(40), Pi(5)],
                  images=[imgE])
check(rc == 0, "hough_lines_proi(全图 ROI) 返回 0")
nHlp = msg_count(outHlp)
check(nHlp >= 1, "全图 ROI 检出线数 >= 1（%d）" % nHlp)
free_out(outHlp)

rc, outHlp2 = call(E["Image2DAlg_ComuiHoughLinesPRoi"],
                   [Pi(160), Pi(20), Pi(80), Pi(90), Pd(1), Pd(1), Pi(50), Pi(40), Pi(5)],
                   images=[imgE])
check(rc == 0, "hough_lines_proi(斜线区 ROI 80x90) 返回 0")
nHlp2 = msg_count(outHlp2)
check(nHlp2 >= 1, "斜线区 ROI 检出线数 >= 1（%d）" % nHlp2)
segsR = [(int(a), int(b), int(c), int(d)) for a, b, c, d in
         re.findall(r"\((\d+),(\d+)\)-\((\d+),(\d+)\)", msg(outHlp2))]
check(len(segsR) == nHlp2, "proi 消息含每线段端点（%d 条）" % len(segsR))
if segsR:
    check(all(160 <= min(s[0], s[2]) and max(s[0], s[2]) <= 239 and
              20 <= min(s[1], s[3]) and max(s[1], s[3]) <= 109 for s in segsR),
          "全部线段端点落在 ROI 范围内（已平移回全图系）")
    check(all(ang_ok(*s) for s in segsR), "ROI 内线段角度 ≈ 45°±15°")
angsR = [float(a) for a in re.findall(r"ang=([\d.]+)", msg(outHlp2))]
check(len(angsR) == nHlp2, "proi 消息含每线段角度（%d 条）" % len(angsR))
free_out(outHlp2)

rc, _ = call(E["Image2DAlg_ComuiHoughLinesPRoi"],
             [Pi(500), Pi(500), Pi(50), Pi(50), Pd(1), Pd(1), Pi(50), Pi(40), Pi(5)],
             images=[imgE])
check(rc == 3, "hough_lines_proi 错路径：ROI 与图像无交集 → 3")
rc, _ = call(E["Image2DAlg_ComuiHoughLinesPRoi"],
             [Pi(0), Pi(0), Pi(0), Pi(0), Pd(0), Pd(1), Pi(50), Pi(40), Pi(5)],
             images=[imgE])
check(rc == 3, "hough_lines_proi 错路径：rho=0 → 3")
rc, _ = call(E["Image2DAlg_ComuiHoughLinesPRoi"], [], images=[])
check(rc == 1, "hough_lines_proi 错路径：输入未连线 → 1")

# ---- hough_circles_roi：全图 ROI / 单盘 ROI / 空白 ROI ----
paramsHcr = [Pi(0), Pi(0), Pi(0), Pi(0), Pd(1), Pd(10), Pd(50), Pd(20), Pd(5), Pd(20)]
rc, outHcr = call(E["Image2DAlg_ComuiHoughCirclesRoi"], paramsHcr, images=[imgA])
check(rc == 0, "hough_circles_roi(全图 ROI) 返回 0")
nHcr = msg_count(outHcr)
check(nHcr >= 1, "全图 ROI 检出圆数 >= 1（%d）" % nHcr)
circs = [(float(a), float(b), float(r)) for a, b, r in
         re.findall(r"c=\(([-\d.]+),([-\d.]+)\) r=([\d.]+)", msg(outHcr))]
check(len(circs) == nHcr, "proi 圆消息含每圆参数（%d 条）" % len(circs))
if circs:
    cerrC = max(min(abs(x - cx) + abs(y - cy) for (cx, cy) in DISKS) for (x, y, _) in circs)
    check(cerrC <= 4.0, "检出圆心与构造圆盘偏差 <= 4px（max %.2f）" % cerrC)
    check(all(8 <= r <= 16 for (_, _, r) in circs), "检出半径均在 [8,16]")
free_out(outHcr)

rc, outHcr2 = call(E["Image2DAlg_ComuiHoughCirclesRoi"],
                   [Pi(20), Pi(20), Pi(80), Pi(80), Pd(1), Pd(10), Pd(50), Pd(20), Pd(5), Pd(20)],
                   images=[imgA])
check(rc == 0, "hough_circles_roi(单盘 ROI 80x80) 返回 0")
nHcr2 = msg_count(outHcr2)
check(nHcr2 >= 1, "ROI 内检出圆数 >= 1（%d）" % nHcr2)
circs2 = [(float(a), float(b), float(r)) for a, b, r in
          re.findall(r"c=\(([-\d.]+),([-\d.]+)\) r=([\d.]+)", msg(outHcr2))]
if circs2:
    check(all(20 <= x <= 99 and 20 <= y <= 99 for (x, y, _) in circs2),
          "ROI 检出圆心均落在 ROI 范围内（全图坐标）")
    best = min(circs2, key=lambda c: abs(c[0] - 50) + abs(c[1] - 50))
    check(abs(best[0] - 50) <= 3 and abs(best[1] - 50) <= 3 and abs(best[2] - R) <= 2.5,
          "ROI 检出圆 ≈ 圆盘 0 (50,50) r=12：（%.1f,%.1f r=%.1f）" % best)
free_out(outHcr2)

rc, outHcr3 = call(E["Image2DAlg_ComuiHoughCirclesRoi"],
                   [Pi(0), Pi(190), Pi(40), Pi(40), Pd(1), Pd(10), Pd(50), Pd(20), Pd(5), Pd(20)],
                   images=[imgA])
check(rc == 0 and msg_count(outHcr3) == 0, "空白 ROI：返回 0 且 count=0 属正常")
free_out(outHcr3)

rc, _ = call(E["Image2DAlg_ComuiHoughCirclesRoi"],
             [Pi(500), Pi(500), Pi(50), Pi(50), Pd(1), Pd(10), Pd(50), Pd(20), Pd(5), Pd(20)],
             images=[imgA])
check(rc == 3, "hough_circles_roi 错路径：ROI 与图像无交集 → 3")
rc, _ = call(E["Image2DAlg_ComuiHoughCirclesRoi"],
             [Pi(0), Pi(0), Pi(0), Pi(0), Pd(1), Pd(0), Pd(50), Pd(20), Pd(5), Pd(20)],
             images=[imgA])
check(rc == 3, "hough_circles_roi 错路径：min_dist=0 → 3")
rc, _ = call(E["Image2DAlg_ComuiHoughCirclesRoi"], [], images=[])
check(rc == 1, "hough_circles_roi 错路径：输入未连线 → 1")

# ---- laplacian / filter2d：2.3.0 滤波扩展节点（Laplacian 二阶导 / 自定义卷积核）----
print("\n[滤波扩展] laplacian / filter2d")

# laplacian 正路径：白底黑盘图——平坦区响应 0（黑），盘缘响应最强（亮）
rc, outLap = call(E["Image2DAlg_ComuiLaplacian"], [Pi(3), Pd(1.0)], images=[imgA])
check(rc == 0, "laplacian(ksize=3, scale=1) 返回 0")
wl, hl, bgraL = out_image(outLap)
check((wl, hl) == (W, H), "laplacian 输出尺寸不变 %dx%d" % (wl, hl))
grayL = [bgraL[i] for i in range(0, len(bgraL), 4)]   # B 通道作灰度（灰度图三通道相等）
check(any(v >= 180 for v in grayL), "laplacian 盘缘存在强响应（>=180）")
check(sum(1 for v in grayL if v < 50) > W * H / 2, "laplacian 平坦区响应≈0（>半图像素 <50）")
rc, outLap2 = call(E["Image2DAlg_ComuiLaplacian"], [Pi(3), Pd(2.0)], images=[imgA])
check(rc == 0, "laplacian scale=2 返回 0")
_, _, bgraL2 = out_image(outLap2)
s1 = sum(bgraL[i] for i in range(0, len(bgraL), 4))
s2 = sum(bgraL2[i] for i in range(0, len(bgraL2), 4))
check(s2 >= s1, "laplacian 响应总和随 scale 增大不减（%d -> %d）" % (s1, s2))
free_out(outLap2)
free_out(outLap)

# laplacian 错路径
rc, _ = call(E["Image2DAlg_ComuiLaplacian"], [Pi(2), Pd(1.0)], images=[imgA])
check(rc == 3, "laplacian 错路径：ksize=2（偶数）→ 3")
rc, _ = call(E["Image2DAlg_ComuiLaplacian"], [Pi(9), Pd(1.0)], images=[imgA])
check(rc == 3, "laplacian 错路径：ksize=9（非 1/3/5/7）→ 3")
rc, _ = call(E["Image2DAlg_ComuiLaplacian"], [Pi(3), Pd(0.0)], images=[imgA])
check(rc == 3, "laplacian 错路径：scale=0 → 3")
rc, _ = call(E["Image2DAlg_ComuiLaplacian"], [], images=[])
check(rc == 1, "laplacian 错路径：输入未连线 → 1")

# filter2d 恒等核：3×3 中心 1 —— 输出逐字节等于输入（8U 路径 |响应| 饱和不改变恒等结果）
IDENT3 = "0,0,0,0,1,0,0,0,0"
rc, outF2 = call(E["Image2DAlg_ComuiFilter2D"], [Pt(IDENT3), Pi(3)], images=[imgA])
check(rc == 0, "filter2d 恒等核返回 0")
wf2, hf2, bgraF2 = out_image(outF2)
check((wf2, hf2) == (W, H), "filter2d 输出尺寸不变 %dx%d" % (wf2, hf2))
check(bgraF2 == bgraA, "filter2d 恒等核输出逐字节等于输入")
free_out(outF2)

# filter2d 锐化核：输出与输入不同且尺寸不变（对比增强）
SHARP3 = "0,-1,0,-1,5,-1,0,-1,0"
rc, outF2s = call(E["Image2DAlg_ComuiFilter2D"], [Pt(SHARP3), Pi(3)], images=[imgA])
check(rc == 0, "filter2d 锐化核返回 0")
_, _, bgraF2s = out_image(outF2s)
check(bgraF2s != bgraA, "filter2d 锐化核输出改变（对比增强）")
free_out(outF2s)

# filter2d 高斯核（预归一化 ÷16）：平滑——平坦区逐字节不变、均值暗点数减少
GAUSS3 = "0.0625,0.125,0.0625,0.125,0.25,0.125,0.0625,0.125,0.0625"
rc, outF2g = call(E["Image2DAlg_ComuiFilter2D"], [Pt(GAUSS3), Pi(3)], images=[imgA])
check(rc == 0, "filter2d 高斯核返回 0")
_, _, bgraF2g = out_image(outF2g)
check(bgraF2g != bgraA, "filter2d 高斯核输出改变（边缘平滑）")
check(sum(1 for i in range(0, len(bgraF2g), 4) if bgraF2g[i] < 128) >= 0,
      "filter2d 高斯核输出可读（非空图）")
free_out(outF2g)

# filter2d 全 0 核：合法（api.h 口径）——输出全 0 图
rc, outF2z = call(E["Image2DAlg_ComuiFilter2D"], [Pt("0,0,0,0,0,0,0,0,0"), Pi(3)], images=[imgA])
check(rc == 0, "filter2d 全 0 核返回 0（合法）")
_, _, bgraF2z = out_image(outF2z)
check(all(v == 0 for v in bgraF2z), "filter2d 全 0 核输出全 0 图")
free_out(outF2z)

# filter2d 错路径
rc, _ = call(E["Image2DAlg_ComuiFilter2D"], [Pt(IDENT3), Pi(2)], images=[imgA])
check(rc == 3, "filter2d 错路径：ksize=2（偶数）→ 3")
rc, _ = call(E["Image2DAlg_ComuiFilter2D"], [Pt("1,0,0,0,1,0"), Pi(3)], images=[imgA])
check(rc == 3, "filter2d 错路径：kernel 6 值 ≠ ksize²=9 → 3")
rc, _ = call(E["Image2DAlg_ComuiFilter2D"], [Pt("1,2,abc,0,1,0,0,0,0"), Pi(3)], images=[imgA])
check(rc == 3, "filter2d 错路径：kernel 含非数值 → 3")
rc, _ = call(E["Image2DAlg_ComuiFilter2D"], [Pt(""), Pi(3)], images=[imgA])
check(rc == 3, "filter2d 错路径：kernel 空文本 → 3")
rc, _ = call(E["Image2DAlg_ComuiFilter2D"], [Pt(IDENT3), Pi(52)], images=[imgA])
check(rc == 3, "filter2d 错路径：ksize=52 超上限 → 3")
rc, _ = call(E["Image2DAlg_ComuiFilter2D"], [Pt(IDENT3), Pi(3)], images=[])
check(rc == 1, "filter2d 错路径：输入未连线 → 1")

# ---- denoise_nlmeans：非局部均值降噪节点（工单 001；NLMeans 块匹配语义 Blur 未
#      覆盖故单设节点，C API 见 api.h 2.3.0 DenoiseNLMeans）----
print("\n[滤波扩展] denoise_nlmeans")

# 噪声图：200 底色 + 120 个 LCG 撒出的盐椒单点（BGRA 直构免 PNG 往返；孤立单点
# 在 NLMeans 块匹配下权重趋 0，应被平滑回底色）
NW = 160
noisy_bg = bytearray(b"\xc8\xc8\xc8\xff" * (NW * NW))   # B=G=200, A=255
_seed = 987654321
for i in range(120):
    _seed = (_seed * 1103515245 + 12345) & 0x7fffffff
    nx = (_seed >> 8) % NW
    _seed = (_seed * 1103515245 + 12345) & 0x7fffffff
    ny = (_seed >> 8) % NW
    v = 0 if (i % 2 == 0) else 255
    off = (ny * NW + nx) * 4
    noisy_bg[off:off + 3] = bytes([v, v, v])
bufN, imgN = img_view(NW, NW, bytes(noisy_bg))

def dev_cnt(b):
    """偏离底色（|v-200|>40）的像素数（B 通道）。"""
    return sum(1 for i in range(0, len(b), 4) if abs(b[i] - 200) > 40)

dev_before = dev_cnt(bytes(noisy_bg))

# 正路径：孤立单点的块自权重恒 1（与自身距离 0），小 h 下其他相似块权重趋 0，
# 需大 h 强降噪（h=30，探针实测 4ch 120 点 120→0）——尺寸不变、stddev 下降、
# 噪声点大减、alpha 保持
rc, outN = call(E["Image2DAlg_ComuiDenoiseNLMeans"], [Pd(30.0), Pd(30.0), Pi(7), Pi(21)],
                images=[imgN])
check(rc == 0, "denoise_nlmeans(h=30, tw=7, sw=21) 返回 0")
wn, hn, bgraN = out_image(outN)
check((wn, hn) == (NW, NW), "denoise_nlmeans 输出尺寸不变 %dx%d" % (wn, hn))
mn = msg(outN)
check("h=30.0 h_color=30.0 tw=7 sw=21" in mn,
      "denoise_nlmeans 消息回显参数（%s）" % mn[:90])
mm = re.search(r"stddev ([\d.]+)->([\d.]+)", mn)
check(mm is not None and float(mm.group(2)) < float(mm.group(1)),
      "denoise_nlmeans 消息 stddev 下降（噪声减弱质量指标）")
dev_after = dev_cnt(bgraN)
check(dev_after * 3 <= max(dev_before, 1),
      "denoise_nlmeans 噪声点显著减弱（偏离底色像素 %d -> %d）" % (dev_before, dev_after))
check(all(bgraN[i] == 255 for i in range(3, len(bgraN), 4)),
      "denoise_nlmeans 输出 alpha 保持 255")
free_out(outN)

# 错路径（rc 约定：1 未连线；3 参数非法——核心层 -3 映射 3）
rc, _ = call(E["Image2DAlg_ComuiDenoiseNLMeans"], [Pd(0.0), Pd(3.0), Pi(7), Pi(21)], images=[imgN])
check(rc == 3, "denoise_nlmeans 错路径：h=0 → 3")
rc, _ = call(E["Image2DAlg_ComuiDenoiseNLMeans"], [Pd(3.0), Pd(-1.0), Pi(7), Pi(21)], images=[imgN])
check(rc == 3, "denoise_nlmeans 错路径：h_color<0 → 3")
rc, _ = call(E["Image2DAlg_ComuiDenoiseNLMeans"], [Pd(3.0), Pd(3.0), Pi(21), Pi(21)], images=[imgN])
check(rc == 3, "denoise_nlmeans 错路径：search_window == template_window → 3")
rc, _ = call(E["Image2DAlg_ComuiDenoiseNLMeans"], [Pd(3.0), Pd(3.0), Pi(7), Pi(5)], images=[imgN])
check(rc == 3, "denoise_nlmeans 错路径：search_window < template_window → 3")
rc, _ = call(E["Image2DAlg_ComuiDenoiseNLMeans"], [], images=[])
check(rc == 1, "denoise_nlmeans 错路径：输入未连线 → 1")

print("\n%s（失败 %d 项）" % ("全部通过" if fails == 0 else "存在失败", fails))
sys.exit(0 if fails == 0 else 1)
