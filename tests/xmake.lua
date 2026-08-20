-- Tests.
--
-- Plain executables asserting via exit code, driven by xmake's add_tests("default").
-- No doctest dependency yet: until the C ABI exists there is nothing to write
-- table-driven tests against, and the Stage 0 checks are single-scenario.

-- Uses only include/vpl/, which is the point: it proves the public ABI stands
-- on its own without any internal header.
target("test-export")
    set_kind("binary")
    set_default(false)
    add_deps("vplot")
    add_files("test_export.cpp")
    add_tests("default")
target_end()

-- The differential harness. Reports each case as skipped, rather than passing,
-- when tests/references/ has not been populated by tools/gen_references.py.
target("test-baseline")
    set_kind("binary")
    set_default(false)
    add_deps("vplot", "agg")
    -- stb_image is a test-only dependency: it decodes matplotlib's baseline
    -- PNGs so they can be compared pixel for pixel. The library itself has no
    -- image decoder and does not need one.
    add_packages("stb")
    add_includedirs("$(projectdir)/source/vplot/core",
                    "$(projectdir)/source/vplot/render",
                    "$(projectdir)/source/vplot/render/mpl",
                    "$(projectdir)/source/vplot/text")
    add_files("test_baseline.cpp")
    add_tests("default")
    if is_plat("windows") then
        add_cxxflags("/wd5055", {tools = {"msvc", "cl"}})
    end
target_end()

target("test-artists")
    set_kind("binary")
    set_default(false)
    add_deps("vplot")
    add_files("test_artists.cpp")
    add_tests("default")
target_end()

-- The C++ ergonomic layer (include/vpl/vpl.hpp), exercised the way a matplotlib
-- user writes it. Builds against the public headers only, so it also proves the
-- wrapper stands on its own over the C ABI.
target("test-hpp")
    set_kind("binary")
    set_default(false)
    add_deps("vplot")
    add_files("test_hpp.cpp")
    add_tests("default")
target_end()

-- The C++ wrapper coverage guard: every C ABI entry must be wrapped by vpl.hpp
-- or exempted with a reason. Reads both public headers; no library link needed.
target("test-hpp-coverage")
    set_kind("binary")
    set_default(false)
    add_files("test_hpp_coverage.cpp")
    add_tests("default")
target_end()

-- The demo catalogue guard. Reads include/vpl/vpl.h and examples/demo/demos.h
-- and fails if an ABI entry is shown by no panel and excused by no reason, so
-- the catalogue cannot fall behind the library the way it had.
target("test-demos")
    set_kind("binary")
    set_default(false)
    add_deps("vplot")
    add_includedirs("$(projectdir)/examples/demo")
    add_files("test_demos.cpp")
    add_tests("default")
target_end()

target("test-showcase")
    set_kind("binary")
    set_default(false)
    add_deps("vplot")
    add_files("test_showcase.cpp")
    add_tests("default")
target_end()

target("test-figure")
    set_kind("binary")
    set_default(false)
    add_deps("vplot", "agg")
    -- path.h pulls in vpl_adaptors.h, so the render core's dir comes along.
    -- Consumers of the finished library won't need any of this: the C ABI in
    -- include/vpl/ is the only surface they see.
    add_includedirs("$(projectdir)/source/vplot/core",
                    "$(projectdir)/source/vplot/render",
                    "$(projectdir)/source/vplot/render/mpl",
                    "$(projectdir)/source/vplot/text")
    add_files("test_figure.cpp")
    add_tests("default")
    if is_plat("windows") then
        add_cxxflags("/wd5055", {tools = {"msvc", "cl"}})
    end
target_end()

target("test-ticker")
    set_kind("binary")
    set_default(false)
    add_deps("vplot")
    add_includedirs("$(projectdir)/source/vplot/core")
    add_files("test_ticker.cpp")
    add_tests("default")
target_end()

target("test-smoke-draw-path")
    set_kind("binary")
    set_default(false)
    add_deps("vplot", "agg")

    -- The lifted render core is internal, so its include dir is not public on
    -- the vplot target; tests that drive it directly opt in here.
    add_includedirs("$(projectdir)/source/vplot/render/mpl")
    add_files("smoke_draw_path.cpp")

    add_tests("default")

    if is_plat("windows") then
        add_cxxflags("/wd5055", {tools = {"msvc", "cl"}})
    end
target_end()
