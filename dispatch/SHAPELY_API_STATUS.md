# ShapelyAlg 接口清单与完成状态（2026-11-12 迭代 14 后，从头文件实测提取）

> 口径：方法名一级（重载计 1 个名字）；全部**已实现并有测试**（701 断言；Release + Debug 双配置 ctest 全绿）。
> 未完成项见文末（方向 C 余项 + 方向 D + 技术债），当前**无半成品/占位接口**。

## Geom —— 二维几何（49 公开 + PreparedGeom 6）

> 计数口径（2026-11-12 全仓对账）：精确 grep `^    static` + 公开成员，全库 270 接口 + 适配层 5 = **275**；
> 分模块：Geom 49 / PreparedGeom 6 / Stereo 29 / KdTree3 6 / ImageProcessor 78 / CloudProcessor 90 /
> CloudIO 6 / MeshIO 4 / HeightMapIO 2 / 适配层 5（GeomCV 2 + StereoPCL 2 + HeightMap 1）

| 分组 | 接口 |
|---|---|
| 查询 | isEmpty / typeName / wkt / **toGeoJSON** / **toDXF** / **lastError**（线程局部 GEOS 诊断，布尔入口清空）/ bounds / toRect2d / getExteriorPoints / numInteriorRings / getInteriorRings / splitParts / getX / getY / isValid / isValidReason |
| 度量 | area / length / centroid / distanceTo / nearestPoints / isWithinDistance |
| 拓扑判定 | intersects / touches / crosses / within / contains |
| 布尔形变 | intersection / unionWith / difference / buffer / convexHull / simplify / makeValid / transformed（2D 仿射） |
| 工厂 | createPoint ×3 / createLine / createLineFromFlat / createLineFromNested / createPolygon / createPolygonFromFlat / createPolygonFromNested / createPolygonWithHoles（带孔）/ fromWKT / createCircle / createEllipse |
| 批量 | unaryUnion（只读指针列表） |
| PreparedGeom | prepare / isEmpty / intersects / contains / covers / distanceTo（私有工厂 make*/parseWKT ×7 不对外） |

## Stereo —— 三维点集（29 + KdTree3 6）

| 分组 | 接口 |
|---|---|
| 数据管理 | isEmpty / size / getPoints / getZBounds / filterByZRange |
| 包围盒 | getAABB / getOBB / getAABBVolume / getOBBVolume |
| 距离与位置 | getCentroid / minDistanceTo（KD 加速）/ findClosestPair（KD 加速）/ hasCoincidentPointWith / **meanNearestNeighborDistance** |
| 点-几何 | distanceToPlane / distanceToBox / pointInAABB / pointInOBB（静态） |
| 桥接与变换 | projectTo2D / transform / translate / **invertTransform** / **composeTransform** |
| 工厂 | createFromPoints / createFromFlatArray / createFromNestedArray / createFromBox ×3 |
| KdTree3 | build / nearest / knn / radiusSearch / empty / size |

## ImageProcessor —— 二维图像（78 静态，24 组）

| 组 | 接口 |
|---|---|
| 1 阈值 | threshold / adaptiveThreshold |
| 2 形态学 | makeKernel / dilate / erode / morphologyEx |
| 3 滤波 | gaussianBlur / medianBlur / bilateralFilter |
| 4 边缘 | canny / sobel |
| 5 几何变换 | resize / warpAffine / warpPerspective |
| 6 模板匹配 | matchTemplate / findBestMatch |
| 7 金字塔 | pyrDown / pyrUp / buildGaussianPyramid |
| 8 瓦片 | splitIntoTiles / buildTilePyramid |
| 9 轮廓 | findContours / connectedComponents / getMinAreaRect / getBoundingRect |
| 10-14 色彩增强 | cvtColor / equalizeHist / clahe / lut / bitwiseNot / bitwiseAnd / bitwiseOr / absdiff / integral / distanceTransform |
| 15 连通增强 | connectedComponentsWithStats / floodFill |
| 16 形状度量 | contourArea / isContourConvex / fitEllipse / fitLine / minEnclosingCircle / pointPolygonTest / matchShapes / convexityDefects |
| 17 直方图 | calcHist8U / compareHist |
| 18 重映射与角点 | remap / warpPolar / **goodFeaturesToTrack**（角点候选）/ **cornerSubPix**（亚像素细化，video 头仅 .cpp）/ **findNonZero** |
| 24 精密测量 | **caliperEdges**（沿线卡尺亚像素边缘）/ **contourHausdorff**（双向形状偏差） |
| 19 绘图标注 | drawLine / drawCircle / drawRect / drawPolylines / fillPolygon / drawCross / drawText |
| 20 匹配增强 | matchTemplateMasked / findBestMatchSubPix / findAllMatches / **findRotatedMatch**（旋转对位） |
| 21 形状拟合 | fitCircle ×2 / fitCircleRobust（RANSAC）/ **fitCircleBatch**（批量）/ approxPolyDP |
| 22 统计卷积 | meanStdDev / countNonZero / minMaxLoc / normalize / filter2D / convertScaleAbs |
| 23 霍夫 | houghLinesP / houghCircles |

