-- External dependencies.

-- Anti-Grain Geometry, vendored from matplotlib's extern/agg24-svn.
--
-- Deliberately NOT the `agg` package in my xmake-repo: that one resolves to
-- aggeom/agg-2.6, which has diverged from the 2.4 snapshot matplotlib renders
-- against. matplotlib's own extern/agg24-svn/meson.build opens with
--
--     "We need a patched Agg not available elsewhere, so always use the
--      vendored version."
--
-- and ships agg_workaround.h on top of it. A different Agg changes antialiasing
-- coverage at the pixel level, which is exactly what the baseline-image
-- comparison in tests/ measures. Fidelity here is the whole reason this library
-- is C++ rather than Rust, so the vendored snapshot stays.
--
-- License: Anti-Grain Geometry 2.4 -- copy/use/modify/sell/distribute permitted
-- provided the copyright notice is retained. See external/agg24-svn/copying and
-- THIRD-PARTY-NOTICES.
--
-- Only the eight translation units matplotlib actually compiles are vendored;
-- the platform/, ctrl/ and X11/win32 support code is unused and omitted.
target("agg")
    set_kind("static")
    set_group("external")
    add_includedirs("agg24-svn/include", {public = true})
    add_files("agg24-svn/src/*.cpp")

    -- Vendored third-party code: don't let its warnings drown out ours, and
    -- don't let -Werror in a parent scope fail the build on 2005-era C++.
    if is_plat("windows") then
        add_cxxflags("/w", {tools = {"msvc", "cl"}})
    else
        add_cxxflags("-w")
    end
target_end()

-- FreeType: glyph rasterization and text metrics.
--
-- FreeType is dual licensed FTL / GPLv2; vplot takes the FTL (BSD-style) option,
-- which requires crediting the FreeType Project in documentation. Recorded in
-- THIRD-PARTY-NOTICES.
--
-- Note for anyone hitting a "cannot runv(meson --version), Permission denied"
-- here: that is not a meson bug. XMAKE_GLOBALDIR must point inside
-- CodeRepository, or xmake installs its package build tools somewhere this
-- machine's WDAC policy refuses to execute them.
add_requires("freetype", {system = false})

-- zlib: the deflate stream PNG mandates. Nothing else in vplot compresses, so
-- this is here purely for the PNG writer.
add_requires("zlib")

-- Used only by examples/vri_imgui. VRI is this project's sibling render
-- hardware interface; imgui and SDL3 come along for the example's window and UI.
-- Test-only: decodes matplotlib's baseline PNGs for the differential harness.
-- Public domain / MIT, and it never reaches the library.
if has_config("vplot_build_tests") then
    add_requires("stb")
end

if has_config("vplot_build_examples") then
    add_requires("vri", "libsdl3")
    -- Only the SDL3 *platform* backend is wanted: rendering goes through VRI's
    -- own ImGui extension (ext/vri_ext_imgui.h), not through an ImGui renderer
    -- backend, which is what keeps the example backend-agnostic across
    -- Vulkan/D3D12/Metal/WebGPU.
    -- The docking branch specifically: examples/common/example_app.h uses
    -- multi-viewport (ImGuiConfigFlags_ViewportsEnable, RendererUserData), which
    -- only exists there.
    add_requires("imgui v1.92.5-docking", {configs = {sdl3 = true}})
end
