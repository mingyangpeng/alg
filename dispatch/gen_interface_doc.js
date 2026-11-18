// alg 项目接口文档生成脚本（docx-js）
// 数据来源：src/*/include/*_api.h、integration/comui/*.node.json、*_comui.cpp、docs/HOST_VERIFY.md（全部实况核对）
const {
  Document, Packer, Paragraph, TextRun, Table, TableRow, TableCell,
  PageBreak, Header, Footer, PageNumber, NumberFormat,
  AlignmentType, HeadingLevel, WidthType, BorderStyle, ShadingType,
  TableOfContents,
} = require("docx");
const fs = require("fs");

// ---------- 调色板 DS-1（Deep Sea，技术文档） ----------
const PAL = {
  bg: "0B1C2C", accent: "529286",
  cover: { titleColor: "FFFFFF", subtitleColor: "B0B8C0", metaColor: "90989F", footerColor: "687078" },
  table: { headerBg: "529286", headerText: "FFFFFF", accentLine: "529286", innerLine: "BECFCC", surface: "E8ECEB" },
  primary: "1A3A4A", // 正文标题色（浅色页用）
};

// ---------- 封面 R1 布局函数（design-system 标准实现） ----------
function splitTitleLines(title, charsPerLine) {
  if (title.length <= charsPerLine) return [title];
  const breakAfter = new Set(["\u7684", "\u4e0e", "\u548c", "\u53ca", "\uff08", "\uff09", "(", ")", "/", "\u2014", "-", "_", " "]);
  const lines = [];
  let remaining = title;
  while (remaining.length > charsPerLine) {
    let breakAt = -1;
    for (let i = charsPerLine; i >= Math.ceil(charsPerLine * 0.6); i--) {
      if (breakAfter.has(remaining[i - 1])) { breakAt = i; break; }
    }
    if (breakAt === -1) {
      const limit = Math.min(remaining.length, Math.ceil(charsPerLine * 1.3));
      for (let i = charsPerLine + 1; i < limit; i++) {
        if (breakAfter.has(remaining[i - 1])) { breakAt = i; break; }
      }
    }
    if (breakAt === -1) {
      breakAt = charsPerLine;
      const prevChar = remaining[breakAt - 1], nextChar = remaining[breakAt];
      if (prevChar && nextChar && !breakAfter.has(prevChar) && !breakAfter.has(nextChar) &&
          /[\u4e00-\u9fff]/.test(prevChar) && /[\u4e00-\u9fff]/.test(nextChar)) breakAt -= 1;
    }
    lines.push(remaining.slice(0, breakAt).trim());
    remaining = remaining.slice(breakAt).trim();
  }
  if (remaining) lines.push(remaining);
  if (lines.length >= 2 && lines[lines.length - 1].length === 1) {
    lines[lines.length - 2] += lines.pop();
  }
  return lines;
}

function calcTitleLayout(title, maxWidthTwips, preferredPt = 40, minPt = 24) {
  const charsPerLine = (pt) => Math.floor(maxWidthTwips / (pt * 20));
  let titlePt = preferredPt, lines;
  while (titlePt >= minPt) {
    const cpl = charsPerLine(titlePt);
    if (cpl < 2) { titlePt -= 2; continue; }
    lines = splitTitleLines(title, cpl);
    if (lines.length <= 3) break;
    titlePt -= 2;
  }
  if (!lines || lines.length > 3) {
    lines = splitTitleLines(title, charsPerLine(minPt));
    titlePt = minPt;
  }
  return { titlePt, titleLines: lines };
}

function calcCoverSpacing(params) {
  const { titleLineCount = 1, titlePt = 36, hasSubtitle = false, hasEnglishLabel = false,
          metaLineCount = 0, fixedHeight = 800, pageHeight = 16838, marginTop = 0, marginBottom = 0 } = params;
  const SAFETY = 3400;   // 2026-11-11: 1200→3400——封面页脚被底缘裁切（两轮视检收敛）
  const usableHeight = pageHeight - marginTop - marginBottom - SAFETY;
  const titleHeight = titleLineCount * (titlePt * 23 + 200);
  const subtitleHeight = hasSubtitle ? (12 * 23 + 600) : 0;
  const englishLabelHeight = hasEnglishLabel ? (9 * 23 + 600) : 0;
  const metaHeight = metaLineCount * (10 * 23 + 100);
  const implicitParaHeight = 3 * 300;
  const contentHeight = titleHeight + subtitleHeight + englishLabelHeight + metaHeight + fixedHeight + implicitParaHeight;
  const safeRemaining = Math.max(usableHeight - contentHeight, 400);
  // 行高 exact=页高后，底部间距只需小固定值（内容余量 >6000 twips，页脚必然完整）
  const topSpacing = Math.max(Math.floor(safeRemaining * 0.45), 400);
  const bottomSpacing = 600;
  return { topSpacing, bottomSpacing };
}

function buildCoverR1(config) {
  const P = config.palette;
  const padL = 1200, padR = 800;
  const availableWidth = 11906 - padL - padR - 300;
  const { titlePt, titleLines } = calcTitleLayout(config.title, availableWidth, 40, 24);
  const titleSize = titlePt * 2;
  const spacing = calcCoverSpacing({
    titleLineCount: titleLines.length, titlePt,
    hasSubtitle: !!config.subtitle, hasEnglishLabel: !!config.englishLabel,
    metaLineCount: (config.metaLines || []).length, fixedHeight: 400,
  });
  const accentLeft = { style: BorderStyle.SINGLE, size: 8, color: P.accent, space: 12 };
  const children = [];
  children.push(new Paragraph({ spacing: { before: spacing.topSpacing } }));
  if (config.englishLabel) {
    children.push(new Paragraph({
      indent: { left: padL, right: padR }, spacing: { after: 500 },
      border: { bottom: { style: BorderStyle.SINGLE, size: 6, color: P.accent, space: 8 } },
      children: [new TextRun({ text: config.englishLabel.split("").join("  "),
        size: 18, color: P.accent, font: { ascii: "Calibri", eastAsia: "SimHei" }, characterSpacing: 40 })],
    }));
  }
  for (let i = 0; i < titleLines.length; i++) {
    children.push(new Paragraph({
      indent: { left: padL },
      spacing: { after: i < titleLines.length - 1 ? 100 : 300, line: Math.ceil(titlePt * 23), lineRule: "atLeast" },
      children: [new TextRun({ text: titleLines[i], size: titleSize, bold: true,
        color: P.cover.titleColor, font: { eastAsia: "SimHei", ascii: "Arial" } })],
    }));
  }
  if (config.subtitle) {
    children.push(new Paragraph({
      indent: { left: padL }, spacing: { after: 800 },
      children: [new TextRun({ text: config.subtitle, size: 24, color: P.cover.subtitleColor,
        font: { eastAsia: "Microsoft YaHei", ascii: "Arial" } })],
    }));
  }
  for (const line of (config.metaLines || [])) {
    children.push(new Paragraph({
      indent: { left: padL + 200 }, spacing: { after: 80 },
      border: { left: accentLeft },
      children: [new TextRun({ text: line, size: 24, color: P.cover.metaColor,
        font: { eastAsia: "Microsoft YaHei", ascii: "Arial" } })],
    }));
  }
  children.push(new Paragraph({
    indent: { left: padL, right: padR },
    border: { top: { style: BorderStyle.SINGLE, size: 6, color: P.accent, space: 8 } },
    spacing: { before: spacing.bottomSpacing },
    children: [new TextRun({ text: config.footerLeft || "", size: 18, color: P.cover.footerColor,
      font: { eastAsia: "Microsoft YaHei", ascii: "Calibri" } })],
  }));
  return children;
}

