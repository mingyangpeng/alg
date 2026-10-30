# OpenCV 4.4.0 world 单库最小配置（本目录为手动解包布局：include/ lib/ bin/，无官方构建树）。
# find_package(OpenCV PATHS <本目录>) 找到这里即可设置 include/库/运行时。
set(OpenCV_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/include")

add_library(opencv_world SHARED IMPORTED GLOBAL)
set_target_properties(opencv_world PROPERTIES
    IMPORTED_LOCATION         "${CMAKE_CURRENT_LIST_DIR}/bin/opencv_world440.dll"
    IMPORTED_LOCATION_RELEASE "${CMAKE_CURRENT_LIST_DIR}/bin/opencv_world440.dll"
    IMPORTED_LOCATION_DEBUG   "${CMAKE_CURRENT_LIST_DIR}/bin/opencv_world440d.dll"
    IMPORTED_IMPLIB           "${CMAKE_CURRENT_LIST_DIR}/lib/opencv_world440.lib"
    IMPORTED_IMPLIB_RELEASE   "${CMAKE_CURRENT_LIST_DIR}/lib/opencv_world440.lib"
    IMPORTED_IMPLIB_DEBUG     "${CMAKE_CURRENT_LIST_DIR}/lib/opencv_world440d.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/include")

set(OpenCV_LIBS opencv_world)
set(OpenCV_FOUND TRUE)
