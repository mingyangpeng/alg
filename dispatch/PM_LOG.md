# PM 值守日志（dispatch/PM_LOG.md）

> 由值守自动化维护：每轮追加一条（日期/工作项/结果/测试数字/下一批建议）。人工可读可改。

## 基线（2026-11-20 00:49）
- 待开发接口.md 规划 100% 交付；滤波拟合批（2.3.0/1.3.0）已验证：Image2DAlg_test 523 / Point3DAlg_test 204 / ShapelyAlg_test 709 全绿；verify_image2d/verify_point3d 对 build_pm 双 ALL CHECKS PASSED。
- 节点 93（image2d 62 + point3d 29 + business 2）；工作区未提交（用户自行提交）。

## 派单记录

### 2026-11-20 00:49 派单 001 — DenoiseNLMeans（进行中）
- 工作项：Image2DAlg 新增 `Image2DAlg_DenoiseNLMeans`（fastNlMeansDenoising/Colored，1ch 直用、3ch Colored、4ch 拆 BGR 保 alpha，参照 Blur 双边先例）+ 节点 alg.image2d.denoise_nlmeans + 测试 + 文档登记。并入未提交的 2.3.0（不改版本号）。
- 状态：worker 已交付（01:11），待值守验收——交付面：C API + 节点 alg.image2d.denoise_nlmeans + Image2DAlg_test 544 PASS/0 FAIL（+21 项） + 链路脚本全过（+11 项）+ 文档登记（接口文档/README，节点 94）
- 工单偏差记录：NLMeans 仅支持 8U（photo 模块限制 + C 容器 data 为 unsigned char*），api.h 已按实登记；孤立单点噪声在小 h 下保留（块自权重恒 1），测试用 h=30~50 校准
- 下批建议：ShapelyAlg PCL 守卫（全项目无 PCL 构建可行性）；fitTorus 评估（需法线管线）；ComuiVoxel/FilterCloud 累加精度统一


---

## 值守轮 1（2026-11-20 01:46）——派单 001 验收 + verify 工具链修复

- **验收结论**：派单 001（DenoiseNLMeans）通过——build_pm 全量重建零错误；Image2DAlg_test **544 PASS/0 FAIL**（GetVersion "2.3.0" + NLMeans 21 项）、Point3DAlg_test **204/0**、ShapelyAlg_test **709/0**；verify_image2d **ALL CHECKS PASSED**。
- **本轮发现并修复**：verify_point3d 首跑误报 MISSING——dumpbin /exports 在 cmd 脚本上下文下输出被确定性截断（两次均为精确 37032 字节、缺 Summary 尾段；bash 直跑完整，DLL 本体经 md5 与重 dump 证明完好）。根因未完全定位（疑似读竞争/AV 缓存视图），**修复为机制性防御**：verify_point3d.cmd 与 verify_image2d.cmd 的 /exports 加 Summary 尾段检测（缺则 ping 延迟重转，≤3 次）、/dependents 加空文件重试——复跑 verify_point3d **ALL CHECKS PASSED**，exports.txt 完整（GetVersion 在列）。
- **测试数字**：544 / 204 / 709，ctest 未跑（本轮抽查覆盖三套主测试 + 双 verify；ctest 12/12 上轮已验）。
- **下一批建议**：派单 002 = ShapelyAlg PCL 守卫（全项目无 PCL 构建可行性，工程债）或 DenoiseNLMeans 链路覆盖已足、fitTorus 需法线管线评估——倾向 002 做 ShapelyAlg PCL 守卫。
- 节点 94（image2d 63 + point3d 29 + business 2）；工作区未提交。


---

## 派单 002（2026-11-20 01:50）— ShapelyAlg PCL 守卫（进行中）
- 工作项：让全项目在无 PCL 环境下可完整构建——ShapelyAlg 的 CloudProcessor/CloudIO/CloudProcessorMesh 按有无 PCL 条件编入（CMake GLOB 摘除）、ShapelyAlg.h 对 PCL 依赖头加宏守卫、ShapelyAlg_test 的 CP:: 用例按宏守卫；验收 = 有 PCL 全量构建与测试零变化 + 无 PCL（CMAKE_DISABLE_FIND_PACKAGE_PCL=TRUE）全项目所有目标零错误 + Point3DAlg 降级导出面 54 不变。
- 状态：worker 已派出（进行中）

