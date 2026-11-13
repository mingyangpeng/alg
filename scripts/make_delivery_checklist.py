# -*- coding: utf-8 -*-
"""make_delivery_checklist.py — 交付目录清单生成与必备项核对（仅 Python 标准库）。

扫描交付目录（缺省 <仓库>/build/output/Release，可用 argv[1] 覆盖），
向 stdout 输出 markdown 交付清单与核对结果，可重定向。

用法：
    python -I scripts/make_delivery_checklist.py [交付目录]

退出码：
    0  必备项齐全（5 算法 DLL + 9 node.json + pcl_*_release.dll 20 个）
    1  有必备项缺失（明细见输出"核对"段）
    2  用法错误 / 目录不存在
"""
import datetime
import sys
from pathlib import Path

ALG_DLLS = [
    "CommonAlg.dll",
    "ShapelyAlg.dll",
    "Image2DAlg.dll",
    "Point3DAlg.dll",
    "BusinessAlg.dll",
]
NODE_JSONS = [
    "business_region_ratio.node.json",
    "business_bump_ng_check.node.json",
    "image2d_absdiff.node.json",
    "image2d_analyze_contours.node.json",
    "image2d_analyze_contours_roi.node.json",
    "image2d_blur.node.json",
    "image2d_canny.node.json",
    "image2d_connected_components.node.json",
    "image2d_connected_components_roi.node.json",
    "image2d_crop.node.json",
    "image2d_enhance.node.json",
    "image2d_fit_circles.node.json",
    "image2d_fit_ellipses.node.json",
    "image2d_find_matches.node.json",
    "image2d_find_matches_ex.node.json",
    "image2d_gamma.node.json",
    "image2d_hough_circles.node.json",
    "image2d_hough_lines.node.json",
    "image2d_hsv_threshold.node.json",
    "image2d_hsv_threshold_wrap.node.json",
    "image2d_match_template_masked.node.json",
    "image2d_match_template_ex.node.json",
    "image2d_match_template_subpix.node.json",
    "image2d_morphology.node.json",
    "image2d_read.node.json",
    "image2d_region_stats.node.json",
    "image2d_resize.node.json",
    "image2d_rotate.node.json",
    "image2d_sobel.node.json",
    "image2d_threshold.node.json",
    "image2d_to_gray.node.json",
    "point3d_cloud_diff.node.json",
    "point3d_circle_fit.node.json",
    "point3d_profile.node.json",
    "point3d_bump_report.node.json",
    "point3d_dome_fit.node.json",
    "point3d_manual_regist.node.json",
    "point3d_read_sick.node.json",
    "point3d_region_plane.node.json",
    "point3d_voxel.node.json",
]
TEST_TOOL_EXES = [
    "CommonAlg_test.exe",
    "ShapelyAlg_test.exe",
    "Image2DAlg_test.exe",
    "Point3DAlg_RegistTest.exe",
    "Point3DAlg_DomeFitTest.exe",
    "Point3DAlg_RegionPlaneTest.exe",
    "Point3DAlg_CloudDiffTest.exe",
    "Point3DAlg_SickSynthTest.exe",
    "BusinessAlg_test.exe",
    "ShapelyAlg_demo.exe",
    "Point3DAlg_RegistTool.exe",
    "Point3DAlg_test.exe",
]
# 按 node.json method 声明，这 3 个 DLL 应含 algo_free 导出；脚本不做二进制级验证
ALGO_FREE_DLLS = ["Image2DAlg.dll", "Point3DAlg.dll", "BusinessAlg.dll"]
PCL_RELEASE_EXPECTED = 20


def human_size(n):
    size = float(n)
    for unit in ("B", "KB", "MB", "GB"):
        if size < 1024 or unit == "GB":
            return ("%d %s" % (n, unit)) if unit == "B" else ("%.1f %s" % (size, unit))
        size /= 1024.0


def row(p):
    st = p.stat()
    ts = datetime.datetime.fromtimestamp(st.st_mtime).strftime("%Y-%m-%d %H:%M:%S")
    return "- `%s` — %s，修改于 %s" % (p.name, human_size(st.st_size), ts)


def named_rows(expected, names_by_name):
    """按预期名单逐名输出：存在给明细，缺失给告警行。"""
    lines = []
    for name in expected:
        p = names_by_name.get(name)
        lines.append(row(p) if p else "- `%s` — **缺失**" % name)
    return lines


def group_summary(files):
    total = sum(f.stat().st_size for f in files)
    return "%d 个，合计 %s" % (len(files), human_size(total))


