# Image2DAlg —— 通用 2D 模块

`src/Image2DAlg` 是面向 ComUI 宿主的通用 2D 算法库：图像读取、通用处理、测量/缺陷检测，以及标定接口类（`Image2DAlg_Calib`）。当前版本 **2.8.0**（100 个通用算子：处理 29 + 特征 17 + 测量 44 + 跟踪配准立体 5 + 标定 5——2.7.0 新增标定域分组，2.8.0 新增稳健拟合 3 算子入测量组（均 C API only）；另有 Init / ReadImage / GetVersion / Free* 等生命周期与释放接口）。2.5.0 新增对位/拼接流水线：批量相位配准 / 偏移重采样 / 掩膜复核 / 拼接融合 / 模板复合对位。2.6.0 新增频域缺陷检测与匹配链路强化：幅度谱显示 / 陷波滤波去周期纹理（频域 2 个，入处理组——同 WarpPolar/OffsetRemap 归属先例，不另设频域分组）；FLANN 大集合匹配 / 双向互为最近匹配 / 全仿射估计（匹配 3 个，入特征组——Descriptors/OrbMatch 容器接口，v1 节点无法持有，C API only）。2.7.0 新增标定域 5 算子：手眼标定 HandEyeCalib（AX=XB，5 方法直传 cv 枚举）/ 3D→2D 投影 ProjectPoints / 图像点去畸变 UndistortImagePoints / 去畸变映射表 UndistortRectifyMap（out_map1+out_map2 双必填，对接 Remap）/ 重投影误差统计 CalibReprojectionStats——多位姿/多维数组输入 ABI v1 节点无法承载，全部 C API only，节点化需 ABI v2。2.8.0 新增稳健拟合变体 3 算子：FitLineRobust 稳健直线拟合（cv::fitLine distType/param 全透传——M-estimator 稳健距离族 1~7，DIST_USER 不支持）/ FitEllipseAMS / FitEllipseDirect 椭圆拟合变体（cv::fitEllipseAMS / cv::fitEllipseDirect 透传——近似均方 / 偏心修正直接法，对噪声/离群比标准 fitEllipse 更稳）——3 接口为点集容器接口（xy 扁平数组），当前 C API only；点集拟合接口已有节点先例（FitCircle / FitCircleEx / FitCircleRobust / FitLine 均已节点化，见 `integration/comui/image2d_fit_circle_ex.node.json` 等），后续可循 fit_circle_ex 先例（ComUI 适配层 cloud 入参包装）节点化，本批节点数不变。

## 定位与依赖

本模块是 `ShapelyAlg::ImageProcessor` 的**上层**：所有图像算子复用 ImageProcessor 实现，本层只负责参数语义（通道/尺寸/模式）、C 容器与 `cv::Mat` 转换、测量结果结构组装。

```
Image2DAlg.dll
 ├─ ShapelyAlg.dll  (ImageProcessor：滤波/阈值/形态学/边缘/轮廓/匹配)
 ├─ CommonAlg.dll   (标定持久化等)
 └─ opencv_world440.dll
```

交付时按现有 `build/output/Release` 整目录平铺即可（`ShapelyAlg.dll` 已在交付清单内）。

## 文件结构

| 文件 | 说明 |
|------|------|
| `Image2DAlg_api.h` | 对外 C ABI（容器、读写、通用算子、测量结果结构） |
| `Image2DAlg_api.cpp` | 版本/Init/Cleanup/FreeImage/ReadImage |
| `Image2DAlg_Ops.cpp` | 通用处理与测量算子（复用 ImageProcessor） |
| `Image2DAlg_Calib.h/.cpp` | 标定接口类（单目/双目/鱼眼 Mei、立体校正），C++ API |
| `Image2DAlg_test.cpp` | C ABI 测试入口（ctest 第 3 项） |
| `Image2DAlg_CalibTest.cpp` | 标定模拟数据验证（2026-11-20 起 add_test 注册 ctest，ctest 第 13 项） |

ComUI 适配层在 `integration/comui/Image2DAlg_comui.cpp`，节点声明在 `integration/comui/image2d_*.node.json`。

## 通道语义

`Image2DAlg_Image { width, height, channels, data }`，`data` 为紧排像素，行步长 `width * channels`。

- 输入支持 **1（灰度）/ 3（BGR）/ 4（BGRA）** 通道，其他通道数返回 `-3`。
- 通用算子**保持输入真实通道数**（1→1、3→3、4→4）；alpha 随通道参与 OpenCV 运算（彩色 CLAHE/双边滤波等需要拆分时保留 alpha）。
- 天然单通道算子输出 1 通道：`ToGray`、`Threshold`、`Canny`、`Enhance(method=0)`。
- 例外：`Image2DAlg_ReadImage` 保持既有宿主契约，输出固定 BGRA 4 通道。
- 输出缓冲一律 `malloc`，用 `Image2DAlg_FreeImage` 释放；测量数组用 `Image2DAlg_FreeBlobs` / `Image2DAlg_FreeContours` 释放（均 NULL 安全）。

## 通用处理算子

