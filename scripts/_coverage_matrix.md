# C API ↔ ComUI 节点覆盖矩阵审计（2026-11-19；2026-11-20 增补滤波拟合批）

范围：`src/Image2DAlg/include/Image2DAlg_api.h`（2.0.0~2.2.0 全部注释）、
`src/Point3DAlg/include/Point3DAlg_api.h` + `Point3DAlg_SickApi.h`、
`src/BusinessAlg/include/BusinessAlg_api.h`，对照 `integration/comui/*.node.json`。
排除基础设施：GetVersion / Init / Cleanup / FreeImage / ReadImage（节点化用途保留）/ 各 Free* / FreePoint2f 等。

分类定义：**A**=值得补且 ABI 可行（真实管线价值、输入输出能映射 image/cloud/text 参数）；
**B**=ABI 不可行（中间结果无法跨节点流转、需要句柄状态、多输出组数不定等）；
**C**=价值存疑（被现有节点覆盖 / 管线需求弱）。

---

## 一、Image2DAlg 完整矩阵（73 个业务 C API）

### 1.1 已节点化（55 / 73，本次补缺后）

| C API | 节点 id / method | 备注 |
|---|---|---|
| ReadImage（基础设施） | alg.image2d.read / Image2DAlg_ComuiRead | 输出恒 BGRA |
| ToGray | alg.image2d.to_gray | |
| Gamma | alg.image2d.gamma | |
| Threshold | alg.image2d.threshold | |
| Binarize | alg.image2d.binarize | 2.2.0 统一二值化（含反转/双阈值/三值化） |
| Blur | alg.image2d.blur | |
| Morphology | alg.image2d.morphology | |
| Canny | alg.image2d.canny | |
| Sobel | alg.image2d.sobel | |
| Resize | alg.image2d.resize | |
| Crop | alg.image2d.crop | |
| Rotate | alg.image2d.rotate | |
| Enhance | alg.image2d.enhance | |
| HSVThreshold | alg.image2d.hsv_threshold | |
| HSVThresholdWrap | alg.image2d.hsv_threshold_wrap | |
| AbsDiff | alg.image2d.absdiff | |
| ConnectedComponents | alg.image2d.connected_components | |
| ConnectedComponentsROI | alg.image2d.connected_components_roi | |
| AnalyzeContours | alg.image2d.analyze_contours | |
| AnalyzeContoursROI | alg.image2d.analyze_contours_roi | |
| ContourPoints | alg.image2d.contour_cloud | 点集导出为 z=0 点云 |
| MatchTemplateMasked | alg.image2d.match_template_masked | |
| MatchTemplateEx | alg.image2d.match_template_ex | 尺度×角度 Mark 对位 |
| MatchTemplateSubPix | alg.image2d.match_template_subpix | |
| FindAllMatches | alg.image2d.find_matches | |
| FindAllMatchesEx | alg.image2d.find_matches_ex | 阵列全检 |
| **FindAllMatchesROI** | **alg.image2d.find_matches_roi（本次补）** | |
| **MatchTemplatePyramid** | **alg.image2d.match_template_pyramid（本次补）** | |
| HoughLinesP | alg.image2d.hough_lines | |
| HoughCircles | alg.image2d.hough_circles | |
| RegionStats | alg.image2d.region_stats | |
| MatchShapes | alg.image2d.match_shapes | |
| FitCircle | alg.image2d.fit_circle | 点集 Kåsa |
| FitLine | alg.image2d.fit_line | |
| FitCirclesFromBinary | alg.image2d.fit_circles | |
| FitEllipsesFromBinary | alg.image2d.fit_ellipses | |
| FitRectsFromBinary | alg.image2d.fit_rects | |
| FitLinesFromBinary | alg.image2d.fit_lines | |
| WarpAffine | **alg.image2d.warp_affine（本次补）** | 矩阵 text 参数 |
| WarpPerspective | **alg.image2d.warp_perspective（本次补）** | 矩阵 text 参数 |
| HarrisCorners | alg.image2d.harris_corners | |
| GoodFeatures | alg.image2d.good_features | |
| CornerSubPix | alg.image2d.corner_sub_pix | |
| ApproxPolyDP | alg.image2d.approx_poly_dp | |
| ORBAlign | alg.image2d.orb_align | |
| FastFeatures | alg.image2d.fast_features | |
| EstimateAffinePartial | alg.image2d.estimate_transform（motion=0）/ alg.image2d.feature_align（内部） | |
| FindHomography | alg.image2d.estimate_transform（motion=1）/ alg.image2d.feature_align（内部） | |
| BlobDetect | alg.image2d.blob_detect | |
| DetectAndMatch | alg.image2d.detect_and_match | |
| CalcOpticalFlowPyrLK | alg.image2d.optical_flow_lk | |
| FindTransformECC | alg.image2d.ecc_align | |
| PhaseCorrelate | alg.image2d.phase_correlate | |
| StereoMatch | alg.image2d.stereo_match | |
| SolvePnP | alg.image2d.solve_pnp | |
| （编排）FastFeatures→ComputeDescriptors→MatchFeatures→EstimateAffinePartial/FindHomography | alg.image2d.feature_align | 一站式，描述子不拆节点（B 类原因） |
| **ConvexityDefects** | **alg.image2d.convexity_defects（本次补）** | 输入须为 contour_cloud（findContours 点序）来源 |