def main(argv):
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")

    if len(argv) > 2:
        print("用法: python -I make_delivery_checklist.py [交付目录]", file=sys.stderr)
        return 2
    if len(argv) == 2:
        target = Path(argv[1])
    else:
        target = Path(__file__).resolve().parents[1] / "build" / "output" / "Release"
    if not target.is_dir():
        print("[错误] 目录不存在或不是目录: %s" % target, file=sys.stderr)
        return 2

    entries = sorted(target.iterdir(), key=lambda p: p.name.lower())
    files = [p for p in entries if p.is_file()]
    subdirs = [p for p in entries if p.is_dir()]
    by_name = {p.name: p for p in files}

    expected_names = set(ALG_DLLS) | set(NODE_JSONS) | set(TEST_TOOL_EXES)
    pcl_rel, pcl_dbg = [], []
    opencv, geos, openni2, boost, unclassified = [], [], [], [], []
    for p in files:
        n = p.name.lower()
        if n.startswith("pcl_") and n.endswith(".dll"):
            (pcl_dbg if "_debug" in n else pcl_rel).append(p)
        elif n.startswith("opencv") and n.endswith(".dll"):
            opencv.append(p)
        elif n.startswith("geos") and n.endswith(".dll"):
            geos.append(p)
        elif n.startswith("openni2") and n.endswith(".dll"):
            openni2.append(p)
        elif n.startswith("boost") and n.endswith(".dll"):
            boost.append(p)
        elif p.name not in expected_names:
            unclassified.append(p)

    out = []
    out.append("# 交付目录清单")
    out.append("")
    out.append("- 扫描目录: `%s`" % target)
    out.append("- 生成时间: %s" % datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"))
    out.append("- 文件总数: %d，合计 %s（另有子目录 %d 个）"
               % (len(files), human_size(sum(p.stat().st_size for p in files)), len(subdirs)))
    out.append("")

    out.append("## 一、算法 DLL（%d 个模块）" % len(ALG_DLLS))
    out.append("")
    out.extend(named_rows(ALG_DLLS, by_name))
    out.append("")

    out.append("## 二、节点声明 node.json（%d 个）" % len(NODE_JSONS))
    out.append("")
    out.extend(named_rows(NODE_JSONS, by_name))
    out.append("")

    out.append("## 三、测试与工具 exe（%d 个，前 5 个为 ctest 注册测试入口）" % len(TEST_TOOL_EXES))
    out.append("")
    out.extend(named_rows(TEST_TOOL_EXES, by_name))
    out.append("")

    out.append("## 四、第三方运行时")
    out.append("")
    out.append("### PCL")
    out.append("- `pcl_*_release.dll`：%s（预期 20 个）" % group_summary(pcl_rel))
    out.append("- `pcl_*_debug.dll`：%s" % group_summary(pcl_dbg))
    out.append("")
    out.append("### OpenCV（%s）" % group_summary(opencv))
    out.extend("- `%s`（%s）" % (p.name, human_size(p.stat().st_size)) for p in opencv)
    out.append("")
    out.append("### GEOS（%s）" % group_summary(geos))
    out.extend("- `%s`（%s）" % (p.name, human_size(p.stat().st_size)) for p in geos)
    out.append("")
    out.append("### OpenNI2（%s）" % group_summary(openni2))
    out.extend("- `%s`（%s）" % (p.name, human_size(p.stat().st_size)) for p in openni2)
    out.append("")
    out.append("### Boost（%s）" % group_summary(boost))
    if boost:
        out.extend("- `%s`（%s）" % (p.name, human_size(p.stat().st_size)) for p in boost)
    else:
        out.append("- 无 —— 预期（Boost 1.68 为静态链接，无运行时 DLL，见 src/DEPENDENCIES.md）")
    out.append("")

    out.append("## 五、其他（未归类文件，%d 个）" % len(unclassified))
    out.append("")
    out.extend(row(p) for p in unclassified)
    for d in subdirs:
        out.append("- `%s/`（子目录，未展开）" % d.name)
    out.append("")

    # ---- 核对 ----
    problems = []
    miss_dll = [n for n in ALG_DLLS if n not in by_name]
    miss_node = [n for n in NODE_JSONS if n not in by_name]
    out.append("## 核对")
    out.append("")
    if miss_dll:
        problems.append("算法 DLL 缺失: %s" % ", ".join(miss_dll))
        out.append("- **[缺失]** 算法 DLL ×5 中缺 %d 个：%s" % (len(miss_dll), ", ".join(miss_dll)))
    else:
        out.append("- **[齐全]** 算法 DLL ×5 全部在位")
    if miss_node:
        problems.append("node.json 缺失: %s" % ", ".join(miss_node))
        out.append("- **[缺失]** node.json ×6 中缺 %d 个：%s" % (len(miss_node), ", ".join(miss_node)))
    else:
        out.append("- **[齐全]** node.json ×6 全部在位：%s" % ", ".join(NODE_JSONS))
    out.append("- **[跳过]** algo_free 导出验证：%s —— 需 dumpbin /exports 做二进制级验证，"
               "脚本不实现，按 T9 审计结论（3/3 均有 algo_free）视为通过" % "、".join(ALGO_FREE_DLLS))
    if len(pcl_rel) != PCL_RELEASE_EXPECTED:
        problems.append("pcl_*_release.dll 数量 %d != 预期 %d" % (len(pcl_rel), PCL_RELEASE_EXPECTED))
        out.append("- **[告警]** `pcl_*_release.dll` 实际 %d 个，预期 %d 个" % (len(pcl_rel), PCL_RELEASE_EXPECTED))
    else:
        out.append("- **[齐全]** `pcl_*_release.dll` %d 个，与预期一致" % len(pcl_rel))
    if boost:
        problems.append("发现 boost*.dll %d 个（Boost 为静态链接，交付目录不应出现）" % len(boost))
        out.append("- **[告警]** 发现 `boost*.dll` %d 个——Boost 应为静态链接，请核实来源" % len(boost))
    else:
        out.append("- **[正常]** 无 `boost*.dll`（静态链接，预期为 0）")
    out.append("")

    if problems:
        out.append("**结论：必备项缺失（%d 项），退出码 1。**" % len(problems))
        for t in problems:
            out.append("- %s" % t)
        code = 1
    else:
        out.append("**结论：必备项齐全，退出码 0。**")
        code = 0
    print("\n".join(out))
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv))
