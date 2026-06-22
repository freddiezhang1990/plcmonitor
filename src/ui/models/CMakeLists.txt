# src/ui/models/CMakeLists.txt
# 定义模型库（原 view 库）

# 查找Qt模块
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Svg SvgWidgets)

message(STATUS "Qt6Svg_INCLUDE_DIRS: ${Qt6Svg_INCLUDE_DIRS}")
message(STATUS "Qt6SvgWidgets_INCLUDE_DIRS: ${Qt6SvgWidgets_INCLUDE_DIRS}")

# 手动查找 Qt6Svg 包含路径
if(NOT Qt6Svg_INCLUDE_DIRS)
    # 针对 MSYS2 UCRT64 环境
    set(Qt6Svg_INCLUDE_DIRS "C:/msys64/ucrt64/include/qt6/Qt6Svg")
    message(STATUS "Manually setting Qt6Svg_INCLUDE_DIRS: ${Qt6Svg_INCLUDE_DIRS}")
endif()

# 设置模型库的源文件
set(MODEL_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/realtimedatamodel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/chartmanager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/trendmanager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/processviewmanager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/devicetreemodel.cpp
)

# 创建模型库（静态库）
add_library(models STATIC
    ${MODEL_SOURCES}
    ${MODEL_HEADERS}
    alarmlistmodel.h alarmlistmodel.cpp
)

# 设置库的包含目录
target_include_directories(models PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}       # 当前目录（ui/models）
    ${CMAKE_SOURCE_DIR}/src/common    # 公共头文件
    ${CMAKE_SOURCE_DIR}/src/core      # 核心头文件
    ${Qt6Svg_INCLUDE_DIRS}            # Qt6 SVG 包含路径
    ${Qt6SvgWidgets_INCLUDE_DIRS}
)

# 链接Qt库
target_link_libraries(models PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Sql
    Qt6::Widgets   # 如果需要 QWidget 相关
    Qt6::Svg
    Qt6::SvgWidgets
)

# 设置C++标准
set_target_properties(models PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# 导出编译特性
target_compile_features(models PUBLIC cxx_std_17)