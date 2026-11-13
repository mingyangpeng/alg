# Point3DAlg —— 通用 3D 模块

`src/Point3DAlg` 是面向 ComUI 宿主的通用 3D 算法库：**SICK Ranger3 扫描数据读取（拼接/交织）**、**点云配准（手动粗调 + 6 策略精配准）**、**量测四件套**（bump 高度 / 平面度 / 共面性 / 金标准 NG 对比）。算法与 UI 物理隔离，ComUI 适配层与节点声明全在 `integration/comui/`。

## 定位与依赖

```
Point3DAlg.dll
 ├─ CommonAlg.dll   (JSON 封装：SickReader 读 recipe/common/Scan3DPart 配置)
 ├─ ShapelyAlg.dll  (CloudProcessor：配准 ICP 家族；仅 PCL_FOUND 时链接)
 ├─ opencv_world440.dll (仅 SaveDepthView/DrawFovBoxes 可视函数)
 └─ PCL 运行时（ShapelyAlg 的传递依赖）
```

核心读取链路（`Point3DAlg_SickReader.cpp`）为**纯 C++ 实现，无 PCL/OpenCV 依赖**；配准与量测层在 `tools/` 下，依赖 PCL，按 `PCL_FOUND` 守卫编译（缺失时库内不含配准/量测层）。

## 文件结构

| 文件 | 说明 |
|------|------|
| `Point3DAlg_SickReader.h/.cpp` | **SICK 扫描读取核心**：.dat 平面布局 → FOV 结构（阶段1编码器）→ 多线程读图（阶段2）→ 单机拼接 → 多相机交织 → 深度图/点云；含 json 配置读取与 PNG/PLY 保存套件 |
| `Point3DAlg_SickIO.cpp` | 一步式 C 接口（ReadSickCloud：folder → 点云，错误码 0/-1/-2/-3/-4） |
| `Point3DAlg_test.cpp` | SICK 读取端到端验证（依赖 `Z:/ScanImage/3D` 外部数据，**不注册 ctest**，数据缺失返回 77） |
| `tools/Point3DAlg_Regist.h/.cpp` | 配准算法层（纯函数零 UI）：6 策略 ICP-P2P/ICP-P2PL/GICP/FPFH+RANSAC/NDT/PCA + 手动变换操作 + 变换读写 |
| `tools/Point3DAlg_RegistTool.cpp` | 手动配准交互工具（PCLVisualizer + 键盘，独立 exe；键位限制见 tools/README.md） |
| `tools/Point3DAlg_RegistTest.cpp` | 配准算法层单测（26 check，ctest） |
| `tools/Point3DAlg_DomeFit.h/.cpp` | **拱形球顶拟合**：稳健基面 → 聚类 → Kåsa 球拟合 → bump 高度/apex/球心/R + 统计（含共面性两口径） |
| `tools/Point3DAlg_DomeFitTest.cpp` | 球顶拟合测试（合成自检注册 ctest；带 PLY 参数 = 真实数据对照 `scripts/_wafer_dome_baseline.py` 基准断言） |
| `tools/Point3DAlg_RegionPlane.h/.cpp` | **ROI 平面量测**：XY 矩形稳健平面拟合 → 高度/平面度 PV/σ/系数 |
| `tools/Point3DAlg_RegionPlaneTest.cpp` | 平面量测测试（18 check，ctest） |
| `tools/Point3DAlg_CloudDiff.h/.cpp` | **点云高度差对比**：栅格 z 中位 → 逐格差值统计 + 超差判定 + 差值点云；可选 XY 粗对齐（格域 RMS 搜索 + 重叠 70% 门槛） |
| `tools/Point3DAlg_CloudDiffTest.cpp` | 高度差测试（20 check，ctest） |
| `tools/Point3DAlg_SickSynthTest.cpp` | SICK 拼接合成回归（自造 .dat：列映射/交织/偏移对齐/FOV 框/蛇形 2 pass/自然序，40 check，ctest） |
| `tools/Point3DAlg_FitUtil.h` | 量测数值内核（MedianOf/FitPlaneZ，header-only，量测家族共用） |
| `tools/Point3DAlg_CircleFit.h/.cpp` | **圆/孔位量测**：高度切片 → 聚类 → Kåsa 圆拟合（圆心/孔径/RMS），FitCircle2D 支持独立轮廓点 |
| `tools/Point3DAlg_Profile.h/.cpp` | **轮廓截面量测**：线段带内投影 → 弧长分格 z 中位 → 剖面点列 + 台阶落差 max_step |
| `tools/Point3DAlg_BumpReport.h/.cpp` | **bump 全参数报表**：高度+截面直径+共面性偏差+NG 位一体（DomeFit×CircleFit 组合） |
| `SickReader::StitchQuality` | **拼接质量自检**：多相机衔接带格级 z 差统计（σ/超差检出） |
| `tools/README.md` | 配准工具专项文档（键位表/已知键位问题/精度说明） |

