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