// ---------- 正文构件辅助 ----------
const FONT_BODY = { eastAsia: "SimSun", ascii: "Times New Roman" };
const FONT_HEAD = { eastAsia: "SimHei", ascii: "Times New Roman" };
const FONT_CODE = { ascii: "Consolas", eastAsia: "Microsoft YaHei" };

function h1(text) {
  return new Paragraph({
    heading: HeadingLevel.HEADING_1, spacing: { before: 360, after: 180, line: 312 },
    children: [new TextRun({ text, bold: true, size: 32, color: PAL.primary, font: FONT_HEAD })],
  });
}
function h2(text) {
  return new Paragraph({
    heading: HeadingLevel.HEADING_2, spacing: { before: 280, after: 140, line: 312 },
    children: [new TextRun({ text, bold: true, size: 28, color: PAL.primary, font: FONT_HEAD })],
  });
}
function p(text, opts) {
  const o = opts || {};
  return new Paragraph({
    alignment: AlignmentType.JUSTIFIED,
    spacing: { after: 80, line: 312 },
    indent: o.noIndent ? undefined : { firstLine: 480 },
    children: [new TextRun({ text, size: o.size || 24, bold: o.bold, color: "000000",
      font: o.code ? FONT_CODE : FONT_BODY })],
  });
}
function pCode(text) {
  return new Paragraph({
    spacing: { after: 60, line: 276 },
    shading: { type: ShadingType.CLEAR, fill: "F4F7F6" },
    indent: { left: 360 },
    children: [new TextRun({ text, size: 18, color: "24404E", font: FONT_CODE })],
  });
}
function note(text) {
  return new Paragraph({
    spacing: { after: 100, line: 312 }, indent: { left: 360 },
    border: { left: { style: BorderStyle.SINGLE, size: 6, color: PAL.accent, space: 8 } },
    children: [new TextRun({ text, size: 21, color: "5A6B75", font: FONT_BODY })],
  });
}
function tableTitle(text) {
  return new Paragraph({
    keepNext: true, spacing: { before: 160, after: 80 },
    children: [new TextRun({ text, bold: true, size: 21, color: PAL.primary, font: FONT_HEAD })],
  });
}
const CELL_MARGIN = { top: 60, bottom: 60, left: 120, right: 120 };
function makeTable(headers, rows, widths) {
  return new Table({
    width: { size: 100, type: WidthType.PERCENTAGE },
    borders: {
      top: { style: BorderStyle.SINGLE, size: 4, color: PAL.table.accentLine },
      bottom: { style: BorderStyle.SINGLE, size: 4, color: PAL.table.accentLine },
      left: { style: BorderStyle.NONE }, right: { style: BorderStyle.NONE },
      insideHorizontal: { style: BorderStyle.SINGLE, size: 1, color: PAL.table.innerLine },
      insideVertical: { style: BorderStyle.NONE },
    },
    rows: [
      new TableRow({
        tableHeader: true, cantSplit: true,
        children: headers.map((t, i) => new TableCell({
          children: [new Paragraph({ children: [new TextRun({ text: t, bold: true, size: 20, color: PAL.table.headerText, font: FONT_HEAD })] })],
          shading: { type: ShadingType.CLEAR, fill: PAL.table.headerBg },
          margins: CELL_MARGIN,
          width: { size: widths[i], type: WidthType.PERCENTAGE },
        })),
      }),
      ...rows.map(r => new TableRow({
        cantSplit: true,
        children: r.map((t, i) => new TableCell({
          children: [new Paragraph({ spacing: { line: 276 }, children: [new TextRun({ text: String(t), size: 20, color: "000000", font: FONT_BODY })] })],
          margins: CELL_MARGIN,
          width: { size: widths[i], type: WidthType.PERCENTAGE },
        })),
      })),
    ],
  });
}

// ---------- 接口数据（全部来自源码实况） ----------
const body = [];

// ===== 封面（独立 section） =====

// ===== 第 1 章 概述 =====
body.push(h1("1  概述"));
body.push(p("本文档整理 alg 算法库对外暴露的全部接口，供宿主（ComUI）接入、第三方调用与内部维护参考。内容依据源码实况整理：C API 以各模块 include/*.h 头文件为准，ComUI 算子以 integration/comui/*.node.json 声明与适配层实现为准，全部经过构建产物导出表核对（dumpbin /exports）。"));
body.push(p("库整体为六层单向依赖架构：BusinessAlg（业务编排）→ Point3DAlg（SICK 扫描与点云）/ Image2DAlg（图像读取）→ ShapelyAlg（几何/图像/点云基础算子）→ CommonAlg（JSON/文件/日志公共底座），另有 integration/comui 集成层（ComUI 宿主接入协议，可整体剥离）。"));
body.push(p("两类接口的使用方式不同：C API 由调用方直接链接 .lib 并 include 头文件；ComUI 算子由宿主按 node.json 声明动态加载 DLL 并调用统一入口，参数按声明顺序以 AlgoCParam 数组传递（宿主 marshal 不带参数名）。"));
body.push(note("全局约定：所有 C 接口返回 int 错误码（0 = 成功，<0 = 失败），不跨 DLL 边界抛异常；ComUI 算子 error 消息为 UTF-8 编码、静态存储（宿主只读不释放，下次调用覆盖）。"));

