# -*- coding: utf-8 -*-
"""check_version_sync.py — GetVersion 字面量多源一致性核查（仅标准库）。

动机：GetVersion 升版后测试断言/文档/README 残留旧版本号（1.2.0、2.3.0 两轮
均出现过），把"版本号必须在各处一致"变成可执行门禁。升版漏改任一源，双
verify 脚本的版本一致性段会红——升版漏改从此无法潜伏。

核查源（每模块 5 处）：
  Image2DAlg: api.cpp 的 return 字面量 / test 的 strcmp 断言 / api.h 版本历史
              最新条目 / README 当前版本 / docs 接口文档 §6 定位行
  Point3DAlg: SickIO.cpp 的 return 字面量 / test 的 strcmp 断言 / api.h 版本
              历史最新条目 / README 当前版本 / docs 接口文档 §7 版本段

用法：
    python -I scripts/check_version_sync.py [Image2DAlg|Point3DAlg]

退出码：0 = 各源一致；1 = 不一致或缺源（明细见输出）。
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

SOURCES = {
    "Image2DAlg": [
        ("impl(GetVersion return)", "src/Image2DAlg/Image2DAlg_api.cpp",
         r'return "(\d+\.\d+\.\d+)"'),
        ("test(strcmp 断言)", "src/Image2DAlg/Image2DAlg_test.cpp",
         r'std::strcmp\(ver,\s*"(\d+\.\d+\.\d+)"\)'),
        ("api.h 版本历史最新条目", "src/Image2DAlg/include/Image2DAlg_api.h",
         r'//\s+(\d+\.\d+\.\d+)\s+\+'),
        ("README 当前版本", "src/Image2DAlg/README.md",
         r'当前版本 \*\*(\d+\.\d+\.\d+)\*\*'),
        ("docs §6 定位行", "docs/接口文档.md",
         r'当前版本 (\d+\.\d+\.\d+)，76 个通用算子'),
    ],
    "Point3DAlg": [
        ("impl(GetVersion return)", "src/Point3DAlg/Point3DAlg_SickIO.cpp",
         r'return "(\d+\.\d+\.\d+)"'),
        ("test(strcmp 断言)", "src/Point3DAlg/Point3DAlg_test.cpp",
         r'Point3DAlg_GetVersion\(\),\s*"(\d+\.\d+\.\d+)"'),
        ("api.h 版本历史最新条目", "src/Point3DAlg/include/Point3DAlg_api.h",
         r'^\s*\*\s+(\d+\.\d+\.\d+)\s'),
        ("README 当前版本", "src/Point3DAlg/README.md",
         r'当前 (\d+\.\d+\.\d+)'),
        ("docs §7 版本段", "docs/接口文档.md",
         r'\*\*版本\*\*：当前 (\d+\.\d+\.\d+)'),
    ],
}


def latest(hist_versions):
    """历史块取语义最新（逐段比较数值），避免取到历史条目而非当前值。"""
    return max(hist_versions, key=lambda v: tuple(int(x) for x in v.split(".")))


def main(argv):
    if len(argv) != 2 or argv[1] not in SOURCES:
        print("用法: python -I check_version_sync.py [Image2DAlg|Point3DAlg]")
        return 1
    module = argv[1]
    found = {}
    for label, rel, pat in SOURCES[module]:
        try:
            text = (ROOT / rel).read_text(encoding="utf-8", errors="replace")
        except OSError as e:
            print(f"[MISS] {module} {label}: 文件读取失败 {e}")
            found[label] = None
            continue
        hits = re.findall(pat, text, re.M)
        if not hits:
            print(f"[MISS] {module} {label}: 未匹配到版本字面量（{rel}）")
            found[label] = None
            continue
        found[label] = latest(hits) if "历史" in label else hits[0]
    versions = sorted({v for v in found.values() if v})
    for label, v in found.items():
        print(f"[{'OK' if v else 'MISS'}] {module} {label} = {v}")
    problems = []
    if any(v is None for v in found.values()):
        problems.append("存在 MISS 源（见上）")
    if len(versions) > 1:
        problems.append("版本不一致: " + ", ".join(f"{k}={v}" for k, v in found.items()))
    if not versions:
        problems.append("所有源均未提取到版本")
    if problems:
        print("== 结论: FAIL ==")
        for t in problems:
            print("  - " + t)
        return 1
    print(f"== 结论: PASS（{module} 五源一致 = {versions[0]}）==")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
