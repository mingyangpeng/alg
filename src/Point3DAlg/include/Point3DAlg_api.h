#ifndef POINT3DALG_API_H
#define POINT3DALG_API_H

#include "AlgExport.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =====================================================================
 * Point3DAlg C API（1.1.0 起）
 *
 * 版本历史：
 *   1.0.0  ReadSickCloud / FreeBuffer（一步式 SICK 读取）
 *          （LoadSickConfig / SaveCloud / SaveDepth / SaveDepthView /
 *           DrawFovBoxes 见 Point3DAlg_SickReader.h，同期存在）
 *   1.1.0  版本/生命周期接口、通用点云容器、点云文件读写、滤波/裁剪/变换
 *          算子（PCL）、SICK 深层接口（句柄族，见 Point3DAlg_SickApi.h）
 *   1.2.0  P1 批次：通用几何拟合 FitModel、分割聚类 SegmentCloud、
 *          KNN/半径搜索 + FreeSearchResult、通用度量 MeasureCloud；
 *          P2 批次：法线估计 EstimateNormals、特征描述 ComputeFeature、
 *          关键点 DetectKeypoints、表面重建 ReconstructSurface + Mesh
 *          容器/FreeMesh、网格处理 MeshOp、3D→2D 桥接 CloudToHeightMap/
 *          HeightMapToCloud/ThicknessMap + HeightMap 容器/FreeHeightMap、
 *          多段点云 FreeClouds（全部 PCL 守卫，实现见 Point3DAlg_CloudOps.cpp）
 *   1.3.0  滤波拟合完善批（无新导出，既有签名扩展取值域）：
 *          FitModel method 5 = line3D 空间直线拟合（out_coeffs 6 值
 *          [px,py,pz, dx,dy,dz]，底层 CloudProcessor::fitLine3D，
 *          SACMODEL_LINE，三种 fit 均支持）；
 *          FilterCloud method 10 = approxVoxel 近似体素降采样
 *          （params [0]=leaf>0，底层 CloudProcessor::filterApproxVoxelGrid，
 *          PCL ApproximateVoxelGrid 大点云快速版）
 *   1.4.0  拟合族补全（无新导出，既有签名扩展取值域）：
 *          FitModel method 6 = torus 圆环面拟合（out_coeffs 8 值
 *          [cx,cy,cz, dx,dy,dz, R, r]，底层 CloudProcessor::fitTorus——
 *          PCL 1.9 无 SACMODEL_TORUS，全自研：分布初值多候选 → LM
 *          数值雅可比精化，残差为点到环面面的精确距离；fit 0=RANSAC/
 *          1=MSAC 走随机采样+内点评分稳健层，fit 2=LMS 走确定性多候选
 *          LM 中值择优，三种 fit 均支持；剔除非有限点后 <8 点 → -2）
 *   1.5.0  GD&T 几何公差组（§9.10a，5 新导出——底层算子均为基础库
 *          既有实现，本版本只补 C 边界）：MeasureCircleRoundness 圆度 /
 *          MeasureCylinderForm 圆柱度 / MeasurePositionDeviation 位置度
 *          偏差表 / PlaneResiduals 平面有符号残差数组 /
 *          MeasurePointToMeshSignedDistance 点到闭合网格带符号距离
 *          （Mesh 面积/体积/无符号逐点距离已由 MeshOp op 3/4/5 覆盖，
 *          不重复）
 *   1.6.0  聚类/分割扩展（无新导出，既有签名扩展取值域）：
 *          SegmentCloud method 5 = conditionalEuclidean 条件欧氏聚类
 *          （params [tolerance,min,max,height_diff]；高度差约束连通，
 *          底层 CloudProcessor::segmentConditionalEuclidean——PCL 1.9
 *          条件函数是函数指针无法捕获参数，KdTree+并查集手工实现）；
 *          method 6 = groundPlane 地面分割（params [window,slope,
 *          initial,max,cell]，底层 PCL ProgressiveMorphologicalFilter，
 *          本层组合管线直调）；method 7 = minCut 图割前景分割（params
 *          [sigma,weight,radius, 种子点 xyz...]，底层 PCL
 *          MinCutSegmentation extract，本层组合管线直调）。
 *          regionGrowingRGB（需 rgb 容器扩展）与 supervoxel（标签载荷
 *          需 ABI v2）暂缓，见待开发清单。
 *   1.7.0  滤波/拟合扩展 II（无新导出，既有签名扩展取值域）：
 *          FilterCloud method 11 = conditionalRemoval 复合条件去除
 *          （params [op,x_min,x_max,y_min,y_max,z_min,z_max]——op 0=AND
 *          三轴全满足才保留/1=OR 任一轴满足即保留，逐轴闭区间，±INFINITY
 *          = 该侧无界，非有限点恒剔除，底层 PCL ConditionalRemoval 组合
 *          管线直调）；method 12 = medianFilter 中值滤波（params [window]，
 *          PCL MedianFilter——1.9 硬性要求有序点云，本层按"1 列 × N 行"
 *          伪有序化呈递，窗口沿输入点序滑动、仅平滑 z）；method 13 =
 *          gridMinimum 格网最低点（params [cell]，PCL GridMinimum，每
 *          XY 格保留 z 最低点——地面提取）。
 *          FitModel method 7 = parallelPlane 平行平面（out_coeffs 4 值
 *          [a,b,c,d]，PCL SACMODEL_PARALLEL_PLANE——轴固定 +Z：法向 ⊥ Z
 *          的竖直平面/墙面，eps 固定 5°，任意轴待后续扩展）；method 8 =
 *          perpendicularPlane 垂直平面（[a,b,c,d]，SACMODEL_PERPENDICULAR_
 *          PLANE——法向 ∥ Z 的水平面/台面）。两者 PCL SACSegmentation
 *          组合管线直调（本层实现，不经 CloudProcessor fit*），三种 fit
 *          均支持；method 9 = stick 线段（out_coeffs 6 值 [px,py,pz,
 *          dx,dy,dz]，SACMODEL_STICK 有限长线段，布局同 line3D）——仅
 *          RANSAC（fit 1/2 回退，PCL 1.9.1 stick 距离口径缺陷，见 9.7
 *          支持度表），RANSAC 直调 + 内点 PCA 精化 + 本层点到线重选内点。
 *   1.8.0  八叉树/体素空间检索组（§9.9 扩展，3 新导出——复用既有
 *          索引/距离输出通道与 FreeSearchResult）：OctreeVoxelSearch
 *          体素检索（查询点所在体素内全部点，底层
 *          CloudProcessor::treeOctreeVoxelSearch）/ OctreeBoxSearch 盒
 *          检索（轴对齐盒内全部点，底层新增 treeOctreeBoxSearch）/
 *          OctreeChangeDetection 双云变化检测（相对基准云新占体素的点，
 *          底层 treeOctreeChangeDetection）。八叉树 KNN/半径与 KdTree
 *          版结果全同，不重复封装。
 *          FitModel method 10 = ellipsoid 椭球拟合（来源 批次 D 5.4：椭球
 *          拟合（自研）——PCL 1.9 无椭球模型：out_coeffs 12 值 [cx,cy,cz,
 *          a,b,c, rx,ry,rz, 保留×3]，半轴与主轴方向一一对应、不保证排序
 *          （特征分解自然顺序，本实现降序 a>=b>=c），rx,ry,rz = 把 +Z 轴
 *          转到第一半轴方向的 Rodrigues 旋转向量（右手系；仅 RANSAC，fit
 *          1/2 回退，参数被忽略）；最小样本 9 点，剔除非有限点后 <9 → -2；
 *          内点判定为 Sampson 一阶近似口径，见 9.7 支持度表）。
 *
 * 错误码全局约定（§8.3）：
 *   点云计算族（LoadCloud/SaveCloudEx/Filter/Extract/Transform 等）：
 *     0 成功；-1 参数/空指针错误（含空点云输入）；-2 空结果/退化
 *     （含文件打不开/格式错误/无有效数据）；-3 参数越界（枚举/数值非法、
 *     params 数量或取值不符）；-4 内存分配失败；-99 PCL 异常（C 边界兜底）。
 *   SICK 读取族（既有 ReadSickCloud 与新句柄族）沿用各自已占用口径：
 *     ReadSickCloud：-1 参数错；-2 无 .dat；-3 帧号越界；-4 配置/数据错误。
 *     两者在同一 DLL 内并存，**调用方必须按接口查各自的错误码注释**，
 *     不得跨族套用（如 ReadSickCloud 的 -3 是"帧号越界"而非"参数越界"）。
 *
 * 依赖守卫：点云计算族依赖 PCL（经 ShapelyAlg::CloudProcessor，点型固定
 *   pcl::PointXYZ），按 CMake PCL_FOUND 守卫——无 PCL 构建时整组符号
 *   **不编译进 DLL（不导出）**（与 tools/ 配准层同策略；错误码返回 -3 的
 *   降级方案不采用，避免与真实参数越界混淆）。SICK 族与版本/生命周期
 *   接口不依赖 PCL，任何构建下都存在。
 * ===================================================================== */