// ===== 第 2 章 统一 C ABI（ComUI 算子协议） =====
body.push(h1("2  ComUI 算子统一 C ABI（v1）"));
body.push(p("宿主与算法 DLL 之间通过 comui_native_abi.h 定义的纯 C 结构交换数据，与 docs/THIRD_PARTY_OPS.md §5.5 一致。每个算子 DLL 必须导出若干 ComuiXxx 入口（签名统一为 int32_t f(const AlgoCInput*, AlgoCOutput*)）与一个 algo_free(void*) 内存释放口。"));
body.push(tableTitle("表 2-1  AlgoCParam 参数单元（宿主按 node.json params 声明顺序传入）"));
body.push(makeTable(
  ["字段", "类型", "说明"],
  [
    ["kind", "int32_t", "参数类型：0 = double，1 = int32，2 = text（UTF-8），3 = bool（i != 0）"],
    ["d", "double", "kind == 0 时有效"],
    ["i", "int32_t", "kind == 1 时有效"],
    ["s", "const char*", "kind == 2 时有效（UTF-8 字符串）"],
  ],
  [15, 20, 65]
));
body.push(tableTitle("表 2-2  输入 / 输出结构"));
body.push(makeTable(
  ["结构", "字段", "说明"],
  [
    ["AlgoCImage", "width / height / bgra", "输入图像：BGRA 4 通道，行主序紧排"],
    ["AlgoCCloud", "count / xyz / rgb", "输入点云：xyz 紧排 float 数组，rgb 可为 NULL"],
    ["AlgoCImageOut", "width / height / bgra", "输出图像（malloc，宿主经 algo_free 释放）"],
    ["AlgoCCloudOut", "count / xyz / rgb", "输出点云（malloc，同上）"],
    ["AlgoCOutput", "images / clouds / error", "error = 失败或结果消息（UTF-8、静态存储、下次调用覆盖）"],
  ],
  [22, 28, 50]
));
body.push(note("ABI v1 已知限制：AlgoCOutput 无 text / number 输出端口，数值结果（配准 fitness、缺陷率等）随 error 字段以消息形式返回；宿主 NativeAlgo.cs 目前仅在返回码非 0 时读取 error，成功消息不显示——真机确认项见 docs/HOST_VERIFY.md。"));

// ===== 第 3 章 Image2DAlg =====
body.push(h1("3  Image2DAlg —— 2D 图像模块"));
body.push(h2("3.1  C API（Image2DAlg_api.h）"));
body.push(tableTitle("表 3-1  Image2DAlg 导出函数"));
body.push(makeTable(
  ["函数", "原型", "说明"],
  [
    ["GetVersion", "const char* Image2DAlg_GetVersion(void)", "返回版本字符串"],
    ["Init", "int Image2DAlg_Init(void)", "初始化（0 成功，可重复调用）"],
    ["Cleanup", "void Image2DAlg_Cleanup(void)", "清理；Cleanup 后可再次 Init"],
    ["FreeImage", "void Image2DAlg_FreeImage(Image2DAlg_Image*)", "释放 Image 内部缓冲"],
    ["ReadImage", "int Image2DAlg_ReadImage(const char* img_path, Image2DAlg_Image* out)", "读取 png/tiff/jpg/jpeg，统一转 BGRA 4 通道行主序紧排"],
  ],
  [16, 44, 40]
));
body.push(tableTitle("表 3-2  Image2DAlg_Image 结构"));
body.push(makeTable(
  ["字段", "类型", "说明"],
  [
    ["width / height", "int", "图像宽高（像素）"],
    ["channels", "int", "固定 4（BGRA）"],
    ["data", "unsigned char*", "BGRA 紧排缓冲（行主序，步长 = width * 4）"],
  ],
  [24, 22, 54]
));

body.push(h2("3.2  ComUI 算子（2 个）"));
body.push(tableTitle("表 3-3  alg.image2d.read —— 图像读取（源节点）"));
body.push(makeTable(
  ["项", "值"],
  [
    ["节点 id / 分组", "alg.image2d.read（2D 图像 / 源）"],
    ["DLL / method", "Image2DAlg.dll / Image2DAlg_ComuiRead"],
    ["输入", "无（源节点）"],
    ["参数", "img_path（path）：图像文件路径"],
    ["输出", "image（BGRA）"],
    ["典型错误", "未设置图像路径（rc=1）；读取失败：路径不存在或格式不支持（rc=2）"],
  ],
  [24, 76]
));
body.push(tableTitle("表 3-4  alg.image2d.gamma —— Gamma 校正（处理节点）"));
body.push(makeTable(
  ["项", "值"],
  [
    ["节点 id / 分组", "alg.image2d.gamma（2D 图像 / 处理）"],
    ["DLL / method", "Image2DAlg.dll / Image2DAlg_ComuiGamma"],
    ["输入 / 输出", "image → image"],
    ["参数", "gamma（number，默认 1.0，范围 0.1–5.0）"],
    ["典型错误", "输入未连线（rc=1）"],
  ],
  [24, 76]
));