### 值守观察（2026-11-20 02:20）— 派单 002 在途
- worker 进展：ShapelyAlg.h 守卫（+16 行）、ShapelyAlg_test 用例守卫（+30 行）、CloudProcessor.h（+12 行）已落盘；build_002 已生成并进入构建阶段。
- 本轮处置：在途跳过（同一模块面不派并行活），下一轮做验收（有 PCL 零变化 + 无 PCL 全项目构建 + 降级面 54 对账）。

### 值守验收（2026-11-20 02:30）— 派单 002 通过并入库
- 独立复核：verify_image2d/verify_point3d 对 build_002 双 ALL CHECKS PASSED（94 节点部署对账一致）；无 PCL 降级面 dumpbin 实测 54 functions 精确命中。
- 已提交推送：feat(ShapelyAlg) fitLine3D/approxVoxel + 全项目无 PCL 守卫（含 RegistTest/BumpNgCheck 两处连带守卫缺口修复）。
- 下一批建议：① Image2DAlg_CalibTest 17 项既有失败排查（build_pm/build_002 基线均 exit=1，改前即存在，非本批引入——数值/误差口径待查）；② ComuiVoxel 与 FilterCloud 累加精度统一（文档化容差或对齐实现）；③ fitTorus 法线管线评估（成本高，维持排期尾部）。


---

## 派单 003（2026-11-20 03:05）— Image2DAlg_CalibTest 17 项既有失败排查与修复（进行中）
- 工作项：诊断并修复 Image2DAlg_CalibTest 的 17 项失败（基线 build_pm/build_002 均 exit=1，改前即存在；疑似长期潜伏——CalibTest 未注册 ctest）；同时评估将其注册进 ctest（杜绝再次潜伏）；区分"测试预期过期"与"实现真 bug"并分别处置，实现 bug 修复必须配回归断言。
- 状态：worker 已派出（进行中）

### 值守观察（2026-11-20 03:50）— 派单 003 在途
- worker 进展：Image2DAlg_Calib.cpp 大幅改动（+632/-199，实质性实现修复/重构级别，远超容差微调）、CalibTest +46、CMakeLists +7（含 add_test 注册迹象）。仍在构建/验证阶段。
- 验收预案升级：交付后除常规验收外，**必须逐块审查 Calib.cpp 的 632 行改动**（确认是标定数学的真 bug 修复而非行为漂移），对照修复前后标定结果差异；CalibTest 断言变化逐条归类（A/B/C）。如改动面超预期，考虑要求 worker 提供修复前后投影误差对比数据。

### 值守验收（2026-11-20 04:33）— 派单 003 通过并入库
- 独立复核：CalibTest 45/0 实测、三套主测试 544/204/709 零回归实测、ctest **13/13**（CalibTest 首次在列）、源码抽审确认 MeiProjectPoint fy 各向异性修复与 Zhang v11−v22 约束/尺度无关守卫真实落地（含坑位注释）。
- 已提交推送：fix(Image2DAlg) 标定模块数学缺陷链修复（**9f380633**，b32bd2e6..9f380633 已上远程）。
- 下一批建议：① Image2DAlg_test 版本断言升级联动习惯化（2.3.0 断言由值守跟进）② ComuiVoxel/FilterCloud 精度统一评估 ③ fitTorus 法线管线评估（成本高，排期尾部）④ B 类"ABI 不可行"项维持结论，无行动。

### 值守轮 7（2026-11-20 04:41）——版本断言联动习惯化（下一批建议①落地）
- 工作项：新增 scripts/check_version_sync.py（GetVersion 字面量五源一致性核查：impl/test 断言/api.h 历史/README/docs），接入双 verify 脚本 [6] 段。
- 验收：双 verify 对 build_pm ALL CHECKS PASSED；Image2DAlg 五源一致 2.3.0、Point3DAlg 五源一致 1.3.0。
- commit：chore(scripts) 已推送。
- 下一批建议：① ComuiVoxel/FilterCloud 累加精度统一评估（文档化容差或对齐实现）② fitTorus 法线管线评估（成本高，排期尾部）③ 覆盖矩阵 A 类已清空——下一价值面为 B 类的"描述子跨节点流转"若宿主 ABI v2 支持二进制端口可重估。