/* SICK Ranger3 扫描数据读取（一步式 C 接口）。
 * 类接口（推荐）见 include/Point3DAlg_SickReader.h，本接口内部同样走 SickReader。
 *
 * folder：扫描数据文件夹（*.dat + 同名 .xml + Scan3DPart*.json）。
 * frame：帧选择。<0 读取全部 .dat 文件；>=0 只读第 frame 帧（按文件名顺序，从 0 计）。
 * recipe_path / common_path：换算参数 json 路径。传 NULL 或空串 = 默认路径
 *   （recipe：Z:\Calibrate\3D\MtxJuxin60003D.dll.user.json，
 *    common：Z:\Juxin60003D_CommonParams.json）。提取不到必需值报错。
 * out_xyz：输出 SoA 点云缓冲（malloc，用 Point3DAlg_FreeBuffer 释放）。
 * out_count：输出点数。
 * 返回 0 成功；<0 错误码（-1 参数错；-2 无 .dat；-3 帧号越界；-4 配置/数据错误）。
 */
POINT3DALG_EXPORT int Point3DAlg_ReadSickCloud(const char* folder, int frame,
                                               const char* recipe_path, const char* common_path,
                                               float** out_xyz, int* out_count);

/* 释放 Point3DAlg_ReadSickCloud 的输出缓冲 */
POINT3DALG_EXPORT void Point3DAlg_FreeBuffer(void* p);

/* =====================================================================
 * 版本与生命周期（1.1.0 新增，对齐 Image2DAlg）
 * ===================================================================== */

/* 返回 DLL 版本字符串（静态存储，调用方不得释放）。当前 "1.8.0"。 */
POINT3DALG_EXPORT const char* Point3DAlg_GetVersion(void);

/* 初始化（占位实现：当前模块无全局状态，恒返回 0；为宿主统一生命周期而设）。
 * 返回 0 成功（恒成功）。 */
POINT3DALG_EXPORT int Point3DAlg_Init(void);

/* 清理（占位实现：无全局状态，空操作）。 */
POINT3DALG_EXPORT void Point3DAlg_Cleanup(void);

/* =====================================================================
 * 通用点云容器（SoA 契约，与 ReadSickCloud 输出一致）
 * ===================================================================== */

/* 通用点云容器：xyz 为 3N 个 float，x/y/z 交错（点 i = xyz[i*3..i*3+2]）。
 * 由本库输出的 xyz 一律 malloc，用 Point3DAlg_FreeCloud 释放。
 * 调用方作为输出参数传入时无需预置字段（实现统一先清零再填充，
 * 不接管调用方预置的指针——重复传入旧指针属调用方泄漏）。 */
typedef struct Point3DAlg_PointCloud {
    float* xyz;    /* 3N，x/y/z 交错 */
    int    count;  /* 点数 N */
} Point3DAlg_PointCloud;

/* 释放 Point3DAlg_PointCloud（NULL 安全）：free xyz 后把两个字段清零。
 * 只用于本库分配的缓冲；调用方自备缓冲不得用它释放。 */
POINT3DALG_EXPORT void Point3DAlg_FreeCloud(Point3DAlg_PointCloud* cloud);

/* =====================================================================
 * 点云文件读写（1.1.0 新增；封装 ShapelyAlg::CloudIO，点型 pcl::PointXYZ）
 * ===================================================================== */

/* 加载点云文件。
 * path：文件路径（ANSI，与既有 SaveCloud 一致）。
 * type：0=PCD（PCL 原生）1=PLY 2=TXT（每行 "x<分隔符>y<分隔符>z"）。
 * delimiter：仅 TXT 生效——字段分隔符（如 " " / "," / "\t"）；NULL 或空串 =
 *   按连续空白切分（CloudIO 语义）；'#' 开头行跳过；非数字/NaN/Inf 行整行跳过；
 *   PCD/PLY 时忽略。
 * out：输出容器（先清零再填充），xyz 由本库 malloc，用 FreeCloud 释放。
 * 错误码（本接口口径）：
 *   0 成功；
 *   -1 path/out 为 NULL、path 空串；
 *   -2 文件打不开（不存在/无权限）、格式错误、无有效数据行（CloudIO 统一
 *      返回 nullptr，本层不再细分）；
 *   -3 type 不是 0/1/2；
 *   -4 内存分配失败；
 *   -99 PCL 异常。 */
POINT3DALG_EXPORT int Point3DAlg_LoadCloud(const char* path, int type,
                                           const char* delimiter,
                                           Point3DAlg_PointCloud* out);

/* 保存点云文件（既有 Point3DAlg_SaveCloud 只写 PLY，本接口为泛化版）。
 * path：文件路径（ANSI）。
 * type：0=PCD 1=PLY 2=TXT。
 * cloud：输入点云（只读；xyz 为 NULL 或 count<=0 视为空点云）。
 * binary：1=二进制 0=ASCII。**仅 PLY 生效**（PCD 恒二进制——CloudIO::ioSavePCD
 *   用 savePCDFileBinary；TXT 恒文本，分隔符固定为单个空格）。其他值按 0
 *   （ASCII）处理。
 * 错误码（本接口口径）：
 *   0 成功；
 *   -1 path/cloud 为 NULL、path 空串、空点云（xyz NULL 或 count<=0）；
 *   -2 写文件失败（路径不存在/无权限/磁盘满）或格式器拒绝；
 *   -3 type 不是 0/1/2；
 *   -99 PCL 异常。 */
POINT3DALG_EXPORT int Point3DAlg_SaveCloudEx(const char* path, int type,
                                             const Point3DAlg_PointCloud* cloud,
                                             int binary /*0=ASCII 1=二进制（仅 PLY 生效）*/);

/* =====================================================================
 * 点云滤波与降采样（1.1.0 新增；封装 CloudProcessor::filter*，点型 PointXYZ；
 * method 11/12/13 为 1.7.0 新增，PCL 组合管线直调不经 CloudProcessor——
 * ConditionalRemoval/MedianFilter/GridMinimum 无底层包装，见逐 method 注）
 * method 与 params 布局表（params 为 double 数组，param_count = 有效个数；
 * param_count 少于"必填"数或超出"必填+可选"数 → -3）：
 *   method  语义                params 布局
 *   0  voxel 体素质心降采样    [0]=leaf_size 体素边长（三轴同值，>0）
 *                              [1]=min_points_per_voxel 可选（>=0，默认
 *                              0=不过滤；<0 → -3）
 *                              必填 1 个，最多 2 个
 *   1  random 随机采样         [0]=sample_size 目标点数（>0；>=点数时返回
 *                              副本）
 *                              [1]=seed 可选（随机种子，>=0，默认 0）
 *                              必填 1 个，最多 2 个
 *   2  uniform 等间隔采样      [0]=every_k 每 k 点保留 1 个（>0）  必填 1 个
 *   3  farthest 最远点采样     [0]=num_samples 目标点数（>0）
 *                              [1]=start_index 可选（首采样点索引，>=0，
 *                              默认 0）
 *                              必填 1 个，最多 2 个
 *   4  removeNonFinite         无参数（剔除 NaN/Inf 点）          必填 0 个
 *   5  removeDuplicates        [0]=tolerance 可选（去重容差，默认 0=坐标完全相等）
 *                              必填 0 个，最多 1 个
 *   6  passThrough 直通滤波    [0]=field 字段号（0=x 1=y 2=z）
 *                              [1]=limit_min 下限  [2]=limit_max 上限
 *                              （保留 [min,max] 闭区间；min>max → -3）
 *                              必填 3 个
 *   7  statisticalOutlier      [0]=mean_k 近邻个数（>=1）
 *                              [1]=std_dev_mul 标准差倍数（>=0）
 *                              必填 2 个
 *   8  radiusOutlier           [0]=radius 搜索半径（>0）
 *                              [1]=min_neighbors 最少邻居数（>=0）
 *                              必填 2 个
 *   9  conditionalXYZ 条件滤波 [0..5]=x_min,x_max,y_min,y_max,z_min,z_max
 *                              （开区间 (min,max)，GT/LT 严格比较；任一
 *                              min>max → -3）                    必填 6 个
 *   10 approxVoxel  近似体素降采样（1.3.0 新增；PCL ApproximateVoxelGrid，
 *                              大点云快速，格点质心与 method 0 可有微小偏差）
 *                              [0]=leaf 体素边长（>0）            必填 1 个
 *                              （param_count 仅做下限校验：<1 → -3；>1 不
 *                              拦截、多余忽略——实现较"超出即 -3"总则宽松，
 *                              漂移登记见交付报告）
 *   11 conditionalRemoval  [0]=op 复合方式（0=AND 三轴全满足才保留，
 *      复合条件去除        1=OR 任一轴满足即保留；其余 → -3）
 *      （1.7.0 新增）      [1..6]=x_min,x_max,y_min,y_max,z_min,z_max
 *                              逐轴闭区间保留条件。±INFINITY = 该侧无界：
 *                              min 侧仅接受有限值或 -INFINITY、max 侧仅
 *                              接受有限值或 +INFINITY（NaN/方向错误的
 *                              INFINITY → -3）；每轴有限值时 min>max → -3。
 *                              非有限点（NaN/Inf）恒剔除。闭区间实现：PCL 比较
 *                              子（GT/LT）为严格大于/小于，以 ε=1e-6 补偿
 *                              （min 侧 GT(min−ε)、max 侧 LT(max+ε)——
 *                              恰好等于 min/max 的点保留，min/max 界外
 *                              ε 内的边缘带也保留）
 *                              必填 7 个
 *   12 medianFilter 中值滤波 [0]=window 窗口（>=3，奇数；偶数自动 +1，
 *      （1.7.0 新增）      <3 → -3）                            必填 1 个
 *                              PCL MedianFilter——1.9 硬性要求有序点云
 *                              （无序输入报错空出），本层按"1 列 × N 行"
 *                              伪有序化呈递：窗口沿输入点序滑动、仅平滑
 *                              z（x/y 保持不变）；非有限中心点原样保留，
 *                              非有限邻点不参与取中值。椒盐噪声/平滑
 *   13 gridMinimum 格网最低点 [0]=cell 格网边长（>0，<=0 → -3）
 *      （1.7.0 新增）      必填 1 个
 *                              PCL GridMinimum——XY 平面分格，每格保留
 *                              z 最低点（地面提取/粗下采样）；非有限点由
 *                              PCL 按 is_dense=false 自行剔除
 *
 * 与既有 ComuiVoxel（ComUI 适配层内联体素）的关系：本接口 method=0 是 PCL
 * VoxelGrid 版（CloudProcessor::filterVoxelGrid，体素内质心），ComuiVoxel 是
 * 等价简化实现（哈希格网质心）——**语义一致、数值口径有差异**（一致性直查
 * 实测：逐格质心差 ≤~1.7e-5，PCL 为 float32(Vector4f) 顺序累加、ComuiVoxel
 * 为 double 累加；非 dyadic leaf 且坐标恰为 leaf 整数倍时分格可能不同，建议
 * 优先 2 的幂 leaf）。节点与本接口可择一使用，避免语义漂移。
 *
 * 错误码（本接口口径）：
 *   0 成功；
 *   -1 in/out 为 NULL、in->xyz 为 NULL、in->count<=0；
 *   -2 method 越界之外的空结果（滤波后 0 点，如全部被剔除）；
 *   -3 method 不是 0~13、param_count 与布局表不符、参数值非法（见上表逐项）；
 *   -4 内存分配失败；
 *   -99 PCL 异常。 */