body.push(h2("3.3  ComUI 节点·图像处理全家桶（24 个）"));
body.push(p("Image2DAlg 共 25 个节点（含 3.2 的 read/gamma）：全部 image → image 单输入单输出（read 为源节点、absdiff/match 系列为双/三输入），method 命名统一为 Image2DAlg_ComuiXxx，error 消息口径一致（输入未连线 rc=1 / 输入点云或路径无效 rc=2 / 内存不足 rc=3）。参数均为 number 或 int，范围与默认值如下表。"));
body.push(tableTitle("表 3-5  图像处理节点总表（24 个，按 id 字母序）"));
body.push(makeTable(
  ["节点 id（alg.image2d.）", "名称", "输入", "参数（默认值，范围）"],
  [
    ["absdiff", "批板差分", "基准板 + 待检板", "无"],
    ["analyze_contours", "轮廓测量", "image", "external_only(int, 1, 0–1)；min_area(number, 1, 0–1e7)"],
    ["analyze_contours_roi", "轮廓测量(ROI)", "image", "roi_x/y/w/h(int, 0/0/0/0)；external_only(int, 1)；min_area(number, 1)"],
    ["blur", "模糊降噪", "image", "method(int, 0, 0–2)；ksize(int, 3, 1–51)；sigma1/sigma2(number, 0, 0–100)"],
    ["canny", "Canny 边缘", "image", "threshold1(number, 50)；threshold2(number, 150)，均 0–1000"],
    ["connected_components", "连通域统计", "image", "connectivity(int, 8, 4–8)；min_area(number, 1)；max_area(number, 0=不限)"],
    ["connected_components_roi", "连通域(ROI)", "image", "roi_x/y/w/h(int) + connectivity + min_area + max_area"],
    ["crop", "裁剪", "image", "x/y(int, 0)；width/height(int, 100, ≥1)"],
    ["enhance", "灰度增强", "image", "method(int, 1, 0–1)；clip_limit(number, 2, 0.1–10)；tile_size(int, 8, 2–64)"],
    ["find_matches", "多目标匹配", "搜索图 + 模板", "method(int, 5, 0–5)；score_threshold(number, 0.8)；max_matches(int, 10)；nms_radius(int, 5)"],
    ["fit_circles", "多目标圆拟合", "image", "min_area(number, 10)；max_area(number, 0)；min_r/max_r(number, 0)；subpixel(int, 1)"],
    ["gamma", "Gamma 校正", "image", "gamma(number, 1.0, 0.1–5.0)"],
    ["hough_circles", "霍夫圆", "image", "dp(number, 1)；min_dist(number, 10)；param1/param2(number, 50/20)；min_r/max_r(number, 5/0)"],
    ["hough_lines", "霍夫直线", "image", "rho/theta_deg(number, 1/1)；threshold(int, 50)；min_line_length/max_line_gap(int, 20/5)"],
    ["hsv_threshold", "HSV 分割", "image", "h_lo/h_hi(number, 0/180)；s_lo/s_hi(number, 0/255)；v_lo/v_hi(number, 0/255)"],
    ["hsv_threshold_wrap", "HSV 分割(跨0)", "image", "h_lo/h_hi(number, 170/10 跨 0 环形) + s/v 六参同上"],
    ["match_template_ex", "Mark 点对位", "搜索图 + 模板", "method(int, 5)；scale_lo/hi/step(number, 0.8/1.2/0.05)；angle_lo/hi/step(number, -15/15/1)"],
    ["match_template_masked", "掩膜匹配", "搜索图 + 模板 + 掩膜", "method(int, 3, 0–5)"],
    ["morphology", "形态学", "image", "op(int, 2, 0–6)；shape(int, 0, 0–2)；ksize(int, 3, 1–51)；iterations(int, 1, 1–20)"],
    ["read", "图像读取（源）", "无", "img_path（path）"],
    ["resize", "缩放", "image", "out_width/out_height(int, 640/480)；interpolation(int, 1, 0–4)"],
    ["rotate", "旋转", "image", "angle(number, 0, -360–360)；scale(number, 1, 0.01–10)；interpolation(int, 1)"],
    ["sobel", "Sobel 梯度", "image", "dx/dy(int, 1/0, 0–2)；ksize(int, 3, 1–7)"],
    ["threshold", "阈值化", "image", "mode(int, 0, 0–4)；thresh/maxval(number, 128/255)；block_size(int, 3, 3–51)；c(number, 0, -100–100)"],
    ["to_gray", "灰度化", "image", "无"],
  ],
  [22, 15, 17, 46]
));
body.push(note("result 语义：全部为 image → image 变换（absdiff/匹配类多输入），标注图/测量结果画在输出图上；数值类结果（圆心、匹配分数）以标注形式呈现，无独立数值端口（ABI v1 限制）。"));

// ===== 第 4 章 Point3DAlg =====
body.push(h1("4  Point3DAlg —— SICK 扫描与 3D 点云模块"));
body.push(h2("4.1  C API（Point3DAlg_api.h）"));
body.push(p("一步式扫描读取接口（内部走 SickReader 类；帧级类接口见 include/Point3DAlg_SickReader.h）。", { noIndent: false }));
body.push(tableTitle("表 4-1  Point3DAlg_ReadSickCloud"));
body.push(makeTable(
  ["参数", "方向", "说明"],
  [
    ["folder", "in", "扫描数据文件夹（*.dat + 同名 .xml + Scan3DPart*.json）"],
    ["frame", "in", "帧选择：<0 读全部 .dat；>=0 按文件名顺序读第 frame 帧（从 0 计）"],
    ["recipe_path / common_path", "in", "换算参数 json；NULL 或空串 = 默认路径（Z:\\Calibrate\\3D\\... 与 Z:\\Juxin60003D_CommonParams.json），提取不到必需值报错"],
    ["out_xyz / out_count", "out", "输出 SoA 点云缓冲（malloc，用 FreeBuffer 释放）与点数"],
    ["返回", "—", "0 成功；-1 参数错；-2 无 .dat；-3 帧号越界；-4 配置/数据错误"],
  ],
  [24, 12, 64]
));
body.push(pCode("POINT3DALG_EXPORT int Point3DAlg_ReadSickCloud(const char* folder, int frame,"));
body.push(pCode("      const char* recipe_path, const char* common_path, float** out_xyz, int* out_count);"));
body.push(pCode("POINT3DALG_EXPORT void Point3DAlg_FreeBuffer(void* p);"));

body.push(tableTitle("表 4-1a  SickReader 类帧级接口（include/Point3DAlg_SickReader.h，C++ 类）"));
body.push(makeTable(
  ["成员", "说明"],
  [
    ["LoadRaw(folders, frame)", "逐相机加载（每相机一个文件夹，如 3D 与 3D-2）；各相机独立拼接后按起始编码器脉冲对齐 → 列交织合并 → 左右裁剪"],
    ["GetDepth() / GetCloud()", "矫正拼接深度图（mm，0=无效）/ 深度图生成 SoA 点云；重复调用返回同一缓冲"],
    ["HasIntensity / GetIntensity / GetCloudIntensity", "强度通道（v1.4 新增）：拼接强度图（行主序 uint8，与深度图同几何）与点云逐点强度；仅新格式 .dat 有——行距 7700B = 高度 2560×uint16 + 强度 2560×uint8 + 编码器 20B（旧行距 5140B 无强度，二义性行距用编码器块 magic 0x40000000 回退判定）"],
    ["FovBoxes(rects, ids)", "FOV 框在最终拼接图中的矩形（可视标注用）"],
    ["StitchQuality(threshold, out)", "拼接质量自检（v1.4 新增）：相邻 rank 相机衔接带逐行配对求 z 差——pairs/dh_mean/dh_sigma/dh_max_abs/outlier/outlier_ratio（Point3DAlg_StitchQualityResult）；差值大 = 标定漂移/丢帧/同步异常；单相机（n<2）返回 false"],
  ],
  [30, 70]
));