> 节点数 51（补缺前）= read + 49 个 API 直映节点 + feature_align（编排）。
> 本次 +5 → 56 节点，Image2DAlg.dll 导出 56 个 ComUI 入口 + algo_free，method 全部可解析。

### 1.2 未节点化（18 / 73，补缺后剩余）

| C API | 分类 | 理由 |
|---|---|---|
| MatchTemplate | C | match_template_subpix 是其严格超集（同一相似度面 + 抛物线插值，输出亚像素 score/x/y） |
| MatchTemplateMulti | C | match_template_ex 以 angle_lo=angle_hi 退化为纯尺度搜索，结果等价 |
| MatchTemplateRotated | C | match_template_ex 以 scale_lo=scale_hi 退化；Ex 输出模板中心坐标语义相同 |
| MatchTemplateROI | C | find_matches_roi（本次补）max_matches=1 即单目标 ROI 匹配 |
| FitCircleEx | A（未入选） | 点集入→消息出，与 fit_circle 同形态；Kåsa+Gauss-Newton 精化提升精度。名额 ≤5 未入选，下一批首选 |
| FitCircleRobust | A（未入选） | RANSAC 抗外点（毛刺/粘连）；out_inliers 索引无跨节点意义，随消息回显即可 |
| HoughLinesPROI | A（未入选） | hough_lines 已有全图版；ROI 版沿用 cc_roi 的坐标平移回全图先例，中价值 |
| HoughCirclesROI | A（未入选） | 同上 |
| Histogram | B | 256 bins int 数组无数值端口；消息通道 900 字节截断装不下；编码成 256×1 图像丢计数精度 |
| CompareHist | B | 依赖 Histogram 的 256 数组在节点间无损流转，ABI v1（image/cloud/消息）不可承载 |
| ComputeDescriptors | B | 描述子为二进制/浮点块，无 image/cloud 端口可载——feature_align 注释已显式声明不拆节点 |
| MatchFeatures | B | 输入 Descriptors 结构无法跨节点流转（同上） |
| BriskFeatures | B | 检测+描述一次完成，描述子输出无法流转；仅出点集则下游无描述子可匹配 |
| AkazeFeatures | B | 同上 |
| SiftFeatures | B | 同上 |

剩余 18 = A 4 + B 7 + C 7。（MatchTemplatePyramid / FindAllMatchesROI / WarpAffine / WarpPerspective / ConvexityDefects 原列 A，本次已补。）

---

## 二、Point3DAlg 汇总矩阵（api.h 19 业务 + SickApi.h 9 = 28；只审不补）

### 2.1 已节点化

