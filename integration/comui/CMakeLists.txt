# integration/comui —— ComUI 集成层（与算法实现隔离）
#
# 设计：ComUI 是**宿主 UI 侧的接入协议**（统一 C ABI + node.json 声明），
# 与算法本身无关。本目录集中存放全部 ComUI 相关代码，算法模块
# （ShapelyAlg / Image2DAlg / Point3DAlg / BusinessAlg）保持零 UI 痕迹。
#
# 内容：
#   *.node.json         —— ComUI 节点声明（部署时复制到输出目录）
#   *_comui.cpp         —— 各算法的 ComUI ABI 适配层（编译进对应算法 DLL）
#   comui_native_abi.h  —— 统一 C ABI 结构定义
#
# 算法模块通过 COMUI_ADAPTER_* 变量挑选自己的适配文件（见 src/<模块>/CMakeLists.txt）。
# 若完全不需要 ComUI：把模块 CMake 里的
#   list(APPEND SOURCES ${COMUI_ADAPTER_xxx}) 和 deploy_comui_nodes() 两行删掉即可，
# 算法库与测试不受任何影响。

# ---- 供算法模块引用的适配源（在父作用域可见）----
set(COMUI_ADAPTER_IMAGE2D ${CMAKE_CURRENT_SOURCE_DIR}/Image2DAlg_comui.cpp PARENT_SCOPE)
set(COMUI_ADAPTER_POINT3D ${CMAKE_CURRENT_SOURCE_DIR}/Point3DAlg_comui.cpp PARENT_SCOPE)
set(COMUI_ADAPTER_BUSINESS ${CMAKE_CURRENT_SOURCE_DIR}/BusinessAlg_comui.cpp PARENT_SCOPE)
set(COMUI_NODE_JSON_DIR  ${CMAKE_CURRENT_SOURCE_DIR} PARENT_SCOPE)

# 把 ComUI 节点声明复制到输出目录（由算法模块显式调用）
function(deploy_comui_nodes target_name)
    set(_dir ${COMUI_NODE_JSON_DIR})
    file(GLOB _node_json ${_dir}/*.node.json)
    if(_node_json)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${_node_json}
            $<TARGET_FILE_DIR:${target_name}>
            COMMENT "Copy ComUI node declarations to output"
        )
    endif()
endfunction()
