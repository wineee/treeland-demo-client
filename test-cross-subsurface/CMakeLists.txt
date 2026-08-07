cmake_minimum_required(VERSION 3.16)

project(test_cross_subsurface LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
find_package(SDL3 REQUIRED CONFIG)
find_package(TreelandProtocols REQUIRED CONFIG)

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
set(XDG_SHELL_XML "${WAYLAND_PROTOCOLS_PREFIX}/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml")
set(CROSS_SUBSURFACE_XML "${TREELAND_PROTOCOLS_DATA_DIR}/treeland-cross-subsurface-unstable-v1.xml")

if(NOT EXISTS "${XDG_SHELL_XML}")
    message(FATAL_ERROR "xdg-shell.xml not found: ${XDG_SHELL_XML}")
endif()
if(NOT EXISTS "${CROSS_SUBSURFACE_XML}")
    message(FATAL_ERROR "treeland cross-subsurface protocol not found: ${CROSS_SUBSURFACE_XML}")
endif()

# ── xdg-shell ──────────────────────────────────────────────────────────────
set(XDG_SHELL_H   "${GENERATED_DIR}/xdg-shell-client-protocol.h")
set(XDG_SHELL_C   "${GENERATED_DIR}/xdg-shell-protocol.c")

add_custom_command(OUTPUT "${XDG_SHELL_H}"
    COMMAND "${WAYLAND_SCANNER}" client-header "${XDG_SHELL_XML}" "${XDG_SHELL_H}"
    DEPENDS "${XDG_SHELL_XML}" VERBATIM)

add_custom_command(OUTPUT "${XDG_SHELL_C}"
    COMMAND "${WAYLAND_SCANNER}" private-code  "${XDG_SHELL_XML}" "${XDG_SHELL_C}"
    DEPENDS "${XDG_SHELL_XML}" VERBATIM)

# ── treeland-cross-subsurface-unstable-v1 ─────────────────────────────────
set(CROSS_SUBSURFACE_H   "${GENERATED_DIR}/treeland-cross-subsurface-unstable-v1-client-protocol.h")
set(CROSS_SUBSURFACE_C   "${GENERATED_DIR}/treeland-cross-subsurface-unstable-v1-protocol.c")

add_custom_command(OUTPUT "${CROSS_SUBSURFACE_H}"
    COMMAND "${WAYLAND_SCANNER}" client-header "${CROSS_SUBSURFACE_XML}" "${CROSS_SUBSURFACE_H}"
    DEPENDS "${CROSS_SUBSURFACE_XML}" VERBATIM)

add_custom_command(OUTPUT "${CROSS_SUBSURFACE_C}"
    COMMAND "${WAYLAND_SCANNER}" private-code  "${CROSS_SUBSURFACE_XML}" "${CROSS_SUBSURFACE_C}"
    DEPENDS "${CROSS_SUBSURFACE_XML}" VERBATIM)

# ── shared protocol sources ────────────────────────────────────────────────
set(PROTO_SRCS
    "${XDG_SHELL_H}"
    "${XDG_SHELL_C}"
    "${CROSS_SUBSURFACE_H}"
    "${CROSS_SUBSURFACE_C}"
)

# ── parent executable ───────────────────────────────────────────────────────
add_executable(test-cross-subsurface
    src/parent.c
    src/common.c
    ${PROTO_SRCS}
)

target_include_directories(test-cross-subsurface PRIVATE
    "${GENERATED_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(test-cross-subsurface
    PRIVATE
        SDL3::SDL3
        PkgConfig::WAYLAND_CLIENT
        m
)

# ── child executable (no SDL — pure Wayland + stdin) ────────────────
add_executable(test-cross-subsurface-child
    src/child.c
    src/common.c
    ${PROTO_SRCS}
)

target_include_directories(test-cross-subsurface-child PRIVATE
    "${GENERATED_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(test-cross-subsurface-child
    PRIVATE
        PkgConfig::WAYLAND_CLIENT
)