body.push(h2("4.2  配准算法层（Point3DAlg_Regist.h，编入 Point3DAlg.dll）"));
body.push(p("手动配准节点的算法本体为纯函数层，宿主不直接调用；7 种配准策略枚举如下（ComUI 参数 strategy 即此值）。"));
body.push(tableTitle("表 4-2  配准策略（strategy 参数 1–7）"));
body.push(makeTable(
  ["值", "策略", "特点"],
  [
    ["1", "ICP 点到点", "通用，最稳"],
    ["2", "ICP 点到面", "平面多时快且准，需法线"],
    ["3", "GICP", "平面/曲面混合，精度最高"],
    ["4", "FPFH + RANSAC", "无需初值，慢"],
    ["5", "NDT", "大初始偏差稳健"],
    ["6", "PCA 主轴粗配准", "极快，给 ICP 找初值"],
    ["7", "多尺度 ICP（multiscale_icp）", "voxel 金字塔粗→细逐级配准（粗级对应距离 3×max_corr 覆盖大初值偏差，细级 0.3×max_corr 精修）；初值偏差超出单级对应半径时明显优于策略 1"],
  ],
  [10, 30, 60]
));
body.push(p("变换矩阵为行主序 4×4（p_target = T · p_source），初值通过 init_transform 传入——配准是局部优化，手工粗调后精配准必须传初值。矩阵的文本格式为 16 值逗号分隔；解析入口 Point3DAlg_RegistParseTransformText 将所有非数字字符视为分隔符（兼容带键名的整行文本）。"));

body.push(h2("4.3  ComUI 节点·基础（read_sick / voxel / manual_regist）"));
body.push(tableTitle("表 4-3  alg.point3d.read_sick —— SICK 扫描读取（源节点）"));
body.push(makeTable(
  ["项", "值"],
  [
    ["节点 id / 分组", "alg.point3d.read_sick（IO / 源）"],
    ["DLL / method", "Point3DAlg.dll / Point3DAlg_ComuiReadSick"],
    ["参数", "folder（path）：扫描数据文件夹；frame（int，默认 -1）：帧选择，<0 全部"],
    ["输出", "cloud"],
    ["典型错误", "未设置扫描数据文件夹（rc=1）；读取失败（配置或数据错误，rc=-4）"],
  ],
  [24, 76]
));
body.push(tableTitle("表 4-4  alg.point3d.voxel_downsample —— 体素下采样（处理节点）"));
body.push(makeTable(
  ["项", "值"],
  [
    ["节点 id / 分组", "alg.point3d.voxel_downsample（3D 点云 / 处理）"],
    ["DLL / method", "Point3DAlg.dll / Point3DAlg_ComuiVoxel"],
    ["输入 / 输出", "cloud → cloud"],
    ["参数", "leaf_size（number，默认 0.01，范围 0.001–1.0）：体素边长"],
    ["典型错误", "输入未连线（rc=1）；输入点云为空（rc=2）"],
  ],
  [24, 76]
));
body.push(tableTitle("表 4-5  alg.point3d.manual_regist —— 手动配准 ICP（处理节点，双输入）"));
body.push(makeTable(
  ["项", "值"],
  [
    ["节点 id / 分组", "alg.point3d.manual_regist（3D 点云 / 处理）"],
    ["DLL / method", "Point3DAlg.dll / Point3DAlg_ComuiManualRegist"],
    ["输入", "源点云 + 目标点云（两片 cloud，按声明顺序）"],
    ["参数 1", "strategy（int，默认 1，范围 1–7）：配准策略，见表 4-2"],
    ["参数 2", "init_transform（text）：16 值逗号分隔行主序 4×4 初值矩阵，默认单位阵"],
    ["参数 3", "max_distance（number，默认 0.0，≥0）：对应点最大搜索距离"],
    ["参数 4", "max_iteration（int，默认 60，≥1）：最大迭代次数"],
    ["输出", "配准点云（cloud）"],
    ["结果消息", "fitness=%.4f rmse=%.4f | T=…（随 error 字段返回，返回码 0）"],
    ["典型错误", "需要两片输入点云（rc=1）；源/目标点云为空（rc=2）"],
  ],
  [24, 76]
));
body.push(note("参数提取按 node.json params 声明顺序（宿主 marshal 不带参数名）；同 kind 参数以出现次序区分——manual_regist 的第 1 个 int 是 strategy、第 2 个 int 是 max_iteration。"));

// ===== 4.3 量测算法层 =====
body.push(h2("4.4  量测算法层（tools/*.h，编入 Point3DAlg.dll，PCL_FOUND 守卫）"));
body.push(p("量测五家族均为纯函数接口（PCL 点云入、结构体出），确定性无随机性（Kåsa 代数拟合，非 RANSAC），结果统一有限性校验（NaN 判失败）。数值内核共用 tools/Point3DAlg_FitUtil.h（MedianOf / FitPlaneZ）。"));

body.push(tableTitle("表 4-6  量测算法层接口概览"));
body.push(makeTable(
  ["接口", "输入 → 输出", "要点"],
  [
    ["Point3DAlg_DomeFit", "点云 → 每 bump 高度/apex/球心/半径/rms",
      "拱形球顶拟合：稳健基面（迭代修剪 LSQ）→ 高度阈值 → 欧式聚类 → Kåsa 球拟合 + MAD 修剪；apex = 球心 + R·法向"],
    ["Point3DAlg_DomeStatsOf", "bumps → 统计",
      "mean/σ/分位数 + 共面性两口径：coplanarity（极差法）/ fit_sigma（最佳拟合面残差，剥离晶圆倾斜翘曲）"],
    ["Point3DAlg_RegionPlaneFit", "点云 + XY 矩形 ROI → 高度/平面度 PV/σ/系数",
      "双侧 MAD 修剪最小二乘；台阶差 = 两 ROI 高度相减；稳健边界：minority 污染 ≤20%"],
    ["Point3DAlg_CloudDiff", "基准 + 被测点云 → 格级 z 差统计/超差/差值点云",
      "栅格 z 中位（cell_size）逐格差；可选 XY 粗对齐（align_search，RMS 得分 + 重叠 70% 门槛，精度 ≈±cell/2）"],
    ["Point3DAlg_DetectCircles / FitCircle2D", "点云 + 高度带 → 每孔圆心/孔径/rms",
      "高度切片 → 聚类 → Kåsa 2D 圆拟合 + MAD 修剪；FitCircle2D 支持已有轮廓点直接拟合"],
    ["Point3DAlg_Profile", "点云 + XY 线段 → 剖面 (s,z) 点列",
      "带内投影 → 弧长分格 z 中位；max_step = 相邻格最大跳变（台阶/落差定位）"],
    ["Point3DAlg_BumpCompare", "基准/被测 bumps → 一对一匹配 + NG",
      "XY 网格哈希最近 + 贪心一对一仲裁；matched/漏测/多出/NG 计数 + 逐配对高度差"],
    ["Point3DAlg_BumpReport", "点云 → 每 bump 高度/直径/位置度/共面性偏差/NG 位",
      "DomeFit + 距顶 0.35 截面圆校验；直径 = 球参数解析（零拟合噪声）；NG 位 bit0~3（bit3 = 位置度越界）；nominal pitch>0 启用位置度"],
  ],
  [26, 30, 44]
));
body.push(note("量测层坐标单位 mm；形貌要求见各头文件注释（对齐可观测性：纯梯度/平面形貌对 XY 平移不可观测，含 z 常差的对比需台阶/凸台特征锚定）。"));

