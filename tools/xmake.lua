-- Developer tools. Not part of the library.

target("export-demos")
    set_kind("binary")
    set_default(false)
    add_deps("vplot")
    add_files("export_demos.cpp")
target_end()

target("pen-probe")
    set_kind("binary")
    set_default(false)
    add_deps("vplot")
    add_includedirs("$(projectdir)/source/vplot/text", "$(projectdir)/source/vplot/render",
                    "$(projectdir)/source/vplot/core", "$(projectdir)/source/vplot/render/mpl")
    add_packages("freetype")
    add_files("pen_probe.cpp")
target_end()
