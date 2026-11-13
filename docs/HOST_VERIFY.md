# 宿主联调验证清单（ABI v1 native 算子）

> 目的：6 个 native 算子在真机宿主（ComUI）上做一轮最小联调，并逐条确认 ABI v1
> 协议中"代码层无法自证、必须真机看效果"的行为。信息全部核对自
> `integration/comui/` 下各 `*_comui.cpp` 与 `*.node.json`
> 与 `docs/THIRD_PARTY_OPS.md §5.5`，不涉及代码改动。
>
> 部署方式见 `docs/THIRD_PARTY_OPS.md` §8：宿主 `comdll/algo/<名称>/` 下放
> DLL + node.json（依赖同目录），F5 重载；本仓库构建产物在
> `build/output/Release/`（5 个算法 DLL + 40 个 node.json 均已部署）。

## 〇、图像链路 29 节点速查（T41/T44/T45/T47/T48/T49/T50/T53 八批，参数以 node.json 为准）

前 2 个（read/gamma）见下文第二节；其余 27 个 image2d 节点（dll=`Image2DAlg.dll`，
method 前缀 `Image2DAlg_Comui`，输入 1 张 BGRA 图、absdiff/match_template_ex/find_matches/
find_matches_ex/match_template_masked 需 2~3 张；输出 BGRA（region_stats 无图像输出，
结果走消息通道）；单通道结果自动转 BGRA 显示；ctypes 端到端 44 项已过）：

| node id | method | params（声明顺序） | 说明 |
|---|---|---|---|
| alg.image2d.threshold | ComuiThreshold | mode(int,0~4) thresh maxval block_size c | 二值化，输出白/黑 BGRA |
| alg.image2d.blur | ComuiBlur | method(0 高斯/1 中值/2 双边) ksize sigma1 sigma2 | 降噪 |
| alg.image2d.morphology | ComuiMorphology | op(0~6) shape(0~2) ksize iterations | 形态学 |
| alg.image2d.canny | ComuiCanny | threshold1 threshold2 | 边缘（0/255→BGRA） |
| alg.image2d.sobel | ComuiSobel | dx dy ksize(1/3/5/7) | 梯度幅值灰度图 |
| alg.image2d.hsv_threshold | ComuiHsvThreshold | h_lo h_hi s_lo s_hi v_lo v_hi | 颜色分割掩膜（红色跨 0 用 hsv_threshold_wrap 节点待增） |
| alg.image2d.to_gray | ComuiToGray | （无） | 灰度化 |
| alg.image2d.resize | ComuiResize | out_width out_height interpolation | 缩放 |
| alg.image2d.crop | ComuiCrop | x y width height | 裁剪（越界钳制） |
| alg.image2d.rotate | ComuiRotate | angle scale interpolation | 绕中心旋转 |
| alg.image2d.enhance | ComuiEnhance | method clip_limit tile_size | 0 均衡 / 1 CLAHE |
| alg.image2d.absdiff | ComuiAbsDiff | （无参数，2 输入） | 批板差分 |
| alg.image2d.connected_components | ComuiConnectedComponents | connectivity(4/8) min_area max_area | 绿框标注图；结果 `count=N;[i]area..bbox..c..` 并入消息 |
| alg.image2d.analyze_contours | ComuiAnalyzeContours | external_only min_area | 绿框+黄最小外接圆标注图；消息含 area/circularity |
| alg.image2d.fit_circles | ComuiFitCircles | min_area max_area min_r max_r subpixel | 拟合圆标注图；消息含圆心/半径/rms |
| alg.image2d.hsv_threshold_wrap | ComuiHsvThresholdWrap | h_lo h_hi s_lo s_hi v_lo v_hi | wrap 版 HSV（h_lo>h_hi 自动环绕，红色跨 0） |
| alg.image2d.match_template_ex | ComuiMatchTemplateEx | method scale_lo/hi/step angle_lo/hi/step（2 输入：搜索图/模板） | Mark 对位：中心十字标注；消息 `score/c/scale/angle`（中心落点） |
| alg.image2d.find_matches | ComuiFindMatches | method score_threshold max_matches nms_radius（2 输入） | 多目标：命中外框标注；消息 `count=N;[i](x,y) score` |
| alg.image2d.hough_lines | ComuiHoughLines | rho theta_deg threshold min_line_length max_line_gap | 输入边缘图；检出线段标注；消息 `count=N;[i](x1,y1)-(x2,y2)` |
| alg.image2d.hough_circles | ComuiHoughCircles | dp min_dist param1 param2 min_r max_r | **内部自动 3x3 预模糊**（param1 默认 50：模糊后边缘梯度偏弱）；检出圆标注；消息 `count=N;[i]c/r` |
| alg.image2d.connected_components_roi | ComuiConnectedComponentsRoi | roi_x roi_y roi_w roi_h(0=全图) connectivity min_area max_area | ROI 版连通域：绿框标注；结果坐标为全图系 |
| alg.image2d.analyze_contours_roi | ComuiAnalyzeContoursRoi | roi_x roi_y roi_w roi_h(0=全图) external_only min_area | ROI 版轮廓测量：绿框+黄圆标注；结果坐标为全图系 |
| alg.image2d.match_template_masked | ComuiMatchTemplateMasked | method(仅 0/3)（3 输入：搜索图/模板/掩膜） | 带掩膜鲁棒匹配：命中外框；消息 `score/at`；掩膜 BGRA 灰度语义自动转单通道 |
| alg.image2d.find_matches_ex | ComuiFindMatchesEx | method score_threshold max_matches nms_radius scale_lo/hi/step angle_lo/hi/step（2 输入） | **Mark 阵列全检**（多目标×旋转×尺度，全局 NMS）：中心十字标注；消息 `count=N;[i]c/s/a/score` |
| alg.image2d.fit_ellipses | ComuiFitEllipses | min_area max_area min_axis max_axis | 多目标椭圆拟合：椭圆标注；消息 `count=N;[i]c/axis/ang` |
| alg.image2d.match_template_subpix | ComuiMatchTemplateSubPix | method（2 输入） | 亚像素对位（±0.5px）：中心十字标注；消息 `score/at` |
| alg.image2d.region_stats | ComuiRegionStats | roi_x roi_y roi_w roi_h(0=全图) | 无图像输出；消息 `mean/stddev/min/max`（批次一致性监控） |

