# 项目开发规则

## 1. 目录结构规范

```
src/
├── CMakeLists.txt              # 通用配置 (第三方库查找、导出宏、部署)
├── DEPENDENCIES.md             # 依赖库版本说明
├── DEVELOPMENT_RULES.md        # 本文件
├── Image2DAlg/                 # 2D 算法子项目
│   ├── CMakeLists.txt
│   ├── Image2DAlg_api.h        # C 风格公共接口
│   ├── Image2DAlg_api.cpp      # 接口实现
│   ├── Image2DAlg_export.h     # 自动生成 (勿手动修改)
│   ├── *.node.json             # ComUI 节点声明
│   └── include/                # 公共头文件
└── Point3DAlg/                 # 3D 算法子项目
    ├── CMakeLists.txt
    ├── Point3DAlg_api.h
    ├── Point3DAlg_api.cpp
    ├── Point3DAlg_export.h     # 自动生成
    ├── *.node.json
    └── include/
```

## 2. CMake 规则

### 2.1 子项目 CMakeLists.txt 最小模板
```cmake
cmake_minimum_required(VERSION 3.16)
project(YourAlg LANGUAGES CXX)

file(GLOB_RECURSE SOURCES "*.cpp" "*.cc" "*.cxx")
file(GLOB_RECURSE HEADERS "*.h" "*.hpp" "*.hxx")

add_library(${PROJECT_NAME} SHARED ${SOURCES} ${HEADERS})

setup_export_macro(${PROJECT_NAME})        # 生成导出宏
setup_shared_library_props(${PROJECT_NAME}) # 符号可见性
link_third_party(${PROJECT_NAME} OPENCV)   # 或 PCL
setup_deployment(${PROJECT_NAME})          # 复制 JSON 到 output/
```

### 2.2 可用通用函数 (定义在 src/CMakeLists.txt)
| 函数 | 参数 | 说明 |
|------|------|------|
| `setup_export_macro(target)` | target | 生成 `*_export.h`，设置 `PRIVATE XXX_EXPORTS` |
| `setup_shared_library_props(target)` | target | `hidden` 可见性 + `WINDOWS_EXPORT_ALL_SYMBOLS OFF` |
| `link_third_party(target OPENCV\|PCL)` | target + 选项 | 链接对应第三方库 |
| `setup_deployment(target)` | target | POST_BUILD 复制 `*.json` 到 output/ |
| `collect_third_party_runtime_dlls()` | 目标列表 | 收集 OpenCV/PCL/Boost DLL (仅 MSVC) |

### 2.3 禁止事项
- ❌ 手动写 `target_compile_definitions(... EXPORTS)` —— 用 `setup_export_macro`
- ❌ `PUBLIC`/`INTERFACE` 传播导出宏定义
- ❌ 手动 `install()` —— 本项目仅输出到 `output/`
- ❌ 硬编码第三方库路径 —— 统一用 `THIRD_PARTY_DIR`

## 3. C++ 接口规范 (ComUI Native Plugin 兼容)

### 3.1 头文件模板
```cpp
// YourAlg_api.h
#pragma once
#include "YourAlg_export.h"  // 自动生成

#ifdef __cplusplus
extern "C" {
#endif

YOURALG_EXPORT int YourAlg_Init(void);
YOURALG_EXPORT void YourAlg_Cleanup(void);
// 算法接口...

#ifdef __cplusplus
}
#endif
```

### 3.2 导出宏使用
```cpp
// 正确：每个对外函数加宏
YOURALG_EXPORT int YourAlg_Process(Image* in, Image* out);

// 错误：类/结构体不加宏 (内部实现不导出)
class InternalImpl { ... };  // 无宏，隐藏符号
```

### 3.3 ComUI 统一 C ABI (THIRD_PARTY_OPS.md §5.5)
如需接入 ComUI 算子库，实现统一入口：
```cpp
// 头文件
#include "comui_native_abi.h"  // 参见 THIRD_PARTY_OPS.md

IMGIO_API int32_t your_algo_entry(const AlgoCInput* in, AlgoCOutput* out);
IMGIO_API void algo_free(void* p);

// JSON 对应
// "runtime": "native", "assembly": "YourAlg.dll", "method": "your_algo_entry"
```

## 4. 代码风格

| 项目 | 规则 |
|------|------|
| C++ 标准 | C++17 |
| 缩进 | 4 空格，无 Tab |
| 命名 | 函数/类: PascalCase; 变量: snake_case; 宏: UPPER_SNAKE |
| 包含顺序 | 系统头 → 第三方头 → 项目头 |
| 智能指针 | 优先 `unique_ptr`/`shared_ptr`，避免裸 `new/delete` |
| 错误处理 | C 接口返回 `int` (0=成功, <0=错误码)，不抛异常跨 DLL 边界 |

