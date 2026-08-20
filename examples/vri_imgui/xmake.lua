-- vplot rendered into an ImGui panel via VRI.
--
-- The dependency runs one way -- plotting library -> render hardware interface
-- -- so VRI is consumed as a package rather than this example living in VRI's
-- tree. The cost of that choice is examples/common/, copied from VRI since its
-- example scaffolding is not part of the installed package.

target("example-vri_imgui")
    set_kind("binary")
    set_default(false)

    add_deps("vplot")
    add_packages("vri", "imgui", "libsdl3")

    add_files("main.cpp")
target_end()