联调要点：CC/AC/FitCircles 三类测量节点消息走 error 字段返回成功结果（manual_regist
先例，宿主当前仅 rc≠0 时读取——见"待真机确认"第 1 条）；错误路径均为 UTF-8 中文。

## 一、待真机确认的 ABI v1 行为

背景：ABI v1 的 `AlgoCOutput` 只有 `images / clouds / error` 三个通道（见
`integration/comui/comui_native_abi.h`）。两点数值结果与状态消息走 `error` 字段
返回（静态存储，宿主只读不释放）。当前宿主侧 NativeAlgo.cs **仅在 rc≠0 时读取
error**，rc=0 时的非空 error（成功消息）被丢弃。下表逐条确认：

| # | 现象 | 涉及节点 | 当前实现返回 | 需真机确认 | 若不行的回归方式 |
|---|------|----------|--------------|------------|------------------|
| 1 | 成功消息不显示：配准的 fitness/T、RegionRatio 的 defect_ratio/area 写在 rc=0 时的 error 字段，宿主当前丢弃，节点上看不到 | `alg.point3d.manual_regist`、`business.region.ratio` | rc=0 + 非空 error（如 `fitness=0.9123 rmse=0.4567 \| T=…`、`defect_ratio=0.8910 area=…`） | 宿主是否把 rc=0 的非空 error 显示为节点消息（或至少进日志面板） | 确认后仍不显示 → 需宿主 NativeAlgo.cs 改为 rc=0 也读 error；DLL 侧零改动（回归无影响，协议字段本就存在） |
| 2 | 非空 error（rc≠0）的显示行为：错误文案是否完整、中文是否乱码 | 全部 6 个节点 | rc≠0 + 非空 error（UTF-8 中文文案，如「未设置图像路径」「输入未连线」） | 节点红点悬停文案是否为该 error 原文、UTF-8 是否正确解码显示 | 若乱码 → 查宿主 error 字段解码方式（应按 UTF-8）；DLL 消息编码已由 T8 用 ctypes 逐字节验证为 UTF-8，无需回归 |
| 3 | fitness / T 随 error 字段返回的最终效果 | `alg.point3d.manual_regist` | rc=0，error=`fitness=<%.4f> rmse=<%.4f> \| T=<16值逗号分隔> \| <策略人话描述>` | 组合 1 确认通过后，读这条消息确认格式可读、T 值与 `Point3DAlg_RegistTool.exe` 导出的 `regist_T_flat.txt` 口径一致 | 若宿主后续提供 text 端口（ABI v2），把 T 改走 text 通道、恢复 node.json outputs 的 text 端口声明；fitness 保留在消息 |
| 4 | defect_ratio / area 随 error 字段返回的最终效果 | `business.region.ratio` | rc=0，error=`defect_ratio=<%.4f> area=<%.2f mm2>`（node.json outputs 为空，无端口可接） | 组合 1 确认通过后，读这条消息确认两个数值量纲正确（area 为 mm²，ratio 为 0~1） | 同上；若 ABI v2 增数值端口，把两值改走 number 通道并补 node.json outputs |

