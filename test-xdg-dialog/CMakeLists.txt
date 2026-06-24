cmake_minimum_required(VERSION 3.16)

project(test_xdg_dialog LANGUAGES C)

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
set(XDG_SHELL_XML "${WAYLAND_PROTOCOLS_PREFIX}/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml")
set(XDG_DIALOG_XML "${WAYLAND_PROTOCOLS_PREFIX}/share/wayland-protocols/staging/xdg-dialog/xdg-dialog-v1.xml")

if(NOT EXISTS "${XDG_SHELL_XML}")
    message(FATAL_ERROR "xdg-shell.xml not found: ${XDG_SHELL_XML}")
endif()
if(NOT EXISTS "${XDG_DIALOG_XML}")
    message(FATAL_ERROR "xdg-dialog-v1.xml not found: ${XDG_DIALOG_XML}")
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

# ── xdg-dialog-v1 ─────────────────────────────────────────────────────────
set(XDG_DIALOG_H   "${GENERATED_DIR}/xdg-dialog-v1-client-protocol.h")
set(XDG_DIALOG_C   "${GENERATED_DIR}/xdg-dialog-v1-protocol.c")

add_custom_command(OUTPUT "${XDG_DIALOG_H}"
    COMMAND "${WAYLAND_SCANNER}" client-header "${XDG_DIALOG_XML}" "${XDG_DIALOG_H}"
    DEPENDS "${XDG_DIALOG_XML}" VERBATIM)

add_custom_command(OUTPUT "${XDG_DIALOG_C}"
    COMMAND "${WAYLAND_SCANNER}" private-code  "${XDG_DIALOG_XML}" "${XDG_DIALOG_C}"
    DEPENDS "${XDG_DIALOG_XML}" VERBATIM)

# ── executable ─────────────────────────────────────────────────────────────
add_executable(test-xdg-dialog
    src/main.c
    "${XDG_SHELL_H}"
    "${XDG_SHELL_C}"
    "${XDG_DIALOG_H}"
    "${XDG_DIALOG_C}"
)

target_include_directories(test-xdg-dialog PRIVATE "${GENERATED_DIR}")
target_link_libraries(test-xdg-dialog
    PRIVATE
        SDL3::SDL3
        PkgConfig::WAYLAND_CLIENT
)

# Add a custom target to run with Wayland backend
add_custom_target(run-xdg-dialog
    COMMAND ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=wayland $<TARGET_FILE:test-xdg-dialog>
    DEPENDS test-xdg-dialog
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running XDG Dialog v1 test with Wayland backend"
)