## CloudProcessor —— 三维点云（90 静态，白名单 PointXYZ/XYZI/XYZRGB）

| 模块 | 接口 |
|---|---|
| filter ×11 | filterVoxelGrid / filterVoxelGridAndTrace / filterRandomSample / filterUniformSample / filterFarthestPointSample / filterRemoveNonFinite / filterRemoveDuplicates / filterPassThrough / filterStatisticalOutlier / filterRadiusOutlier / filterConditionalXYZ |
| extract ×5 | extractByIndices / extractByIndicesInverted / extractCropBox / extractCropOBB / extractByPolygonROI |
| trans ×7 | transApplyMatrix / transProject / transTranslate / transRotate / transRotateAround / transScale / transScaleAround |
| feature ×8 | featureNormalEstimation / featureNormalEstimationRadius / featureOrientNormalsToDirection / …TowardsPoint / …ConsistentTangentPlane / featureFPFH / featureCovariances / featureMahalanobisDistance / featureNearestNeighborDistance |
| keypoint ×2 | keypointHarris3D / keypointISS3D |
| fit ×5 | fitPlane / fitSphere / fitCylinder / **fitCone**（绕开 PCL CONE 缺陷）/ **fitCircle3D** |
| GD&T ×5 | **measureCircleRoundness**（圆度）/ **measureCylinderForm**（圆柱度）/ **measurePositionDeviation**（位置度表，PositionDeviation 结构）/ planeResiduals（有符号残差数组）/ measureFlatness |
| segment ×5 | segmentPlaneRANSAC / segmentEuclideanCluster / segmentDBSCAN / segmentRegionGrowing / **segmentPlanesMulti** |
| regist ×7 | registICP_PointToPoint / registICP_PointToPlane / registNDT / registPCA / registGICP / registFeatureMatching / registEvaluate / registInformationMatrix |
| tree ×4 | treeKdTreeKNN / treeKdTreeRadiusSearch / treeOctreeVoxelSearch / treeOctreeChangeDetection |
| surface ×11 | surfaceConvexHull / surfaceConcaveHull / surfaceSmoothMLS / surfacePoisson / surfaceGreedyProjection / surfaceMarchingCubes / surfaceMarchingCubesRBF / surfaceGridProjection / surfaceOrganizedFastMesh / surfaceMeshLaplacianSmooth / surfaceMeshClusterDecimate |
| measure ×13 | measureMinMax / measureAABB / measureOBB / measureCentroid / measureMeanAndCovariance / measurePointCloudDistance / measureFlatness / **measurePlaneAngle / measurePlaneIntersection** / **measureMeshArea / measureMeshVolume / measurePointToMeshDistance** / cloudToHeightMap（mode 3=点计数密度）/ **thicknessMap** / **heightMapToCloud** |
| mesh 几何 ×4 | **meshFaceNormals**（每面法线）/ **meshVertexNormals**（面积加权顶点法线）/ **meshCropByOBB**（面级裁剪重编号）/ **measurePointToMeshSignedDistance**（内负外正，斜向射线奇偶） |

## CloudIO + MeshIO + HeightMapIO —— 文件读写

- **CloudIO**（模板 ×3 点型）：ioLoadPCD / ioSavePCD / ioLoadPLY / ioSavePLY / ioLoadTXT / ioSaveTXT
- **MeshIO**：saveOBJ / loadOBJ / saveSTL / loadSTL
- **HeightMapIO**：save / load（SHMAP1 二进制：magic+元数据+cells+mask）
- **HeightMapAdapter**（ShapelyAlg.h 适配层，header-only）：toImage（8U min-max 归一化 / 16U 原值截断，NaN/空格→0）
- **适配层**：GeomCVAdapter（createRect/createEllipse）/ StereoPCLAdapter（createFromCloud）/ HeightMapAdapter（toImage）