POINT3DALG_EXPORT int Point3DAlg_FilterCloud(const Point3DAlg_PointCloud* in, int method,
                                             const double* params, int param_count,
                                             Point3DAlg_PointCloud* out);

/* =====================================================================
 * 点云提取与裁剪（1.1.0 新增；封装 CloudProcessor::extract*，点型 PointXYZ）
 * method 与 indices/params 布局表（数量不符 → -3）：
 *   method  语义                indices / params 布局
 *   0  byIndices 按索引提取     indices/indices_count 必填（NULL 或 <=0 → -1）；
 *                              无 params。索引越界（<0 或 >=count）→ -3
 *   1  byIndicesInverted        indices 可选（NULL/0 = 保留全部点的副本）；
 *      反向提取（剔除给定索引）无 params。索引越界 → -3
 *   2  cropBox AABB 盒裁剪      params[0..5]=min_x,min_y,min_z,max_x,max_y,max_z
 *                              （保留 [min,max] 闭区间；任一 min>max → -3）
 *                              必填 6 个，无 indices
 *   3  cropOBB 有向盒裁剪       params[0..2]=center xyz（盒中心）
 *                              params[3..5]=extents xyz（三主轴半长，>0）
 *                              params[6..14]=rotation 3x3 行主序（第 d 行 =
 *                              第 d 主轴方向，与 CloudProcessor OBB3d 一致）
 *                              params[15]=invert 可选（非 0=保留盒外点，
 *                              默认 0）
 *                              必填 15 个，最多 16 个
 *   4  cropPolygonROI 多边形裁剪 params=顶点 XY 对 [x0,y0,x1,y1,...]（XY 投影
 *                              + 射线法；顶点数 >=3，param_count=2×顶点数）；
 *                              无 indices
 *
 * 错误码（本接口口径）：
 *   0 成功；
 *   -1 in/out 为 NULL、in->xyz 为 NULL、in->count<=0、byIndices 的 indices
 *      缺失（NULL 或 indices_count<=0）；
 *   -2 空结果（裁剪/提取后 0 点，如盒外全空、ROI 内无点）；
 *   -3 method 不是 0~4、索引越界、params 数量或取值不符（见上表逐项）；
 *   -4 内存分配失败；
 *   -99 PCL 异常。 */
POINT3DALG_EXPORT int Point3DAlg_ExtractCloud(const Point3DAlg_PointCloud* in, int method,
                                              const int* indices, int indices_count,
                                              const double* params, int param_count,
                                              Point3DAlg_PointCloud* out);

/* =====================================================================
 * 点云空间变换（1.1.0 新增；封装 CloudProcessor::trans*，点型 PointXYZ）
 * method 与 params 布局表（数量不符 → -3）：
 *   method  语义                params 布局
 *   0  applyMatrix 应用 4x4    params[0..15]=4x4 行主序（与 RegistApplyTransform /
 *                              CloudProcessor::transApplyMatrix 同约定：
 *                              [r00 r01 r02 tx | r10 r11 r12 ty | r20 r21 r22 tz | 0 0 0 1]）
 *                              必填 16 个
 *   1  translate 平移          params[0..2]=dx,dy,dz                    必填 3 个
 *   2  rotate 绕轴旋转         params[0..2]=axis xyz（过原点，不需单位化，
 *                              内部归一；零向量 → -3）
 *                              params[3]=angle 旋转角（弧度，右手系）   必填 4 个
 *   3  rotateAround 绕点轴旋转 params[0..2]=pivot xyz（轴过此点）
 *                              params[3..5]=axis xyz；params[6]=angle（弧度）
 *                              必填 7 个
 *   4  scale 三轴缩放          params[0..2]=sx,sy,sz                    必填 3 个
 *   5  scaleAround 绕点缩放    params[0..2]=scale xyz；params[3..5]=center xyz
 *                              （p' = c + s·(p - c)）                   必填 6 个
 *   6  project 投影到平面      params[0..3]=a,b,c,d（平面 ax+by+cz+d=0；
 *                              法向零向量 → -3）                        必填 4 个
 *
 * 错误码（本接口口径）：
 *   0 成功；
 *   -1 in/out 为 NULL、in->xyz 为 NULL、in->count<=0；
 *   -2 空结果（点数不会因变换改变，当前实现不会返回；预留口径）；
 *   -3 method 不是 0~6、param_count 与布局表不符、旋转轴/平面法向零向量
 *      或含非有限分量（NaN/Inf 轴与零向量同样产出垃圾点云，本层按参数
 *      非法拦截）、rotate 的 angle 非有限；
 *   -4 内存分配失败；
 *   -99 PCL 异常。 */
POINT3DALG_EXPORT int Point3DAlg_TransformCloud(const Point3DAlg_PointCloud* in, int method,
                                                const double* params, int param_count,
                                                Point3DAlg_PointCloud* out);

/* =====================================================================
 * P1 批次（1.2.0 新增）：拟合 / 分割 / 空间索引 / 通用度量
 * ---------------------------------------------------------------------
 * 全部封装 ShapelyAlg::CloudProcessor（点型固定 pcl::PointXYZ），本层只做
 * 容器转换与参数/错误码翻译（例外：FitModel method 7/8/9 为 1.7.0 新增，
 * 本层手写确定性搜索不经 CloudProcessor——1.7.0 缺陷修复：原 PCL SAC
 * 直调路径在共线采样/纯线段云上产出退化/垃圾模型，整体重写，见 9.7 支持
 * 度表；FitModel method 10 = ellipsoid 为 1.8.0 新增，同为不经
 * CloudProcessor 的本层实现——全自研手写 RANSAC）。错误码
 * 为点云计算族口径（见文件头全局约定），
 * 各接口注释写明本接口细化语义；依赖守卫同 1.1.0（无 PCL 不导出）。
 * 输出数组内存约定：一级指针输出缓冲（out_coeffs/out_values）由调用方
 * 预分配（capacity 不足 → -3 并经 out_*_count 回填所需长度，两段式调用）；
 * 二级指针输出（out_inliers/out_segments/out_indices/out_dists 等）由本库
 * malloc，配套 Free* 释放（全部 NULL 安全）。
 * ===================================================================== */