### 备忘：注释口径事项（T9 审计已通过，仅记录，不改代码）

T9（node.json↔C 接口一致性审计）6/6 通过：method↔导出表逐字一致、params 数量/类型↔适配层
提取代码一一对应、outputs 均合规。以下为文档口径备忘，无行为影响：

| 位置 | 事项 | 说明 |
|------|------|------|
| `Point3DAlg_comui.cpp` 手动配准参数提取注释 | ~~注释曾称"按 kind 顺序提取"~~ **已由 T15 修正**：现注释为"按 node.json params 声明顺序提取（宿主 marshal 不带参数名）"，同 kind 参数以出现次序区分——第 1 个 int=strategy、第 2 个 int=max_iteration、首个 text=init_transform、首个 double=max_distance；旧"按 kind 顺序"措辞全仓已清零 | 注释与实现、node.json 声明顺序（strategy, init_transform, max_distance, max_iteration）及宿主 marshal 行为（§5.5 绑定规则）三者一致，事项关闭 |
| `Point3DAlg_api.h` ReadSickCloud 错误码注释 | api.h 注释列 -1/-2/-3/-4 四档，SickIO 实现现只返回 -1（参数空，适配层已提前拦截为 rc=1）与 -4（统一错误：无 .dat / 帧号越界 / 配置或数据错误） | 宿主联调实际只会见到「读取失败（配置或数据错误）：…」；若出现「错误码 N」分支即底层接口有扩展，需回头核对 |

## 二、6 个节点最小联调步骤

公共前置：

1. 复制 `build/output/Release/` 下 5 个算法 DLL + 6 个 node.json 到宿主
   `comdll/algo/`（连同第三方运行时 DLL，宿主只在本目录解析）；
   覆盖 DLL 前先退出宿主（不随 F5 卸载），改 node.json 可 F5 生效。
2. F5 重载，底部日志应出现 6 条 `算子(JSON)已注册: <名称> <<id>> ← <文件>`；
   少一条 = id 重复被静默忽略或 JSON 解析失败（§8 排查表）。
3. 所有节点参数按 §5.5 绑定规则 marshal：参数按 params 声明顺序、kind 区分类型；
   输入按 inputs 端口顺序；输出按 outputs 端口顺序分发。
4. 看结果：末端接内置 `pub.image` / `pub.cloud` 发布节点（或 `draw.2d` / `draw.3d`）。