## 数据结构（Types.h / 各头）

Point2d / Point3d / BoundingBox2d / AABB3d / OBB3d（编译期契约 ×10）、Tile、Match2d、Mesh3d、HeightMap（at() 越界安全）、FitMethod

---

## 未完成（无占位实现，均为规划项）

### 技术债（需决策/观察）
- **D1** Geom 布尔运算 GEOS 返 NULL 与"空交集"不可区分 —— 等 API 方向拍板
- **D4** GEOS thread_local context 跨线程析构（存疑，观察项）
- ~~D2 README 计数~~ ✅ 已清 / ~~D3 Stereo noexcept new~~ 维持现状（不变量模式）
- ~~P2 六项~~ ✅ 迭代 7/8 全落地（GeoJSON·DXF 导出、批量拟合、fitCone、旋转对位、KNN、HeightMapIO）

## 下阶段开发方向

### 方向 A —— GD&T 测量家族 —— ✅ 全部完成（2026-11-12 迭代 12）
- ✅ measureCircleRoundness（圆度）/ measureCylinderForm（圆柱度）/
    measurePositionDeviation（位置度表，贪心配对）/ planeResiduals（残差数组）

### 方向 B —— 网格互操作深化 —— ✅ 全部完成（2026-11-12 迭代 12）
- ✅ meshVertexNormals（面积加权）/ meshCropByOBB（面级裁剪）/
    measurePointToMeshSignedDistance（内负外正：精确距离 × 斜向射线奇偶——
    **轴对齐射线在轴对齐网格上命中共享棱被双重计数，实测修正为固定斜向**）

### 方向 C —— 2D 精密测量 —— ✅ 全部完成（C 迭代 12 + 迭代 14）
- ✅ caliperEdges（沿线卡尺：双线性采样 + 中心差分 + 抛物线亚像素，极性过滤）
- ✅ contourHausdorff（双向最大-最小）
- ✅ cornerSubPix（迭代 14，用户"继续"放行；video 头仅 .cpp 引用，公共头仍零 video，
  opencv_world 单 DLL 部署不变）+ findNonZero 透传

### 方向 D —— 性能工程 —— ✅ 完成（2026-11-12 迭代 13/15，基线 + OpenMP + Debug 回归）
- ✅ **基准 harness**：ShapelyAlg_bench.exe（确定性 LCG 数据，不注册 ctest）。基线（Release/单线程→OpenMP 后）：
  filterVoxelGrid 500k 49.8ms / cloudToHeightMap 8-10ms / KdTree3 build 200k 50ms + nearest 0.9µs/query /
  fitPlane RANSAC 100k 4.8ms / findRotatedMatch 256px×24 角度 16.8ms —— **均 <60ms，无需并行**
- ✅ **唯一热点**：pointToMeshDistance 20k×12.8 万面 29.3s / signedDistance 44.5s → **OpenMP 11 倍提速至 2.6s/4.0s**
- ⚠️ **实现约束（实测 segfault 教训）**：/openmp:llvm 只用于 CloudProcessorMesh.cpp（纯 POD 数学 TU）——
  对含 Eigen/PCL 重型模板的 CloudProcessor.cpp 全量开 OpenMP 会访问冲突（崩溃点漂移）；运行时 vcomp140.dll
  见 DEPENDENCIES.md 部署节
- ✅ **Debug 配置回归**（迭代 15）：实锤并修复两类 Debug-only 中断——NaN 进 RANSAC 触发 PCL KdTree
  断言（fitInputClean 守卫，inlier 索引经全局映射保持指向原始点云）+ registICP_PointToPlane 薄几何
  奇异（AABB 预判直接走点到点回退）；修复后 **Debug 701/701 exit 0 零断言**
- ⬜ KdTree3 批量查询并行版（0.9µs/query 已够快，暂无必要）
- ⬜ PCL 点型白名单扩展评估（PointNormal / XYZL）

### 技术债收口
- ~~D1 布尔失败语义~~ ✅ 迭代 13 落地（Geom::lastError：线程局部 GEOS 诊断 handler + 布尔/形变入口清空；
  isEmpty + lastError 非空 = 失败，空 = 真空；测试 3 条契约）
- D4 GEOS thread_local context 跨线程析构（存疑，观察项）