ComUI 适配层：`integration/comui/Point3DAlg_comui.cpp`；节点声明：`integration/comui/point3d_*.node.json`（9 个）。

## 接口速查

### C ABI（导出 13 个）

| 函数 | 用途 |
|---|---|
| `Point3DAlg_ReadSickCloud(folder, frame, recipe, common, &xyz, &count)` | 一步式：扫描文件夹 → SoA 点云（mm）。返回 0 / -1 参数 / **-2 无 .dat / -3 帧越界** / -4 配置数据错误 |
| `Point3DAlg_FreeBuffer` | 释放 ReadSickCloud 输出 |
| `Point3DAlg_LoadSickConfig(recipe, common, &cfg, err, len)` | 读配方/通用参数 json → SickConfig（错误码 1~6 + err_msg） |
| `Point3DAlg_SaveCloud / SaveDepth / SaveDepthView / DrawFovBoxes` | PLY 点云 / 16 位深度 PNG / 8 位可视 PNG / FOV 框标注 |

### C++ 类 `Point3DAlg_SickReader`

```cpp
Point3DAlg_SickConfig cfg;                       // LoadSickConfig 填充或手工填
Point3DAlg_SickReader reader(cfg);
reader.LoadRaw({"Z:/ScanImage/3D", "Z:/ScanImage/3D-2"});   // 多相机
const float* depth = reader.GetDepth();          // 拼接+交织深度图（mm，0=无效）
int n = 0;
const float* cloud = reader.GetCloud(&n);        // SoA 点云（mm）
std::vector<int> rects, ids;
reader.FovBoxes(rects, ids);                     // FOV 框（可视标注用）
// Raw()/Encoder() 懒构建（首次调用才拼合相机 0 原始数据）
```

坐标约定见 `Point3DAlg_SickReader.h` 头注释（.dat 平面布局、拼接旋转 90°、交织列序、x 步距 = 实测 scanStep×mmPulse/相机数）。

### 量测层（`tools/`，PCL 类型接口）