### 1. alg.image2d.read（图像读取）

| 项 | 值 |
|----|----|
| DLL / method | Image2DAlg.dll / `Image2DAlg_ComuiRead` |
| 输入 | 无（源节点） |
| 输出 | `image`（image） |
| 参数 | `img_path`（path，默认空）：图像路径，支持 png/tiff/jpg/jpeg |
| 联调 | 填一个存在的图片路径 → 运行，节点绿；接 `pub.image` 看 2D 工作台出图 |
| 预期错误 | rc=1「未设置图像路径」；rc=2「读取失败：路径不存在或格式不支持（png/tiff/jpg/jpeg）」；rc=3「内存不足」 |

### 2. alg.image2d.gamma（Gamma 校正）

| 项 | 值 |
|----|----|
| DLL / method | Image2DAlg.dll / `Image2DAlg_ComuiGamma` |
| 输入 | `输入`（image） |
| 输出 | `输出`（image） |
| 参数 | `gamma`（number，默认 1.0，min 0.1 / max 5.0）：Gamma 值，<0.05 时钳到 0.05 |
| 联调 | 接 alg.image2d.read 输出 → gamma 分别设 0.5 / 2.0 → 节点绿，图像亮度明显变化；接 `pub.image` 查看 |
| 预期错误 | rc=1「输入未连线」；rc=2「内存不足」 |

### 3. alg.point3d.read_sick（SICK 扫描读取）

| 项 | 值 |
|----|----|
| DLL / method | Point3DAlg.dll / `Point3DAlg_ComuiReadSick` |
| 输入 | 无（源节点） |
| 输出 | `点云`（cloud，无颜色，查看器按高度伪彩上色） |
| 参数 | `folder`（path，默认空）：扫描数据文件夹（\*.dat + 同名 \*.xml + Scan3DPart\*.json）；`frame`（int，默认 -1，min -1）：指定帧，<0 读全部，>=0 按文件名顺序从 0 计 |
| 联调 | 指定一份真实 SICK 扫描文件夹 → 节点绿；接 `pub.cloud` 看 3D 工作台点云；换算参数走默认路径（`Point3DAlg_api.h` 注释），提取不到必需值报错 |
| 预期错误 | rc=1「未设置扫描数据文件夹」；rc=2「读取失败（配置或数据错误）：<路径>」——适配层注释说明底层现只返回 -4（统一错误：文件夹无 .dat / 帧号越界 / 配置或数据错误；api.h 注释另列 -1/-2/-3，其中 -1 参数空已被 rc=1 提前拦截，-2/-3 现统一归入 -4），出现其他码时显示「读取失败（错误码 N）：<路径>」；rc=3「内存不足」 |

### 4. alg.point3d.voxel_downsample（体素下采样）

| 项 | 值 |
|----|----|
| DLL / method | Point3DAlg.dll / `Point3DAlg_ComuiVoxel` |
| 输入 | `输入`（cloud） |
| 输出 | `输出`（cloud，每格质心 + 颜色均值） |
| 参数 | `leaf_size`（number，默认 0.01，min 0.001 / max 1.0）：体素边长（mm），<1e-6 时钳到 1e-6 |
| 联调 | 接 alg.point3d.read_sick 输出 → leaf_size 由小调大 → 输出点数明显减少（点云消息可对比）；接 `pub.cloud` 查看 |
| 预期错误 | rc=1「输入未连线」；rc=2「输入点云为空」；rc=3「内存不足」 |

### 5. alg.point3d.manual_regist（手动配准 ICP）

