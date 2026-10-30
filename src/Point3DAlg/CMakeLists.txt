cmake_minimum_required(VERSION 3.16)
project(Point3DAlg LANGUAGES CXX)

# 设置源文件（排除测试入口，测试 exe 单独成目标）
file(GLOB_RECURSE SOURCES "*.cpp" "*.cc" "*.cxx")
list(FILTER SOURCES EXCLUDE REGEX ".*_test\\.cpp$")
file(GLOB_RECURSE HEADERS "*.h" "*.hpp" "*.hxx")

add_library(${PROJECT_NAME} SHARED ${SOURCES} ${HEADERS})

# 导出宏 + 可见性设置
setup_export_macro(${PROJECT_NAME})
setup_shared_library_props(${PROJECT_NAME})

# 链接第三方库（OpenCV：FOV 标注画框/文字；JSON：nlohmann/json 配置解析；PCL 预留）
link_third_party(${PROJECT_NAME} OPENCV JSON PCL)

# 部署配置：复制 JSON 到 output/
setup_deployment(${PROJECT_NAME})

# -----------------------------------------------------------------------------
# 测试入口：控制台 exe，调 SickReader 类链路验证读取（参数写死在 Point3DAlg_test.cpp）
# Release 也带完整调试信息（/Z7 + /DEBUG，与 Juxin6X3DAlg 的调试配置一致）
# -----------------------------------------------------------------------------
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Z7 /Od")
set_target_properties(${PROJECT_NAME} PROPERTIES
    LINK_FLAGS "/INCREMENTAL /DEBUG /OPT:REF"
)

add_executable(Point3DAlg_test Point3DAlg_test.cpp)
target_link_libraries(Point3DAlg_test PRIVATE ${PROJECT_NAME})
set_target_properties(Point3DAlg_test PROPERTIES
    LINK_FLAGS "/INCREMENTAL /DEBUG /OPT:REF"
)
