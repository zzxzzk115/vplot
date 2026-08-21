-- The vplot demo browser: every supported feature, one panel each.

target("example-demo")
    set_kind("binary")
    set_default(false)

    add_deps("vplot")
    add_packages("vri", "imgui")

    add_files("main.cpp")

    if is_plat("wasm") then
        -- Web build: emitted as .html so emrun can host it; windowing comes from
        -- the GLFW Emscripten port (via VRI's GL backend), so no SDL3 here.
        set_extension(".html")
        -- ASYNCIFY for WebGPU's async device/adapter/map; -fexceptions for spirv-cross
        -- (the GL fallback). The default font is compiled in, so no --preload-file.
        -- GROWABLE_ARRAYBUFFERS=0: with the default (1) a growable heap becomes a
        -- *resizable* ArrayBuffer, and Chrome 128+ rejects resizable views passed to
        -- WebGL bufferSubData / WebGPU writeBuffer. 0 keeps memory growth via the old
        -- copy-on-grow path, whose views are non-resizable and accepted.
        add_ldflags("-sASYNCIFY", "-sALLOW_MEMORY_GROWTH=1", "-sGROWABLE_ARRAYBUFFERS=0",
                    "-sEXIT_RUNTIME=1", "-fexceptions", "--emrun", {force = true})
        add_ldflags("--shell-file=" .. path.join(os.scriptdir(), "..", "common", "shell.html"),
                    {force = true})
    else
        add_packages("libsdl3")
    end
target_end()