| 函数 | 输出通道 | 说明 |
|------|----------|------|
| `Image2DAlg_ToGray(in, out)` | 1 | BGR(A) → 灰度 |
| `Image2DAlg_Gamma(in, gamma, out)` | 同输入 | LUT `255*(v/255)^(1/gamma)`，gamma 钳制 ≥ 0.05 |
| `Image2DAlg_Threshold(in, mode, thresh, maxval, block, C, out)` | 1 | mode：0 固定二值 / 1 反二值 / 2 OTSU / 3 自适应均值 / 4 自适应高斯 |
| `Image2DAlg_Binarize(in, mode, thresh, thresh2, maxval, mid, block, C, invert, out)` | 1 | 统一二值化：mode 0~4 同 Threshold；5 TRUNC / 6 TOZERO / 7 TOZERO_INV；8 双阈值区间内前景（low≤v≤high）/ 9 双阈值区间外前景 / 10 双阈值三值化（low/high/mid）；`invert` 非 0 对结果取反 |
| `Image2DAlg_Blur(in, method, ksize, sigma1, sigma2, out)` | 同输入 | method：0 高斯 / 1 中值 / 2 双边；ksize 自动补奇 |
| `Image2DAlg_Morphology(in, op, shape, ksize, iterations, out)` | 同输入 | op：0 erode / 1 dilate / 2 open / 3 close / 4 gradient / 5 tophat / 6 blackhat；shape：0 矩形 / 1 十字 / 2 椭圆 |
| `Image2DAlg_Canny(in, t1, t2, out)` | 1 | Canny 边缘（0/255） |
| `Image2DAlg_Sobel(in, dx, dy, ksize, out)` | 1 | Sobel 梯度，输出 \|梯度\| 缩放 0~255（dx+dy≥1，ksize 1/3/5/7） |
| `Image2DAlg_Resize(in, w, h, interpolation, out)` | 同输入 | 目标尺寸缩放 |
| `Image2DAlg_Crop(in, x, y, w, h, out)` | 同输入 | ROI 裁剪，越界自动钳制 |
| `Image2DAlg_Rotate(in, angle, scale, interpolation, out)` | 同输入 | 绕图像中心旋转，输出同尺寸 |
| `Image2DAlg_Enhance(in, method, clip, tile, out)` | 1 或同输入 | method 0 直方图均衡（1 通道）/ 1 CLAHE（彩色走 Lab 亮度通道） |
| `Image2DAlg_HSVThresholdWrap(in, h_lo, h_hi, s_lo, s_hi, v_lo, v_hi, out)` | 1 | wrap 版 HSV 分割：h_lo > h_hi 自动按环绕区间（红色跨 0 一跳搞定） |
| `Image2DAlg_WarpAffine(in, m[6], w, h, interpolation, out)` | 同输入 | 仿射变换（2x3 行主序；画布 w/h；越界填黑）——标定对齐用 |
| `Image2DAlg_WarpPerspective(in, m[9], w, h, interpolation, out)` | 同输入 | 透视变换（3x3 行主序） |
| `Image2DAlg_HSVThreshold(in, h_lo, h_hi, s_lo, s_hi, v_lo, v_hi, out)` | 1 | HSV 阈值分割 0/255 掩膜（h∈[0,180]，红色跨 0 需两次调用；灰度图 -3） |
| `Image2DAlg_AbsDiff(in1, in2, out)` | 同输入 | 批板差分：逐像素差的绝对值（尺寸/通道必须一致） |
| `Image2DAlg_Laplacian(in, ksize, scale, out)` | 同输入 | Laplacian 二阶导（2.3.0）：ksize 1/3/5/7；8U/16U 走 CV_16S 中间取绝对值×scale 饱和回原深度，浮点同深度保符号 |
| `Image2DAlg_Filter2D(in, kernel, ksize, out)` | 同输入 | 自定义 k×k 浮点核卷积（2.3.0）：ksize 奇数 1~51、行主序锚点=核中心；8U/16U 走 CV_16S 中间取绝对值饱和回原深度，浮点同深度保符号。中值/双边/高斯由 Blur method 0/1/2 覆盖，不另设入口 |
| `Image2DAlg_DenoiseNLMeans(in, h, h_color, tw, sw, out)` | 同输入 | 非局部均值降噪（2.3.0）：1ch → fastNlMeansDenoising / 3ch → fastNlMeansDenoisingColored / 4ch 拆 BGR 后回贴 alpha（同 Blur 双边先例）；h 亮度强度（>0，建议 3）、h_color 彩色强度（通常 = h）、tw 模板窗（默认 7，自动补奇）、sw 搜索窗（默认 21，须 > tw 否则 -3，自动补奇）；NLMeans 块匹配语义 Blur 未覆盖故单设入口 |
| `Image2DAlg_DistanceTransform(in, distance_type, mask_size, dst_type, out)` | 1（深度 32F/8U） | 距离变换（2.4.0）：8U 单通道二值图 → 每前景点到最近背景点的距离图（背景 0）；distance_type 1=L1（精确整数）/2=L2/3=C，mask_size 3/5、0=精确算法（仅 L2，此时 dst 必须为 CV_32F），dst_type 5=CV_32F / 0=CV_8U——骨架提取、圆焊球中心预定位（距离局部极大值=圆心） |
| `Image2DAlg_FloodFill(in, seed_x, seed_y, new_val, lo_diff, up_diff, out, out_rect, out_filled)` | 1 | 泛洪填充（2.4.0）：种子连通同色域（与当前像素差 ∈ [lo_diff, up_diff]）重绘为 new_val，8U 单通道限定、内部拷贝工作副本不改输入；out_rect=[x,y,w,h] 包围盒（可 NULL）、out_filled=重绘像素数（必填）；二值图孔洞封闭 |
| `Image2DAlg_Remap(in, map1, map2, interpolation, out)` | 同输入 | 任意重映射（2.4.0）：dst(x,y)=src(map1(x,y),map2(x,y))；map1/map2 契约=1 通道 CV_32F 位流（w·h 个 float、w·h·4 字节由调用方保证）、宽高须与 in 一致；标定畸变校正查表（initUndistortRectifyMap 输出直接喂入） |
| `Image2DAlg_WarpPolar(in, dsize_w, dsize_h, center_x, center_y, max_radius, flags, out)` | 同输入 | 极坐标展开（2.4.0）：圆形区域 → dsize 矩形条带，flags 0=LINEAR/1=LOG、max_radius>0、圆心可在图外；轴序实测口径：**列 X = rho 半径轴、行 Y = 角度轴**；环形工件（点阵 COB/圆形基板）等角度缺陷在展开图中变等间距 |
| `Image2DAlg_OffsetRemap(in, dx, dy, interpolation, out)` | 同输入 | 偏移重采样（2.5.0）：dst(x,y)=src(x−dx,y−dy)，把"内容平移了 (dx,dy)"的图搬回原位（对位流水线回正步）；越界补 0，interpolation 0=最近邻（掩膜/二值图，避免插值产生非 0/255 过渡灰度）/1=双线性（灰度/连续图），其他 -3 |
| `Image2DAlg_MaskShiftRecheck(golden_mask, current_mask, dx, dy, &matched, &new_cnt, &missed_cnt)` | —（输出 3 个计数） | 掩膜平移复核（2.5.0）：golden 缺陷位图按对位偏移平移采样 shifted(x,y)=golden(x−dx,y−dy)（最近邻，掩膜不做亚像素插值）后与当前掩膜逐像素比对，三计数 = 确认/新增/消失缺陷；两掩膜均须 8U 单通道且同尺寸，dx/dy 来自对位结果（符号语义 = golden 内容相对 current 平移 (dx,dy)） |
| `Image2DAlg_StitchBlend(base, next, dx, dy, &origin_x, &origin_y, out)` | 同输入 | 两图拼接融合（2.5.0）：next 相对 base 平移 (dx,dy)（floor 取整整数拼接，亚像素精拼先走 OffsetRemap），画布 = 两矩形包围盒，重叠区沿 X 线性渐变（w_base 起点 1→终点 0，重叠宽 1 像素特例 0.5，非重叠区直接拷贝）；out_origin = base 原点在画布中的位置（恒 ≥ 0），画布尺寸读 out->width/height |
| `Image2DAlg_FourierSpectrum(in, out)` | 1 | 幅度谱显示图（2.6.0）：灰度 → 0 补边到最优 DFT 尺寸（getOptimalDFTSize，宽高各自）→ 复数 DFT → log(1+幅度) min-max 归一化（全图同值输出全 0）→ 象限重排 DC 居中 → 裁回原尺寸；未加窗（工业谱显示口径，补边区边缘能量会进入谱图）；周期纹理（布纹/网纹/摩尔纹）呈关于 DC 对称的亮点对——PeriodicNotchFilter 选陷波点的直接依据 |
| `Image2DAlg_PeriodicNotchFilter(in, notch_xy, notch_count, notch_radius, out_clean, out_defect)` | 1（clean/defect 双输出） | 陷波滤波去周期纹理（2.6.0）：陷波点 = 相对 DC 中心偏移（像素，可负，取谱图上关于 DC 对称的亮点对），自动清共轭镜像点（两点重合只清一次），与 DC 中心重叠的陷波点自动跳过（保亮度与低频轮廓）；out_clean IDFT 值域 8U 钳制；out_defect = \|原灰度 − clean\| 按全图最大差归一化 ×255（无周期成分被滤除 → 全 0）；notch_count < 1 / notch_radius ≤ 0 → -3 |

