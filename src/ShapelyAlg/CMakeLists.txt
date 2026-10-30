cmake_minimum_required(VERSION 3.16)
project(ShapelyAlg LANGUAGES CXX)

# 单文件夹布局：
#   ShapelyAlg.h        对外总入口（业务层只 include 它）
#   *.h/*.cpp           模块头与实现（业务层不直接引用）
#   ShapelyAlg_test.cpp 测试入口

file(GLOB SOURCES "*.cpp")
list(FILTER SOURCES EXCLUDE REGEX ".*_test\.cpp$")
file(GLOB HEADERS "*.h")

add_library(${PROJECT_NAME} SHARED ${SOURCES} ${HEADERS})

# 头文件搜索路径：模块根（全部头都在这一层）
target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

# 导出宏 + 可见性设置
setup_export_macro(${PROJECT_NAME})
setup_shared_library_props(${PROJECT_NAME})

# 链接第三方库（OpenCV：cv::Mat/Point 数据结构；GEOS：二维几何；PCL：点云算子）
link_third_party(${PROJECT_NAME} OPENCV GEOS PCL)

# 部署配置：复制 JSON 到 output/
setup_deployment(${PROJECT_NAME})

# -----------------------------------------------------------------------------
# 测试入口：控制台 exe，验证 shapely 全部模块（只 include ShapelyAlg.h）
# -----------------------------------------------------------------------------
add_executable(ShapelyAlg_test ShapelyAlg_test.cpp)
target_link_libraries(ShapelyAlg_test PRIVATE ${PROJECT_NAME})