| C API / 能力 | 节点 |
|---|---|
| ReadSickCloud | alg.point3d.read_sick |
| LoadCloud | alg.point3d.load_cloud |
| FilterCloud | alg.point3d.filter_cloud |
| ExtractCloud | alg.point3d.extract_cloud |
| TransformCloud | alg.point3d.transform_cloud（apply_transform 覆盖 method=0 语义，走 Regist 工具） |
| FitModel | alg.point3d.fit_model |
| SegmentCloud | alg.point3d.segment_cloud |
| MeasureCloud | alg.point3d.measure_cloud |
| DetectKeypoints | alg.point3d.detect_keypoints |
| CloudToHeightMap | alg.point3d.height_map |
| （编排，走 tools，非 api.h 直映） | voxel_downsample、manual_regist、dome_fit、dome_fit_base、bump_report、region_plane、cloud_diff、circle_fit、profile、fit_circle_2d、apply_transform、save_transform |

### 2.2 未节点化（18）

| C API | 分类 | 理由 |
|---|---|---|
| SaveCloudEx | A（并行任务候选） | cloud 入 + path text 参数即可；落盘输出节点（与 save_transform 的变换文件保存不同物） |
| KnnSearch | A（并行任务候选） | query 点 3 个 number 参数承载；命中点可作 z=0 点云输出 + 索引/距离进消息 |
| RadiusSearch | A（并行任务候选） | 同上 |
| ThicknessMap | A（并行任务候选） | 2 点云入 → 入口内栅格化 → 渲染 8U BGRA 显示 + 厚度统计消息（height_map 节点同款先例）；板厚/锡膏测量有真实价值 |
| StitchQuality | A（并行任务候选） | 一次性封装可行：folder 入 → 入口内 Create/LoadRawInto/StitchQuality/Free → 统计进消息；多相机衔接带自检 |
| GetFovBoxes | A（并行任务候选） | 一次性封装可行：矩形坐标随消息/点云输出 |
| EstimateNormals | B | N×4 法线数组无端口可流转 |
| ComputeFeature | B | count×dim 特征数组无端口 |
| ReconstructSurface | B | Mesh 容器（顶点+面索引）无对应输出端口；只出顶点点云会丢面片 |
| MeshOp | B | 输入 Mesh 无端口 |
| HeightMapToCloud | B | HeightMap 容器无法跨节点流转（height_map 节点输出为 8U 显示图，float cells+mask 丢失） |
| CreateSickReader / LoadRawInto / FreeSickReader | B | 不透明句柄状态无法跨节点 |
| GetSickInfo | C | 诊断信息查询，价值低 |
| GetSickRowY | C | 运动表数组输出（数组端口问题 + 调试用） |
| GetSickRaw | C | uint16 原始高度图中间数据，调试用 |
| GetSickIntensity | C | 强度图中间数据，调试用 |

剩余 18 = A- 6 + B 8 + C 4。**补缺由并行任务负责，本次未改 Point3D 侧任何文件。**

---

## 三、BusinessAlg 汇总（只审不补）

| C API / 能力 | 节点 | 状态 |
|---|---|---|
| ComputeRegionRatio | business.region.ratio | ✓ 直映（结果并入消息通道——ABI v1 无数值端口） |
| （编排）DomeFit + BumpCompare | business.bump_ng_check | ✓ 编排节点（走 Point3DAlg tools） |

1 个 C API 全覆盖 + 1 个编排节点，无缺口。

---

## 四、审计意外发现

