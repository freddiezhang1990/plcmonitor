# src/comm/CMakeLists.txt — 通讯模块构建配置
cmake_minimum_required(VERSION 3.16)

find_package(Qt6 REQUIRED COMPONENTS Core)

set(LIBMODBUS_SRC_DIR "${CMAKE_SOURCE_DIR}/third_party/libmodbus")

add_library(comm STATIC
    iconnector.h
    commmanager.h        commmanager.cpp
    s7connector.h        s7connector.cpp
    modbustcpconnector.h modbustcpconnector.cpp
    modbusrtuconnector.h modbusrtuconnector.cpp
    modbushelper.h

    # libmodbus 源码直接编译到 comm 库（无需独立 DLL）
    ${LIBMODBUS_SRC_DIR}/src/modbus.c
    ${LIBMODBUS_SRC_DIR}/src/modbus-data.c
    ${LIBMODBUS_SRC_DIR}/src/modbus-rtu.c
    ${LIBMODBUS_SRC_DIR}/src/modbus-tcp.c
)

target_link_libraries(comm
    PUBLIC
        Qt6::Core
        Snap7::Snap7
        common
)

target_include_directories(comm PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/common
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/third_party/snap7/include
    ${LIBMODBUS_SRC_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party/libmodbus
)

# libmodbus 使用标准 C 编译
set_source_files_properties(
    ${LIBMODBUS_SRC_DIR}/src/modbus.c
    ${LIBMODBUS_SRC_DIR}/src/modbus-data.c
    ${LIBMODBUS_SRC_DIR}/src/modbus-rtu.c
    ${LIBMODBUS_SRC_DIR}/src/modbus-tcp.c
    PROPERTIES LANGUAGE C
)

# 抑制 libmodbus C 文件的编译警告
if(MSVC)
    target_compile_options(comm PRIVATE /w)
else()
    target_compile_options(comm PRIVATE -w)
endif()

set_target_properties(comm PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)
