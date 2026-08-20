# The smallest CMake project that uses vplot.
#
# Nothing here is built by xmake -- it exists to be built on its own, and to be
# the thing that proves the CMake path works:
#
#     cmake -S examples/cmake_consumer -B build/consumer -DVPLOT_FETCH_DEPENDENCIES=ON
#     cmake --build build/consumer --config Release
#
# Run it from the repository root, since the font is still loaded from
# assets/fonts at runtime.
#
# A real project would pull vplot in rather than sitting next to it:
#
#     include(CPM.cmake)
#     CPMAddPackage("gh:zzxzzk115/vplot@0.1.0")
#     target_link_libraries(app PRIVATE vplot::vplot)
#
# The source is C, not C++, because the header claims to be a pure C ABI and
# that claim is worth testing rather than asserting.

cmake_minimum_required(VERSION 3.21)
project(vplot_cmake_consumer LANGUAGES C)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../.. vplot-build)

add_executable(cmake_consumer main.c)
target_link_libraries(cmake_consumer PRIVATE vplot::vplot)