## 测量 / 缺陷检测

| 函数 | 说明 |
|------|------|
| `Image2DAlg_ConnectedComponents(in, connectivity, min_area, max_area, &blobs, &count)` | 连通域统计，返回 `Image2DAlg_Blob{label, area, left, top, width, height, cx, cy}` 数组；`max_area<=0` 表示不设上限 |
| `Image2DAlg_ConnectedComponentsROI(in, rx, ry, rw, rh, connectivity, min_area, max_area, &blobs, &count)` | ROI 版：仅统计 ROI 内前景，结果坐标已平移回全图系；`rw/rh<=0` = 全图，无交集 -3 |
| `Image2DAlg_AnalyzeContours(in, external_only, min_area, &contours, &count)` | 轮廓测量，返回 `Image2DAlg_ContourInfo`：面积、周长、圆度 `4πA/P²`、包围盒、质心、最小外接矩形、最小外接圆 |
| `Image2DAlg_AnalyzeContoursROI(in, rx, ry, rw, rh, external_only, min_area, &contours, &count)` | ROI 版轮廓测量（坐标同上平移回全图系） |
| `Image2DAlg_MatchTemplate(image, templ, method, &score, &x, &y)` | 模板匹配（`method` 同 `cv::TemplateMatchModes` 0~5），返回最佳左上角与得分（SQDIFF 系越小越好） |
| `Image2DAlg_MatchTemplateROI(image, rx, ry, rw, rh, templ, method, &score, &x, &y)` | ROI 版：仅 ROI 内匹配，命中回全图系（模板大于 ROI -3） |
| `Image2DAlg_FindAllMatchesROI(image, rx, ry, rw, rh, templ, method, thr, max, nms, &matches, &count)` | ROI 版多目标匹配（NMS 在 ROI 内，命中回全图系） |
| `Image2DAlg_MatchTemplateMasked(image, templ, mask, method, &score, &x, &y)` | 带掩膜匹配（遮挡/干扰背景鲁棒；仅 SQDIFF/CCORR_NORMED） |
| `Image2DAlg_MatchTemplateRotated(image, templ, angle_start, angle_end, angle_step, method, &score, &cx, &cy, &angle)` | 旋转模板搜索（Mark 点带角度对位），输出**模板中心**落点 |
| `Image2DAlg_FindAllMatches(image, templ, method, score_threshold, max_matches, nms_radius, &matches, &count)` | 多目标匹配（NMS 去重，按得分从优到劣），配 `FreeMatches` |
| `Image2DAlg_MatchTemplateEx(image, templ, method, scale_lo/hi/step, angle_lo/hi/step, &score, &cx, &cy, &scale, &angle)` | **多尺度 × 多角度**组合搜索，取跨尺度全局最优 |
| `Image2DAlg_MatchTemplateSubPix(image, templ, method, &score, &x, &y)` | 亚像素对位（整像素峰 + 抛物线插值 ±0.5px） |
| `Image2DAlg_FindAllMatchesEx(image, templ, method, score_threshold, max_matches, nms_radius, scale_lo/hi/step, angle_lo/hi/step, &matches, &count)` | **多目标 × 旋转 × 尺度阵列全检**（跨组合全局 NMS），`MatchEx{cx, cy, score, scale, angle}` 中心落点；配 `FreeMatchesEx` |
| `Image2DAlg_MatchTemplatePyramid(image, templ, method, scale_lo/hi/step, angle_lo/hi/step, levels, &score, &cx, &cy, &scale, &angle)` | 金字塔粗搜提速版（顶层全范围粗搜 → 逐级邻域精化，典型提速 4~10 倍；模板最小边 ≥12 约束，弱纹理先用 Ex 验证） |
| `Image2DAlg_MatchTemplateMulti(image, templ, method, lo, hi, step, &score, &x, &y, &scale)` | 多尺度模板匹配：步进缩放模板逐一匹配取跨尺度最佳，输出含 `scale`；某尺度越界自动跳过 |
| `Image2DAlg_FitCircle(xy, count, &fit)` | 点集最小二乘圆拟合（Kåsa+SVD），返回 `Image2DAlg_CircleFit{cx, cy, r, rms}`；退化输入可能得无意义圆，用 rms 判断质量 |
| `Image2DAlg_FitLine(xy, count, &fit)` | 点集直线拟合（DIST_L2，按像素取整），返回 `Image2DAlg_LineFit{vx, vy, x0, y0, rms}` |
| `Image2DAlg_FitLineRobust(xy, count, dist_type, param, &fit)` | 稳健直线拟合（2.8.0）：cv::fitLine 透传，distType 全透传 M-estimator 稳健距离族——1=L1 2=L2 3=C 4=L12 5=FAIR 6=WELSCH 7=HUBER（DIST_USER=-1 用户自定义代价不支持；其他 → -3）；param = 代价函数参数 C（FAIR/C/WELSCH/HUBER 用，0 = cv 自动取最优默认——FAIR 1.3998 / WELSCH 2.9846 / HUBER 1.345）；低崩溃代价迭代加权抗大离群/厚尾噪声（FitLine 固定 DIST_L2 会被外点拉偏）；返回 `LineFit`（结构同 FitLine，rms 复算口径同 FitLine——全体点到直线垂直距离（叉积模）的 RMS）；≥2 点；直传 cv 保留浮点精度（不按像素取整） |
| `Image2DAlg_FitCircleEx(xy, count, &fit)` | 圆拟合高精度版（Kåsa 初值 + GN 几何精化，封装 ImageProcessor::fitCircle） |
| `Image2DAlg_FitCircleRobust(xy, count, threshold, max_iters, seed, &fit, &inliers, &n)` | 圆拟合 RANSAC 抗外点（毛刺/粘连场景），配 `FreeInliers` |
| `Image2DAlg_FitCircleBatch(xy, group_offsets, group_count, threshold, &fits, &valid, seed, max_iters)` | 批量圆拟合（2.4.0）：N 个独立点集一次完成（焊盘阵列/孔阵列）；`group_offsets`=长度 N+1 前缀偏移（[0] 必须 0、单调不减、末值=总点数）；threshold>0 逐组 FitCircleRobust（抗外点）/≤0 逐组 FitCircle（纯最小二乘）；退化组（<3 点/失败）valid=0 且字段置 0；fits/valid 调用方预分配 N 个 |
| `Image2DAlg_FitEllipse(xy, count, &fit)` | 椭圆拟合（≥5 点），返回 `EllipseFit{cx, cy, width, height, angle}`（全轴长；宽高顺序随 OpenCV 角度约定） |
| `Image2DAlg_FitEllipseAMS(xy, count, &fit)` | 椭圆拟合 AMS 法（2.8.0）：cv::fitEllipseAMS 透传——Approximate Mean Square 近似均方（代数代价上叠加一阶导数矩阵约束做广义特征值求解），对噪声/离群比 FitEllipse 标准代数拟合更稳；≥5 点，返回 `EllipseFit`（转换口径同 FitEllipse——cv RotatedRect 的 center/size/angle 直取；注意 OpenCV RotatedRect 口径：wh/角度可能与直觉互换，测试实测 wh=(40,60)+115° 对应构造的 (60,40)+25°）；退化输入 cv 抛异常 → -99（同 FitEllipse 先例） |
| `Image2DAlg_FitEllipseDirect(xy, count, &fit)` | 椭圆拟合 Direct 法（2.8.0）：cv::fitEllipseDirect 透传——Fitzgibbon 直接最小二乘（偏心修正，4·Axx·Ayy − Axy² > 0 椭圆性约束），高偏心（细长）椭圆比标准代数拟合残差更小；其余契约同 FitEllipseAMS（≥5 点、EllipseFit 同构、退化 → -99） |
| `Image2DAlg_FitEllipsesFromBinary(in, min_area, max_area, min_axis, max_axis, &fits, &count)` | 多目标椭圆拟合（椭圆焊盘/异形孔）：二值逐外轮廓 fitEllipse + 全轴长过滤；配 `FreeEllipses` |
| `Image2DAlg_FitLinesFromBinary(in, min_area, max_area, min_length, &lines, &count)` | 多目标直线拟合（板边/边缘线）：二值逐外轮廓 fitLine DIST_L2 + 覆盖跨度过滤；配 `FreeLineFits` |
| `Image2DAlg_FitRect(xy, count, &fit)` | 最小外接旋转矩形，返回 `RectFit{cx, cy, width, height, angle}`（零面积退化 → -3） |
| `Image2DAlg_ApproxPolyDP(xy, count, epsilon, closed, &out_xy, &out_count)` | 轮廓顶点逼近（Douglas-Peucker，2.0.0）：矩形/多边形形状判定——epsilon 内删冗余顶点（矩形轮廓 → 4 角点）；epsilon < 0 → -3、逼近过度退化（结果 < 3 顶点，epsilon 过大）→ -3；输出扁平点集配 `FreeContourPoints` |
| `Image2DAlg_FitRectsFromBinary(in, min_area, max_area, &rects, &count)` | 多目标旋转矩形拟合（2.0.0，异形件/料口测量）：二值前景逐外轮廓 minAreaRect + 面积过滤；无结果 NULL/0/返回 0；配 `FreeRectFits` |
| `Image2DAlg_FitPoly(xy, count, degree, &fit)` | 多项式拟合 y=Σcoef[i]·x^i（degree 1~5，SVD），返回 `PolyFit{coef[6], degree, rms}`——边缘直线度/翘曲趋势 |
| `Image2DAlg_HoughLinesP(in, rho, theta_deg, threshold, min_len, max_gap, &lines, &count)` | 概率霍夫直线（线段端点 `Line4[]`），配 `FreeLines` |
| `Image2DAlg_HoughLinesPROI(in, rx, ry, rw, rh, ...)` | ROI 版霍夫直线（端点回全图系） |
| `Image2DAlg_HoughCircles(in, dp, min_dist, param1, param2, min_r, max_r, &circles, &count)` | 霍夫圆检测（`HoughCircle{cx,cy,r}[]`，锐利二值图先接 Blur 节点），配 `FreeCirclesHough` |
| `Image2DAlg_HoughCirclesROI(in, rx, ry, rw, rh, ...)` | ROI 版霍夫圆（圆心回全图系） |
| `Image2DAlg_RegionStats(in, rx, ry, rw, rh, &st)` | ROI 内灰度统计 `GrayStats{mean, stddev, min, max}`（批次一致性监控） |
| `Image2DAlg_Histogram(in, out_hist[256])` | 灰度直方图 256 bins 计数 |
| `Image2DAlg_CompareHist(hist1, hist2, method, &score)` | 直方图相似度（method 0 CORREL/1 CHISQR/2 INTERSECT/3 BHATTACHARYYA） |
| `Image2DAlg_FitCirclesFromBinary(in, min_area, max_area, min_r, max_r, subpixel, &circles, &count)` | 多目标圆拟合（孔位/焊球阵列）：二值前景逐外轮廓 Kåsa 拟合；`subpixel!=0` 走灰度梯度亚像素细化（~0.3 像素）；配 `FreeCircles` |
| `Image2DAlg_ContourPoints(in, external_only, min_area, &xy, &points, &offsets, &count)` | 轮廓原始点集导出（扁平 [x0,y0,...] + 每条轮廓起始点索引表）；配 `FreeContourPoints` |
| `Image2DAlg_MatchShapes(xy1, count1, xy2, count2, method, &score)` | 形状相似度（Hu 矩，越小越相似，0=一致；method 1/2/3 = cv::CONTOURS_MATCH_I1~I3） |
| `Image2DAlg_ConvexityDefects(xy, count, &defects, &count)` | 凸性缺陷（引脚变形/缺料检测），返回 `Image2DAlg_Defect{start/end/farthest 索引, depth, fx, fy}`；无凹陷 count=0 |
| `Image2DAlg_ContourHausdorff(xy_a, count_a, xy_b, count_b, &distance)` | 双轮廓 Hausdorff 距离（2.4.0）：2D 双向最大-最小距离（暴力 O(n·m)）；实测轮廓 vs 理论轮廓形状偏差直接量化（MatchShapes Hu 矩之外的直接口径）；相同点集 → 0、平移方 → 平移量 |
| `Image2DAlg_PhaseCorrelateBatch(ref, moved, tile_w, tile_h, out_dx, out_dy, out_response, out_count)` | 批量局部相位配准（2.5.0）：两图按统一网格切 tile_w×tile_h 瓦片逐块相位配准（口径同 PhaseCorrelate：灰度 + CV_32F + 默认窗），瓦片行主序 index = r*cols+c、整除向下取整余数边缘条带裁掉；两段式容量（tile_count 超容量 → -3 且 *out_count = tile_count，扩容重调）；response 低的瓦片原样输出由调用方过滤——晶圆/面板大图局部错位分布检测 |
| `Image2DAlg_TemplateAffineAlign(golden, current, rough_scale_lo/hi, rough_angle_lo/hi, n_features, ratio, ransac_threshold, m[6], &inliers, &rough_score)` | 模板复合仿射对位（2.5.0）：多尺度×多角度模板匹配粗对位（口径同 MatchTemplateEx，退化区间 lo==hi 只算单值；旋转外接画布超出 current 的角度跳过）限定 ROI 后 ORB 精修，输出 golden→current 2x3 仿射（行主序，可接 WarpAffine）+ RANSAC 内点数（≥10 可靠）+ 粗对位得分——金样 EB align / scan area align 二合一 |

