-- The vplot demo browser: every supported feature, one panel each.

target("example-demo")
    set_kind("binary")
    set_default(false)

    add_deps("vplot")
    add_packages("vri", "imgui", "libsdl3")

    add_files("main.cpp")
target_end()