| 项 | 值 |
|----|----|
| DLL / method | Point3DAlg.dll / `Point3DAlg_ComuiManualRegist` |
| 输入 | `源点云`（cloud，待变换）、`目标点云`（cloud，参考） |
| 输出 | `配准点云`（cloud，源 × T；rgb 为空） |
| 参数 | `strategy`（int，默认 1，min 1 / max 6）：配准策略，1=ICP点到点 2=ICP点到面 3=GICP 4=FPFH+RANSAC 5=NDT 6=PCA主轴；`init_transform`（text，默认单位阵 16 值逗号分隔）：初始变换 4x4 行主序，可用 `Point3DAlg_RegistTool.exe` 键盘粗调后导出 `regist_T_flat.txt` 内容粘入，非数字字符视为分隔符（兼容 4x4 矩阵/单行 16 值），解析失败保持单位阵；`max_distance`（number，默认 0.0，min 0.0）：对应点最大距离 mm，<=0 按点云间距自适应；`max_iteration`（int，默认 60，min 1）：单次最大迭代 |
| 联调 | 最小冒烟：同一片点云同时接「源点云」与「目标点云」（init_transform 保持默认单位阵）→ 节点绿，fitness 应≈1.0、rmse≈0；再用两份不同扫描（或 RegistTool 粗调导出的初值）测真实配准；接 `pub.cloud` 看配准后点云是否与目标重合；确认项一/三：读节点消息应含 `fitness=… rmse=… \| T=…` |
| 预期错误 | rc=1「需要两片输入点云（源/目标）」；rc=2「源点云为空」/「目标点云为空」；rc=3「内存不足」。注意：strategy 越界静默钳回 1，max_iteration<1 钳回 1，不报错 |

### 6. business.region.ratio（区域缺陷率统计）

| 项 | 值 |
|----|----|
| DLL / method | BusinessAlg.dll / `BusinessAlg_ComuiRegionRatio` |
| 输入 | `点云`（cloud，SoA float[3N]） |
| 输出 | 无端口（结果并入节点消息，见确认项一/四） |
| 参数 | `min_z`（number，默认 0.0）：高度下限 mm；`max_z`（number，默认 10.0）：高度上限 mm；`roi_x`（number，默认 0.0）：ROI 左上角 X mm；`roi_y`（number，默认 0.0）：ROI 左上角 Y mm；`roi_w`（number，默认 100.0，min 0.001）：ROI 宽 mm；`roi_h`（number，默认 100.0，min 0.001）：ROI 高 mm |
| 联调 | 接 alg.point3d.read_sick 输出 → 按点云实际高度范围调 min_z/max_z、ROI 覆盖有效区域 → 节点绿；确认项四：读节点消息应含 `defect_ratio=… area=… mm2`；ctypes 已端到端验证 1000 点平面云 ratio=0.8910 |
| 预期错误 | rc=1「输入未连线」；rc=2「输入点云为空」；rc=3「参数不足（需要 min_z/max_z/roi_x/roi_y/roi_w/roi_h）」；rc=4「参数非法（空点云或 ROI 宽高<=0）」（底层 -1）/「过滤后无有效投影点」（底层 2）/「计算失败（错误码 N）」 |

## 三、已知限制（ABI v1，升级 v2 才能解）

1. **无 text/number 输出端口**：`AlgoCOutput` 只有 `images`/`clouds`/`error` 三个通道，
   没有 text 与 number 输出端口。因此节点声明的数值结果（fitness/T、
   defect_ratio/area）只能走消息通道（见确认项一/三/四）；manual_regist 与
   region_ratio 的 node.json outputs 均不声明 text 端口（T1 已按方案 a 砍掉）。
2. **数值结果走消息通道**：消息格式为人类可读文本（非结构化），宿主侧如需程序化
   消费（记录历史、阈值报警等），当前协议下只能解析该文本，或等 ABI v2。
3. **宿主仅在 rc≠0 时读 error**（NativeAlgo.cs 现状）：rc=0 时的非空 error（成功
   消息）被丢弃——这正是第一节确认项 1 的来源；在宿主支持前，成功消息不出现在
   任何面板。
4. **error 为静态存储**：宿主只读不释放（`algo_free` 不作用于 error），且下次
   调用会覆盖；宿主如需留存需自行拷贝。