测量输入为非零即前景的二值语义；彩色输入内部先转灰度。无结果时返回 `0` 且 `count=0`。

## 特征点与匹配对位（2.0.0 / 2.1.0 / 2.6.0）

2.0.0 特征点族（大形变对位前置）：

| 函数 | 说明 |
|------|------|
| `Image2DAlg_HarrisCorners(in, block, ksize, k, thresh_ratio, subpix, &pts, &n)` | Harris 角点（相对最大响应阈值 + 3x3 NMS；`subpix!=0` 亚像素细化） |
| `Image2DAlg_GoodFeatures(in, max_corners, quality, min_dist, subpix, &pts, &n)` | Shi-Tomasi 角点（质量降序 + 最小间距 NMS） |
| `Image2DAlg_CornerSubPix(in, pts, count, win, &out)` | 角点亚像素细化（细化失败保持原位） |
| `Image2DAlg_ORBAlign(in1, in2, n_features, ratio, m[6], &matches, &count, &inliers)` | 一站式：ORB 检测 + Hamming 匹配（Lowe 比率）+ RANSAC 全仿射，`m` 为 in1→in2 的 2x3 行主序；配 `FreeOrbMatches` |

2.1.0 特征链路拆分（检测 → 描述 → 匹配 → 几何估计可独立复用，与 ORBAlign 并存）：