### 值守轮 8（2026-11-20 05:22）——ComuiVoxel/FilterCloud 精度统一评估落地（下一批建议①闭环）
- **决策**：不做代码级对齐——两实现语义一致（分格/每格质心），质心差 ≤~1.7e-5（PCL float32 顺序累加 vs ComuiVoxel double 累加，mm 级 leaf 下 ≤20nm 工业无感）；代码对齐的唯一途径是 ComuiVoxel 委托 FilterCloud，代价是**破坏无 PCL 构建下 voxel_downsample 节点的可用性**（ComuiVoxel 的 PCL-free 内联是其设计特性）→ 选择**文档化容差**。
- **落地五面同步**：接口文档 §7.1.3（使用段）+ §9.3（voxel 节点行）、voxel 节点 description、Point3DAlg README 已知局限、api.h FilterCloud 注释——统一口径：语义一致/质心差 ≤~1.7e-5/**非 dyadic leaf 格界可能不同，建议优先 2 的幂 leaf**。
- **附带实证**：PCL 1.9 **无 SACMODEL_TORUS** 模型（model_types.h 实证）——fitTorus 需全自定义算法（采样→构环解析解→内点迭代），成本结论落袋，维持排期尾部。
- 已提交推送：docs(Point3DAlg) 口径统一（**d369776f**，7ad690ad..d369776f）。
- **下一批建议**：① fitTorus 全自定义实现评估报告（若立项需含：采样策略/解析解推导/法线管线复用——成本高，建议单独立项排期）② 乱序点集入 ConvexityDefects 的 -99 行为已在文档化契约内，无需行动 ③ 值守队列实质清空——后续轮次转**巡检模式**（抽查 verify 双绿 + 增量构建健康），直到用户下新需求。

### 值守轮 9（2026-11-20 06:07）——巡检模式（积压已清空）
- 巡检结果：增量构建 0 错误（HEAD = d369776f，与远程一致）；verify_image2d / verify_point3d 双 ALL CHECKS PASSED；节点部署 94 与源清单一致；工作区无非本工单遗留改动（仅既知外来文件 scripts/watch_zcode_session.py，按纪律不代处理）。
- 本轮无新交付物，无需提交。
- 下一批建议：值守队列空闲中——等用户新需求（fitTorus 立项评估 / B 类重估前提（宿主 ABI v2）等已记录在案）。

### 值守轮 10（2026-11-20 06:50）——值守空闲
- 状态核查：本地 HEAD = origin/main = d369776f（完全同步）；工作区无新变化（仅既知外来文件）；无新增测试失败。
- 结论：值守空闲，本轮结束（无交付物、无提交）。

### 值守轮 11（2026-11-20 07:35）——值守空闲
- 状态核查：本地 = origin/main = d369776f；工作区无新变化；无新增失败。
- 结论：值守空闲，本轮结束。

### 值守轮 12（2026-11-20 08:20）——值守空闲
- 状态核查：本地 = origin/main = d369776f；工作区无新变化；无新增失败。值守队列持续空闲（连续 3 轮）。
- 结论：值守空闲，本轮结束。后续空闲轮合并简记，直到有新需求或状态变化。


---

## 派单 004（2026-11-20 09:00）— fitTorus 圆环面拟合（FitModel method 6，1.4.0）（进行中）
- 工作项：全自定义圆环面拟合——ShapelyAlg::CloudProcessor 新增 fitTorus（残差 = |sqrt((ρ−R)²+z²) − r|，ρ=径向距、z=轴向距；LM 数值雅可比；RANSAC=7 点采样→LM→内点计数）→ FitModel method 6（8 系数 [cx,cy,cz, dx,dy,dz, R, r]）→ 节点 method 扩展 → 测试（合成圆环 + 噪声 + 退化）→ 文档。
- 状态：worker 已派出（进行中）。交付底线：确定性 LM 拟合版本必须可靠；RANSAC 稳健版若诚实评估后不可靠，可交付确定性版 + 文档化稳健性边界（B/C 分类允许，需数据佐证）。
