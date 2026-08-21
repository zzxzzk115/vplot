# vplot, for CMake consumers.
#
# xmake remains the development build: it drives the examples, the tests and the
# differential harness, and it is what CI runs. This file exists so that a CMake
# project can CONSUME the library without adopting xmake -- through CPM.cmake,
# FetchContent, add_subdirectory, or an installed find_package(vplot).
#
# Deliberately it builds ONE thing: the library and its vendored Agg. Keeping
# the overlap between the two build systems down to a single static library is
# what stops them drifting apart, and the examples need packages (VRI, ImGui)
# that only exist in the xmake repo anyway.
#
#     CPMAddPackage("gh:zzxzzk115/vplot@0.1.0")
#     target_link_libraries(app PRIVATE vplot::vplot)

cmake_minimum_required(VERSION 3.21)

project(vplot
    VERSION 0.1.2
    DESCRIPTION "Publication-quality plotting behind a pure C ABI"
    HOMEPAGE_URL "https://github.com/zzxzzk115/vplot"
    LANGUAGES C CXX)

option(VPLOT_INSTALL "Generate the install and find_package rules"
       ${PROJECT_IS_TOP_LEVEL})
option(VPLOT_FETCH_DEPENDENCIES
       "Download FreeType and zlib when the system has neither" OFF)

# ---------------------------------------------------------------------------
# Dependencies: FreeType for glyphs, zlib for PNG's deflate. Nothing else.
#
# A consumer that already has them -- vcpkg, Conan, its own FetchContent -- has
# them used as they are; the download is opt-in, because pulling the network
# into somebody else's build without asking is rude.
# ---------------------------------------------------------------------------
if(NOT TARGET freetype)
    find_package(Freetype QUIET)
endif()
if(NOT TARGET ZLIB::ZLIB AND NOT TARGET zlibstatic)
    find_package(ZLIB QUIET)
endif()

set(_vplot_missing "")
if(NOT TARGET freetype AND NOT Freetype_FOUND)
    list(APPEND _vplot_missing FreeType)
endif()
if(NOT TARGET ZLIB::ZLIB AND NOT TARGET zlibstatic AND NOT ZLIB_FOUND)
    list(APPEND _vplot_missing zlib)
endif()

set(_vplot_fetched FALSE)