// ===== 4.4 量测 ComUI 节点 =====
body.push(h2("4.5  量测 ComUI 节点（6 个）"));

body.push(tableTitle("表 4-7  alg.point3d.dome_fit —— 拱形球顶拟合（测量节点）"));
body.push(makeTable(
  ["项", "值"],
  [
    ["节点 id / 分组", "alg.point3d.dome_fit（3D 点云 / 测量）"],
    ["DLL / method", "Point3DAlg.dll / Point3DAlg_ComuiDomeFit"],
    ["输入 / 输出", "cloud → cloud（每 bump 一点：x/y=球顶 XY，z=高度 mm）"],
    ["参数", "height_threshold（number，默认 0.02，相对基面）；cluster_tolerance（number，默认 0.12）；min_points（int，默认 8）；max_fit_rms（number，默认 0.005）"],
    ["结果消息", "bump N 个：高度均值/σ/中位/范围 + 共面性(极差)/拟合面σ + 异常簇拦截数（error 字段随行，rc=0）"],
    ["典型错误", "输入未连线（rc=1）；点云为空（rc=2）；拟合失败/无有效 bump（rc=4）"],
  ],
  [24, 76]
));

body.push(tableTitle("表 4-8  alg.point3d.region_plane —— ROI 平面量测（测量节点）"));
body.push(makeTable(
  ["项", "值"],
  [
    ["节点 id / 分组", "alg.point3d.region_plane（3D 点云 / 测量）"],
    ["DLL / method", "Point3DAlg.dll / Point3DAlg_ComuiRegionPlane"],
    ["输入 / 输出", "cloud → 无云输出（量测数值并入消息）"],
    ["参数", "x / y / w / h（number×4，默认 0,0,1,1）：XY 矩形 ROI（mm）"],
    ["结果消息", "ROI(...) 高度（ROI 中心）| 平面度PV σ | 内点数 | 平面系数 z=c+a·x+b·y"],
    ["典型错误", "输入未连线（rc=1）；点云为空（rc=2）；ROI 内点 <3 或退化（rc=4）"],
  ],
  [24, 76]
));

body.push(tableTitle("表 4-9  alg.point3d.cloud_diff —— 点云高度差对比（测量节点，双输入）"));
body.push(makeTable(
  ["项", "值"],
  [
    ["节点 id / 分组", "alg.point3d.cloud_diff（3D 点云 / 测量）"],
    ["DLL / method", "Point3DAlg.dll / Point3DAlg_ComuiCloudDiff"],
    ["输入 / 输出", "基准 cloud + 被测 cloud → 差值点云（每格一点，z=差值，正=被测高）"],
    ["参数", "cell_size（number，默认 0.1）；min_points（int，默认 3）；diff_threshold（number，默认 0.02）；align_search（number，默认 0=不对齐，>0 启用 XY 粗对齐，精度 ≈±cell/2）"],
    ["结果消息", "cells/mean/σ/max|d|/超差计数与占比（%）+ 对齐量（启用时）"],
    ["典型错误", "需要两片输入点云（rc=1）；云为空（rc=2）；不重叠或格数据不足（rc=4）"],
  ],
  [24, 76]
));

body.push(tableTitle("表 4-10  alg.point3d.circle_fit —— 圆/孔位量测（测量节点）"));
body.push(makeTable(
  ["项", "值"],
  [
    ["节点 id / 分组", "alg.point3d.circle_fit（3D 点云 / 测量）"],
    ["DLL / method", "Point3DAlg.dll / Point3DAlg_ComuiCircleFit"],
    ["输入 / 输出", "cloud → cloud（每孔一点：x/y=圆心，z=半径）"],
    ["参数", "z_low / z_high（number×2，默认 0/0.1）：切片高度带；cluster_tolerance（number，默认 0.2）；min_points（int，默认 20）；radius_min / radius_max（number，默认 0.05/50）"],
    ["结果消息", "检出圆/孔 N 个（有效 M）：z 带 [...]"],
    ["典型错误", "输入未连线（rc=1）；点云为空（rc=2）；切片无点/无有效圆（rc=4）"],
  ],
  [24, 76]
));

body.push(tableTitle("表 4-11  alg.point3d.profile —— 轮廓截面量测（测量节点）"));
body.push(makeTable(
  ["项", "值"],
  [
    ["节点 id / 分组", "alg.point3d.profile（3D 点云 / 测量）"],
    ["DLL / method", "Point3DAlg.dll / Point3DAlg_ComuiProfile"],
    ["输入 / 输出", "cloud → cloud（剖面点列：线上实际 XY + z=格内中位高度）"],
    ["参数", "x0 / y0 / x1 / y1（number×4，默认 0,0,1,0）：线段两端点；band（number，默认 0.2）：带半宽；bin_size（number，默认 0.1）：弧长分格"],
    ["结果消息", "剖面 N 格（L）| z 范围及弧长位置 | max_step（台阶/落差）及位置"],
    ["典型错误", "输入未连线（rc=1）；点云为空（rc=2）；带内无点或参数非法（rc=4）"],
  ],
  [24, 76]
));

