cmake_minimum_required(VERSION 3.21)
project(canvasinfinite CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
# hyprland.pc pulls in aquamarine/hyprutils/hyprlang/pixman/wayland/etc transitively.
pkg_check_modules(DEPS REQUIRED IMPORTED_TARGET
    hyprland
    pixman-1
    libdrm
    pangocairo
)

add_library(canvasinfinite SHARED
    src/main.cpp
    src/CanvasMode.cpp
)

target_compile_options(canvasinfinite PRIVATE -fPIC -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(canvasinfinite PRIVATE PkgConfig::DEPS)
set_target_properties(canvasinfinite PROPERTIES PREFIX "")  # -> canvasinfinite.so

install(TARGETS canvasinfinite
    LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/hyprland/plugins)
