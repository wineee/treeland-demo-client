cmake_minimum_required(VERSION 3.16)

project(treeland_demo_client LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
find_package(SDL3 REQUIRED CONFIG)

pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
pkg_get_variable(WAYLAND_SCANNER wayland-scanner wayland_scanner)
if(NOT WAYLAND_SCANNER)
    find_program(WAYLAND_SCANNER_EXECUTABLE wayland-scanner REQUIRED)
    set(WAYLAND_SCANNER "${WAYLAND_SCANNER_EXECUTABLE}")
endif()

set(PROTOCOL_XML "${CMAKE_CURRENT_SOURCE_DIR}/protocols/xdg-foreign-unstable-v2.xml")
set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(PROTOCOL_HEADER "${GENERATED_DIR}/xdg-foreign-unstable-v2-client-protocol.h")
set(PROTOCOL_CODE "${GENERATED_DIR}/xdg-foreign-unstable-v2-protocol.c")

file(MAKE_DIRECTORY "${GENERATED_DIR}")

add_custom_command(
    OUTPUT "${PROTOCOL_HEADER}"
    COMMAND "${WAYLAND_SCANNER}" client-header "${PROTOCOL_XML}" "${PROTOCOL_HEADER}"
    DEPENDS "${PROTOCOL_XML}"
    VERBATIM
)

add_custom_command(
    OUTPUT "${PROTOCOL_CODE}"
    COMMAND "${WAYLAND_SCANNER}" private-code "${PROTOCOL_XML}" "${PROTOCOL_CODE}"
    DEPENDS "${PROTOCOL_XML}"
    VERBATIM
)

add_executable(treeland-demo-client
    src/main.c
    "${PROTOCOL_HEADER}"
    "${PROTOCOL_CODE}"
)

target_include_directories(treeland-demo-client PRIVATE "${GENERATED_DIR}")
target_link_libraries(treeland-demo-client PRIVATE SDL3::SDL3 PkgConfig::WAYLAND_CLIENT)