| 函数 | 说明 |
|------|------|
| `Image2DAlg_FastFeatures(in, threshold, nonmax_suppression, &pts, &n)` | FAST 角点（比 Harris 快一个数量级；`threshold<=0` → -3；平坦图无检出返回 0/0）；配 `FreePoint2f` |
| `Image2DAlg_ComputeDescriptors(in, pts, count, type, n_features, &out)` | 关键点描述子计算，type：0=ORB(32B) / 1=BRISK(64B) / 2=AKAZE(61B) / 3=SIFT(浮点 128 维)；`n_features` 仅 ORB 有效；关键点越界（图外）→ -2（贴边点由库复制边框扩图兜底，正常出描述子）；输出 `Image2DAlg_Descriptors{type, dim, is_float, count, data, fdata}` |
| `Image2DAlg_FreeDescriptors(d)` | 释放描述子 data/fdata 并置空（结构体归调用方；NULL 安全） |
| `Image2DAlg_MatchFeatures(d1, d2, pts1, pts2, ratio, &matches, &count)` | BFMatcher kNN(k=2) + Lowe 比率：二进制用 Hamming、SIFT 用 L2；ratio ∉ (0,1) → -3，type/dim/is_float 不一致 → -3，匹配 < 2 对 → -2；**复用** `Image2DAlg_OrbMatch` 结构与 `FreeOrbMatches` |
| `Image2DAlg_EstimateAffinePartial(matches, count, thr, m[6], &inliers)` | 部分仿射估计（4 DOF：旋转+等比缩放+平移，工业对位首选）：RANSAC；count < 3 → -2，thr <= 0 → -3；`m` 为 in1→in2 的 2x3 行主序（同 ORBAlign 方向，可接 WarpAffine） |
| `Image2DAlg_FindHomography(matches, count, thr, h[9], &inliers)` | 单应估计（3x3 透视，可接 WarpPerspective）：RANSAC；count < 4 → -2，共线/退化 → -2，thr <= 0 → -3；`h` 为 in1→in2 行主序、h[8]=1 |