/* ---------------------------------------------------------------------
 * 9.7 通用几何拟合（fit 组）
 * method：0=plane 1=sphere 2=cylinder 3=circle3D 4=cone 5=line3D 6=torus
 *         7=parallelPlane 8=perpendicularPlane 9=stick（7~9 为 1.7.0 新增）
 *         10=ellipsoid（1.8.0 新增）
 * fit   ：0=RANSAC 1=MSAC 2=LMS(LMEDS)
 *         （映射 shapely::FitMethod RANSAC/LMEDS/MSAC——注意与底层枚举
 *          顺序不同，本接口以 0=RANSAC/1=MSAC/2=LMS 为准）
 *
 * out_coeffs 布局（权威口径，对齐 CloudProcessor fitPlane/fitSphere/
 * fitCylinder/fitCircle3D/fitCone/fitTorus 的输出注释；§13 输出字典欠账在此结清）：
 *   method  模型       out_coeffs 布局（double，长度 = *out_coeff_count）
 *   0  plane    4  [a,b,c,d]  平面 ax+by+cz+d=0，|n|=1
 *   1  sphere   4  [cx,cy,cz,r]  球心 + 半径
 *   2  cylinder 7  [px,py,pz, dx,dy,dz, r]  轴上一点 + 单位轴方向
 *              （PCL SampleConsensusModelCylinder 系数顺序）+ 半径
 *   3  circle3D 7  [cx,cy,cz, nx,ny,nz, r]  圆心(3D) + 圆平面单位法向
 *              + 半径（底层另有圆周残差 RMS，本接口未暴露——可用
 *              out_inliers 自行复算）
 *   4  cone     7  [ax,ay,az, dx,dy,dz, angle]  锥顶点 + 单位轴方向
 *              + 半开角（弧度，0~π/2）
 *   5  line3D  6  [px,py,pz, dx,dy,dz]  线上一点 + 单位方向（1.3.0 新增，
 *              PCL SACMODEL_LINE；三种 fit 均支持）
 *   6  torus    8  [cx,cy,cz, dx,dy,dz, R, r]  环面中心 + 单位轴方向
 *              （符号不定：â 与 −â 同一环面）+ 主半径 R（管心圆）+
 *              管半径 r（1.4.0 新增，底层 CloudProcessor::fitTorus 全自研——
 *              PCL 1.9 无 SACMODEL_TORUS；残差为点到环面面的精确距离，
 *              不需要法线；三种 fit 均支持）
 *   7  parallelPlane 4  [a,b,c,d]  平面 ax+by+cz+d=0（1.7.0 新增；
 *              PCL SACMODEL_PARALLEL_PLANE——轴固定 +Z（1.7.0 简化契约，
 *              任意轴待后续扩展）：找法向 ⊥ Z 的竖直平面（墙面/立面），
 *              eps 固定 5°）
 *   8  perpendicularPlane 4  [a,b,c,d]（1.7.0 新增；PCL
 *              SACMODEL_PERPENDICULAR_PLANE——轴固定 +Z、eps 5°：找法向
 *              ∥ Z 的水平面（台面/端面））
 *   9  stick    6  [px,py,pz, dx,dy,dz]  线上一点 + 单位方向（1.7.0 新增，
 *              PCL SACMODEL_STICK 有限长线段；布局同 line3D——PCL 系数
 *              第 7 值 line_width 本接口不输出；内点 <3 → -2 退化）
 *   10 ellipsoid 12 [cx,cy,cz, a,b,c, rx,ry,rz, 保留×3]（1.8.0 新增，全
 *              自研——PCL 1.9 无椭球模型）：[0..2]=椭球中心；[3..5]=三条
 *              半轴 a/b/c——不保证排序（特征分解自然顺序，本实现为降序
 *              a>=b>=c），与 [6..8] 的主轴方向一一对应；[6..8]=rx,ry,rz
 *              Rodrigues 旋转向量（右手系，把 +Z 轴转到第一半轴 a 的
 *              方向；半轴方向为无向直线、特征向量符号任意 → 同一椭球的
 *              rvec 不唯一）；[9..11]=保留位恒 0（缓冲按 12 值分配）
 * 模型支持度（PCL 1.9 限制，底层自动回退，不报错）：
 *   plane/line3D/torus 三种 fit 均支持（torus 的 fit 0/1 = 采样稳健层：
 *   RANSAC 内点计数最大 / MSAC 截断平方残差最小，fit 2 = 确定性多候选 LM
 *   残差中值择优——高外点比例建议 0/1）；sphere 支持 RANSAC/LMEDS
 *   （MSAC 回退 RANSAC）；cylinder/cone 仅 RANSAC（fit 参数被忽略）；
 *   circle3D 的 fit/threshold 复用其内部平面拟合口径。
 *   1.7.0 新增支持度表（method 7/8/9，本层手写确定性搜索——1.7.0 缺陷修复
 *   后原 PCL SAC 直调路径整体弃用，实现见 Point3DAlg_CloudOps.cpp
 *   FitPlaneConstrained / FitStickRansac——轴固定 +Z、eps 固定 5°）：
 *     parallelPlane（7） / perpendicularPlane（8）：三种 fit 均支持——手写
 *     约束搜索对三 fit 统一内点计数评分（精确数据下三者同解，fit 取值不
 *     影响结果）：固定种子随机采样 3 点组 min(2000, C(n,3)) 次 → 法向叉积
 *     归一（|n|<1e-6 即共线/重合直接跳过——零法向退化候选被天然拒绝，不
 *     复现 PCL 退化路径）→ 轴约束校验（|n·axis| <= sin(5°) 为 PARALLEL /
 *     >= cos(5°) 为 PERPENDICULAR，弧度角口径同原 setEpsAngle）→ 最优候选
 *     内点 PCA 精化（质心 + 协方差最小特征向量）；精化后法向越出 5° 容差
 *     （极端倾斜内点集）→ -2。
 *     stick（9）：仅 RANSAC（fit 1/2 回退，参数被忽略）——弃用 PCL 直调的
 *     原因（头文件实测）：PCL 1.9.1 SampleConsensusModelStick 系数口径缺陷
 *     （getDistancesToModel 把 [3..5] 当"自原点方向"、countWithinDistance/
 *     selectWithinDistance 按"两点差向"解析，MSAC/LMEDS 评分基于前者不可
 *     信），且 isSampleGood 要求两采样点三坐标全不等——纯线段点云没有任何
 *     合法样本对，只能产出"线点×远点"垃圾段。本层实现（函数名保留 RANSAC
 *     字样仅为注释引用兼容，已非随机共识）：点对候选搜索（n<=400 穷举
 *     C(n,2)，否则固定种子随机 40000 对）→ 内点（>=3）PCA 精化（质心 +
 *     主特征向量）→ 按精化线以点到线正交距离重选内点；距离口径为点到
 *     直线距离（端点截断不参与）；内点 <3 或方向退化 → -2。
 *   1.8.0 新增支持度表（method 10，全自研，实现见 Point3DAlg_CloudOps.cpp
 *   FitEllipsoidRansac——PCL 1.9 无椭球模型）：
 *     ellipsoid（10）：仅 RANSAC（fit 1/2 回退 RANSAC，参数被忽略）。
 *     两阶段自研：固定种子随机采样 9 点最小样本 min(2000, C(n,9)) 组 →
 *     每组解一般二次方程 10 系数齐次线性系统（Eigen JacobiSVD 零空间；
 *     第二小奇异值比率塌陷 = 9 点共面/共线/重复 → 解不唯一 → 跳过）→
 *     二次矩阵特征分解非正定（圆柱/抛物面/双曲面/平面等）→ 跳过 →
 *     Sampson 一阶近似内点计数取最优 → 内点代数最小二乘精化两轮（每轮
 *     退化则沿用上一轮）。点到椭球面的精确距离需解四次方程，**内点判定
 *     为 Sampson 一阶近似口径**（delta ≈ |F|/||∇F||，F 为二次型值、
 *     ∇F = 2(Aq+p)，threshold 与坐标同单位、近似几何距离口径）；半轴
 *     任一 <= 0 或非有限、最优候选仍退化 → -2。
 *
 * distance_threshold：局内点判定距离阈值（与坐标同单位，>0）。
 * out_coeffs / coeffs_capacity：调用方预分配 double 缓冲与容量。
 * out_inliers：局内点索引（相对 in->xyz 的行号）——**库 malloc int 数组，
 *   经二级指针交回调用方**，用 Point3DAlg_FreeBuffer 释放；可传 NULL =
 *   不需要内点（out_inlier_count 仍回填）。
 * out_inlier_count：必填（NULL → -1）；成功 = 内点数（out_inliers 数组长
 *   度——系数个数无需另查，由 method 唯一确定：plane/sphere=4、line3D=6、
 *   torus=8、parallel/perpendicular=4、stick=6、ellipsoid=12、其余=7）；
 *   -3 时回填所需系数长度（两段式：先 capacity=0 查长度，再分配重调）。
 * 错误码（本接口口径）：
 *   0 成功；
 *   -1 in/out_inlier_count 为 NULL、in->xyz 为 NULL、in->count<=0；
 *   -2 拟合失败/无局内点/退化（点数不足、投影点共线、圆锥不收敛、
 *      环面剔除非有限点后 <8 点或精化后内点 <8、stick 内点 <3 或方向
 *      退化、椭球剔除非有限点后 <9 点/无正定候选/精化后仍退化等）；
 *   -3 method/fit 不是 0~10、distance_threshold<=0、out_coeffs 为 NULL 或
 *      coeffs_capacity 不足（*out_inlier_count = 所需系数长度，两段式回填）；
 *   -4 内存分配失败（出参全部保持 NULL/0，不承载任何有效数据）；
 *   -99 PCL 异常。 */
POINT3DALG_EXPORT int Point3DAlg_FitModel(const Point3DAlg_PointCloud* in, int method, int fit,
                                          double distance_threshold,
                                          double* out_coeffs, int coeffs_capacity,
                                          int** out_inliers, int* out_inlier_count);

