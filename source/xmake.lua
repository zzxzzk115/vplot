-- vplot: the library itself.

target("vplot")
    set_kind("static")
    add_deps("agg")
    add_packages("freetype", "zlib")

    -- Public C ABI. This is the only thing consumers see.
    add_includedirs("$(projectdir)/include", {public = true})

    -- Internal C++23 implementation.
    add_includedirs("vplot/core", "vplot/render", "vplot/render/mpl", "vplot/text")
    add_files("vplot/abi/*.cpp", "vplot/core/*.cpp", "vplot/render/*.cpp",
              "vplot/render/mpl/*.cpp", "vplot/text/*.cpp")

    -- The lifted matplotlib sources are C++17-era code (matplotlib builds them
    -- with cpp_std=c++17) compiled here at C++23. Keep their warnings visible --
    -- unlike Agg they are code we now maintain -- but don't treat them as errors
    -- while the port is in progress.
    --
    -- C5055 (deprecated arithmetic between enumerations and floating-point) fires
    -- from Agg's own headers, which get pulled into our translation units, so the
    -- /w on the agg target cannot suppress it here. It is inherent to how Agg
    -- 2.4 writes its colour math; silencing just this one keeps our own warnings
    -- readable.
    if is_plat("windows") then
        add_cxxflags("/wd5055", {tools = {"msvc", "cl"}})
    end
target_end()
