# alg 算法库

面向 ComUI 宿主的 C++ 算法库集合：几何/图像/点云原子算子 + 通用模块 + 业务层，通过统一 C ABI 与 node.json 节点声明对接宿主（dotnet 反射或 native C++ DLL）。

## 六层架构

依赖方向**单向向下**（上层依赖下层，禁止反向）：

| 层 | 模块 | 职责 | 依赖 |
|----|------|------|------|
| 业务层 | `src/BusinessAlg` | 流程编排、业务语义（如区域占比） | 通用/基础模块 |
| 通用 3D | `src/Point3DAlg` | SICK Ranger3 扫描读取、点云生成与配准 | OpenCV / PCL / JSON、CommonAlg |
| 通用 2D | `src/Image2DAlg` | 2D 图像读取、通用处理与测量算子（含标定接口） | OpenCV、ShapelyAlg、CommonAlg |
| 基础库 | `src/ShapelyAlg` | 几何/图像/点云原子算子（四模块 + KdTree3/MeshIO，接口级文档见其 README） | GEOS / OpenCV / PCL |
| 最底层 | `src/CommonAlg` | JSON / 文件 / 日志等通用能力 | nlohmann_json |
| 集成层 | `integration/comui` | ComUI 宿主接入协议（C ABI 适配 + 8 个 node.json），非算法代码 | 各算法 DLL |

- 算法模块零 UI 痕迹；ComUI 适配代码全部集中在 `integration/comui/`，编译产物仍是算法 DLL。
- 算法模块对外统一 C 接口，不抛异常，返回 int 错误码；宿主接入规则见 docs/THIRD_PARTY_OPS.md。

## 快速开始

```bat
:: 1. 配置（Visual Studio 2022 生成器）
cmake -S . -B build -G "Visual Studio 17 2022"

:: 2. 构建（Release 全量）
cmake --build build --config Release

:: 3. 测试（6 项全过）
ctest --test-dir build -C Release

:: 4. 产物位置
::    build/output/Release/
```

测试共 6 项：CommonAlg_test、ShapelyAlg_test、Image2DAlg_test、Point3DAlg_RegistTest、Point3DAlg_DomeFitTest、BusinessAlg_test。
（另有 ShapelyAlg_demo 演示程序与 Point3DAlg_RegistTool 配准工具，不注册 ctest。）

开发环境（VS Code）：.vscode 已配调试与任务——F5 直接调试 6 个测试 exe（launch.json，另含 Point3DAlg_RegistTool）；Ctrl+Shift+B 默认构建（tasks.json 的 alg-build）；alg-ctest 任务跑全部测试。

## 文档索引

| 文档 | 内容 |
|------|------|
| src/DEVELOPMENT_RULES.md | 目录结构、分层规则、编码/命名等开发规范 |
| src/DEPENDENCIES.md | 第三方依赖版本（OpenCV 4.4 / PCL 1.9 / GEOS / Boost 1.68 等） |
| src/ShapelyAlg/README.md | ShapelyAlg 基础库唯一完整文档（API 详解） |
| src/Image2DAlg/README.md | 通用 2D 模块文档（49 个算子、通道语义、错误码） |
| src/Point3DAlg/tools/README.md | 点云配准/球顶拟合工具（RegistTest/RegistTool/DomeFit）使用与 26 项 check |
| docs/HOST_VERIFY.md | 宿主真机联调清单（6 节点最小联调步骤、ABI v1 已知限制） |
| docs/THIRD_PARTY_OPS.md | 第三方宿主接入规范（dotnet 反射 / native C ABI / node.json 格式） |
| dispatch/BOARD.md | 开发任务板（任务派发与完成记录） |

## 交付物（build/output/Release/）

- **5 个算法 DLL**：CommonAlg.dll、ShapelyAlg.dll、Image2DAlg.dll、Point3DAlg.dll、BusinessAlg.dll
- **40 个 node.json**（image2d 29 + point3d 9 + business 2，随各模块迭代持续增长，全量清单以 scripts/make_delivery_checklist.py 为准）：image2d_read、image2d_gamma、image2d_threshold、image2d_blur、image2d_morphology、image2d_canny、image2d_sobel、image2d_hsv_threshold、image2d_hsv_threshold_wrap、image2d_to_gray、image2d_resize、image2d_crop、image2d_rotate、image2d_enhance、image2d_absdiff、image2d_region_stats、image2d_connected_components、image2d_connected_components_roi、image2d_analyze_contours、image2d_analyze_contours_roi、image2d_fit_circles、image2d_fit_ellipses、image2d_match_template_ex、image2d_match_template_masked、image2d_match_template_subpix、image2d_find_matches、image2d_find_matches_ex、image2d_hough_lines、image2d_hough_circles（图像链路 29 节点）+ point3d 9 个（read_sick、voxel、manual_regist、dome_fit、region_plane、cloud_diff、circle_fit、profile、bump_report）+ business 2 个（region_ratio、bump_ng_check）
- **9 个测试 exe**：CommonAlg_test.exe、ShapelyAlg_test.exe、Image2DAlg_test.exe、Point3DAlg_RegistTest.exe、Point3DAlg_DomeFitTest.exe、Point3DAlg_RegionPlaneTest.exe、Point3DAlg_CloudDiffTest.exe、Point3DAlg_SickSynthTest.exe、BusinessAlg_test.exe（另含 ShapelyAlg_demo.exe、Point3DAlg_RegistTool.exe）
- **第三方运行时**：opencv_world440、OpenNI2、geos、pcl_*_release.dll（全量 20 个，代码直接依赖其中 10 个子模块）等

交付前核对：`python -I scripts/make_delivery_checklist.py` 生成 markdown 交付清单并自动核对必备项（5 个算法 DLL / 40 个 node.json / pcl 20 个，缺失即告警），退出码 0=齐全 / 1=缺失 / 2=目录不存在。
Image2DAlg 一键只读验证：`scripts\verify_image2d.cmd`（测试 exe / dumpbin 导出与依赖 / ctest / 交付清单逐项核对）。scripts/ 下另存临时探针脚本（`_wafer_*.py`、`_analyze_wafer.py`、`_test_comui_dome_fit.py`），不入文档索引。