/* ---------------------------------------------------------------------
 * 9.8 分割与聚类（segment 组）
 * method 与 params 布局表（params 为 double 数组，数量不符 → -3）：
 *   method  语义                 params 布局
 *   0  planeRANSAC 主平面分割   [0]=distance_threshold（>0）      必填 1 个
 *   1  euclideanCluster 欧式聚类 [0]=cluster_tolerance（>0）
 *                              [1]=min_size（>=1）  [2]=max_size（>=min）
 *                              必填 3 个
 *   2  dbscan 密度聚类          [0]=eps 邻域半径（>0）
 *                              [1]=min_points 核心点最小邻居数（>=1，含自身）
 *                              必填 2 个
 *   3  regionGrowing 区域生长  [0]=k_neighbors（>=3，兼作内部法线估计的
 *                              KNN 数） [1]=smoothness_deg 法线夹角阈值
 *                              （度，>0） [2]=curvature_thresh（>=0）
 *                              [3]=min_size（>=1） [4]=max_size（>=min）
 *                              必填 5 个
 *   4  planesMulti 多平面迭代  [0]=distance_threshold（>0）
 *                              [1]=max_planes（>=1） [2]=min_inliers（>=3）
 *                              必填 3 个
 *   5  conditionalEuclidean   [0]=cluster_tolerance（>0）
 *      条件欧氏聚类（高度差  [1]=min_size（>=1） [2]=max_size（>=min）
 *      约束，1.6.0 新增）    [3]=max_height_diff 相邻连通点最大高度差（>0）
 *                              必填 4 个（邻点对同时满足欧氏距离与高度差
 *                              才连通——台阶/叠层工件按高度分层）
 *   6  groundPlane 地面分割   [0]=max_window_size（>=1，窗口上限）
 *      （PMF 渐进形态学，    [1]=slope 坡度（>0）
 *      1.6.0 新增）          [2]=initial_distance（>0） [3]=max_distance（>0）
 *                              [4]=cell_size（>0）
 *                              必填 5 个
 *   7  minCut 图割前景分割   [0]=sigma（>0） [1]=source_weight（>0）
 *      （1.6.0 新增）        [2]=radius 图构建邻域半径（>0，PCL setRadius）
 *                              [3..5]=前景种子点 xyz
 *                              必填 6 个（另可追加更多种子：6+3K，K>=0）
 *
 * out_segments 语义（库 malloc 数组 + 每段 xyz，全部用 FreeClouds 释放）：
 *   0 → 恒 2 段：[0]=主平面局内点（恒非空），[1]=其余点——**允许为空段
 *       （count=0）**，如全部点都落在平面上；按下标配段无需判段数分支
 *       （平面方程可用 FitModel 另取）
 *   1 → 每簇一段（点数降序）
 *   2 → 每簇一段（点数降序编号；噪声点丢弃；输入含 NaN/Inf 点时按剔除后
 *       的有效点参与聚类——与其他 method 的入口剔除行为一致，段索引映射
 *       回原始坐标）
 *   3 → 每区域一段（点数降序；法线由本接口内部按 k_neighbors KNN 估计）
 *   4 → 每平面局内点一段（按检出顺序 = 点数降序趋势）
 *   5 → 每簇一段（点数降序；高度差条件连通，见上表）
 *   6 → 恒 2 段：[0]=地面点（PMF 局内），[1]=非地面点——[1] 允许为空段
 *       （全部点被判为地面时）；地面段为空（全部点被判非地面）→ -2，
 *       不输出空地面段（实现口径，与 method 0 的 [1] 空段契约不同）
 *   7 → 前景段 1 段（minCut 图割 object 簇——PCL 口径 clusters[1]；多种子
 *       仍归同一前景簇；背景不输出）
 * out_segments/out_segment_count 必填（NULL → -1）；无有效段 → -2（
 *   out_segments 置 NULL、*out_segment_count=0）。
 * 错误码（本接口口径）：
 *   0 成功；
 *   -1 in/out_segments/out_segment_count 为 NULL、in->xyz 为 NULL、
 *      in->count<=0；
 *   -2 无有效结果（分割 0 段，如 DBSCAN 全噪声、平面拟合失败、minCut
 *      图割 0 前景点）；
 *   -3 method 不是 0~7、param_count 与布局表不符、参数值非法（见上表）；
 *   -4 内存分配失败；
 *   -99 PCL 异常。 */
POINT3DALG_EXPORT int Point3DAlg_SegmentCloud(const Point3DAlg_PointCloud* in, int method,
                                              const double* params, int param_count,
                                              Point3DAlg_PointCloud** out_segments,
                                              int* out_segment_count);

/* 释放 SegmentCloud 的输出（NULL 安全）：逐段 free xyz 后 free 数组本身。
 * segments 必须是 SegmentCloud 出参（本库 malloc 的数组）；count = 调用方
 * 从 SegmentCloud 拿到的段数（数组长度随 count 传入，与 free(ptr, size)
 * 同型契约）；segments 为 NULL 时忽略（count 可为任意值）。 */
POINT3DALG_EXPORT void Point3DAlg_FreeClouds(Point3DAlg_PointCloud* segments, int count);

/* ---------------------------------------------------------------------
 * 9.9 空间索引与近邻搜索（tree 组，KdTree + Octree/体素）
 * query：查询点 [x,y,z]（double，本层转 float 进树索引）。
 * out_indices/out_dists：均为库 malloc，用 Point3DAlg_FreeSearchResult 释放
 *   （一次调用同时收两个缓冲，避免二次释放歧义——不复用 FreeBuffer，§9.9
 *   先例）；两者可传 NULL 表示不需要（count 仍回填）。本组全部六接口共用
 *   本通道与 FreeSearchResult。
 * out_dists 布局：与 out_indices 一一对应的**欧氏距离**（KdTree 底层返回
 *   平方距离，本层开方——注意不是 PCL 原始口径），按距离升序；距离参照
 *   点见各接口声明（OctreeChangeDetection 无距离语义，恒填 0）。
 * 错误码（六接口相同口径）：
 *   0 成功（含 0 命中：邻域/体素/盒内无点或变化检测 0 新增 → 0 成功 +
 *     *out_count=0）；
 *   -1 in（OctreeChangeDetection 为 in_base/in_cmp）/out_count 为 NULL、
 *      xyz 为 NULL、count<=0（ChangeDetection 的 in_base 空云例外——
 *      "无历史基准，全部新增"合法，见其声明）；
 *   -2 不使用（无空结果语义——0 命中属正常成功）；
 *   -3 KnnSearch：k<=0；RadiusSearch：radius<=0；OctreeVoxelSearch：
 *      resolution<=0；OctreeBoxSearch：box 逐维 min>=max；
 *      OctreeChangeDetection：min_resolution<=0；
 *   -4 内存分配失败；
 *   -99 PCL 异常。 */
POINT3DALG_EXPORT int Point3DAlg_KnnSearch(const Point3DAlg_PointCloud* in,
                                           const double query[3], int k,
                                           int** out_indices, float** out_dists, int* out_count);
POINT3DALG_EXPORT int Point3DAlg_RadiusSearch(const Point3DAlg_PointCloud* in,
                                              const double query[3], double radius,
                                              int** out_indices, float** out_dists, int* out_count);

/* Octree 体素检索（1.8.0）：查询点 query 所在体素（边长 resolution，与点云
 * 同单位）内的全部点索引。底层 CloudProcessor::treeOctreeVoxelSearch
 * （PCL OctreePointCloudSearch::voxelSearch，体素归属按 floor 划分）。
 * out_dists：各命中点到 query 的欧氏距离（本层计算），升序。
 * resolution<=0 → -3（octree 非法分辨率会内部挂死，入口即拒）。 */
POINT3DALG_EXPORT int Point3DAlg_OctreeVoxelSearch(const Point3DAlg_PointCloud* in,
                                                   const double query[3], double resolution,
                                                   int** out_indices, float** out_dists, int* out_count);

/* Octree 盒检索（1.8.0）：轴对齐盒 [box_min, box_max]（各分量 double[3]，
 * 与点云同单位）内全部点索引，含边界点。底层 CloudProcessor::treeOctree
 * BoxSearch（PCL boxSearch）——KdTree 无对应查询（半径球域 ≠ 轴对齐盒）。
 * out_dists：各命中点到盒中心的欧氏距离（本层计算），升序。
 * box_min/box_max 任一为 NULL → -1；任一维 min >= max（含 NaN）→ -3。 */
POINT3DALG_EXPORT int Point3DAlg_OctreeBoxSearch(const Point3DAlg_PointCloud* in,
                                                 const double box_min[3], const double box_max[3],
                                                 int** out_indices, float** out_dists, int* out_count);

/* Octree 变化检测（1.8.0）：in_cmp 相对 in_base 新占体素（分辨率
 * min_resolution 粒度下的体素差集）的点索引，保持 PCL 变化检测输出序
 * （体素遍历序，不排序）。底层 CloudProcessor::treeOctreeChangeDetection
 * （PCL OctreePointCloudChangeDetector 双缓冲）。
 * out_dists：无距离语义，恒填 0（占位以共用输出通道）。
 * in_base->count==0 合法（无历史基准 → in_cmp 全部点为"新增"）；
 * in_cmp->count<=0 → -1（无可比对象，参数无效）。min_resolution<=0 → -3。 */
POINT3DALG_EXPORT int Point3DAlg_OctreeChangeDetection(const Point3DAlg_PointCloud* in_base,
                                                       const Point3DAlg_PointCloud* in_cmp,
                                                       double min_resolution,
                                                       int** out_indices, float** out_dists,
                                                       int* out_count);

/* 释放本组（KnnSearch/RadiusSearch/Octree 三接口）的输出（NULL 安全）：
 * free indices + free dists。只用于本库分配的缓冲。 */
POINT3DALG_EXPORT void Point3DAlg_FreeSearchResult(int* indices, float* dists);

