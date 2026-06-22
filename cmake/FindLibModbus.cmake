# FindLibModbus.cmake - 跨平台适配版 (支持 Windows MinGW/MSVC 和 Linux)
#
# 定义:
#   LibModbus_FOUND          - 系统是否找到 libmodbus
#   LibModbus::LibModbus     - 导入的目标库
#   LIBMODBUS_DLL            - Windows 下 DLL 路径（供 POST_BUILD 拷贝）

if(TARGET LibModbus::LibModbus)
    return()
endif()

if(WIN32)
    # ========= Windows 逻辑 =========
    set(LIBMODBUS_ROOT "${CMAKE_SOURCE_DIR}/third_party/libmodbus")
    message(STATUS "Looking for libmodbus in: ${LIBMODBUS_ROOT}")

    set(LIBMODBUS_INCLUDE_DIR "${LIBMODBUS_ROOT}/include")
    if(NOT EXISTS "${LIBMODBUS_INCLUDE_DIR}")
        message(FATAL_ERROR
            "libmodbus include dir not found: ${LIBMODBUS_INCLUDE_DIR}\n"
            "请将 libmodbus 预编译库放入 third_party/libmodbus/，\n"
            "可通过 vcpkg install libmodbus:x64-windows 获取。")
    endif()

    set(LIBMODBUS_LIB_DIR "${LIBMODBUS_ROOT}/lib")

    if(MINGW)
        # MinGW：优先查找 libmodbus.dll.a 或 libmodbus.a
        find_library(LIBMODBUS_LIBRARY
            NAMES modbus libmodbus
            PATHS "${LIBMODBUS_LIB_DIR}"
            NO_DEFAULT_PATH
        )
    else()
        # MSVC：查找 modbus.lib 或 libmodbus.lib
        find_library(LIBMODBUS_LIBRARY
            NAMES modbus libmodbus
            PATHS "${LIBMODBUS_LIB_DIR}"
            NO_DEFAULT_PATH
        )
    endif()

    # 查找 DLL（供运行时拷贝）
    find_file(LIBMODBUS_DLL
        NAMES modbus.dll libmodbus.dll
        PATHS "${LIBMODBUS_LIB_DIR}" "${LIBMODBUS_ROOT}/bin"
        NO_DEFAULT_PATH
    )

    include(FindPackageHandleStandardArgs)
    find_package_handle_standard_args(LibModbus DEFAULT_MSG
        LIBMODBUS_LIBRARY LIBMODBUS_INCLUDE_DIR)

    if(LibModbus_FOUND AND NOT TARGET LibModbus::LibModbus)
        add_library(LibModbus::LibModbus SHARED IMPORTED)
        set_target_properties(LibModbus::LibModbus PROPERTIES
            IMPORTED_IMPLIB "${LIBMODBUS_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${LIBMODBUS_INCLUDE_DIR}"
        )
        if(LIBMODBUS_DLL)
            set_target_properties(LibModbus::LibModbus PROPERTIES
                IMPORTED_LOCATION "${LIBMODBUS_DLL}"
            )
            message(STATUS "Found libmodbus DLL: ${LIBMODBUS_DLL}")
        else()
            message(WARNING "libmodbus DLL not found, runtime may fail")
        endif()
        message(STATUS "Found libmodbus: ${LIBMODBUS_LIBRARY}")
    endif()

else()
    # ========= Linux 逻辑（通过 pkg-config 查找） =========
    message(STATUS "Looking for libmodbus via pkg-config (Linux)")

    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBMODBUS QUIET libmodbus)
    endif()

    if(NOT LIBMODBUS_FOUND)
        # 回退：直接查找系统路径
        find_path(LIBMODBUS_INCLUDE_DIR
            NAMES modbus.h
            PATHS /usr/include/modbus /usr/include /usr/local/include
        )
        find_library(LIBMODBUS_LIBRARY
            NAMES modbus
            PATHS /usr/lib /usr/local/lib /lib
        )
    endif()

    include(FindPackageHandleStandardArgs)
    find_package_handle_standard_args(LibModbus DEFAULT_MSG
        LIBMODBUS_LIBRARY LIBMODBUS_INCLUDE_DIRS)

    if(LibModbus_FOUND AND NOT TARGET LibModbus::LibModbus)
        add_library(LibModbus::LibModbus SHARED IMPORTED)
        set_target_properties(LibModbus::LibModbus PROPERTIES
            IMPORTED_LOCATION "${LIBMODBUS_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${LIBMODBUS_INCLUDE_DIRS}"
        )
        message(STATUS "Found libmodbus: ${LIBMODBUS_LIBRARY}")
    endif()
endif()
