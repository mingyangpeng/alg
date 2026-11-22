# ShapelyAlg 开发路线图（2026-11-10 盘点，2026-11-11 P1 完成）

> **状态（2026-11-22 盘点）**：本路线图已由 `dispatch/SHAPELY_API_STATUS.md`（接口清单与
> 完成状态，逐批更新）与 `待开发接口.md`（C API/节点级未完成清单）接管维护，本文保留作
> 2026-11-10~11 的历史规划存档、状态不再逐项更新。截至盘点时点：文中 P2/P3 绝大多数项
> 已交付（GeoJSON/DXF 导出、批量拟合、fitCone、旋转对位、KNN、HeightMapIO、OpenMP 并行、
> D1 布尔失败语义），D5（Calib 联动）已于 2026-11-20 交付并注册 ctest（dispatch/PM_LOG.md
> 派单 003）。剩余尾项以 SHAPELY_API_STATUS"未完成"节与待开发接口.md 为准。

> 状态基线（存档时点）：ShapelyAlg_test **601 断言**全过 / ctest 全库 9 项 / README 与代码同步。
> 维护约定：新文件 UTF-8 BOM；4 空格缩进；C++17；导出宏走 src/AlgExport.h；
> 源码搜索 grep -a；算法模块零 UI 痕迹；占位实现必须删除；每改必配回归测试。

## 一、已完成（能力全景）

### Geom（2D 几何，GEOS 3.15）
- ✅ 工厂：点/线/面/圆/椭圆/WKT（鸭子类型 + 扁平/嵌套数组）
- ✅ 度量：area/length/centroid（类型门）/bounds/toRect2d/顶点导出（Multi 递归）
- ✅ 拓扑：intersects/contains/within/crosses/touches + PreparedGeom 批量判定
- ✅ 布尔：intersection/unionWith/difference/buffer/convexHull/simplify
- ✅ 本轮新增：带孔多边形工厂+内环读取 / unaryUnion 批量 / nearestPoints /
  isWithinDistance / makeValid / isValidReason / transformed（2D 仿射，保 Multi+孔洞）

### Stereo（3D 纯数学）
- ✅ AABB/OBB（雅可比 PCA，旋转盒回归已修）/质心/Z 过滤/projectTo2D/刚体变换
- ✅ minDistanceTo/findClosestPair（KD 树加速 O(n·log m)）/重合判定
- ✅ KdTree3：build/nearest/radiusSearch（对拍暴力验证）
- ✅ 点-几何：distanceToPlane/DistanceToBox/pointInAABB/pointInOBB

### ImageProcessor（2D 图像，OpenCV 4.4，23 组）
- ✅ 基础 18 组：阈值/形态学/滤波/边缘/几何变换/模板匹配/金字塔/瓦片/轮廓/
  色彩/增强/LUT/积分图/距离变换/连通域/形状度量/直方图/重映射
- ✅ 绘图标注 7 原语（业务层不再被迫 include imgproc）
- ✅ 匹配增强：带掩膜模板匹配（NaN/Inf 归一）/多目标 NMS/亚像素细化
- ✅ 形状拟合：fitCircle（Kasa+GN，双重载）/approxPolyDP；直线/椭圆/旋转矩形已有
- ✅ 统计卷积：meanStdDev/countNonZero/minMaxLoc/normalize/filter2D/convertScaleAbs
- ✅ 霍夫检测：houghLinesP/houghCircles

### CloudProcessor + CloudIO（3D 点云，PCL 1.9，白名单三点点型）
- ✅ 11 模块 70 算子（filter/extract/trans/feature/keypoint/fit/segment/regist/tree/surface/measure）
- ✅ 高度图：cloudToHeightMap（max/min/mean，2000 万格上限）
- ✅ extractByPolygonROI（2D 多边形裁剪，射线法）
- ✅ 网格后处理：Laplacian 平滑 + 顶点聚类抽稀
- ✅ IO：PCD/PLY/TXT（严格解析+9 位精度）+ MeshIO OBJ（往返无损）