/* ---------------------------------------------------------------------
 * 9.10 通用度量（measure 组）
 * method 与输入/params/输出布局表（§13 输出字典口径）：
 *   method  语义          输入            params                    out_values 布局
 *   0  AABB               in=点云         无                        6 [minx,miny,minz,
 *                                                                    maxx,maxy,maxz]
 *   1  OBB                in=点云（>=3 点）无                      15 [cx,cy,cz,
 *                                                                    ex,ey,ez(半长),
 *                                                                    R9 行主序(行=主轴)]
 *   2  centroid           in=点云         无                        3 [cx,cy,cz]
 *   3  meanCovariance     in=点云（>=2 点）无                       12 [mx,my,mz,
 *                                                                    c00..c22 行主序]
 *   4  pointCloudDistance in=源点云       无                        N（=in->count）
 *      （逐点最近距离）   other=目标点云（必填，NULL → -1）
 *                          每点到 other 最近点的欧氏距离（非平方）
 *   5  flatness 平面度    in=点云         [0]=distance_threshold    3 [pv 峰峰值,
 *                          （局内点阈值>0） 必填 1 个                  sigma 局内点残差
 *                                                                    标准差(本层复算:
 *                                                                    |残差|<=threshold
 *                                                                    者口径),
 *                                                                    inlier_count]
 *   6  planeAngle 两平面   in/other 忽略   [0..3]=平面1 [a,b,c,d]     1 [angle_deg 锐角
 *      夹角              （可 NULL）     [4..7]=平面2 [a,b,c,d]        [0,90]]
 *                                        必填 8 个（法向零向量 → -3）
 *   7  planeIntersection  同 method 6     同 method 6（法向零向量 →   6 [px,py,pz 交线上
 *      交线              （可 NULL）     -3；近平行 → -2）             一点(最近原点),
 *                                                                    dx,dy,dz 单位方向]
 * capacity 不足 → -3（*out_value_count = 所需长度，两段式）；其余出参
 * out_value_count 必填（NULL → -1）。in/other 输入只读。
 * 错误码（本接口口径）：
 *   0 成功；
 *   -1 in/out_value_count 为 NULL、方法 6/7 之外的 in 缺失（in->xyz 为
 *      NULL、in->count<=0；方法 1 需 >=3 点、3 需 >=2 点）、方法 4 的
 *      other 缺失（NULL/空）；
 *   -2 方法 7 两平面近平行（无有限交线）、方法 5 平面拟合失败/局内点<2
 *      （方法 1/3 点数不足按 -1 报，见 -1 行——原 -2 行"方法 3 点数不足"
 *      与实现不符，实现恒 -1，漂移已登记）；
 *   -3 method 不是 0~7、param_count 与布局表不符（方法 5 需 1 个、6/7 需
 *      8 个）、参数值非法（阈值<=0、法向零向量或含非有限分量等）、
 *      out_values 为 NULL 或 values_capacity 不足（*out_value_count = 所需
 *      长度，两段式回填）；
 *   -4 内存分配失败；
 *   -99 PCL 异常。 */
POINT3DALG_EXPORT int Point3DAlg_MeasureCloud(const Point3DAlg_PointCloud* in,
                                              const Point3DAlg_PointCloud* other,
                                              int method, const double* params, int param_count,
                                              double* out_values, int values_capacity,
                                              int* out_value_count);

/* ---------------------------------------------------------------------
 * 9.10a GD&T 几何公差组（1.5.0 新增；底层 CloudProcessor::measure*
 * 系列，算子自基础库既有实现——本组只做 C 边界封装，无新算法）。
 * Mesh 面积/体积/无符号逐点距离已由 MeshOp op 3/4/5 覆盖，不重复；
 * 点到网格带符号距离（第 5 项）依赖 Mesh 容器入参，声明见 §9.12a
 * （MeshOp 之后——Point3DAlg_Mesh typedef 在 P2 段才定义）。
 *
 * 错误码遵循本文件全局约定（0/-1/-2/-3/-4/-99）。fit 参数映射同
 * FitModel（0=RANSAC/1=MSAC/2=LMS）。数组出参均为库 malloc，
 * 用 Point3DAlg_FreeBuffer 释放。 */

/* 圆度（GD&T 简化口径）：3D 空间圆拟合 → 圆周局内点径向残差峰峰值。
 * 底层 CloudProcessor::measureCircleRoundness（fitCircle3D 内嵌口径）。
 * fit：同 FitModel 映射（circle3D 的 fit/threshold 复用其内部平面拟合
 * 口径，与 FitModel method 3 一致）。
 * out_values 布局（9 值）：[0]=roundness 峰峰值（与坐标同单位）
 *   [1..3]=cx,cy,cz 圆心  [4..6]=nx,ny,nz 单位法向  [7]=r 半径
 *   （拟合参数同 FitModel method 3 circle3D 布局）。
 * 错误码：0 成功；-1 in/out_value_count 为 NULL、in 缺失（xyz NULL/
 *   count<=0）；-2 拟合失败/圆周局内点<2/退化；-3 fit 越界、
 *   distance_threshold<=0、out_values NULL 或容量不足（回填所需长度
 *   9，两段式）；-4/-99 同全局约定。 */
POINT3DALG_EXPORT int Point3DAlg_MeasureCircleRoundness(const Point3DAlg_PointCloud* in,
                                                        int fit, double distance_threshold,
                                                        double* out_values, int values_capacity,
                                                        int* out_value_count);

/* 圆柱度（GD&T 简化口径）：fitCylinder 局内点到轴距离与半径之差峰峰值。
 * 底层 CloudProcessor::measureCylinderForm。注意：PCL 1.9 圆柱模型仅
 * 支持 RANSAC，fit 参数被忽略（与 FitModel method 2 同限制）。
 * out_values 布局（8 值）：[0]=form 峰峰值（与坐标同单位）
 *   [1..3]=px,py,pz 轴上一点  [4..6]=dx,dy,dz 单位轴向  [7]=r 半径
 *   （拟合参数同 FitModel method 2 cylinder 布局，PCL 顺序）。
 * 错误码：0 成功；-1/-2/-3/-4/-99 同上（-2 = 拟合失败/局内点<2；
 * -3 容量不足回填 8，两段式）。 */
POINT3DALG_EXPORT int Point3DAlg_MeasureCylinderForm(const Point3DAlg_PointCloud* in,
                                                     int fit, double distance_threshold,
                                                     double* out_values, int values_capacity,
                                                     int* out_value_count);

/* 位置度（孔位偏差表）：理论孔位列表 vs 实测圆心点云，逐理论位就近贪心
 * 配对（每实测点至多配一位，配对距离 <= match_radius）。
 * 底层 CloudProcessor::measurePositionDeviation。
 * measured：实测圆心点云（逐点取 XY；典型来源 = 各孔 FitModel method 3
 *   circle3D 的圆心；非有限坐标点剔除，不参与配对）。
 * params 布局（1+2T 个）：[0]=match_radius 配对半径（>=0；0 = 仅精确
 *   重合）+ T 组理论孔位 [x0,y0, x1,y1, ...]（T>=1，全须有限）。
 * out_values 布局（4T 个，两段式 capacity 不足回填 4T）：按理论位顺序
 *   每位 4 值 [dx, dy, dist, measured_idx]——dx/dy = 实测−理论（X/Y），
 *   dist = sqrt(dx²+dy²)，measured_idx = 实测点行号（-1 = 该理论位在
 *   match_radius 内无实测：此时 dx/dy/dist 均为 0）。
 * 错误码：0 成功；-1 measured/out_value_count 为 NULL、measured 缺失
 *   （xyz NULL/count<=0）；-2 无有效实测点（全部被剔除）；-3 params
 *   NULL、param_count != 1+2T、match_radius<0 或理论位坐标含非有限值、
 *   out_values NULL 或容量不足（两段式回填 4T）；-4/-99 同全局约定。 */
POINT3DALG_EXPORT int Point3DAlg_MeasurePositionDeviation(
    const Point3DAlg_PointCloud* measured, const double* params, int param_count,
    double* out_values, int values_capacity, int* out_value_count);

/* 点云对给定平面方程的有符号残差数组（residual = n·p + d，逐点；
 * n 不归一时内部除 |n|，残差恒为真实距离口径）。非有限点输出 NaN。
 * 底层 CloudProcessor::planeResiduals。
 * params 布局（4 个）：[a,b,c,d] 平面方程（法向零向量或含非有限分量 → -3）。
 * out_residuals：库 malloc float 数组（长度 = in->count，与输入逐行对齐），
 *   用 Point3DAlg_FreeBuffer 释放。
 * 错误码：0 成功；-1 in/out_residuals/out_residual_count 为 NULL、in
 *   缺失；-2 底层残差计算失败（防御路径，正常输入不触发）；-3 params
 *   不是 4 个或法向非法；-4/-99 同全局约定。 */
POINT3DALG_EXPORT int Point3DAlg_PlaneResiduals(const Point3DAlg_PointCloud* in,
                                                const double* params, int param_count,
                                                float** out_residuals, int* out_residual_count);

/* =====================================================================
 * P2 批次（1.2.0 新增）：特征 / 关键点 / 表面重建 / 3D→2D 桥接
 * ===================================================================== */

/* ---------------------------------------------------------------------
 * 9.11a 法线估计（feature 组）
 * method：0=KNN（param = 近邻个数 k，>=3）
 *         1=radius（param = 搜索半径，>0；邻居上限固定 100，Open3D
 *           max_nn 语义；半径内不足 3 点 → 该点法线 = NaN，保留在输出中）
 * out_normals：库 malloc float 数组，**N×4 布局**——每点连续 4 个 float
 *   [nx, ny, nz, curvature]（单位法向 + 曲率；底层 pcl::Normal 的曲率
 *   字段顺手带出，选 N×4 而非 N×3）。用 Point3DAlg_FreeBuffer 释放。
 *   输入含非有限坐标的点：法线逐点保留输出（对应点通常为 NaN/零向量，
 *   KNN 模式下 NaN 点的邻域结果未定义）——下游重建/配准前建议先
 *   FilterCloud(method=4) 剔除，或交给 ReconstructSurface 入口自动剔除。
 * out_count：必填（NULL → -1）；成功 = 点数 N（与输入一一对应）。
 * 错误码（本接口口径）：
 *   0 成功；
 *   -1 in/out_normals/out_count 为 NULL、in->xyz 为 NULL、in->count<=0；
 *   -2 法线估计失败（底层返回空，理论上不发生——输入非空即有输出）；
 *   -3 method 不是 0/1、param 非法（k<3、radius<=0）；
 *   -4 内存分配失败；
 *   -99 PCL 异常。
 * 注：C API only（容器无法经 ABI v1 端口流转）。 */