2.6.0 匹配链路强化（Descriptors/OrbMatch 容器接口，v1 节点无法持有，C API only——同 ComputeFeature 先例）：

| 函数 | 说明 |
|------|------|
| `Image2DAlg_MatchFeaturesFlann(d1, d2, pts1, pts2, ratio, &matches, &count)` | FLANN 大集合匹配（2.6.0）：契约同 MatchFeatures（kNN(2) + Lowe、复用 `OrbMatch` + `FreeOrbMatches`）；浮点（SIFT）用 KD-Tree 索引、二进制（ORB/BRISK/AKAZE）用 LSH 索引——数千级描述子显著快于暴力 BF；LSH 为近似索引（不保证返回满 k 个邻居，与精确 BF 允许分歧，邻居不足 2 的对直接丢弃）；比率过滤后 0 命中 = rc 0 + NULL + 0（与 MatchFeatures 的 <2 对 → -2 出口不同） |
| `Image2DAlg_MatchFeaturesSymmetric(d1, d2, pts1, pts2, ratio, &matches, &count)` | 双向互为最近匹配（2.6.0）：正向 d1→d2 与反向 d2→d1 各做 BF kNN(2) + Lowe 比率过滤后取交集交叉校验——比单向 ratio 更严、误匹配率更低（代价是召回下降）；其余契约同 MatchFeaturesFlann（0 命中 = rc 0 + NULL + 0） |
| `Image2DAlg_EstimateAffineFull(matches, count, thr, m[6], &inliers)` | 全仿射估计（2.6.0，6 DOF：平移 + 旋转 + 各向异性缩放 + 剪切，cv::estimateAffine2D）：比 EstimateAffinePartial（4 DOF）能表达剪切/各向异性形变（视角倾斜的平面工件）；约束更弱、对误匹配更敏感，用 inliers（≥10 视为可靠）判断质量；count < 3 → -2、thr <= 0 → -3；`m` 为 in1→in2 的 2x3 行主序（同 ORBAlign/Partial 方向，可接 WarpAffine） |