body.push(tableTitle("表 4-12  alg.point3d.bump_report —— bump 全参数报表（测量节点）"));
body.push(makeTable(
  ["项", "值"],
  [
    ["节点 id / 分组", "alg.point3d.bump_report（3D 点云 / 测量）"],
    ["DLL / method", "Point3DAlg.dll / Point3DAlg_ComuiBumpReport"],
    ["输入 / 输出", "cloud → cloud（每 bump 一点：x/y=球顶 XY，z=高度 mm）"],
    ["参数 1", "height_threshold（number，默认 0.02）：bump 判定高度阈值"],
    ["参数 2", "cluster_tolerance（number，默认 0.25）：聚类连通距离（须 > 冠直径）"],
    ["参数 3/4", "height_min / height_max（number，默认 0/0=不判）：高度 NG 上下限"],
    ["参数 5/6", "diameter_min / diameter_max（number，默认 0/0=不判）：直径 NG 上下限"],
    ["参数 7/8", "nominal_pitch_x / nominal_pitch_y（number，默认 0/0=不算位置度）：名义网格 X/Y 间距（mm，与阵列实际间距一致）"],
    ["参数 9/10", "nominal_origin_x / nominal_origin_y（number）：名义网格原点（首个名义位置，mm）"],
    ["参数 11", "pos_threshold（number，默认 0.05）：位置度 NG 阈值（mm）；bump apex 距最近名义点 > 阈值 → NG bit3"],
    ["结果消息", "bump N 个：高度 mean±σ [min~max] 直径 mean±σ | 共面性σ | NG 高度 H 直径 D 位置度 P（漏测 M）（启用名义网格时带位置度段；漏测 = 已匹配名义索引范围内无 bump 对应的名义点数）"],
    ["典型错误", "输入未连线（rc=1）；点云为空（rc=2）；报表失败/未检出 bump（rc=4）"],
  ],
  [24, 76]
));
body.push(note("量测节点 ABI v1 无数值输出通道：量测结果统一并入 error 消息字段随行返回（rc=0 表示流程成功），或以点云形式输出（z 维承载量测值，宿主伪彩即可视化）。"));

// ===== 第 5 章 BusinessAlg =====
body.push(h1("5  BusinessAlg —— 业务编排模块"));
body.push(h2("5.1  C API（BusinessAlg_api.h）"));
body.push(p("业务层定位为流程编排：参数校验、流程串联、业务语义判定、结果组装；原子能力全部来自 ShapelyAlg / Point3DAlg / Image2DAlg。首个示例算子：区域缺陷率统计（点云 → 高度过滤 → 2.5D 投影 → 区域占比）。"));
body.push(pCode("BUSINESSALG_EXPORT int BusinessAlg_ComputeRegionRatio("));
body.push(pCode("    const float* xyz, int count, double min_z, double max_z,"));
body.push(pCode("    double roi_x, double roi_y, double roi_w, double roi_h,"));
body.push(pCode("    double* out_defect_ratio, double* out_area);"));
body.push(note("defect_ratio 为面积口径：ROI 内有效点投影面积（XY 凸包 ∩ ROI）/ ROI 面积，非点数占比。新增业务算子步骤见头文件注释（适配层入口 → C 声明 → node.json）。"));
body.push(h2("5.2  ComUI 算子（2 个）"));
body.push(tableTitle("表 5-1  business.region.ratio —— 区域缺陷率统计（处理节点）"));
body.push(makeTable(
  ["项", "值"],
  [
    ["节点 id / 分组", "business.region.ratio（业务算子 / 处理）"],
    ["DLL / method", "BusinessAlg.dll / BusinessAlg_ComuiRegionRatio"],
    ["输入", "点云（cloud）"],
    ["参数", "min_z（number，默认 0.0）/ max_z（number，默认 10.0）：高度过滤区间；roi_x / roi_y（number，默认 0.0）：ROI 左上角；roi_w / roi_h（number，默认 100.0，≥0.001）：ROI 宽高"],
    ["输出", "无端口（ABI v1 无数值输出通道，结果走消息）"],
    ["结果消息", "defect_ratio=%.4f area=%.2f mm2（随 error 字段返回，返回码 0）"],
    ["典型错误", "无输入（rc=1）；点云为空（rc=2）"],
  ],
  [24, 76]
));
body.push(tableTitle("表 5-2  business.bump_ng_check —— bump 高度 NG 判定（处理节点，双输入，跨模块管线）"));
body.push(makeTable(
  ["项", "值"],
  [
    ["节点 id / 分组", "business.bump_ng_check（业务算子 / 处理）"],
    ["DLL / method", "BusinessAlg.dll / BusinessAlg_ComuiBumpNgCheck"],
    ["输入", "基准点云 + 被测点云（两片 cloud，按声明顺序）"],
    ["参数 1", "ng_threshold（number，默认 0.02）：NG 判定高度差阈值（mm）"],
    ["参数 2", "match_radius（number，默认 0.3）：基准/被测 bump 配对半径（mm）"],
    ["参数 3", "height_threshold（number，默认 0.02）：bump 检出高度阈值"],
    ["参数 4", "cluster_tolerance（number，默认 0.25）：bump 聚类容差"],
    ["输出", "NG 差异点云（cloud）"],
    ["结果消息", "bump 配对 %d/%d/%d（配对/漏测/多出）| NG %d 个（|dh|>阈值，百分比）| dh mean/rms/max（随 error 字段返回，rc=0）"],
    ["典型错误", "无输入（rc=1）；点云为空（rc=2）；参数不足（rc=3）；基准/被测云未检出 bump、无配对 bump（检查 XY 对齐与 match_radius）（rc=4）"],
    ["底层链路", "DomeFit（bump 检出/高度）→ BumpCompare（XY 匹配 + 高度差）→ NG 判定与差异点云组装"],
  ],
  [24, 76]
));

// ===== 第 6 章 ShapelyAlg =====
body.push(h1("6  ShapelyAlg —— 基础算子库（C++ 接口）"));
body.push(p("ShapelyAlg 是 C++ 基础库（不直接暴露 C API），唯一入口头 ShapelyAlg.h，完整接入说明见 src/ShapelyAlg/README.md。六个组成部分：Types.h（Point2d/3d、BoundingBox2d、AABB3d、OBB3d 原生结构，static_assert 结构契约）、Geom.h（GEOS 二维几何：工厂/谓词/运算/buffer/WKT/PreparedGeom）、Stereo.h（三维度量，纯数学）、ImageProcessor.h（OpenCV 图像算子）、CloudProcessor.h（PCL 点云算子，filter/extract/trans/feature/keypoint/fit/segment/regist/tree/surface/measure 11 组约 68 算子，显式实例化 PointXYZ/PointXYZI/PointXYZRGB 三白名单点型）、CloudIO.h（PCD/PLY/TXT 读写）。"));
body.push(tableTitle("表 6-1  快速上手示例（来自 ShapelyAlg.h 头注释）"));
body.push(makeTable(
  ["场景", "写法"],
  [
    ["二维几何", "Geom box = Geom::createPolygon(pts);  double a = box.intersection(circle).area();"],
    ["多体拆分", "for (const Geom& part : poly.unionWith(other).splitParts()) …"],
    ["预处理加速", "PreparedGeom roi = PreparedGeom::prepare(roiPoly);  roi.intersects(defect);"],
    ["OpenCV 适配", "Geom r = GeomCVAdapter::createRect(cv::Rect(0,0,30,40));"],
    ["点云 IO", "CloudIO<PointXYZ>::ioSavePLY(\"out.ply\", cloud, true);"],
  ],
  [22, 78]
));
body.push(note("硬约束：任何 .cpp 直接 include <pcl/io/*.h> 前必须先 include \"BoostCompat.h\"（Boost 1.68 + 新版 MSVC STL 的 seekpos C2039 垫片）；经 CloudIO.h/CloudProcessor.h 使用点云功能则库内已处理。"));