POINT3DALG_EXPORT int Point3DAlg_EstimateNormals(const Point3DAlg_PointCloud* in, int method,
                                                 double param, float** out_normals, int* out_count);

/* ---------------------------------------------------------------------
 * 9.11b 特征描述（feature 组）
 * desc_type 与 param/out_dim 布局表：
 *   desc_type  语义             param                        out_dim
 *   0  FPFH 33 维直方图      特征计算半径（>0）             33
 *      （内部先按 KNN k=10 估计法线——SoA 容器无法传入法线，固定口径）
 *   1  逐点协方差            K 近邻个数（>=3）              9（3x3 行主序）
 *   2  马氏距离              忽略（传 0 即可）              1
 *      （要求点数>=4 且全有限，否则 -2）
 *   3  最近邻距离            取第 k 近邻（>=1）             1
 *      （每点到第 k 近邻的欧氏距离，可用均值估计点距）
 * out_features：库 malloc float 数组，count*dim 个（点 i 的描述子 =
 *   out_features[(size_t)i*dim .. +dim-1]）。用 Point3DAlg_FreeBuffer 释放。
 * out_dim/out_count：必填（任一 NULL → -1）。
 * 错误码（本接口口径）：
 *   0 成功；
 *   -1 in/out_features/out_dim/out_count 为 NULL、in->xyz 为 NULL、
 *      in->count<=0；
 *   -2 特征计算失败（FPFH 点数<3 或法线全无效；马氏点数<4 或含非有限点）；
 *   -3 desc_type 不是 0~3、param 非法（FPFH 半径<=0、协方差 k<3、
 *      最近邻距离 k<1）；
 *   -4 内存分配失败；
 *   -99 PCL 异常。
 * 注：C API only（容器无法经 ABI v1 端口流转）。 */
POINT3DALG_EXPORT int Point3DAlg_ComputeFeature(const Point3DAlg_PointCloud* in, int desc_type,
                                                double param, float** out_features,
                                                int* out_dim, int* out_count);

/* ---------------------------------------------------------------------
 * 9.11c 关键点检测（keypoint 组）
 * type：0=Harris3D  1=ISS3D
 * type 语义与参数映射（radius/threshold 双参数按 type 复用）：
 *   0  Harris3D   radius = 邻域搜索半径（>0）；threshold = 响应阈值
 *                 （>=0，越小保留越多；非极大值抑制恒开启）
 *   1  ISS3D      radius = salient_radius 显著性半径（>0，典型取点距
 *                 数倍）；threshold = non_max_radius 非极大值抑制半径
 *                 （>0，典型 < salient_radius）
 * out：关键点云（Point3DAlg_PointCloud，xyz 库 malloc，用 FreeCloud 释放）；
 *   0 个关键点 → -2。输入只读。
 * 数据注记（ISS3D）：显著性条件要求邻域散布矩阵最小特征值严格 > 0——
 *   无噪声的精确解析曲面（如理想圆柱/平面）上邻域严格共面 → e3 恒 0 →
 *   恒 0 关键点（-2）；真实扫描或带噪合成数据不受影响（实现时实测定位）。
 * 错误码（本接口口径）：
 *   0 成功；
 *   -1 in/out 为 NULL、in->xyz 为 NULL、in->count<=0；
 *   -2 无关键点（0 检出）；
 *   -3 type 不是 0/1、radius/threshold 非法（见上表）；
 *   -4 内存分配失败；
 *   -99 PCL 异常。 */
POINT3DALG_EXPORT int Point3DAlg_DetectKeypoints(const Point3DAlg_PointCloud* in, int type,
                                                 double radius, double threshold,
                                                 Point3DAlg_PointCloud* out);

/* ---------------------------------------------------------------------
 * 9.12 表面重建（surface 组）
 * Point3DAlg_Mesh 容器（SoA POD，见下方结构体注释）；faces 为三角面索引
 * （每面连续 3 个 int），底层四边形面已统一扇形三角化（CloudProcessor
 * meshFromPolygonMesh 同口径）。 */
typedef struct Point3DAlg_Mesh {
    float* vertices;      /* 3V，x/y/z 交错 */
    int    vertex_count;  /* 顶点数 V */
    int*   faces;         /* 3F，每面连续 3 个顶点索引 */
    int    face_count;    /* 三角面数 F */
} Point3DAlg_Mesh;

/* type 与 params 布局表（params 为 double 数组，数量不符 → -3）：
 *   type  语义                 params 布局
 *   0  convexHull 凸包         无参数（本层直接调 PCL ConvexHull 取面片——
 *                              CloudProcessor::surfaceConvexHull 只回顶点云）
 *   1  concaveHull 凹包        [0]=alpha（>0，越小越贴合）      必填 1 个
 *                              （同上，本层直接调 PCL ConcaveHull）
 *   2  MLS 平滑重采样          [0]=radius（>0，须大于点距）
 *                              [1]=polynomial_order 可选（>=1，默认 2）
 *                              [2]=voxel_size 可选（>0 开启体素膨胀
 *                              上采样，<=0 仅投影平滑，默认 0）
 *                              必填 1 个，最多 3 个。输出仅顶点
 *                              （face_count=0，MLS 是点云算子非网格算子）
 *   3  Poisson 泊松重建        [0]=depth 可选（八叉树深度，默认 9）
 *                              [1]=iso_divide 可选（>=1，否则 -3；默认 8；
 *                              >depth 时底层自动钳到 depth，不报错）
 *                              [2]=samples_per_node 可选（>0，否则 -3；
 *                              默认 1.0）
 *                              最多 3 个。法线由本接口内部估计并强制
 *                              朝向一致化（见下），depth<2 → -3
 *   4  greedyProjection 贪婪   [0]=search_radius（>0，典型点距 2~5 倍）
 *      投影三角化              [1]=mu 可选（默认 2.5）  [2]=max_neighbors
 *                              可选（默认 100，>=3）
 *                              必填 1 个，最多 3 个
 *   5  marchingCubes Hoppe     [0]=resolution 可选（>=8，默认 64）
 *   6  marchingCubesRBF        [0]=resolution 可选（>=8，默认 64）
 *                              [1]=off_surface_epsilon 可选（离面绝对
 *                              位移，与坐标同单位，>0，默认 0.1）
 *   7  gridProjection 网格投影 [0]=resolution（>0，体素边长）  必填 1 个
 *                              [1]=padding_size 可选（0~3，默认 1；
 *                              每体素邻域需 >=10 点——稀疏云返回 -2）
 *   8  organizedFastMesh       [0]=triangle_pixel_size 可选（>=1，默认 1）
 *                              [1]=adaptive_cut 可选（1=自适应对角线，
 *                              默认 1）。**恒 -2**：SoA 容器表达不了
 *                              有序点云（height 恒 1），保留枚举与报错
 *                              口径供宿主显式感知
 *
 * 法线自动管线（type 3~7）：本接口内部先按 KNN k=10 估计法线，再经
 * orientNormalsConsistentTangentPlane（MST 传播，k=30）统一朝向后传入
 * 底层重建——§9.12 强制约束"Poisson 前必须统一法线朝向"，对同样吃法线的
 * greedy/marchingCubes/gridProjection 一并执行。
 * NaN 输入口径：输入含非有限坐标点/法线邻域退化点时，底层重建入口统一
 * 剔除 NaN/Inf 坐标与 NaN/零长法线的点再重建（有效点占比 <50% → 整体
 * 拒绝，本层报 -2），不会静默产出坏网格——代价是输出顶点数可能少于输入
 * 点数（被剔除点不参与重建）。
 * out_mesh：输出容器（实现统一先清零再填充），vertices/faces 库 malloc，
 *   用 Point3DAlg_FreeMesh 释放。重建失败（valid=false）/空网格 → -2。
 * 错误码（本接口口径）：
 *   0 成功；
 *   -1 in/out_mesh 为 NULL、in->xyz 为 NULL、in->count<=0；
 *   -2 重建失败/空网格（含 type 8 有序点云限制、gridProjection 稀疏拒算）；
 *   -3 type 不是 0~8、param_count 与布局表不符、参数值非法（见上表）；
 *   -4 内存分配失败；
 *   -99 PCL 异常。
 * 注：C API only（容器无法经 ABI v1 端口流转）。 */
POINT3DALG_EXPORT int Point3DAlg_ReconstructSurface(const Point3DAlg_PointCloud* in, int type,
                                                    const double* params, int param_count,
                                                    Point3DAlg_Mesh* out_mesh);

/* 释放 Point3DAlg_Mesh（NULL 安全）：free vertices + free faces 后全部清零。
 * 只用于本库分配的缓冲；调用方自备缓冲不得用它释放。 */
POINT3DALG_EXPORT void Point3DAlg_FreeMesh(Point3DAlg_Mesh* mesh);

