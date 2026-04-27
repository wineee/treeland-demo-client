cmake_minimum_required(VERSION 3.16)

project(treeland-demos LANGUAGES C)

# ── sub-projects ───────────────────────────────────────────────────────────
# Each subdirectory has its own standalone CMakeLists.txt and can still be
# configured / built independently with:
#
#   cmake -S test-wine-window -B build-wine
#   cmake -S test-xdg-foreign  -B build-xdg-foreign
#
# To build everything at once from the repo root:
#
#   cmake -S . -B build
#   cmake --build build
# ──────────────────────────────────────────────────────────────────────────

add_subdirectory(test-wine-window)
add_subdirectory(test-xdg-foreign)
