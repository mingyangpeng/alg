cmake_minimum_required(VERSION 3.16)
project(Image2DAlg LANGUAGES CXX)

# 设置源文件
file(GLOB_RECURSE SOURCES "*.cpp" "*.cc" "*.cxx")
file(GLOB_RECURSE HEADERS "*.h" "*.hpp" "*.hxx")

add_library(${PROJECT_NAME} SHARED ${SOURCES} ${HEADERS})

# 导出宏 + 可见性设置
setup_export_macro(${PROJECT_NAME})
setup_shared_library_props(${PROJECT_NAME})

# 链接第三方库
link_third_party(${PROJECT_NAME} OPENCV)

# 部署配置：复制 JSON 到 output/
setup_deployment(${PROJECT_NAME})


# 普通构建 (只编译你的代码，生成 Image2DAlg.dll, Point3DAlg.dll)：
# cmake --build . --config Release
# 部署构建 (额外把 OpenCV/PCL/Boost 的 DLL 也复制到 output/)：
# cmake --build . --config Release --target deploy_all