/* ---------------------------------------------------------------------
 * 网格处理与度量（MeshOp；输入 in 为只读网格，输出按 op 分流）
 * op 与 params/出参分流表（数量不符 → -3；所需出参为 NULL → -1）：
 *   op  语义                  params 布局                出参
 *   0  laplacianSmooth        [0]=iterations（>=1）      out_mesh（顶点/
 *      网格拉普拉斯平滑       [1]=lambda（(0,1] 步长）      面数不变）
 *                             必填 2 个
 *   1  clusterDecimate        [0]=cell（>0 体素边长）    out_mesh
 *      网格顶点聚类抽稀       必填 1 个
 *   2  cropByOBB 网格裁剪     params[0..14]=center xyz + out_mesh（保留三顶点
 *                             extents xyz(半长>0) +        全在盒内的面，
 *                             rotation 9 行主序（行=主轴， 重编号）
 *                             与 ExtractCloud cropOBB 同约定）
 *                             必填 15 个
 *   3  area 总表面积          无参数                     out_scalar（双精度）
 *   4  volume 有向体积        无参数                     out_scalar（仅闭合
 *                                                          水密网格有物理意义）
 *   5  pointToMeshDistance    params=查询点列表           out_array/out_array_
 *      点到网格最近距离       [x0,y0,z0, x1,y1,z1, ...]    count：N 个距离
 *                             （param_count=3N，N>=1）     （欧氏，FreeBuffer
 *                                                          释放；非有限查询点
 *                                                          → NaN）
 *   6  faceNormals 面法线     无参数                     out_array=3F 个
 *                                                          float（out_array_
 *                                                          count=F 条，每条
 *                                                          3 个单位分量，退化
 *                                                          面=(0,0,0)）
 *   7  vertexNormals 顶点法线 无参数                     out_array=3V 个
 *                                                          float（out_array_
 *                                                          count=V 条；相邻面
 *                                                          面积加权平均）
 * 法线输出处理说明（9.12 实施决策）：Mesh 容器无顶点属性位，法线"写回
 * out_mesh 顶点属性"不可行——op 6/7 改为输出到独立 float 数组（out_array，
 * FreeBuffer 释放），op 5 的逐点距离同走 out_array。
 * 错误码（本接口口径）：
 *   0 成功；
 *   -1 in 或 op 所需出参为 NULL（0/1/2 需 out_mesh，3/4 需 out_scalar，
 *      5/6/7 需 out_array+out_array_count）；
 *   -2 结果为空（0/1/2 输出网格无面片、输入网格无效）；
 *   -3 op 不是 0~7、param_count 与布局表不符（含 op 5 的 params 缺失/
 *      非 3 倍数）、参数值非法（见上表）；
 *   -4 内存分配失败；
 *   -99 PCL 异常。
 * 注：C API only（容器无法经 ABI v1 端口流转）。 */
POINT3DALG_EXPORT int Point3DAlg_MeshOp(const Point3DAlg_Mesh* in, int op,
                                        const double* params, int param_count,
                                        Point3DAlg_Mesh* out_mesh, double* out_scalar,
                                        float** out_array, int* out_array_count);

/* ---------------------------------------------------------------------
 * 9.12a 点到闭合网格带符号距离（GD&T 组第 5 项，1.5.0 新增——声明置于
 * MeshOp 之后因 Point3DAlg_Mesh typedef 在本段定义；组内其余 4 接口见
 * §9.10a）
 * 带符号距离数组（内负外正；距离 = 点到三角面精确最近距离，符号 = 固定
 * 斜向射线穿越三角面次数奇偶——奇为内，与面绕向无关）。仅闭合（水密）
 * 网格符号有物理意义，非水密网格距离仍准、符号无意义。
 * 底层 CloudProcessor::measurePointToMeshSignedDistance（暴力 O(n·f)，
 * 网格大时调用方先用 MeshOp op 1 减面）。非有限点输出 NaN。
 * MeshOp op 5（无符号逐点距离，params 承载查询点）已覆盖"少量查询点"
 * 场景；本接口面向"整片点云 vs 检测网格"的批量口径（SoA 点云入参）。
 * in：查询点云（逐点求解）；mesh：目标网格（只读）。
 * out_distances：库 malloc float 数组（长度 = in->count，与输入逐行对齐），
 *   用 Point3DAlg_FreeBuffer 释放。
 * 错误码：0 成功；-1 in/mesh/out_distances/out_distance_count 为 NULL、
 *   in 缺失或 mesh 无效（vertices/faces NULL、数量<=0）；-2 防御路径（底层
 *   返回空——入口已拦空输入，正常不触发）；-4/-99 同全局约定。
 * 注：C API only（容器无法经 ABI v1 端口流转）。 */
POINT3DALG_EXPORT int Point3DAlg_MeasurePointToMeshSignedDistance(
    const Point3DAlg_PointCloud* in, const Point3DAlg_Mesh* mesh,
    float** out_distances, int* out_distance_count);

/* ---------------------------------------------------------------------
 * 9.13 3D→2D 桥接（heightmap 组）
 * Point3DAlg_HeightMap 容器：镜像 ShapelyAlg HeightMap 结构（CloudIO.h/
 * CloudProcessor.h 权威），拍平成 POD C 容器。数据为 float 高度格，宿主
 * 可直接按 width×height 单通道浮点图（对应 Image2DAlg 的 CV_32F 口径）
 * 使用——空格 = NaN，配合 mask（1=有点）区分"高度恰好为 0"与"无点"；
 * 转 8U 灰度显示由宿主按 cells+mask 自行映射（库不预压缩）。
 * 坐标：col=(x-x0)/cell、row=(y-y0)/cell，row 0 = y 最小（imshow 显示
 * 时 Y 向下，与 Image2DAlg 图像坐标系一致）。 */
typedef struct Point3DAlg_HeightMap {
    int            width;   /* 列数（X 方向栅格数） */
    int            height;  /* 行数（Y 方向栅格数） */
    double         x0;      /* 栅格 (0,0) 的 X（= 有效点 x 最小值） */
    double         y0;      /* 栅格 (0,0) 的 Y（= 有效点 y 最小值） */
    double         cell;    /* 栅格边长（与坐标同单位） */
    float*         cells;   /* width*height 行优先 z 值；空格 = NaN */
    unsigned char* mask;    /* width*height；1=有点 / 0=空 */
    int            valid;   /* 1 = 有效图 */
} Point3DAlg_HeightMap;

/* 释放 Point3DAlg_HeightMap（NULL 安全）：free cells + free mask 后全部
 * 清零。只用于本库分配的缓冲。 */
POINT3DALG_EXPORT void Point3DAlg_FreeHeightMap(Point3DAlg_HeightMap* hm);

/* 点云 XY 栅格化为高度图（3D AOI → 2D ImageProcessor 全部算子的桥）。
 * cell：栅格边长（>0；总栅格数上限 2000 万，超出 → -2）。
 * mode：0=每格取最高 z  1=每格取最低 z  2=每格平均 z  3=点计数密度
 *       （其他值 → -3）。
 * out：输出容器（先清零再填充），cells/mask 库 malloc，用 FreeHeightMap
 *   释放。非有限坐标/高度的点自动剔除。
 * 错误码（本接口口径）：
 *   0 成功；-1 in/out 为 NULL、in->xyz 为 NULL、in->count<=0；
 *   -2 栅格化失败（无有效点/超 2000 万格上限）；
 *   -3 cell<=0、mode 不是 0~3；-4 内存分配失败；-99 PCL 异常。 */
POINT3DALG_EXPORT int Point3DAlg_CloudToHeightMap(const Point3DAlg_PointCloud* in,
                                                  double cell, int mode,
                                                  Point3DAlg_HeightMap* out);

/* 高度图反向点云化：每个有效格（mask=1）生成一个点，坐标取格心
 * x = x0+(col+0.5)*cell、y = y0+(row+0.5)*cell、z = cells 值
 * （mode=3 密度图的 z 即计数值；mask=1 但 cells 为 NaN 的手工构造输入
 * 会产出 z=NaN 的点——本层不自检，建议只用 CloudToHeightMap/ThicknessMap
 * 的输出）。out 用 FreeCloud 释放。
 * 错误码（本接口口径）：
 *   0 成功；
 *   -1 hm/out 为 NULL、结构字段不一致（cells/mask 为 NULL。注：HeightMap
 *      结构本身不携带缓冲长度，调用方须保证 cells/mask 各含
 *      width*height 个元素——长度不足属调用方错误，无法在本层校验）；
 *   -2 hm 无效（valid=0）/宽高<=0/无有效格；
 *   -4 内存分配失败；-99 PCL 异常。
 * 注：C API only（容器无法经 ABI v1 端口流转）。 */
POINT3DALG_EXPORT int Point3DAlg_HeightMapToCloud(const Point3DAlg_HeightMap* hm,
                                                  Point3DAlg_PointCloud* out);

/* 厚度图：同一工件上/下表面两点云沿 Z 配对求差（板厚/锡膏厚度测量）。
 * 栅格范围 = 两云有效点 XY 包围盒并集；上云每格取最高 z、下云每格取最低
 * z；同格双有效 → 厚度 = zTop - zBottom（cells）、mask=1；任一缺失 →
 * NaN/0。**负厚度保留不钳制**（数据异常如上下表面重叠时 cells 为负值，
 * 调用方按业务判定）。cell>0；上限同 CloudToHeightMap。
 * 错误码（本接口口径）：
 *   0 成功；
 *   -1 top/bottom/out 为 NULL、任一云 xyz 为 NULL 或 count<=0；
 *   -2 栅格化失败（无有效点/超上限）；
 *   -3 cell<=0；
 *   -4 内存分配失败；-99 PCL 异常。 */
POINT3DALG_EXPORT int Point3DAlg_ThicknessMap(const Point3DAlg_PointCloud* top,
                                              const Point3DAlg_PointCloud* bottom,
                                              double cell, Point3DAlg_HeightMap* out);

#ifdef __cplusplus
}
#endif

#endif /* POINT3DALG_API_H */