典型链路：`FastFeatures → ComputeDescriptors → MatchFeatures（数千级大集合改 Flann、误匹配敏感改 Symmetric）→ EstimateAffinePartial / EstimateAffineFull（6 DOF）/ FindHomography → WarpAffine/WarpPerspective`。

## 特征扩展与跟踪配准立体（2.2.0）

2.2.0 特征扩展（检测+描述一次完成、斑点、一站式匹配）：

| 函数 | 说明 |
|------|------|
| `Image2DAlg_BriskFeatures(in, n_features, contrast_threshold, &pts, &n, &desc)` | BRISK 检测+描述一次完成：`contrast_threshold` → BRISK `thresh`（<=0 默认 30）；`n_features` 忽略（无上限参数）；描述子 64 字节 |
| `Image2DAlg_AkazeFeatures(...)` | AKAZE 检测+描述：`contrast_threshold` → AKAZE `threshold`（<=0 默认 0.001）；描述子 61 字节；`out_desc` 传 NULL = 只检测 |
| `Image2DAlg_SiftFeatures(...)` | SIFT 检测+描述：`n_features` → `nfeatures`（<=0 不限）、`contrast_threshold` → `contrastThreshold`（<=0 默认 0.04）；浮点 128 维 |
| `Image2DAlg_BlobDetect(in, min_area, max_area, min_circularity, min_convexity, &blobs, &n)` | 斑点检测（SimpleBlobDetector 连通域形状过滤，与 FitCirclesFromBinary 的轮廓圆拟合互补）；面积/圆度/凸性过滤，<=0 = 不过滤该项；暗斑默认（亮斑先反转）；配 `BlobFeature{x,y,radius}` + `FreeBlobFeatures` |
| `Image2DAlg_DetectAndMatch(in1, in2, type, n_features, ratio, &matches, &n)` | 一站式检测+描述+匹配（ORBAlign 泛化版，type 0=ORB/1=BRISK/2=AKAZE/3=SIFT）；复用 `OrbMatch` + `FreeOrbMatches` |

2.2.0 跟踪、配准与立体：

| 函数 | 说明 |
|------|------|
| `Image2DAlg_CalcOpticalFlowPyrLK(prev, next, pts, count, win_size, max_level, &out_pts, out_status)` | 金字塔 LK 光流：`win_size` 偶数自动补奇、`max_level >= 0`；**`out_status` 一级指针契约——调用方预分配 count 字节**；`out_pts` 本库 malloc 配 `FreePoint2f` |
| `Image2DAlg_FindTransformECC(templ, input, motion_type, max_iters, epsilon, m[9], &corr)` | ECC 对齐：motion_type 0=平移(2x3)/1=仿射(2x3)/2=单应(3x3)（与 cv 原生枚举取值不同）；`m` 为模板坐标取 input(m·x) 的采样映射；2x3 仅前 6 个有效；-2 = 不收敛/相关性 < 0.5 |
| `Image2DAlg_PhaseCorrelate(a, b, &dx, &dy, &resp)` | 相位相关平移估计（亚像素）：输出 b 相对 a 的位移；尺寸不等 → -3 |
| `Image2DAlg_StereoMatch(left, right, min_disparity, num_disparities, block_size, out_disparity)` | SGBM 双目视差：num_disparities 须为 16 倍数（否则 -3）；**`out_disparity` 一级指针契约——调用方预分配**（width/height/channels 与左图一致、1 通道）；输出 8U = CV_16S 定点值/16 四舍五入，无效视差 → 0；左图 x < min_disparity+num_disparities 条带视差不可靠（SGBM 特性） |
| `Image2DAlg_SolvePnP(object_points, image_points, count, camera_matrix, dist_coeffs, dist_count, rvec, tvec)` | 2D-3D 位姿（默认迭代法 SOLVEPNP_ITERATIVE）：count >= 4（否则 -2）；`dist_coeffs` 可 NULL（dist_count<=0 无畸变）；object_points 3N 扁平 double |

