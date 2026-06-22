# src/common/CMakeLists.txt
# 原有逻辑：将 common 目录下的 cpp 编译为静态库
add_library(common STATIC
    logger.cpp
    configloader.cpp
    common_types.h
    simplecrypt.h simplecrypt.cpp
)
# 【关键修改】包含目录
# 1. PUBLIC: 使得包含此库的目标也能访问这些目录
# 2. 添加 ${CMAKE_CURRENT_SOURCE_DIR}/../core: 指向 core 目录
target_include_directories(common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}           # 原有的：当前 common 目录
    ${CMAKE_CURRENT_SOURCE_DIR}/../core   # 新增的：core 目录，用于找到 plcdata.h
)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Sql)
# 链接 Qt 库
target_link_libraries(common PRIVATE Qt6::Core Qt6::Widgets Qt6::Sql)