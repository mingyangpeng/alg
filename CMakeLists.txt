cmake_minimum_required(VERSION 3.16)
project(AlgProject VERSION 1.0.0 LANGUAGES CXX)

# 默认构建动态库
option(BUILD_SHARED_LIBS "Build shared libraries" ON)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# -----------------------------------------------------------------------------
# 输出目录设置：所有动态库/可执行文件输出到 output/
# -----------------------------------------------------------------------------
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/output)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/output)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/output)

# 多配置生成器 (MSVC) 按配置分子目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/output/Debug)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/output/Debug)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/output/Debug)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/output/Release)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/output/Release)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/output/Release)

# 包含源码目录
add_subdirectory(src)

# -----------------------------------------------------------------------------
# 收集第三方运行时 DLL 到 output/ (仅 Windows/MSVC)
# -----------------------------------------------------------------------------
collect_third_party_runtime_dlls(Image2DAlg Point3DAlg ShapelyAlg)

# 便捷目标：一键部署所有运行时依赖
add_custom_target(deploy_all
    DEPENDS deploy_runtime_dlls
    COMMENT "Deploy all runtime DLLs to output directory"
)