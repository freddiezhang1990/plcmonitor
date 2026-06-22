# CMakeLists.txt (顶层)
cmake_minimum_required(VERSION 3.16)
project(PLCMonitor VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ---------- Qt ----------
find_package(Qt6 REQUIRED
  COMPONENTS Core Widgets Sql svg Network PrintSupport Concurrent
)

# ---------- 自定义 CMake 模块路径 ----------
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# ---------- 查找 Snap7 ----------
find_package(Snap7 REQUIRED)

# ---------- 子目录 ----------
add_subdirectory(src/common)
add_subdirectory(src/core)
add_subdirectory(src/comm)    # 通讯模块
add_subdirectory(src/ui/models)    # 产生 models 库
add_subdirectory(src/ui/dialogs)
add_subdirectory(src/app)

# ---------- 资源 ----------
file(GLOB_RECURSE QT_RESOURCE_FILES
    "${CMAKE_SOURCE_DIR}/resources/resources.qrc"
)

if(QT_RESOURCE_FILES)
    message(STATUS "找到Qt资源文件: ${QT_RESOURCE_FILES}")
    qt_add_resources(${PROJECT_NAME} "app_resources" ${QT_RESOURCE_FILES})
    foreach(res_file ${QT_RESOURCE_FILES})
        message(STATUS "  - 处理资源文件: ${res_file}")
    endforeach()
else()
    message(FATAL_ERROR "未找到resources.qrc资源文件！请检查路径: ${CMAKE_SOURCE_DIR}/resources/resources.qrc")
endif()

# ---------- Windows DLL 拷贝 ----------
if(WIN32 AND Snap7_DLL)
    message(STATUS "Copying Snap7 DLL to output directory")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${Snap7_DLL}"
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
    )
endif()