// ===== 第 7 章 接入与部署 =====
body.push(h1("7  接入、构建与部署"));
body.push(h2("7.1  构建"));
body.push(pCode("cmake -S . -B build -G \"Visual Studio 17 2022\""));
body.push(pCode("cmake --build build --config Release"));
body.push(pCode("ctest --test-dir build -C Release          // 11 项测试全过"));
body.push(p("产物与全部第三方运行时输出到 build/output/Release/，交付前可用清单脚本核对："));
body.push(pCode("python -I scripts/make_delivery_checklist.py   // 退出码 0 = 必备项齐全"));
body.push(h2("7.2  ComUI 接入"));
body.push(p("宿主把全部 *.node.json（随节点声明增长，当前 17 个）与对应 DLL 一并放到 comdll/algo/<名称>/ 目录，重启 ComUI 即注册节点；节点 id 冲突会被静默忽略。参数与端口声明以 node.json 为准，method 必须与 DLL 导出函数逐字一致。真机联调的最小步骤与预期输出见 docs/HOST_VERIFY.md。"));
body.push(h2("7.3  错误处理约定"));
body.push(tableTitle("表 7-1  返回码约定"));
body.push(makeTable(
  ["层级", "约定"],
  [
    ["C API", "int 返回：0 成功，<0 错误码（各模块错误码语义见对应头文件注释）"],
    ["ComUI 算子", "返回码：0 成功（结果可能随 error 携带消息）；1 输入缺失；2 输入无效；其他负值 = 底层错误透传"],
    ["异常", "C 边界不抛异常，内部捕获转错误码；error 消息 UTF-8、静态存储、宿主只读不释放"],
  ],
  [20, 80]
));
body.push(note("调试：.vscode/launch.json 已配调试入口（F5，覆盖各测试 exe）；文档索引——src/ShapelyAlg/README.md（基础库）、src/Point3DAlg/tools/README.md（配准工具）、docs/THIRD_PARTY_OPS.md（宿主协议）、docs/HOST_VERIFY.md（真机联调）、src/DEVELOPMENT_RULES.md（开发规则）、src/DEPENDENCIES.md（依赖版本）。"));

// ---------- 文档组装 ----------
const doc = new Document({
  styles: {
    default: {
      document: { run: { font: FONT_BODY, size: 24, color: "000000" } },
    },
  },
  sections: [
    // 封面 section（无页眉页脚、无页码）
    {
      properties: {
        page: {
          size: { width: 11906, height: 16838 },
          margin: { top: 0, bottom: 0, left: 0, right: 0 },
        },
        titlePage: false,
      },
      children: [
        new Table({
          width: { size: 11906, type: WidthType.DXA },
          borders: {
            top: { style: BorderStyle.NONE }, bottom: { style: BorderStyle.NONE },
            left: { style: BorderStyle.NONE }, right: { style: BorderStyle.NONE },
            insideHorizontal: { style: BorderStyle.NONE }, insideVertical: { style: BorderStyle.NONE },
          },
          rows: [new TableRow({
            cantSplit: true,
            height: { value: 16838, rule: "exact" },   // 2026-11-11: 封面行高 exact=页高——
            // 修复页脚被底缘裁切（估算式撑高误差不可控，exact 后内容流布局必然完整）
            children: [new TableCell({
              shading: { type: ShadingType.CLEAR, fill: PAL.bg },
              margins: { top: 0, bottom: 0, left: 0, right: 0 },
              children: buildCoverR1({
                title: "alg 算法库接口文档",
                subtitle: "C API · ComUI 算子 · 接入与部署",
                englishLabel: "ALG INTERFACES",
                metaLines: [
                  "模块：CommonAlg / ShapelyAlg / Image2DAlg / Point3DAlg / BusinessAlg",
                  "宿主协议：ComUI Native ABI v1（35 个算子节点）",
                  "版本：1.4（2026-11-13 增补 SickReader 强度通道与拼接质量自检、bump_report 位置度参数，2026-11-12 增补 BumpReport/BumpCompare，35 节点全收录）",
                  "日期：2026-11-12",
                ],
                footerLeft: "alg project · internal",
                palette: PAL,
              }),
              width: { size: 11906, type: WidthType.DXA },
            })],
          })],
        }),
      ],
    },
    // 目录 + 正文 section
    {
      properties: {
        page: {
          size: { width: 11906, height: 16838 },
          margin: { top: 1440, bottom: 1440, left: 1701, right: 1417 },
          pageNumbers: { start: 1, formatType: NumberFormat.DECIMAL },
        },
      },
      headers: {
        default: new Header({
          children: [new Paragraph({
            alignment: AlignmentType.RIGHT,
            border: { bottom: { style: BorderStyle.SINGLE, size: 4, color: PAL.table.innerLine } },
            children: [new TextRun({ text: "alg 算法库接口文档", size: 16, color: "8A9AA5", font: FONT_BODY })],
          })],
        }),
      },
      footers: {
        default: new Footer({
          children: [new Paragraph({
            alignment: AlignmentType.CENTER,
            children: [new TextRun({ children: [PageNumber.CURRENT], size: 18, color: "8A9AA5", font: FONT_BODY })],
          })],
        }),
      },
      children: [
        // 目录（标题不用 Heading，防自索引）
        new Paragraph({
          alignment: AlignmentType.CENTER, spacing: { before: 480, after: 360 },
          children: [new TextRun({ text: "目  录", bold: true, size: 32, font: FONT_HEAD })],
        }),
        new TableOfContents("Table of Contents", { hyperlink: true, headingStyleRange: "1-2" }),
        new Paragraph({
          spacing: { before: 200 },
          children: [new TextRun({
            text: "注：本目录由域代码生成。若编辑文档后页码变化，请在目录上右键选择\u201c更新域\u201d刷新页码。",
            italics: true, size: 18, color: "888888", font: FONT_BODY })],
        }),
        new Paragraph({ children: [new PageBreak()] }),
        ...body,
      ],
    },
  ],
});

Packer.toBuffer(doc).then(buf => {
  fs.writeFileSync("D:/pmy/alg/docs/alg_Interface_API.docx", buf);
  console.log("OK: D:/pmy/alg/docs/alg_Interface_API.docx", buf.length, "bytes");
});
