cmake_minimum_required(VERSION 3.16)

find_package(Qt6 REQUIRED COMPONENTS Widgets Sql)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)   # 启用自动UI处理

set(DIALOG_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/connectionconfigdialog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/databaseconfigdialog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/logindialog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/usermanagedialog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/historyquerydialog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/DeviceConfigDialog.cpp
)

set(DIALOG_UI
    ${CMAKE_CURRENT_SOURCE_DIR}/usermanagedialog.ui
    ${CMAKE_CURRENT_SOURCE_DIR}/logindialog.ui
    ${CMAKE_CURRENT_SOURCE_DIR}/DeviceConfigDialog.ui
    ${CMAKE_CURRENT_SOURCE_DIR}/ConnectionConfigDialog.ui
)

# 将源文件和 UI 文件一起编译（AUTOUIC 会自动处理 .ui 文件）
add_library(dialogs STATIC
    ${DIALOG_SOURCES}
    ${DIALOG_UI}
)

target_include_directories(dialogs PUBLIC
    ${CMAKE_CURRENT_BINARY_DIR}          # 生成的头文件会在这里
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/app
    ${CMAKE_SOURCE_DIR}/src/ui/models
    ${CMAKE_SOURCE_DIR}/src/common
    ${CMAKE_SOURCE_DIR}/src/core
)

target_link_libraries(dialogs PRIVATE
    Qt6::Widgets
    Qt6::Sql
    models
    common
    core
)

set_target_properties(dialogs PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)