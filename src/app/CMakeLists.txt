# src/app/CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

# 查找 Qt6
find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Core Network PrintSupport)

# 启用自动 UI 文件处理
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOMOC ON)

# 检测MinGW编译器并添加大对象文件支持
if(MINGW)
    add_compile_options(-Wa,-mbig-obj)
    message(STATUS "Detected MinGW compiler, added -Wa,-mbig-obj flag")
endif()

# 添加Qt资源处理
qt_add_resources(PLCMonitor_SRCS "${CMAKE_SOURCE_DIR}/resources/resources.qrc")

# 添加 QCustomPlot 源文件
set(QCUSTOMPLOT_SRCS
    ${CMAKE_SOURCE_DIR}/src/common/qcustomplot.cpp
    ${CMAKE_SOURCE_DIR}/src/common/qcustomplot.h
)

# 为qcustomplot.cpp添加编译优化选项
if(MINGW)
    set_source_files_properties(
        ${CMAKE_SOURCE_DIR}/src/common/qcustomplot.cpp
        PROPERTIES
        COMPILE_FLAGS "-O1 -g0"
    )
endif()

# 定义UI文件
set(UI_FILES
    mainwindow.ui
)

# 定义源文件（仅保留 app 自身源文件，对话框已移至 dialogs 库）
set(SOURCE_FILES
    main.cpp
    mainwindow.cpp
    application.cpp
)

# ---------- 定义可执行文件 ----------
qt_add_executable(${PROJECT_NAME}
    ${PLCMonitor_SRCS}
    ${QCUSTOMPLOT_SRCS}
    ${SOURCE_FILES}
    ${UI_FILES}
    # 不再包含对话框源文件，它们属于 dialogs 库
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Release>:QT_NO_DEBUG_OUTPUT;QT_NO_INFO_OUTPUT>
)

# ---------- 链接依赖库 ----------
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Widgets
    Qt6::Sql
    Qt6::Core
    Qt6::Network
    Qt6::PrintSupport

    core
    models
    dialogs           # 新增：链接对话框库
    common

    Snap7::Snap7
)

# ---------- 包含目录 ----------
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/ui/models
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs
    ${CMAKE_SOURCE_DIR}/src/common
    ${CMAKE_SOURCE_DIR}/third_party/snap7/include
)

# ---------- 设置目标属性 ----------
set_target_properties(${PROJECT_NAME} PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

if(MINGW)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        LINK_FLAGS "-Wl,--allow-multiple-definition"
    )
endif()

# ---------- 复制配置文件 ----------
configure_file(
    ${CMAKE_SOURCE_DIR}/configs/plc_config.json
    ${CMAKE_BINARY_DIR}/configs/plc_config.json
    COPYONLY
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(${PROJECT_NAME} PRIVATE DEBUG)
endif()