## 标定域（2.7.0，C API only）

2.7.0 新增标定域 5 算子（来源：批次 C 4.3/4.4 手眼标定与标定增强）：多位姿/多维数组输入 ABI v1 节点无法承载，全部 C API only，节点化需 ABI v2。kmat 均为 3x3 内参行主序 9 值扁平数组；dist_coeffs 均为 cv 口径 4/5/8/12/14 项，dist_count == 0 表示无畸变（可传 NULL）。

| 函数 | 说明 |
|------|------|
| `Image2DAlg_HandEyeCalib(gripper2base_rt, target2cam_rt, pose_count, method, &out_cam2gripper_rt)` | 手眼标定 AX=XB（2.7.0，cv::calibrateHandEye 透传）：gripper2base/target2cam 各为 pose_count×6 扁平 [rvec3, tvec3]（旋转向量入，内部 Rodrigues 转矩阵）；method 0=TSAI / 1=PARK / 2=HORAUD / 3=ANDREFF / 4=DANIILIDIS（直传 cv 枚举）；位姿 <3 组 → -2；输出 cam2gripper 6 值 [rvec, tvec]；位姿共线/退化时 cv 抛异常 → -99——C API only |
| `Image2DAlg_ProjectPoints(obj_pts, count, rvec, tvec, kmat, dist_coeffs, dist_count, out_pts, &out_count)` | 3D→2D 投影（2.7.0，cv::projectPoints 透传）：世界点经 [R(rvec)\|tvec] 变换、针孔投影、畸变模型落到像素坐标；obj_pts count×3 扁平入、out_pts count×2 调用方预分配——标定复核 / 重投影统计的投影侧——C API only |
| `Image2DAlg_UndistortImagePoints(src_pts, count, kmat, dist_coeffs, dist_count, out_pts)` | 图像点去畸变（2.7.0）：畸变像素坐标 → 理想像素坐标（cv::undistortPoints 迭代法，默认 5 次迭代收敛阈值 0.01）；count×2 扁平入出、调用方预分配；标定后点级复核（角点回验）——C API only |
| `Image2DAlg_UndistortRectifyMap(kmat, dist_coeffs, dist_count, new_kmat, new_w, new_h, &out_map1, &out_map2)` | 去畸变映射表（2.7.0，cv::initUndistortRectifyMap 透传，R = 单位阵；new_kmat NULL = 用 kmat 纯去畸变不重投影）：**out_map1（X 坐标图）+ out_map2（Y 坐标图）双必填**——CV_32F 位流（new_w×new_h 个 float），与 Remap 的 map1/map2 输入口径同构，`UndistortRectifyMap → (map1+map2) → Remap` 为完全对称的畸变校正查表链路；两 map 均 `FreeImage` 释放——C API only |
| `Image2DAlg_CalibReprojectionStats(obj_pts, img_pts, count, rvec, tvec, kmat, dist_coeffs, dist_count, out_errors, &mean, &rms, &max)` | 重投影误差统计（2.7.0）：内部直接调 cv::projectPoints 后与实测 img_pts 逐点欧氏距离 = 重投影误差，输出逐点误差 + mean/rms/max（mean 观察系统偏差、rms/max 定位粗差点，标定质量闭环）——C API only |

## 错误码

| 码 | 含义 |
|----|------|
| 0 | 成功 |
| -1 | 空指针 / 空图 |
| -2 | 特征/匹配/估计不足（特征族与 ORBAlign：无关键点、匹配对数过少、几何估计退化——含 EstimateAffineFull 点数不足/无法求解（2.6.0）；跟踪配准族：ECC 不收敛或相关性不足、PnP 求解失败；ReadImage 例外——-2 为打开失败；FloodFill 例外——-2 为填充失败，底层返回 -1；TemplateAffineAlign——粗对位失败（模板全尺度越界/得分无效）或精修匹配不足（2.5.0）；MatchFeaturesFlann/Symmetric 不使用 -2——比率过滤后 0 命中返回 0 + NULL + 0（2.6.0）；HandEyeCalib——位姿组数不足（< 3 组，2.7.0）） |
| -3 | 通道数、模式或参数不支持 |
| -4 | 内存不足 |
| -99 | OpenCV 异常（在 C 边界统一捕获，异常不跨 DLL） |

## 构建与测试

```bat
cmake -S . -B build -G "Visual Studio 17 2022"
cmake --build build --config Release --target Image2DAlg Image2DAlg_test
ctest --test-dir build -C Release -R Image2DAlg_test --output-on-failure
```

测试工作目录为 `build/output/Release`（与运行时 DLL 同目录），临时 png 跑完自清理。