```cpp
// 拱形球顶拟合：点云 → 每个 bump 的高度/球顶/球心/R + 共面性统计
Point3DAlg_DomeFit(cloud, params, bumps, &base);
Point3DAlg_DomeStats st = Point3DAlg_DomeStatsOf(bumps);
// st.coplanarity（极差法）/ st.fit_sigma（剥离倾斜的最佳拟合面残差）/ st.fit_p·q（翘曲向）

// ROI 平面量测：XY 矩形 → 高度/平面度 PV/σ（台阶差 = 两 ROI 高度相减）
Point3DAlg_RegionPlaneFit(cloud, x, y, w, h, params, r);

// 点云高度差对比：基准/被测 → 格级 z 差统计 + 超差判定 + 差值点云（z=diff）
Point3DAlg_CloudDiff(cloudA, cloudB, params, result, &diffCloud);

// 圆/孔位量测：高度切片 → 聚类 → 每簇圆心/孔径
Point3DAlg_DetectCircles(cloud, z_low, z_high, params, circles);
Point3DAlg_FitCircle2D(xy, count, ...);   // 已有轮廓点时直接拟合

// 轮廓截面：沿 XY 线段提取 z 剖面（台阶落差 max_step 定位）
Point3DAlg_Profile(cloud, x0, y0, x1, y1, params, result, &profile);

// bump 全参数报表：高度+截面直径+共面性偏差+NG 位（一次性输出）
Point3DAlg_BumpReport(cloud, params, entries, &stats);

// 拼接质量自检：多相机衔接带 z 差（σ 大 = 相机漂移/丢帧）
reader.StitchQuality(0.02, quality);

// 配准（手动粗调 + 精配准，变换 4×4 行主序 float[16]）
Point3DAlg_RegistRun(session, src, dst, params, &step);   // 策略 1~6
```

全部纯函数、确定性（无 RANSAC 随机性，量测可复现）；拟合结果统一有限性校验（NaN 判失败不静默输出）。

## ComUI 节点（9 个）

| 节点 id | 功能 | 输出 |
|---|---|---|
| `alg.point3d.read_sick` | 扫描文件夹 → 点云 | cloud |
| `alg.point3d.voxel_downsample` | 体素下采样 | cloud |
| `alg.point3d.manual_regist` | 两片点云 ICP 精配准（初值 16 值参数） | cloud + 消息 |
| `alg.point3d.dome_fit` | bump 高度量测 | cloud（每 bump 一点 z=高度）+ 消息（统计+共面性） |
| `alg.point3d.region_plane` | ROI 高度/平面度 | 消息（台阶差=两节点高度相减） |
| `alg.point3d.cloud_diff` | 金标准 NG 对比（可选 XY 粗对齐，精度 ≈±cell/2） | cloud（差值点云）+ 消息（超差占比/对齐量） |
| `alg.point3d.circle_fit` | 圆/孔位量测（高度切片） | cloud（每孔一点 z=半径）+ 消息 |
| `alg.point3d.profile` | 轮廓截面（台阶落差/剖面） | cloud（剖面点列）+ 消息（max_step 定位） |
| `alg.point3d.bump_report` | bump 全参数报表（高度/直径/NG） | cloud（每 bump 一点 z=高度）+ 消息（统计/NG） |

## 测试与验证

| 入口 | 内容 | ctest |
|---|---|---|
| `Point3DAlg_RegistTest` | 配准 26 check | ✅ |
| `Point3DAlg_DomeFitTest` | 球顶拟合合成 22 check；带 PLY 参数 = moi_wafer 基准断言 | ✅（无参模式） |
| `Point3DAlg_RegionPlaneTest` | 平面量测 18 check | ✅ |
| `Point3DAlg_CloudDiffTest` | 高度差 20 check | ✅ |
| `Point3DAlg_SickSynthTest` | SICK 拼接合成 40 check | ✅ |
| `Point3DAlg_test` | SICK 真实数据端到端 | ❌（Z: 数据依赖，手动跑） |
| `scripts/_test_comui_*.py` ×3 | ctypes 模拟宿主端到端 | 手动 |
| `scripts/_wafer_dome_baseline.py` | DomeFit Python 基准（数值对照） | 手动 |

## 已知局限与遗留

- **`.xml` 换算兜底预留未实现**：头注释已如实标注，换算完全依赖 recipe/common json
- 16 位深度 PNG 无压缩（zlib 存储块），大图体积大；`zmin` 有效点映射 0 与"无效=0"语义接近（zmin 处会被当无效点，读回时需注意）
- `Point3DAlg_RegistTool` 部分键位受 PCL/VTK 内置行为限制（详见 tools/README.md 已知键位问题）
- `Image2DAlg_Calib` 同模块但属 2D 标定方向，见 Image2DAlg 文档
