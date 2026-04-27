cmake_minimum_required(VERSION 3.16)

project(test_wine_window_management LANGUAGES C)

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

set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GENERATED_DIR}")

# ── xdg-shell ──────────────────────────────────────────────────────────────
set(XDG_SHELL_XML "${CMAKE_CURRENT_SOURCE_DIR}/protocols/xdg-shell.xml")
set(XDG_SHELL_H   "${GENERATED_DIR}/xdg-shell-client-protocol.h")
set(XDG_SHELL_C   "${GENERATED_DIR}/xdg-shell-protocol.c")

add_custom_command(OUTPUT "${XDG_SHELL_H}"
    COMMAND "${WAYLAND_SCANNER}" client-header "${XDG_SHELL_XML}" "${XDG_SHELL_H}"
    DEPENDS "${XDG_SHELL_XML}" VERBATIM)

add_custom_command(OUTPUT "${XDG_SHELL_C}"
    COMMAND "${WAYLAND_SCANNER}" private-code  "${XDG_SHELL_XML}" "${XDG_SHELL_C}"
    DEPENDS "${XDG_SHELL_XML}" VERBATIM)

# ── treeland-wine-window-management-unstable-v1 ────────────────────────────
set(WINE_XML "${CMAKE_CURRENT_SOURCE_DIR}/protocols/treeland-wine-window-management-unstable-v1.xml")
set(WINE_H   "${GENERATED_DIR}/treeland-wine-window-management-unstable-v1-client-protocol.h")
set(WINE_C   "${GENERATED_DIR}/treeland-wine-window-management-unstable-v1-protocol.c")

add_custom_command(OUTPUT "${WINE_H}"
    COMMAND "${WAYLAND_SCANNER}" client-header "${WINE_XML}" "${WINE_H}"
    DEPENDS "${WINE_XML}" VERBATIM)

add_custom_command(OUTPUT "${WINE_C}"
    COMMAND "${WAYLAND_SCANNER}" private-code  "${WINE_XML}" "${WINE_C}"
    DEPENDS "${WINE_XML}" VERBATIM)

# ── executable ─────────────────────────────────────────────────────────────
add_executable(test-wine-window-management
    src/main.c
    "${XDG_SHELL_H}"
    "${XDG_SHELL_C}"
    "${WINE_H}"
    "${WINE_C}"
)

target_include_directories(test-wine-window-management PRIVATE "${GENERATED_DIR}")
target_link_libraries(test-wine-window-management
    PRIVATE
        SDL3::SDL3
        PkgConfig::WAYLAND_CLIENT
)
