-- set project name
set_project("vplot")

-- set project version
set_version("0.1.1")

-- set language version: C++ 23
set_languages("cxx23")

-- root ?
local is_root = (os.projectdir() == os.scriptdir())
set_config("root", is_root)
set_config("project_dir", os.scriptdir())

-- global options
option("vplot_build_examples") -- build examples?
    set_default(true)
    set_showmenu(true)
    set_description("Enable vplot examples")
option_end()

option("vplot_build_tests") -- build tests?
    set_default(true)
    set_showmenu(true)
    set_description("Enable vplot tests")
option_end()

-- if build on windows
if is_plat("windows") then
    add_cxxflags("/Zc:__cplusplus", {tools = {"msvc", "cl"}}) -- fix __cplusplus == 199711L error
    add_cxxflags("/bigobj") -- avoid big obj
    add_cxxflags("-D_SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING")
    add_cxxflags("/EHsc")

    -- MSVC runtime: static (MT/MTd) by default, to match the VRI / libvultra
    -- package ecosystem. Those packages are built MT; mixing runtimes fails at
    -- link time with LNK2038. Change this one variable if a project genuinely
    -- needs the dynamic runtime (and make sure its packages are MD too).
    local msvc_runtime = is_mode("debug") and "MTd" or "MT"
    set_runtimes(msvc_runtime)

    -- Propagate that runtime to every resolved package.
    --
    -- This is the half that is easy to forget: set_runtimes() only configures the
    -- project's own targets. Without the line below, packages build with their own
    -- default (usually MD) and then refuse to link into an MT target -- the same
    -- LNK2038, now with a confusing cause because set_runtimes() *looks* like it
    -- should have covered it.
    add_requireconfs("**", {configs = {runtimes = msvc_runtime}})
else
    add_cxxflags("-fexceptions")
end

-- add rules
rule("clangd.config")
    on_config(function (target)
        if is_host("windows") then
            os.cp(".clangd.win", ".clangd")
        else
            os.cp(".clangd.nowin", ".clangd")
        end
    end)
rule_end()

add_rules("mode.debug", "mode.release")
add_rules("plugin.vsxmake.autoupdate")
add_rules("plugin.compile_commands.autoupdate", {outputdir = ".vscode", lsp = "clangd"})
add_rules("clangd.config")

-- add repositories
add_repositories("my-xmake-repo https://github.com/zzxzzk115/xmake-repo.git backup")

-- Web (Emscripten) build: VRI defaults to Vulkan, which does not exist on wasm,
-- so enable the WebGPU and WebGL2 backends instead. VriGraphicsAPI_Auto then
-- picks WebGPU first and falls back to WebGL2.
if is_plat("wasm") then
    add_requires("vri", {configs = {vulkan = false, gl = true, wgpu = true}})
end

-- include external libraries
includes("external")

-- include source
includes("source")

-- developer tools (the demo exporter)
includes("tools")

-- include tests
if has_config("vplot_build_tests") then
    includes("tests")
end

-- if build examples, then include examples
if has_config("vplot_build_examples") then
    includes("examples")
end