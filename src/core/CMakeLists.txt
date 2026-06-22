# src/core/CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

# 查找 Qt6 组件（确保 Sql 模块可用）
find_package(Qt6 REQUIRED COMPONENTS Core Sql Gui)

# 定义 core 静态库
add_library(core STATIC
  controller.h controller.cpp
  alarmmanager.h alarmmanager.cpp
  databasemanager.h databasemanager.cpp
  usermanager.h usermanager.cpp
)

# 添加源文件（注意：头文件不需要编译，但可以保留用于 IDE）
target_sources(core PRIVATE
  plcdata.cpp
)

# 链接依赖库
target_link_libraries(core
  PUBLIC
    Qt6::Core
    Qt6::Sql
    Qt6::Gui
    common
    models          # 注意：原 view 库已改名为 models
    comm
)

# 包含目录
target_include_directories(core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/src/common
  ${CMAKE_SOURCE_DIR}/src/ui/models   # 修改为 models 目录
  ${CMAKE_SOURCE_DIR}/src/comm    # 通讯模块头文件
  ${CMAKE_SOURCE_DIR}/third_party/snap7/include
)

# 设置目标属性
set_target_properties(core PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
)