### 质量资产
- ✅ 582 断言 / ctest 9 项全过 / Types.h 编译期结构契约 ×10 / README 接口级文档 /
  demo 三场景 / dumpbin 导出审计 / 五轮 review 全部实锤 bug 修复

## 二、未完成（已知技术债，小项）

| # | 事项 | 性质 | 处置建议 |
|---|------|------|----------|
| D1 | Geom 布尔运算 GEOS 返 NULL 与"空交集"不可区分（拓扑异常静默） | API 设计决策 | 需要你拍板：加 out 错误参 / optional 语义 / 维持现状仅文档 |
| D2 | README 计数声称滞后 | 文档同步 | ✅ 已清（根 README "68 算子"改非数值口径；测试计数归用户会话维护口径） |
| D3 | Stereo 移动构造 noexcept 内 new（bad_alloc→terminate，理论风险） | 代码洁癖 | 维持现状——与 KdTree3 同"源对象非空"不变量模式，改动需全量访问器审计，收益不值 |
| D4 | GEOS thread_local context 跨线程析构（存疑，未实测出问题） | 存疑 | 观察项，出问题再修 |
| D5 | Calib 联动避让中（Image2DAlg_Calib 你在开发）——**✅ 已交付（2026-11-20 派单 003：标定数学缺陷链修复 + CalibTest 注册 ctest 13 项）** | 边界 | 已纳入测试/文档体系 |

## 三、待开发方向

### P1 —— 测量主路径 —— ✅ 全部完成（2026-11-11）
1. ✅ **3D 空间圆拟合 fitCircle3D**（CloudProcessor）：RANSAC 主平面 → 投影 →
   2D 几何最小二乘（FitUtil.h 共享核）→ 回投 3D；输出圆心/单位法向/半径/内点/RMS。
2. ✅ **RANSAC 版 2D 圆拟合 fitCircleRobust**（ImageProcessor）：三点定圆采样 →
   内点统计 → 最优集 GN 精化 → 精化后重算内点；LCG 固定种子可复现。
3. ✅ **平面度 measureFlatness**（CloudProcessor）：主平面局内点残差峰峰值（GD&T 简化口径）。
4. ✅ **MeshIO saveSTL/loadSTL**（二进制+ASCII，加载按 84+50n 长度自动识别；
   法向自动计算；二进制 float32 精度损失已文档化）。
5. ✅ **heightMapToCloud**（格心坐标，与 cloudToHeightMap 往返栅格一致）。
   附带工程化：拟合核抽 **FitUtil.h** 内部共享头（fitCircle/fitCircle3D 同源不漂移）。

### P2 —— 扩展能力（有明确场景再做）
6. Geom 导出 DXF/GeoJSON（DXF 渲染导出在交付场景已被提及；GeoJSON 供上位机）。
7. 批量拟合 API：一次输入 N 个轮廓/索引组，批量 fitCircle/fitLine 输出数组
   （焊盘阵列一步拟合；业务层循环也可，但库内做可复用内点缓冲）。
8. fitCone/fitTorus（PCL 1.9 有 SACMODEL_CONE；沉头孔/倒角测量）。
9. ImageProcessor：模板匹配的"旋转-缩放搜索"封装（金字塔+逐角度 matchTemplate，
   Mark 点带角度对位）——方案级链路，建议封装成独立类而非散函数。
10. KdTree3 增加批量 KNN 与"点到点云平均距离"汇总（点云密度自适应参数依据）。

### P3 —— 性能/工程（测量主线需要时再做）
11. 大点云算子 OpenMP 并行（filterVoxelGrid/heightMap 等；先基准测试再动手，
    避免过度设计）。
12. Geom 布尔失败语义升级（D1 决策后的实现）。
13. PCL 点型白名单扩展评估（PointNormal/XYZL——分割标签联动需求出现时）。

## 四、执行约定
- 每个方向开工前先在此文件标注 [进行中]，完成后移入"已完成"并写交付摘要。
- 与用户在 Image2DAlg/Point3DAlg 的开发撞构建窗口时等 60s 重试（T32/迭代 5 先例）。
- 白名单点型/导出宏/公共头纯净三大铁律不变。