1. **Image2DAlg_ConvexityDefects 的"点序自适应"仅覆盖 findContours 点序**（即 contour_cloud 来源）。手工密集轮廓点（顺/逆时针两种序均实测）得不到凹口缺陷，而是四角伪缺陷（depth 77~108）；ShapelyAlg 单测同款 L 形点集经 C API 调用直接 -99（cv::convexityDefects 对非单调凸包索引抛异常）。疑似 contourArea(oriented=true) 反转分支与 convexHull(clockwise=false) 的方向约定不匹配（src/** 本次只读未改，建议核心层复核）。冒烟脚本因此改走 contour_cloud → convexity_defects 真实链路验证（凹口深度 30.0 精确命中）。
2. **WarpAffine 头注释与实现语义表述不一致**：头文件写 dst(x,y)=src(M·[x,y,1]ᵀ)，实现为 cv::warpAffine 默认语义（M 为前向映射 src→dst，按 M 的逆采样）。已在新增节点注释按实现语义写明。
3. **一致性校验**：83 个 node.json（含并行任务新增 3 个 point3d）id 全局唯一；Image2DAlg.dll 56 个声明 method 全部可解析导出。
4. build_s2 的 deploy_runtime_dlls 曾在并行任务 Point3DAlg CMake 中间态下重生成失败（"Cannot specify compile definitions for target Point3DAlg"），60s 后重试成功。

---

## 五、增补（2026-11-20 滤波拟合完善批）

本轮覆盖矩阵后续交付的节点化与新 C API 落地记录：

| 项 | 交付 | 状态 |
|---|---|---|
| FitCircleEx / FitCircleRobust / HoughLinesPROI / HoughCirclesROI | §一 "A（未入选）下一批首选" 4 项全部节点化：alg.image2d.fit_circle_ex / fit_circle_robust / hough_lines_proi / hough_circles_roi（适配层 2.4.0 段） | ✅ 已闭环 |
| Image2DAlg_Laplacian / Image2DAlg_Filter2D | 2.3.0 新增 2 个 C API（总数 95→97），当批节点化 alg.image2d.laplacian / filter2d；中值/双边/高斯经审计由 Blur method 0/1/2 覆盖，不另设入口 | ✅ A 类新增即闭环 |
| Point3DAlg FitModel method 5 = line3D / FilterCloud method 10 = approxVoxel | 1.3.0 无新导出（既有接口扩取值域，C ABI 42 不变）；节点 point3d_fit_model / point3d_filter_cloud 扩 method 上限并登记 | ✅ 已闭环 |
| Point3DAlg SaveCloudEx / KnnSearch / RadiusSearch / ThicknessMap | §二 A- 类 4 项节点化：alg.point3d.save_cloud_ex / knn_search / radius_search / thickness_map（T2 交付） | ✅ 已闭环 |
| 节点计数 | 83 → 93（image2d 62 + point3d 29 + business 2）；id 全局唯一复核通过；声明 method 全部可在 DLL 导出表解析（Laplacian/Filter2D 核心 + 62 Comui 入口实测） | ✅ |
| Point3DAlg FitModel method 6 = torus | 1.4.0 无新导出（既有接口扩取值域，C ABI 42 不变）；底层新增 CloudProcessor::fitTorus 全自研（PCL 1.9 无 SACMODEL_TORUS——分布初值多候选 + LM 数值雅可比，残差为点到环面面精确距离、不需要法线；fit 0/1=采样稳健层、2=确定性多候选 LM）；节点 point3d_fit_model method 上限 5→6 并登记，out_coeffs 预分配 8=布局最长；测试 +9 用例（含 25% 外点恢复、退化路径） | ✅ 已闭环 |
| 节点计数 | 93 → 94（image2d 63 + point3d 29 + business 2，2026-11-21 复核）；sick_fov 为批次 0 唯一剩余缺项 | ✅ |
| Point3DAlg GD&T 几何公差组（批次 B 3.1） | 1.5.0 新增 5 C API（C ABI 42→47）：MeasureCircleRoundness / MeasureCylinderForm / MeasurePositionDeviation / PlaneResiduals（§9.10a）+ MeasurePointToMeshSignedDistance（§9.12a，Mesh typedef 顺序约束）；底层 measure* 系列基础库既有算子，纯 C 边界封装；Mesh 面积/体积/无符号点距由 MeshOp op 3/4/5 覆盖不重复；节点化 alg.point3d.gdt（method 0~3，汇总消息）→ 节点 94→95；测试 +22 用例 | ✅ 已闭环 |