## 5. 版本管理

- 版本号在根 `CMakeLists.txt` 设置：`project(AlgProject VERSION x.y.z)`
- 语义化版本：主版本.次版本.补丁
- 破坏性 API 变更 → 主版本+1

## 6. 构建与部署

### 6.1 日常开发
```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release
# 产物在 ../output/Release/
```

### 6.2 发布部署 (含第三方依赖)
```bash
cmake --build . --config Release --target deploy_all
# 直接拷贝 build/output/Release/ 到 comdll/algo/<名称>/
```

### 6.3 单配置生成器 (Ninja/Make)
```bash
cmake -G Ninja ..
ninja
ninja deploy_all
```

## 7. JSON 节点声明规范

### 7.1 文件命名
- 位置：子项目目录下 (如 `Image2DAlg/image2d_xxx.node.json`)
- 命名：`<模块>_<功能>.node.json`

### 7.2 必填字段 (参考 THIRD_PARTY_OPS.md §2)
```json
{
  "node": {
    "id": "vendor.name",      // 全局唯一，带厂商前缀
    "name": "显示名称",
    "group": "分组",          // 算子库栏目
    "category": "源/处理/输出"
  },
  "runtime": "native",
  "assembly": "YourDll.dll",
  "method": "YourDll_EntryFunction",
  "inputs": [ {"name": "输入", "type": "image|cloud"} ],
  "params": [ {"name": "param", "type": "number|int|enum...", "default": ...} ],
  "outputs": [ {"name": "输出", "type": "image|cloud"} ]
}
```

### 7.3 Id 命名空间
- 格式：`<厂商/项目>.<功能>` 如 `alg.image2d.gamma`
- **冲突 = 静默忽略** (ComUI 不报错，不覆盖内置)

### 7.4 JSON 生成方式
**当前为手写维护，非自动生成。**

| 项 | 说明 |
|----|------|
| 来源 | 开发者在子项目目录手写 `*.node.json` |
| 复制 | `setup_deployment()` POST_BUILD 自动复制到 `output/` |
| 同步责任 | **开发者需保持 JSON 与 C++ 接口一致** (method名、参数、类型) |

**字段对应关系：**
- `method` ↔ C++ 导出函数名 (必须完全一致)
- `inputs[i].type` → `image`/`cloud` 对应 `AlgoCImage`/`AlgoCCloud`
- `params[i].type` 映射表：
  | C++ 类型 | JSON type |
  |----------|-----------|
  | `double` | `number` |
  | `int`/`int32_t` | `int` |
  | `bool` | `bool` |
  | `const char*`/`std::string` | `text`/`path` |
  | 枚举 | `enum` (需 `options`) |

**如需自动化** (接口多、变更频繁)，可后续引入：
- Clang LibTooling / castxml 解析头文件
- 自定义脚本映射类型 → 生成 JSON
- 作为独立工具集成到 CI

## 8. 第三方依赖管理

| 库 | 版本 | 位置 | 更新流程 |
|----|------|------|----------|
| OpenCV | 4.4.0 | `thrid/opencv-4.4.0/` | 替换目录 + 修正 CMake 查找路径 |
| PCL | 1.9.1 | `thrid/PCL-1.9.1/` | 同左 |
| Boost/Eigen/FLANN | 随 PCL | `thrid/PCL-1.9.1/3rdParty/` | 随 PCL 更新 |

**新增依赖流程：**
1. 放入 `thrid/<库名>-<版本>/`
2. 在 `src/CMakeLists.txt` 添加 `find_package()`
3. 在 `link_third_party()` 添加选项
4. 更新 `collect_third_party_runtime_dlls()` 收集 DLL
5. 更新 `DEPENDENCIES.md`

## 9. 调试技巧

### 9.1 检查导出符号
```bash
# Windows
dumpbin /exports Image2DAlg.dll

# Linux
nm -D libImage2DAlg.so | grep IMAGE2DALG
```

### 9.2 运行时依赖检查
```bash
# Windows (需在 PATH 或同目录)
depends.exe Image2DAlg.dll

# Linux
ldd libImage2DAlg.so
```

## 10. 常见问题

| 问题 | 解决 |
|------|------|
| 符号未导出 | 检查 `PRIVATE XXX_EXPORTS`、函数是否加 `XXX_EXPORT` |
| 运行时找不到 DLL | 运行 `deploy_all`，确保第三方 DLL 在同目录 |
| JSON 未复制 | 确认文件在子项目目录下、扩展名 `.json`、已触发 POST_BUILD |
| Id 冲突不显示 | 改用唯一前缀 `alg.`，重启 ComUI |
| C++ 异常跨 DLL 崩溃 | C 接口不抛异常，内部捕获转错误码返回 |