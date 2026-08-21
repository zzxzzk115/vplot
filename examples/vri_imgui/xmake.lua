-- vplot rendered into an ImGui panel via VRI.
--
-- The dependency runs one way (plotting library -> render hardware interface),
-- so VRI is consumed as a package rather than this example living in VRI's tree.
-- examples/common/ is copied from VRI, whose example scaffolding is not part of
-- the installed package.

target("example-vri_imgui")
    set_kind("binary")
    set_default(false)

    add_deps("vplot")
    add_packages("vri", "imgui")

    add_files("main.cpp")

    if is_plat("wasm") then
        set_extension(".html")
        -- GROWABLE_ARRAYBUFFERS=0: the default (1) makes a growable heap a *resizable*
        -- ArrayBuffer, whose views Chrome 128+ rejects in WebGL bufferSubData / WebGPU
        -- writeBuffer. 0 keeps growth via copy-on-grow, whose views are accepted.
        add_ldflags("-sASYNCIFY", "-sALLOW_MEMORY_GROWTH=1", "-sGROWABLE_ARRAYBUFFERS=0",
                    "-sEXIT_RUNTIME=1", "-fexceptions", "--emrun", {force = true})
        add_ldflags("--shell-file=" .. path.join(os.scriptdir(), "..", "common", "shell.html"),
                    {force = true})
    else
        add_packages("libsdl3")
    end
target_end()
