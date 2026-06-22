# FindSnap7.cmake - 跨平台适配版 (支持 Windows MinGW/MSVC 和 Linux)
#
# 定义:
#   Snap7_FOUND        - 系统是否找到 Snap7
#   Snap7::Snap7       - 导入的目标库
#
# 对于 Windows，此模块还会自动链接 ws2_32 和 winmm 系统库。

if(TARGET Snap7::Snap7)
    return()
endif()

# 辅助：检查并添加系统依赖
macro(add_snap7_system_deps TARGET_NAME)
    if(WIN32)
        target_link_libraries(${TARGET_NAME} INTERFACE ws2_32 winmm)
    endif()
    # Linux 下不需要额外系统库
endmacro()

if(WIN32)
    # ========= Windows 逻辑 =========
    message(STATUS "Looking for Snap7 in: ${CMAKE_SOURCE_DIR}/third_party/snap7")

    set(SNAP7_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/third_party/snap7/include")
    if(NOT EXISTS "${SNAP7_INCLUDE_DIR}/snap7.h")
        message(FATAL_ERROR "snap7.h not found at: ${SNAP7_INCLUDE_DIR}")
    endif()

    set(SNAP7_LIB_DIR "${CMAKE_SOURCE_DIR}/third_party/snap7/lib")
    # 根据编译器类型选择库文件后缀
    if(MINGW)
        # MinGW 优先查找导入库 libsnap7.dll.a，其次直接使用 .dll
        find_library(SNAP7_LIBRARY
            NAMES libsnap7 snap7
            PATHS "${SNAP7_LIB_DIR}"
            NO_DEFAULT_PATH
        )
    else()
        # MSVC 查找 .lib
        find_library(SNAP7_LIBRARY
            NAMES snap7
            PATHS "${SNAP7_LIB_DIR}"
            NO_DEFAULT_PATH
        )
    endif()

    include(FindPackageHandleStandardArgs)
    find_package_handle_standard_args(Snap7 DEFAULT_MSG SNAP7_LIBRARY SNAP7_INCLUDE_DIR)

    if(Snap7_FOUND AND NOT TARGET Snap7::Snap7)
        # 根据库文件类型创建导入目标
        get_filename_component(SNAP7_LIB_EXT "${SNAP7_LIBRARY}" LAST_EXT)
        if("${SNAP7_LIB_EXT}" STREQUAL ".dll")
            # 直接使用 .dll（MinGW 可能这样）
            add_library(Snap7::Snap7 SHARED IMPORTED)
            set_target_properties(Snap7::Snap7 PROPERTIES
                IMPORTED_LOCATION "${SNAP7_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${SNAP7_INCLUDE_DIR}"
            )
            message(STATUS "Using Snap7 DLL: ${SNAP7_LIBRARY}")
        else()
            # .lib 或 .a 导入库
            add_library(Snap7::Snap7 SHARED IMPORTED)
            # 查找对应的 .dll
            find_file(SNAP7_DLL
                NAMES snap7.dll
                PATHS "${SNAP7_LIB_DIR}" "${CMAKE_CURRENT_BINARY_DIR}"
                NO_DEFAULT_PATH
            )
            if(NOT SNAP7_DLL)
                message(WARNING "snap7.dll not found, runtime may fail")
            endif()
            set_target_properties(Snap7::Snap7 PROPERTIES
                IMPORTED_IMPLIB "${SNAP7_LIBRARY}"
                IMPORTED_LOCATION "${SNAP7_DLL}"
                INTERFACE_INCLUDE_DIRECTORIES "${SNAP7_INCLUDE_DIR}"
            )
            message(STATUS "Using Snap7 import lib: ${SNAP7_LIBRARY}")
        endif()
        add_snap7_system_deps(Snap7::Snap7)
    endif()

else()
    # ========= Linux 逻辑（使用系统安装的 snap7） =========
    message(STATUS "Looking for Snap7 in system paths (Linux)")

    find_path(SNAP7_INCLUDE_DIR NAMES snap7.h PATHS /usr/include /usr/local/include)
    find_library(SNAP7_LIBRARY NAMES snap7 PATHS /usr/lib /usr/local/lib /lib)

    include(FindPackageHandleStandardArgs)
    find_package_handle_standard_args(Snap7 REQUIRED_VARS SNAP7_LIBRARY SNAP7_INCLUDE_DIR)

    if(Snap7_FOUND AND NOT TARGET Snap7::Snap7)
        add_library(Snap7::Snap7 SHARED IMPORTED)
        set_target_properties(Snap7::Snap7 PROPERTIES
            IMPORTED_LOCATION "${SNAP7_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${SNAP7_INCLUDE_DIR}"
        )
        message(STATUS "Found Snap7: ${SNAP7_LIBRARY}")
        # Linux 下不需要额外系统库
    endif()
endif()