cmake_minimum_required(VERSION 3.16)

project(test_subsurface LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
find_package(SDL3 REQUIRED CONFIG)

pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)

add_executable(test-subsurface
    main.c
)

target_link_libraries(test-subsurface
    PRIVATE
        SDL3::SDL3
        PkgConfig::WAYLAND_CLIENT
)
