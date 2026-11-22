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
| 集成层 | `integration/comui` | ComUI 宿主接入协议（C ABI 适配 + 105 个 node.json），非算法代码 | 各算法 DLL |

- 算法模块零 UI 痕迹；ComUI 适配代码全部集中在 `integration/comui/`，编译产物仍是算法 DLL。
- 算法模块对外统一 C 接口，不抛异常，返回 int 错误码；宿主接入规则见 docs/THIRD_PARTY_OPS.md。

## 快速开始

```bat
:: 1. 配置（Visual Studio 2022 生成器）
cmake -S . -B build -G "Visual Studio 17 2022"

:: 2. 构建（Release 全量）
cmake --build build --config Release

:: 3. 测试（13 项全过）
ctest --test-dir build -C Release

:: 4. 产物位置
::    build/output/Release/
```

测试共 13 项：CommonAlg_test、ShapelyAlg_test、Image2DAlg_test、Image2DAlg_CalibTest、Point3DAlg_RegistTest、Point3DAlg_DomeFitTest、Point3DAlg_RegionPlaneTest、Point3DAlg_CloudDiffTest、Point3DAlg_CircleFitTest、Point3DAlg_ProfileTest、Point3DAlg_BumpReportTest、Point3DAlg_SickSynthTest、BusinessAlg_test。
（另有 ShapelyAlg_demo 演示程序、Point3DAlg_RegistTool 配准工具与 Point3DAlg_test（依赖 Z: 盘扫描数据，缺数据自动 SKIP）不注册 ctest。）

开发环境（VS Code）：.vscode/（launch.json 调试各测试 exe + tasks.json 构建任务）为本地可选配置，已被 .gitignore 忽略不入库；未配置时按上方命令行构建与 ctest 即可。

## 文档索引

| 文档 | 内容 |
|------|------|
| src/DEVELOPMENT_RULES.md | 目录结构、分层规则、编码/命名等开发规范 |
| src/DEPENDENCIES.md | 第三方依赖版本（OpenCV 4.4 / PCL 1.9 / GEOS / Boost 1.68 等） |
| src/ShapelyAlg/README.md | ShapelyAlg 基础库唯一完整文档（API 详解） |
| src/Image2DAlg/README.md | 通用 2D 模块文档（100 个通用算子、通道语义、错误码） |
| src/Point3DAlg/tools/README.md | 点云配准/球顶拟合工具（RegistTest/RegistTool/DomeFit）使用与 26 项 check |
| docs/HOST_VERIFY.md | 宿主真机联调清单（6 节点最小联调步骤、ABI v1 已知限制） |
| docs/THIRD_PARTY_OPS.md | 第三方宿主接入规范（dotnet 反射 / native C ABI / node.json 格式） |
| dispatch/BOARD.md | 开发任务板（任务派发与完成记录） |

## 交付物（build/output/Release/）

- **5 个算法 DLL**：CommonAlg.dll、ShapelyAlg.dll、Image2DAlg.dll、Point3DAlg.dll、BusinessAlg.dll
- **105 个 node.json**（image2d 70 + point3d 33 + business 2，随各模块迭代持续增长；逐接口功能/参数见 docs/接口文档.md，全量清单以 scripts/make_delivery_checklist.py 与 scripts/_coverage_matrix.md 为准）
- **14 个测试/验证 exe**（13 项注册 ctest + Point3DAlg_test）：CommonAlg_test.exe、ShapelyAlg_test.exe、Image2DAlg_test.exe、Image2DAlg_CalibTest.exe、Point3DAlg_RegistTest.exe、Point3DAlg_DomeFitTest.exe、Point3DAlg_RegionPlaneTest.exe、Point3DAlg_CloudDiffTest.exe、Point3DAlg_CircleFitTest.exe、Point3DAlg_ProfileTest.exe、Point3DAlg_BumpReportTest.exe、Point3DAlg_SickSynthTest.exe、BusinessAlg_test.exe + Point3DAlg_test.exe（依赖 Z: 扫描数据，不注册 ctest；另含 ShapelyAlg_demo.exe、Point3DAlg_RegistTool.exe）
- **第三方运行时**：opencv_world440、OpenNI2、geos、pcl_*_release.dll（全量 20 个，代码直接依赖其中 11 个子模块——common/io/filters/features/kdtree/keypoints/octree/registration/sample_consensus/segmentation/surface）等

交付前核对：`python -I scripts/make_delivery_checklist.py` 生成 markdown 交付清单并自动核对必备项（5 个算法 DLL / 全量 node.json / pcl 20 个，缺失即告警），退出码 0=齐全 / 1=缺失 / 2=目录不存在。
一键只读验证：`scripts\verify_image2d.cmd` 与 `scripts\verify_point3d.cmd`（测试 exe / dumpbin 导出与依赖 / ctest / GetVersion 五源一致性 / 交付清单逐项核对）。scripts/ 下另存临时探针与链路脚本（`_wafer_*.py`、`_analyze_wafer.py`、`_test_comui_*.py` 等，`_` 前缀不入文档索引）。