if(_vplot_missing AND VPLOT_FETCH_DEPENDENCIES)
    include(FetchContent)
    set(_vplot_fetched TRUE)

    if(NOT TARGET ZLIB::ZLIB AND NOT TARGET zlibstatic AND NOT ZLIB_FOUND)
        set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(zlib
            GIT_REPOSITORY https://github.com/madler/zlib.git
            GIT_TAG v1.3.1
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(zlib)
        # zlib exports no namespaced target and generates zconf.h into the build
        # tree, so the include path has to name both directories.
        target_include_directories(zlibstatic INTERFACE
            $<BUILD_INTERFACE:${zlib_SOURCE_DIR}>
            $<BUILD_INTERFACE:${zlib_BINARY_DIR}>)
    endif()

    if(NOT TARGET freetype AND NOT Freetype_FOUND)
        FetchContent_Declare(freetype
            GIT_REPOSITORY https://github.com/freetype/freetype.git
            GIT_TAG VER-2-13-3
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(freetype)
    endif()
elseif(_vplot_missing)
    string(JOIN ", " _vplot_missing_text ${_vplot_missing})
    message(FATAL_ERROR
        "vplot needs FreeType and zlib, and could not find "
        "${_vplot_missing_text}. Either provide it -- vcpkg, Conan, a system "
        "package, your own FetchContent -- or configure with "
        "-DVPLOT_FETCH_DEPENDENCIES=ON to have vplot download it.")
endif()

# ---------------------------------------------------------------------------
# Anti-Grain Geometry, vendored from matplotlib's extern/agg24-svn.
#
# Not a package: matplotlib patches this snapshot, and a different Agg changes
# antialiasing coverage at the pixel level -- which is precisely what the
# differential tests measure. See external/xmake.lua for the long version.
# ---------------------------------------------------------------------------
file(GLOB VPLOT_AGG_SOURCES CONFIGURE_DEPENDS external/agg24-svn/src/*.cpp)

add_library(vplot_agg OBJECT ${VPLOT_AGG_SOURCES})
target_include_directories(vplot_agg PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external/agg24-svn/include>)
set_target_properties(vplot_agg PROPERTIES POSITION_INDEPENDENT_CODE ON)
# Vendored 2005-era C++: its warnings are not ours to fix, and a -Werror in a
# parent scope must not fail the build over them.
target_compile_options(vplot_agg PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/w>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-w>)

# ---------------------------------------------------------------------------
# The library.
#
# Globbed with CONFIGURE_DEPENDS on purpose: an explicit list here would be a
# second copy of source/xmake.lua's, and the two WOULD drift. The glob cannot.
# ---------------------------------------------------------------------------
file(GLOB VPLOT_SOURCES CONFIGURE_DEPENDS
    source/vplot/abi/*.cpp
    source/vplot/core/*.cpp
    source/vplot/render/*.cpp
    source/vplot/render/mpl/*.cpp
    source/vplot/text/*.cpp)

add_library(vplot STATIC ${VPLOT_SOURCES})
add_library(vplot::vplot ALIAS vplot)

# The public C ABI is the only thing consumers see; the C++23 implementation
# behind it is PRIVATE, its header directories included.
target_include_directories(vplot
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        source/vplot/core
        source/vplot/render
        source/vplot/render/mpl
        source/vplot/text)

target_compile_features(vplot PRIVATE cxx_std_23)
set_target_properties(vplot PROPERTIES
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON)

target_link_libraries(vplot PRIVATE vplot_agg)
if(TARGET freetype)
    target_link_libraries(vplot PRIVATE freetype)
else()
    target_link_libraries(vplot PRIVATE Freetype::Freetype)
endif()
if(TARGET ZLIB::ZLIB)
    target_link_libraries(vplot PRIVATE ZLIB::ZLIB)
else()
    target_link_libraries(vplot PRIVATE zlibstatic)
endif()

target_compile_options(vplot PRIVATE
    # Without /Zc:__cplusplus that macro reads 199711L and the C++23 code stops
    # compiling; /bigobj because the lifted matplotlib translation units are
    # large; C5055 comes out of Agg's colour math, which our headers pull in.
    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus;/bigobj;/EHsc;/wd5055>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fexceptions>)

# ---------------------------------------------------------------------------
# Install.
#
# The font travels with the library: FontEngine::load_default_face looks for
# assets/fonts/DejaVuSans.ttf relative to the working directory, and will keep
# needing to until it is embedded in the archive.
# ---------------------------------------------------------------------------
if(VPLOT_INSTALL AND _vplot_fetched)
    # A static library defers its dependencies to whoever links it, so an
    # INSTALLED vplot names FreeType and zlib on the consumer's link line --
    # and a downloaded copy inside this build tree is not something they can
    # find. Fetching is for embedding (CPM, add_subdirectory); installing needs
    # dependencies the consumer can resolve too.
    message(STATUS
        "vplot: dependencies were downloaded, so the install rules are off. "
        "Provide FreeType and zlib (vcpkg, Conan, a system package) to install.")
    set(VPLOT_INSTALL OFF)
endif()

if(VPLOT_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    install(TARGETS vplot vplot_agg
        EXPORT vplotTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(DIRECTORY include/vpl DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(DIRECTORY assets/fonts
        DESTINATION ${CMAKE_INSTALL_DATADIR}/vplot/assets)

    install(EXPORT vplotTargets
        FILE vplotTargets.cmake
        NAMESPACE vplot::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/vplot)

    configure_package_config_file(
        cmake/vplotConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/vplotConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/vplot)
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/vplotConfigVersion.cmake
        COMPATIBILITY SameMajorVersion)

    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/vplotConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/vplotConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/vplot)
endif()
