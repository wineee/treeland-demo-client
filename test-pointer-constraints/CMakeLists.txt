cmake_minimum_required(VERSION 3.16)

project(test_pointer_constraints LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
find_package(SDL3 REQUIRED CONFIG)

pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(WAYLAND_PROTOCOLS REQUIRED wayland-protocols)
pkg_get_variable(WAYLAND_SCANNER wayland-scanner wayland_scanner)
if(NOT WAYLAND_SCANNER)
    find_program(WAYLAND_SCANNER_EXECUTABLE wayland-scanner REQUIRED)
    set(WAYLAND_SCANNER "${WAYLAND_SCANNER_EXECUTABLE}")
endif()

set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GENERATED_DIR}")

# ── protocol XML from system packages ─────────────────────────────────────
set(PC_XML "${WAYLAND_PROTOCOLS_PREFIX}/share/wayland-protocols/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml")
set(RP_XML "${WAYLAND_PROTOCOLS_PREFIX}/share/wayland-protocols/unstable/relative-pointer/relative-pointer-unstable-v1.xml")

if(NOT EXISTS "${PC_XML}")
    message(FATAL_ERROR "pointer-constraints protocol not found: ${PC_XML}")
endif()
if(NOT EXISTS "${RP_XML}")
    message(FATAL_ERROR "relative-pointer protocol not found: ${RP_XML}")
endif()

# ── pointer-constraints-unstable-v1 ────────────────────────────────────────
set(PC_H   "${GENERATED_DIR}/pointer-constraints-unstable-v1-client-protocol.h")
set(PC_C   "${GENERATED_DIR}/pointer-constraints-unstable-v1-protocol.c")

add_custom_command(OUTPUT "${PC_H}"
    COMMAND "${WAYLAND_SCANNER}" client-header "${PC_XML}" "${PC_H}"
    DEPENDS "${PC_XML}" VERBATIM)

add_custom_command(OUTPUT "${PC_C}"
    COMMAND "${WAYLAND_SCANNER}" private-code  "${PC_XML}" "${PC_C}"
    DEPENDS "${PC_XML}" VERBATIM)

# ── relative-pointer-unstable-v1 ────────────────────────────────────────────
set(RP_H   "${GENERATED_DIR}/relative-pointer-unstable-v1-client-protocol.h")
set(RP_C   "${GENERATED_DIR}/relative-pointer-unstable-v1-protocol.c")

add_custom_command(OUTPUT "${RP_H}"
    COMMAND "${WAYLAND_SCANNER}" client-header "${RP_XML}" "${RP_H}"
    DEPENDS "${RP_XML}" VERBATIM)

add_custom_command(OUTPUT "${RP_C}"
    COMMAND "${WAYLAND_SCANNER}" private-code  "${RP_XML}" "${RP_C}"
    DEPENDS "${RP_XML}" VERBATIM)

# ── executable ─────────────────────────────────────────────────────────────
add_executable(test-pointer-constraints
    src/main.c
    "${PC_H}"
    "${PC_C}"
    "${RP_H}"
    "${RP_C}"
)

target_include_directories(test-pointer-constraints PRIVATE "${GENERATED_DIR}")
target_link_libraries(test-pointer-constraints
    PRIVATE
        SDL3::SDL3
        PkgConfig::WAYLAND_CLIENT
        m
)