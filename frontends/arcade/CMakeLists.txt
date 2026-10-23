cmake_minimum_required(VERSION 3.16)
project(octave_arcade C)

# OCTAVE Arcade: a C + SDL2 frontend over the service API. Builds
# warning-free with -Wall -Wextra; the vendored libraries are compiled
# with warnings off (they are not ours to fix).

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
find_package(Threads REQUIRED)

add_library(vendored STATIC third_party/cjson/cJSON.c src/stb_impl.c)
target_include_directories(vendored PUBLIC third_party/cjson third_party/stb)
target_compile_options(vendored PRIVATE -w)
target_link_libraries(vendored PUBLIC m)

add_executable(octave-arcade src/main.c src/link.c src/font.c src/ui.c src/art.c)
target_compile_options(octave-arcade PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(octave-arcade PRIVATE vendored PkgConfig::SDL2 Threads::Threads)
