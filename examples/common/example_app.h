// example_app.h - shared host scaffolding for the VRI examples.
//
// Backend selection (VRI_API env / ?backend= URL / Auto), windowing (SDL3 on desktop,
// the page #canvas on the web), device + swapchain + an optional depth target, the
// present loop (desktop while-loop / emscripten main loop), headless auto-exit
// (VRI_MAX_FRAMES env / ?frames=N URL) with a pixel self-check + BMP capture - all live
// here so a backend/platform fix lands once for every example instead of being copy-pasted
// (and missed) across the three mains.
//
// An example fills in only what differs: it creates its resources/pipeline after Init(),
// sets onUpdate (per-frame uploads) + onRecord (pipeline + draw inside the pass), then
// Run(). The frame skeleton (acquire -> barriers -> begin/clear -> record -> present) is
// shared. Header-only; examples only.
#pragma once

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#else
#include <SDL3/SDL.h>
#include <imgui_impl_sdl3.h> // desktop input; on web we feed ImGuiIO via Emscripten
#include <vri/integration/vri_sdl3.h>
#endif

#include <imgui.h>
#include <vri/vri.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <vector>
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
#include <filesystem> // auto-detect the Wayland socket so `xmake run` works from an SSH shell
#endif

#include "capture.h"

namespace vriex
{
    // VRI_API (vulkan|webgpu|opengl|d3d12|metal|software) or, on the web, ?backend=webgpu|webgl
    // override the backend; otherwise VriGraphicsAPI_Auto lets VRI pick (WebGPU first, then WebGL).
    inline VriGraphicsAPI SelectApi()
    {
        const char*    env = std::getenv("VRI_API");
        VriGraphicsAPI api = VriGraphicsAPI_Auto;
        if (env && std::strcmp(env, "webgpu") == 0)
            api = VriGraphicsAPI_WebGPU;
        else if (env && (std::strcmp(env, "opengl") == 0 || std::strcmp(env, "gl") == 0))
            api = VriGraphicsAPI_OpenGL;
        else if (env && (std::strcmp(env, "d3d12") == 0 || std::strcmp(env, "dx12") == 0))
            api = VriGraphicsAPI_D3D12;
        else if (env && std::strcmp(env, "vulkan") == 0)
            api = VriGraphicsAPI_Vulkan;
        else if (env && (std::strcmp(env, "metal") == 0 || std::strcmp(env, "mtl") == 0))
            api = VriGraphicsAPI_Metal;
        else if (env && (std::strcmp(env, "software") == 0 || std::strcmp(env, "sw") == 0))
            api = VriGraphicsAPI_Software; // CPU rendering via a software Vulkan ICD (SwiftShader)
#if defined(__EMSCRIPTEN__)
        const int b = EM_ASM_INT({
            var s = location.search;
            if (s.indexOf('backend=webgpu') >= 0)
                return 1;
            if (s.indexOf('backend=webgl') >= 0 || s.indexOf('backend=opengl') >= 0)
                return 2;
            return 0;
        });
        if (b == 1)
            api = VriGraphicsAPI_WebGPU;
        else if (b == 2)
            api = VriGraphicsAPI_OpenGL;
#endif
        return api;
    }

    // Default diagnostic sink: every backend routes its native validation/diagnostics (VK
    // debug-utils, D3D12 InfoQueue, GL KHR_debug, WebGPU uncaptured errors) plus VRI's own
    // validation layer through this one callback. The examples install it so problems print
    // instead of vanishing; a real app would plug in its own logger here.
    inline void VRI_CALL DefaultMessageCallback(void*, VriMessageSeverity severity, const char* message)
    {
        const char* s = severity == VriMessageSeverity_Error   ? "ERROR" :
                        severity == VriMessageSeverity_Warning ? "WARN" :
                                                                 "INFO";
        std::fprintf(stderr, "[VRI][%s] %s\n", s, message);
        std::fflush(stderr);
    }

    // VRI_MAX_FRAMES env (desktop) or ?frames=N URL (web): auto-exit after N frames.
    inline uint64_t QueryMaxFrames()
    {
        const char* env = std::getenv("VRI_MAX_FRAMES");
        uint64_t    n   = env ? std::strtoull(env, nullptr, 10) : 0;
#if defined(__EMSCRIPTEN__)
        const int f = EM_ASM_INT({
            var s = location.search;
            var i = s.indexOf('frames=');
            return i >= 0 ? (parseInt(s.substring(i + 7)) | 0) : 0;
        });
        if (f > 0)
            n = static_cast<uint64_t>(f);
#endif
        return n;
    }

    // VRI_PRESENT (fifo|relaxed|mailbox|immediate) picks the swapchain present mode. Default: Fifo
    // (vsync) - universally supported and warning-free. Set VRI_PRESENT=mailbox for smooth, low-latency
    // presentation with no tearing (renders uncapped). The web build always uses Fifo (the browser paces).
    inline VriPresentMode SelectPresentMode()
    {
#if defined(__EMSCRIPTEN__)
        return VriPresentMode_Fifo;
#else
        const char* env = std::getenv("VRI_PRESENT");
        if (env)
        {
            if (std::strcmp(env, "fifo") == 0 || std::strcmp(env, "vsync") == 0)
                return VriPresentMode_Fifo;
            if (std::strcmp(env, "relaxed") == 0)
                return VriPresentMode_FifoRelaxed;
            if (std::strcmp(env, "mailbox") == 0)
                return VriPresentMode_Mailbox;
            if (std::strcmp(env, "immediate") == 0 || std::strcmp(env, "novsync") == 0)
                return VriPresentMode_Immediate;
        }
        return VriPresentMode_Fifo;
#endif
    }

    // Fill a ShaderVariants {ptr, size} slot from an embedded blob symbol. The D3D12 (DXBC/DXIL)
    // headers are only #included on Windows, so VRI_SHADER_D3D12 drops the symbol entirely off
    // Windows (the slot is never read there, since useDxbc is false) - the same construction then
    // compiles on every platform.
#define VRI_SHADER_BLOB(sym) (sym), sizeof(sym)
#if defined(_WIN32)
#define VRI_SHADER_D3D12(sym) (sym), sizeof(sym)
#else
#define VRI_SHADER_D3D12(sym) nullptr, 0
#endif

    struct ExampleApp
    {
        // ---- config (override before/at Init) ----
        const char* name  = "example";
        uint32_t    width = 640, height = 480;
        VriFormat   swapFormat      = VriFormat_BGRA8_UNORM;
        VriFormat   depthFormat     = VriFormat_D32_SFLOAT;
        bool        hasDepth        = false;
        uint64_t    requestFeatures = 0; // VriFeatureBits to request at device creation (bestEffort)
        float       clearColor[4]   = {0.08f, 0.10f, 0.14f, 1.0f};

        // Real seconds elapsed since the previous frame (measured per platform). Examples scale their
        // animation by this so motion is frame-rate independent - identical wall-clock speed whether a
        // backend runs at 60, 45, or 144 fps. In headless capture (maxFrames != 0) it is pinned to
        // 1/60 so frame N is deterministic (reproducible pixel self-check / BMP). Also fed to ImGui as
        // io.DeltaTime, so the on-screen FPS is the real rate (not a hardcoded 60).
        float dt = 1.0f / 60.0f;

        // ---- valid after Init() ----
        VriDevice*            dev = nullptr;
        VriCoreInterface      c {};
        VriSwapChainInterface swap {};
        VriQueue*             queue     = nullptr;
        VriSwapChain*         swapchain = nullptr;
        VriTexture*           depth     = nullptr;
        VriDescriptor*        depthView = nullptr;
        VriCommandAllocator*  alloc     = nullptr;
        VriCommandBuffer*     cmd       = nullptr;
        VriFence*             fence     = nullptr;
        VriGraphicsAPI        api       = VriGraphicsAPI_Auto;
        const char*           apiName   = "Vulkan";
        char apiLabel[64] = {}; // backs apiName for the GL family ("OpenGL 4.6" / "OpenGL ES 3.1" / "WebGL2 (ES 3.0)")
        bool useWgsl = false, useDxbc = false;

        // VRI takes raw per-stage bytecode; which blob to feed is a backend choice - SPIR-V for
        // Vulkan/OpenGL/Metal (the GL/Metal backends transpile it), WGSL for WebGPU, DXBC or DXIL
        // for D3D12. Bundle a shader's per-backend blobs in a ShaderVariants and let Shader() pick,
        // so examples stop repeating the `useDxbc ? .. : useWgsl ? wgsl : spv` ladder per shader.
        // (Picking *which* shader - e.g. a software vs hardware variant - stays the example's job.)
        struct ShaderVariants
        {
            const void* spv      = nullptr;
            size_t      spvLen   = 0; // Vulkan / OpenGL / Metal (transpiled)
            const void* wgsl     = nullptr;
            size_t      wgslLen  = 0; // WebGPU
            const void* d3d12    = nullptr;
            size_t      d3d12Len = 0; // D3D12 (DXBC sm5.1 or DXIL sm6.x)
        };
        VriShaderDesc Shader(VriShaderStageBits stage, const char* entry, const ShaderVariants& v) const
        {
            VriShaderDesc d {};
            d.stage          = stage;
            d.entryPointName = entry;
            if (useDxbc)
            {
                d.bytecode     = v.d3d12;
                d.bytecodeSize = v.d3d12Len;
            }
            else if (useWgsl)
            {
                d.bytecode     = v.wgsl;
                d.bytecodeSize = v.wgslLen;
            }
            else
            {
                d.bytecode     = v.spv;
                d.bytecodeSize = v.spvLen;
            }
            return d;
        }

        // ---- the example fills these in before Run() ----
        // onUpdate: CPU-side per-frame work (e.g. write a staging buffer). Runs BEFORE the
        //   swapchain image is acquired - important on WebGPU, where a buffer map does an
        //   ASYNCIFY yield that must not sit between acquire and present.
        // onPreRender: record copies/barriers (e.g. staging -> constant buffer) after the
        //   command buffer begins, before the render pass opens.
        // onRecord: record pipeline + draw inside the render pass.
        std::function<void(uint64_t)>          onUpdate;
        std::function<void(VriCommandBuffer*)> onPreRender;
        std::function<void(VriCommandBuffer*)> onRecord;
        // onGui: build the ImGui UI for the frame (between NewFrame and Render). The example
        // adds widgets that drive its variables; a backend/FPS overlay is always shown.
        std::function<void()> onGui;

        // ---- internal ----
        VriBuffer*                            captureBuf  = nullptr;
        const char*                           capturePath = nullptr;
        std::vector<VriBuffer*>               uploadStaging; // staging buffers held until EndUpload() frees them
        uint64_t                              frameValue = 1;
        uint64_t                              maxFrames  = 0;
        bool                                  depthInit  = false;
        bool                                  running    = true;
        std::chrono::steady_clock::time_point lastFrameTime {};
        bool                                  haveFrameTime = false; // for real dt
        // VRI's built-in ImGui renderer (VRI_INTERFACE_IMGUI). The app owns the ImGui environment
        // (context/input/NewFrame/Render) and each frame flattens ImDrawData into guiData.
        VriImguiInterface                guiApi {};
        VriImgui*                        gui = nullptr;
        std::vector<VriImguiVertex>      guiVerts;
        std::vector<uint16_t>            guiIndices;
        std::vector<VriImguiDrawCommand> guiCmds;
        VriImguiDrawData                 guiData {};
        ImDrawData*                      guiDraw       = nullptr;
        bool                             guiReady      = false; // ImGui renderer + this frame's draw data
        bool                             wantViewports = false; // multi-viewport (detached OS windows)
#if !defined(__EMSCRIPTEN__)
        SDL_Window* window = nullptr;
#endif

        [[noreturn]] void Fail(const char* msg)
        {
            std::fprintf(stderr, "[%s] %s\n", name, msg);
            std::exit(1);
        }

#if defined(__linux__) && !defined(__EMSCRIPTEN__)
        // Make `xmake run example-*` reach the screen on a Wayland box (e.g. a Raspberry Pi)
        // regardless of which shell it's launched from. Whenever there's a live Wayland session,
        // lock SDL to the Wayland driver and drop any DISPLAY, because:
        //   * a local terminal usually has BOTH WAYLAND_DISPLAY and DISPLAY=:0 set, and SDL may
        //     otherwise pick X11 (XWayland) - which here doesn't present, so you'd have to
        //     `unset DISPLAY` by hand;
        //   * an SSH shell has neither, and a leftover `export DISPLAY=:0` makes SDL hang on a
        //     dead XWayland.
        // The session is "live Wayland" if WAYLAND_DISPLAY is set, or a compositor socket
        // (XDG_RUNTIME_DIR/wayland-N) exists. An explicit SDL_VIDEODRIVER always wins.
        static void AutoDetectDisplay()
        {
            if (std::getenv("SDL_VIDEODRIVER"))
                return; // explicit driver choice wins

            bool haveWayland = std::getenv("WAYLAND_DISPLAY") != nullptr;
            if (!haveWayland)
            {
                if (const char* rt = std::getenv("XDG_RUNTIME_DIR"))
                {
                    std::error_code ec;
                    for (const auto& entry : std::filesystem::directory_iterator(rt, ec))
                    {
                        const std::string fn = entry.path().filename().string();
                        if (fn.rfind("wayland-", 0) == 0 && fn.find(".lock") == std::string::npos &&
                            std::filesystem::is_socket(entry.path(), ec))
                        {
                            setenv("WAYLAND_DISPLAY", fn.c_str(), 1);
                            haveWayland = true;
                            break;
                        }
                    }
                }
            }
            if (haveWayland)
            {
                setenv("SDL_VIDEODRIVER", "wayland", 1); // we're on Wayland -> don't let SDL pick X11
                unsetenv("DISPLAY");                     // and don't let SDL/EGL fall back to a dead :0
            }
        }
#endif

        // The depth target's aspect: depth, plus stencil when depthFormat is a combined format (so an
        // example can ask for a stencil buffer just by setting a D*S* depthFormat before Init).
        static bool DepthFormatHasStencil(VriFormat f)
        {
            return f == VriFormat_D24_UNORM_S8_UINT || f == VriFormat_D32_SFLOAT_S8_UINT || f == VriFormat_S8_UINT;
        }
        VriImageAspectFlags DepthAspect() const
        {
            return VriImageAspect_Depth | (DepthFormatHasStencil(depthFormat) ? VriImageAspect_Stencil : 0u);
        }

        void SetClearColor(float r, float g, float b, float a = 1.0f)
        {
            clearColor[0] = r;
            clearColor[1] = g;
            clearColor[2] = b;
            clearColor[3] = a;
        }

        // Bring up backend + window + device + swapchain (+ depth). The example creates its
        // own resources afterwards using dev / c / useWgsl / useDxbc, then calls Run().
        void Init(const char* exampleName, uint32_t w, uint32_t h, bool depthWanted)
        {
            name     = exampleName;
            width    = w;
            height   = h;
            hasDepth = depthWanted;
            api      = SelectApi();
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
            AutoDetectDisplay(); // make `xmake run` work straight from an SSH shell on a Wayland box
#endif
#if !defined(__EMSCRIPTEN__)
            if (!SDL_Init(SDL_INIT_VIDEO))
                Fail("SDL_Init failed");
#endif
            // Create the window BEFORE the device: the native OpenGL ES backend on Wayland needs
            // the app's wl_display at device-creation time (its EGL context and the present
            // surface must share one connection). Other backends/platforms ignore nativeDisplay
            // and build their surface from the window handle at swapchain time. The title is set
            // once the backend is resolved (it names the API), so start with the example name.
            VriWindowHandle wh {};
            const void*     nativeDisplay = nullptr;
#if defined(__EMSCRIPTEN__)
            wh.type                = VriWindowSystem_Web;
            wh.handle.web.selector = "#canvas";
#else
            window = SDL_CreateWindow(name, static_cast<int>(width), static_cast<int>(height), 0);
            if (!window)
                Fail("SDL_CreateWindow failed");
            wh = vriWindowHandleFromSDL3(window);
            if (wh.type == VriWindowSystem_Wayland)
                nativeDisplay = wh.handle.wayland.display;
#endif

            VriDeviceCreationDesc dd {};
            dd.graphicsAPI      = api;
            dd.enableValidation = VRI_TRUE;
            dd.bestEffort       = VRI_TRUE;
            dd.enabledFeatures  = requestFeatures; // bestEffort: ungranted features just leave the matching hasX false
            dd.nativeDisplay    = nativeDisplay;   // used by the native OpenGL ES backend on Wayland; ignored otherwise
            static VriCallbackInterface cb {};
            cb.MessageCallback   = DefaultMessageCallback;
            dd.callbackInterface = &cb;
            if (vriCreateDevice(&dd, &dev) != VriResult_Success)
                Fail("vriCreateDevice failed");
            if (vriGetInterface(dev, VRI_INTERFACE_CORE, sizeof(c), &c) != VriResult_Success ||
                vriGetInterface(dev, VRI_INTERFACE_SWAPCHAIN, sizeof(swap), &swap) != VriResult_Success)
                Fail("vriGetInterface failed");

            // Depth format fallback: the requested format may be unsupported on this device
            // (e.g. Apple/MoltenVK has no D24_UNORM_S8_UINT). Substitute a supported format
            // with the same aspects so the depth target, its view, and pipelines all agree.
            if (hasDepth && !(c.GetFormatSupport(dev, depthFormat) & VriFormatSupport_DepthStencil))
            {
                const bool      needStencil  = DepthFormatHasStencil(depthFormat);
                const VriFormat candidates[] = {
                    needStencil ? VriFormat_D32_SFLOAT_S8_UINT : VriFormat_D32_SFLOAT,
                    needStencil ? VriFormat_D24_UNORM_S8_UINT : VriFormat_D16_UNORM,
                };
                for (VriFormat cand : candidates)
                {
                    if (c.GetFormatSupport(dev, cand) & VriFormatSupport_DepthStencil)
                    {
                        depthFormat = cand;
                        break;
                    }
                }
            }

            const VriDeviceDesc* devDesc = c.GetDeviceDesc(dev);
            api                          = devDesc->graphicsAPI;         // Auto resolves to a concrete backend
            useWgsl                      = api == VriGraphicsAPI_WebGPU; // GL consumes SPIR-V (transpiled), like Vulkan
            useDxbc                      = api == VriGraphicsAPI_D3D12;
            // Label the API, distinguishing desktop OpenGL / OpenGL ES / WebGL and showing the
            // version the driver actually gave (e.g. "OpenGL 4.6", "OpenGL ES 3.1", "WebGL2 (ES 3.0)").
            const unsigned glMaj = devDesc->apiVersionMajor, glMin = devDesc->apiVersionMinor;
            if (api == VriGraphicsAPI_OpenGL)
            {
                std::snprintf(apiLabel, sizeof(apiLabel), "OpenGL %u.%u", glMaj, glMin);
                apiName = apiLabel;
            }
            else if (api == VriGraphicsAPI_OpenGLES)
            {
#if defined(__EMSCRIPTEN__)
                std::snprintf(apiLabel, sizeof(apiLabel), "WebGL2 (ES %u.%u)", glMaj, glMin); // WebGL2 == GLES 3.0
#else
                std::snprintf(apiLabel, sizeof(apiLabel), "OpenGL ES %u.%u", glMaj, glMin);
#endif
                apiName = apiLabel;
            }
            else
            {
                apiName = api == VriGraphicsAPI_WebGPU   ? "WebGPU" :
                          api == VriGraphicsAPI_D3D12    ? "D3D12" :
                          api == VriGraphicsAPI_Metal    ? "Metal" :
                          api == VriGraphicsAPI_Software ? "Software (CPU)" :
                                                           "Vulkan";
            }

            // Now the backend is known, label the window/page with it.
#if defined(__EMSCRIPTEN__)
            // Feed the custom shell (examples/common/shell.html) the example name + backend.
            EM_ASM(
                {
                    if (Module.vriSetTitle)
                        Module.vriSetTitle(UTF8ToString($0), UTF8ToString($1));
                },
                name,
                apiName);
#else
            char title[64];
            std::snprintf(title, sizeof(title), "VRI %s (%s)", name, apiName);
            SDL_SetWindowTitle(window, title);
            // A GL backend that brought up GLFW for its context (desktop) grabs activation, so
            // the SDL window can open unfocused/behind. Raise it to claim key + focus (harmless
            // no-op for the other backends and the GLFW-free native-ES build).
            SDL_RaiseWindow(window);
#endif
            c.GetQueue(dev, VriQueueType_Graphics, 0, &queue);
            VriSwapChainDesc scd {};
            scd.window      = wh;
            scd.queue       = queue;
            scd.format      = swapFormat;
            scd.width       = width;
            scd.height      = height;
            scd.textureNum  = 3;
            scd.presentMode = SelectPresentMode();
            if (swap.CreateSwapChain(dev, &scd, &swapchain) != VriResult_Success)
                Fail("CreateSwapChain failed");

            if (hasDepth)
            {
                VriTextureDesc dtd {};
                dtd.type           = VriTextureType_2D;
                dtd.format         = depthFormat;
                dtd.width          = width;
                dtd.height         = height;
                dtd.depth          = 1;
                dtd.mipNum         = 1;
                dtd.layerNum       = 1;
                dtd.sampleNum      = 1;
                dtd.usage          = VriTextureUsage_DepthStencilAttachment;
                dtd.memoryLocation = VriMemoryLocation_Device;
                dtd.clearValue.depthStencil.depth =
                    1.0f; // matches the per-frame depth clear (below) for D3D12 fast-clear
                if (c.CreateTexture(dev, &dtd, &depth) != VriResult_Success)
                    Fail("depth CreateTexture failed");
                VriTextureViewDesc dvd {};
                dvd.texture  = depth;
                dvd.viewType = VriTextureViewType_2D;
                dvd.format   = VriFormat_Unknown;
                dvd.aspect   = DepthAspect();
                if (c.CreateTextureView(dev, &dvd, &depthView) != VriResult_Success)
                    Fail("depth view failed");
            }

            c.CreateCommandAllocator(dev, VriQueueType_Graphics, &alloc);
            c.CreateCommandBuffer(alloc, &cmd);
            c.CreateFence(dev, 0, &fence);

            // ---- Dear ImGui: context + VRI renderer + per-platform input ----
            ImGui::CreateContext();
            ImGui::StyleColorsDark();
            ImGui::GetIO().IniFilename = nullptr;                         // don't write imgui.ini next to the examples
            ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable; // dockable panels
            // Multi-viewport (detached OS windows) is opt-in (VRI_IMGUI_VIEWPORTS=1): it needs the
            // per-viewport swapchain + platform-callback wiring below and a real display to verify.
#if !defined(__EMSCRIPTEN__)
            // Multi-viewport (detached OS windows) is on by default on desktop; opt out with
            // VRI_IMGUI_VIEWPORTS=0. Never in headless capture (VRI_MAX_FRAMES / VRI_CAPTURE renders
            // one offscreen target + a pixel self-check, so detached windows would break it).
            const char* vpEnv    = std::getenv("VRI_IMGUI_VIEWPORTS");
            const bool  headless = std::getenv("VRI_MAX_FRAMES") || std::getenv("VRI_CAPTURE");
            wantViewports        = !headless && !(vpEnv && vpEnv[0] == '0');
            if (wantViewports)
                ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
#endif
#if defined(__EMSCRIPTEN__)
            InstallWebInput();
#else
            ImGui_ImplSDL3_InitForOther(window);
#endif
            // VRI's built-in ImGui renderer. VRI doesn't link ImGui, so the app hands it the font
            // atlas pixels and (each frame) the flattened draw data; one renderer works on every backend.
            if (vriGetInterface(dev, VRI_INTERFACE_IMGUI, sizeof(guiApi), &guiApi) != VriResult_Success)
                Fail("VRI_INTERFACE_IMGUI unavailable");
            unsigned char* fontPixels = nullptr;
            int            fontW = 0, fontH = 0;
            ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&fontPixels, &fontW, &fontH);
            VriImguiDesc gd {};
            gd.uploadQueue = queue;
            gd.colorFormat = swapFormat;
            gd.depthFormat = hasDepth ? depthFormat : VriFormat_Unknown;
            gd.fontAtlas   = fontPixels;
            gd.fontWidth   = uint32_t(fontW);
            gd.fontHeight  = uint32_t(fontH);
            if (guiApi.CreateImgui(dev, &gd, &gui) != VriResult_Success)
                Fail("CreateImgui failed");
            ImGui::GetIO().Fonts->SetTexID(reinterpret_cast<ImTextureID>(guiApi.GetImguiFontView(gui)));
#if !defined(__EMSCRIPTEN__)
            if (wantViewports)
                RegisterViewportRenderer();
#endif
        }

        // Flatten an ImDrawData (per-list vertex/index buffers + list-relative offsets) into the
        // neutral VriImguiDrawData the renderer consumes (one buffer each, global offsets). Static so
        // both the main window and each multi-viewport window can use it with their own buffers/size.
        static void FlattenDrawData(ImDrawData*                       dd,
                                    std::vector<VriImguiVertex>&      verts,
                                    std::vector<uint16_t>&            indices,
                                    std::vector<VriImguiDrawCommand>& cmds,
                                    VriImguiDrawData&                 out,
                                    uint32_t                          fbW,
                                    uint32_t                          fbH)
        {
            verts.clear();
            indices.clear();
            cmds.clear();
            out = {};
            if (!dd || dd->TotalVtxCount == 0)
                return;
            verts.reserve(size_t(dd->TotalVtxCount));
            indices.reserve(size_t(dd->TotalIdxCount));
            uint32_t vtxBase = 0, idxBase = 0;
            for (int n = 0; n < dd->CmdListsCount; ++n)
            {
                const ImDrawList* cl = dd->CmdLists[n];
                for (int v = 0; v < cl->VtxBuffer.Size; ++v)
                {
                    const ImDrawVert& s = cl->VtxBuffer.Data[v];
                    verts.push_back(VriImguiVertex {{s.pos.x, s.pos.y}, {s.uv.x, s.uv.y}, s.col});
                }
                for (int i = 0; i < cl->IdxBuffer.Size; ++i)
                    indices.push_back(static_cast<uint16_t>(cl->IdxBuffer.Data[i]));
                for (int i = 0; i < cl->CmdBuffer.Size; ++i)
                {
                    const ImDrawCmd& dc = cl->CmdBuffer[i];
                    if (dc.UserCallback)
                    {
                        dc.UserCallback(cl, &dc);
                        continue;
                    }
                    VriImguiDrawCommand o {};
                    o.clipRect[0]  = dc.ClipRect.x;
                    o.clipRect[1]  = dc.ClipRect.y;
                    o.clipRect[2]  = dc.ClipRect.z;
                    o.clipRect[3]  = dc.ClipRect.w;
                    o.indexCount   = dc.ElemCount;
                    o.indexOffset  = idxBase + dc.IdxOffset;          // into the concatenated index buffer
                    o.vertexOffset = int32_t(vtxBase + dc.VtxOffset); // index values stay list-relative
                    // The texture this command samples (we stored each VRI view as ImGui's texture id);
                    // routes the font and any user image to the renderer's per-command textureView.
                    o.textureView = reinterpret_cast<VriDescriptor*>(dc.GetTexID());
                    cmds.push_back(o);
                }
                vtxBase += uint32_t(cl->VtxBuffer.Size);
                idxBase += uint32_t(cl->IdxBuffer.Size);
            }
            out.vertices          = verts.data();
            out.vertexCount       = uint32_t(verts.size());
            out.indices           = indices.data();
            out.indexCount        = uint32_t(indices.size());
            out.indexSize         = sizeof(uint16_t);
            out.commands          = cmds.data();
            out.commandCount      = uint32_t(cmds.size());
            out.displayPos[0]     = dd->DisplayPos.x;
            out.displayPos[1]     = dd->DisplayPos.y;
            out.displaySize[0]    = dd->DisplaySize.x;
            out.displaySize[1]    = dd->DisplaySize.y;
            out.framebufferWidth  = fbW;
            out.framebufferHeight = fbH;
        }

        void BuildGuiDrawData(ImDrawData* dd)
        {
            FlattenDrawData(dd, guiVerts, guiIndices, guiCmds, guiData, width, height);
        }

#if !defined(__EMSCRIPTEN__)
        // ---- Multi-viewport: render ImGui's detached OS windows (opt-in, VRI_IMGUI_VIEWPORTS=1) ----
        // Each platform window ImGui spawns gets its own VRI swapchain + VriImguiViewport (geometry
        // buffers) + command machinery; the renderer's per-viewport buffers keep their staging from
        // clobbering the main window's. Wired into ImGuiPlatformIO's Renderer_* callbacks, which
        // ImGui::RenderPlatformWindowsDefault() drives once per extra window each frame.
        struct ViewportRD
        {
            ExampleApp*                      app        = nullptr;
            VriSwapChain*                    swapchain  = nullptr;
            VriImguiViewport*                gvp        = nullptr;
            VriCommandAllocator*             alloc      = nullptr;
            VriCommandBuffer*                cmd        = nullptr;
            VriFence*                        fence      = nullptr;
            uint64_t                         frameValue = 0;
            uint32_t                         w = 1, h = 1;
            std::vector<VriImguiVertex>      verts;
            std::vector<uint16_t>            indices;
            std::vector<VriImguiDrawCommand> cmds;
            VriImguiDrawData                 data {};
        };
        static inline ExampleApp* s_viewportApp = nullptr; // the single app the callbacks render with

        static void VpCreate(ImGuiViewport* vp)
        {
            ExampleApp* app = s_viewportApp;
            ViewportRD* rd  = new ViewportRD();
            rd->app         = app;
            rd->w           = uint32_t(vp->Size.x > 0 ? vp->Size.x : 1);
            rd->h           = uint32_t(vp->Size.y > 0 ? vp->Size.y : 1);
            // ImGui_ImplSDL3 stores the SDL *window id* in PlatformHandle (the native NSWindow/HWND is
            // in PlatformHandleRaw); resolve it back to the SDL_Window* the swapchain helper needs.
            SDL_Window* win =
                SDL_GetWindowFromID(static_cast<SDL_WindowID>(reinterpret_cast<uintptr_t>(vp->PlatformHandle)));
            if (win)
            {
                VriSwapChainDesc scd {};
                scd.window      = vriWindowHandleFromSDL3(win);
                scd.queue       = app->queue; // present on the same graphics queue as the main window
                scd.format      = app->swapFormat;
                scd.width       = rd->w;
                scd.height      = rd->h;
                scd.textureNum  = 3;
                scd.presentMode = SelectPresentMode();
                if (app->swap.CreateSwapChain(app->dev, &scd, &rd->swapchain) != VriResult_Success)
                    rd->swapchain = nullptr;
            }
            rd->gvp = app->guiApi.CreateImguiViewport(app->gui);
            app->c.CreateCommandAllocator(app->dev, VriQueueType_Graphics, &rd->alloc);
            app->c.CreateCommandBuffer(rd->alloc, &rd->cmd);
            app->c.CreateFence(app->dev, 0, &rd->fence);
            vp->RendererUserData = rd;
        }

        static void VpDestroy(ImGuiViewport* vp)
        {
            ViewportRD* rd = static_cast<ViewportRD*>(vp->RendererUserData);
            if (!rd)
                return;
            ExampleApp* app = rd->app;
            app->c.DeviceWaitIdle(app->dev);
            if (rd->gvp)
                app->guiApi.DestroyImguiViewport(rd->gvp);
            if (rd->fence)
                app->c.DestroyFence(rd->fence);
            if (rd->alloc)
                app->c.DestroyCommandAllocator(rd->alloc);
            if (rd->swapchain)
                app->swap.DestroySwapChain(rd->swapchain);
            delete rd;
            vp->RendererUserData = nullptr;
        }

        static void VpSetSize(ImGuiViewport* vp, ImVec2 size)
        {
            ViewportRD* rd = static_cast<ViewportRD*>(vp->RendererUserData);
            if (!rd || !rd->swapchain)
                return;
            rd->w = uint32_t(size.x > 0 ? size.x : 1);
            rd->h = uint32_t(size.y > 0 ? size.y : 1);
            rd->app->swap.Resize(rd->swapchain, rd->w, rd->h);
        }

        static void VpRender(ImGuiViewport* vp, void*)
        {
            ViewportRD* rd = static_cast<ViewportRD*>(vp->RendererUserData);
            if (!rd || !rd->swapchain)
                return;
            ExampleApp* app = rd->app;
            FlattenDrawData(vp->DrawData, rd->verts, rd->indices, rd->cmds, rd->data, rd->w, rd->h);
            app->guiApi.UploadImguiDataTo(rd->gvp, &rd->data);

            uint32_t index = 0;
            if (app->swap.AcquireNextTexture(rd->swapchain, nullptr, 0, &index) == VriResult_OutOfDate)
            {
                app->swap.Resize(rd->swapchain, rd->w, rd->h);
                return;
            }
            VriTexture* bb[8] = {};
            uint32_t    n     = 8;
            app->swap.GetSwapChainTextures(rd->swapchain, bb, &n);
            VriTexture*        backbuffer = bb[index];
            VriTextureViewDesc bvd {};
            bvd.texture           = backbuffer;
            bvd.viewType          = VriTextureViewType_2D;
            bvd.format            = VriFormat_Unknown;
            bvd.aspect            = VriImageAspect_Color;
            VriDescriptor* bbView = nullptr;
            app->c.CreateTextureView(app->dev, &bvd, &bbView);

            auto barrier = [&](VriAccessFlags        ba,
                               VriLayout             bl,
                               VriPipelineStageFlags bs,
                               VriAccessFlags        aa,
                               VriLayout             al,
                               VriPipelineStageFlags as) {
                VriTextureBarrierDesc b {};
                b.texture       = backbuffer;
                b.before.access = ba;
                b.before.layout = bl;
                b.before.stages = bs;
                b.after.access  = aa;
                b.after.layout  = al;
                b.after.stages  = as;
                b.aspect        = VriImageAspect_Color;
                VriBarrierGroupDesc g {};
                g.textures   = &b;
                g.textureNum = 1;
                app->c.CmdBarrier(rd->cmd, &g);
            };

            app->c.ResetCommandAllocator(rd->alloc);
            app->c.BeginCommandBuffer(rd->cmd);
            app->guiApi.CmdCopyImguiDataTo(rd->cmd, rd->gvp);
            barrier(VriAccess_None,
                    VriLayout_Undefined,
                    VriPipelineStage_None,
                    VriAccess_ColorAttachmentWrite,
                    VriLayout_ColorAttachment,
                    VriPipelineStage_ColorAttachmentOutput);
            VriAttachmentDesc rt {};
            rt.view = bbView;
            rt.loadOp =
                (vp->Flags & ImGuiViewportFlags_NoRendererClear) ? VriAttachmentLoadOp_Load : VriAttachmentLoadOp_Clear;
            rt.storeOp                 = VriAttachmentStoreOp_Store;
            rt.clearValue.color.f32[3] = 1.0f; // opaque black behind the UI
            VriAttachmentsDesc att {};
            att.colors            = &rt;
            att.colorNum          = 1;
            att.renderArea.width  = rd->w;
            att.renderArea.height = rd->h;
            att.layerNum          = 1;
            app->c.CmdBeginRendering(rd->cmd, &att);
            VriViewport vpr {0, 0, float(rd->w), float(rd->h), 0, 1};
            app->c.CmdSetViewports(rd->cmd, &vpr, 1);
            VriRect sc {0, 0, rd->w, rd->h};
            app->c.CmdSetScissors(rd->cmd, &sc, 1);
            app->guiApi.CmdDrawImguiTo(rd->cmd, app->gui, rd->gvp, &rd->data);
            app->c.CmdEndRendering(rd->cmd);
            barrier(VriAccess_ColorAttachmentWrite,
                    VriLayout_ColorAttachment,
                    VriPipelineStage_ColorAttachmentOutput,
                    VriAccess_None,
                    VriLayout_Present,
                    VriPipelineStage_AllCommands);
            app->c.EndCommandBuffer(rd->cmd);

            VriFenceSubmitDesc sig {};
            sig.fence  = rd->fence;
            sig.value  = ++rd->frameValue;
            sig.stages = VriPipelineStage_AllCommands;
            VriQueueSubmitDesc sub {};
            sub.commandBuffers   = &rd->cmd;
            sub.commandBufferNum = 1;
            sub.signalFences     = &sig;
            sub.signalFenceNum   = 1;
            app->c.QueueSubmit(app->queue, &sub);
            app->c.Wait(rd->fence, rd->frameValue);
            app->c.DestroyDescriptor(bbView);
        }

        static void VpSwap(ImGuiViewport* vp, void*)
        {
            ViewportRD* rd = static_cast<ViewportRD*>(vp->RendererUserData);
            if (rd && rd->swapchain)
                rd->app->swap.Present(rd->swapchain, nullptr, 0);
        }

        void RegisterViewportRenderer()
        {
            s_viewportApp = this;
            ImGui::GetIO().BackendFlags |= ImGuiBackendFlags_RendererHasViewports;
            ImGuiPlatformIO& pio       = ImGui::GetPlatformIO();
            pio.Renderer_CreateWindow  = VpCreate;
            pio.Renderer_DestroyWindow = VpDestroy;
            pio.Renderer_SetWindowSize = VpSetSize;
            pio.Renderer_RenderWindow  = VpRender;
            pio.Renderer_SwapBuffers   = VpSwap;
        }
#endif

        // ---- one-shot upload helper (kills the staging boilerplate examples kept repeating) --
        // Usage: BeginUpload(); UploadBuffer(...)/UploadTexture(...) ...; EndUpload();
        // Each Upload* creates a host staging buffer, records the copy + the
        // copy->read barrier, and EndUpload() submits, waits, and frees the staging.
        void BeginUpload()
        {
            c.ResetCommandAllocator(alloc);
            c.BeginCommandBuffer(cmd);
        }

        void UploadBuffer(VriBuffer*            dst,
                          const void*           data,
                          uint64_t              size,
                          VriAccessFlags        afterAccess,
                          VriPipelineStageFlags afterStage)
        {
            VriBufferDesc sd {};
            sd.size           = size;
            sd.usage          = VriBufferUsage_TransferSrc;
            sd.memoryLocation = VriMemoryLocation_HostUpload;
            VriBuffer* stg    = nullptr;
            c.CreateBuffer(dev, &sd, &stg);
            std::memcpy(c.MapBuffer(stg, 0, size), data, size);
            c.UnmapBuffer(stg);
            uploadStaging.push_back(stg);
            VriBufferCopyDesc cp {};
            cp.size = size;
            c.CmdCopyBuffer(cmd, dst, stg, &cp);
            VriBufferBarrierDesc bb {};
            bb.buffer        = dst;
            bb.before.access = VriAccess_CopyDestinationWrite;
            bb.before.stages = VriPipelineStage_Transfer;
            bb.after.access  = afterAccess;
            bb.after.stages  = afterStage;
            VriBarrierGroupDesc g {};
            g.buffers   = &bb;
            g.bufferNum = 1;
            c.CmdBarrier(cmd, &g);
        }

        // Upload `layers` images (each w*h, layerBytes each, contiguous in `data`) into a 2D or
        // 2D-array texture, leaving it sampleable. Backends handle row-pitch alignment internally.
        void
        UploadTexture(VriTexture* dst, const void* data, uint32_t w, uint32_t h, uint32_t layers, uint32_t layerBytes)
        {
            const uint64_t total = uint64_t(layerBytes) * layers;
            VriBufferDesc  sd {};
            sd.size           = total;
            sd.usage          = VriBufferUsage_TransferSrc;
            sd.memoryLocation = VriMemoryLocation_HostUpload;
            VriBuffer* stg    = nullptr;
            c.CreateBuffer(dev, &sd, &stg);
            std::memcpy(c.MapBuffer(stg, 0, total), data, total);
            c.UnmapBuffer(stg);
            uploadStaging.push_back(stg);
            VriTextureBarrierDesc tb {};
            tb.texture       = dst;
            tb.before.layout = VriLayout_Undefined;
            tb.before.stages = VriPipelineStage_None;
            tb.after.access  = VriAccess_CopyDestinationWrite;
            tb.after.layout  = VriLayout_CopyDestination;
            tb.after.stages  = VriPipelineStage_Transfer;
            tb.aspect        = VriImageAspect_Color;
            tb.layerNum      = layers;
            VriBarrierGroupDesc g0 {};
            g0.textures   = &tb;
            g0.textureNum = 1;
            c.CmdBarrier(cmd, &g0);
            for (uint32_t layer = 0; layer < layers; ++layer)
            {
                VriBufferTextureCopyDesc up {};
                up.bufferOffset      = uint64_t(layer) * layerBytes;
                up.texture.aspect    = VriImageAspect_Color;
                up.texture.baseLayer = layer;
                up.texture.layerNum  = 1;
                up.texture.width     = w;
                up.texture.height    = h;
                c.CmdUploadBufferToTexture(cmd, dst, stg, &up);
            }
            VriTextureBarrierDesc tb2 {};
            tb2.texture       = dst;
            tb2.before.access = VriAccess_CopyDestinationWrite;
            tb2.before.layout = VriLayout_CopyDestination;
            tb2.before.stages = VriPipelineStage_Transfer;
            tb2.after.access  = VriAccess_ShaderResourceRead;
            tb2.after.layout  = VriLayout_ShaderResource;
            tb2.after.stages  = VriPipelineStage_FragmentShader;
            tb2.aspect        = VriImageAspect_Color;
            tb2.layerNum      = layers;
            VriBarrierGroupDesc g1 {};
            g1.textures   = &tb2;
            g1.textureNum = 1;
            c.CmdBarrier(cmd, &g1);
        }

        void EndUpload()
        {
            c.EndCommandBuffer(cmd);
            VriFenceSubmitDesc sig {};
            sig.fence = fence;
            sig.value = 1; // before the first frame (frameValue starts at 1 -> first frame signals 2)
            VriQueueSubmitDesc sub {};
            sub.commandBuffers   = &cmd;
            sub.commandBufferNum = 1;
            sub.signalFences     = &sig;
            sub.signalFenceNum   = 1;
            c.QueueSubmit(queue, &sub);
            c.Wait(fence, 1);
            for (VriBuffer* stg : uploadStaging)
                c.DestroyBuffer(stg);
            uploadStaging.clear();
        }

        // Enable headless auto-exit + capture. Call after the example's resources exist.
        // VRI_CAPTURE writes a BMP (desktop); on the web ?frames=N triggers a pixel self-check.
        void SetupCapture()
        {
            maxFrames   = QueryMaxFrames();
            capturePath = std::getenv("VRI_CAPTURE");
            if (capturePath && maxFrames == 0)
                maxFrames = 40;
            bool want = capturePath != nullptr;
#if defined(__EMSCRIPTEN__)
            want = want || maxFrames != 0; // read the last frame back for the self-check
#endif
            if (want)
            {
                VriBufferDesc cb {};
                cb.size           = uint64_t(width) * height * 4;
                cb.usage          = VriBufferUsage_TransferDst;
                cb.memoryLocation = VriMemoryLocation_HostReadback;
                c.CreateBuffer(dev, &cb, &captureBuf);
            }
        }

        void Finish()
        {
            running = false;
#if defined(__EMSCRIPTEN__)
            emscripten_cancel_main_loop();
            std::printf(
                "[%s] %s: %llu frames presented\n", name, apiName, static_cast<unsigned long long>(frameValue - 1));
            std::fflush(stdout);
            // Close the tab when done (headless runs), deferred so emrun can POST the final
            // stdout (self-check + "frames presented") before the page goes away.
            emscripten_async_call(
                [](void*) {
                    // clang-format off
                    // Embedded JS: clang-format mangles the strict-inequality operator (!== -> != =), so keep this verbatim.
                    EM_ASM({ if (typeof window !== 'undefined' && window.close) window.close(); });
                    // clang-format on
                    emscripten_force_exit(0);
                },
                nullptr,
                1500);
#endif
        }

#if defined(__EMSCRIPTEN__)
        // Feed the canvas's mouse events to ImGui (no SDL3 on the web). Keyboard is omitted -
        // the example controls are mouse-driven sliders/checkboxes.
        void InstallWebInput()
        {
            emscripten_set_mousemove_callback(
                "#canvas", nullptr, EM_FALSE, [](int, const EmscriptenMouseEvent* e, void*) -> EM_BOOL {
                    ImGui::GetIO().AddMousePosEvent(float(e->targetX), float(e->targetY));
                    return EM_FALSE;
                });
            emscripten_set_mousedown_callback(
                "#canvas", nullptr, EM_FALSE, [](int, const EmscriptenMouseEvent* e, void*) -> EM_BOOL {
                    ImGui::GetIO().AddMouseButtonEvent(e->button == 1 ? 2 : e->button == 2 ? 1 : 0, true);
                    return EM_FALSE;
                });
            emscripten_set_mouseup_callback(
                "#canvas", nullptr, EM_FALSE, [](int, const EmscriptenMouseEvent* e, void*) -> EM_BOOL {
                    ImGui::GetIO().AddMouseButtonEvent(e->button == 1 ? 2 : e->button == 2 ? 1 : 0, false);
                    return EM_FALSE;
                });
            emscripten_set_wheel_callback(
                "#canvas", nullptr, EM_FALSE, [](int, const EmscriptenWheelEvent* e, void*) -> EM_BOOL {
                    ImGui::GetIO().AddMouseWheelEvent(0.0f, float(-e->deltaY) * 0.01f);
                    return EM_FALSE;
                });
        }
#endif

        // Measure real seconds since the previous frame into `dt`. Pinned to 1/60 in headless capture
        // (maxFrames != 0) so frame N is deterministic; clamped otherwise to absorb startup/stall spikes
        // (e.g. a tab switch) and to keep ImGui's required DeltaTime > 0.
        void UpdateDeltaTime()
        {
            const auto now  = std::chrono::steady_clock::now();
            float      real = 1.0f / 60.0f;
            if (haveFrameTime)
                real = std::chrono::duration<float>(now - lastFrameTime).count();
            lastFrameTime = now;
            haveFrameTime = true;
            if (maxFrames != 0)
            {
                dt = 1.0f / 60.0f;
                return;
            } // headless: fixed step for reproducible frames
            if (real < 1.0e-5f)
                real = 1.0e-5f;
            if (real > 0.1f)
                real = 0.1f;
            dt = real;
        }

        // Run ImGui new-frame + build the UI for this frame; leaves guiDraw ready for the renderer.
        void BeginGui()
        {
            ImGuiIO& io = ImGui::GetIO();
#if defined(__EMSCRIPTEN__)
            io.DisplaySize = ImVec2(float(width), float(height));
#else
            ImGui_ImplSDL3_NewFrame(); // pulls window size + accumulated input
#endif
            io.DeltaTime = dt; // real frame time (fixed 1/60 in headless) -> truthful on-screen FPS
            ImGui::NewFrame();
            // Anchor the panel to the main window. With multi-viewport on, ImGui window coordinates are
            // absolute virtual-desktop (screen) space, so a literal (8,8) would land at the screen
            // origin and spawn a detached window; the main viewport's WorkPos offsets it back onto the
            // main window (WorkPos is (0,0) without viewports, so single-viewport behaviour is unchanged).
            const ImGuiViewport* mainVp = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(ImVec2(mainVp->WorkPos.x + 8, mainVp->WorkPos.y + 8), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowBgAlpha(0.7f);
            if (ImGui::Begin(name, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
            {
                ImGui::Text("%s  -  %.1f FPS", apiName, io.Framerate);
                if (onGui)
                {
                    ImGui::Separator();
                    onGui();
                }
            }
            ImGui::End();
            ImGui::Render();
            guiDraw = ImGui::GetDrawData();
            BuildGuiDrawData(guiDraw);
            guiApi.UploadImguiData(gui, &guiData); // map+memcpy here (before acquire): may yield on WebGPU
        }

        void Frame()
        {
            UpdateDeltaTime(); // refresh dt before the UI + onUpdate use it
            BeginGui();        // build the UI first so a control change applies to this same frame
            if (onUpdate)
                onUpdate(frameValue); // CPU work (may yield on WebGPU) before acquire

            uint32_t index = 0;
            if (swap.AcquireNextTexture(swapchain, nullptr, 0, &index) == VriResult_OutOfDate)
            {
                swap.Resize(swapchain, width, height);
                return;
            }
            VriTexture* backbuffers[8] = {};
            uint32_t    count          = 8;
            swap.GetSwapChainTextures(swapchain, backbuffers, &count);
            VriTexture*        backbuffer = backbuffers[index];
            VriTextureViewDesc bvd {};
            bvd.texture           = backbuffer;
            bvd.viewType          = VriTextureViewType_2D;
            bvd.format            = VriFormat_Unknown;
            bvd.aspect            = VriImageAspect_Color;
            VriDescriptor* bbView = nullptr;
            c.CreateTextureView(dev, &bvd, &bbView);

            c.ResetCommandAllocator(alloc);
            c.BeginCommandBuffer(cmd);
            if (onPreRender)
                onPreRender(cmd);
            guiApi.CmdCopyImguiData(cmd, gui); // copy ImGui verts/indices (staged pre-acquire) before the pass

            VriTextureBarrierDesc bgr[2] {};
            bgr[0].texture       = backbuffer;
            bgr[0].before.layout = VriLayout_Undefined;
            bgr[0].before.stages = VriPipelineStage_None;
            bgr[0].after.access  = VriAccess_ColorAttachmentWrite;
            bgr[0].after.layout  = VriLayout_ColorAttachment;
            bgr[0].after.stages  = VriPipelineStage_ColorAttachmentOutput;
            bgr[0].aspect        = VriImageAspect_Color;
            uint32_t bn          = 1;
            if (hasDepth)
            {
                bgr[1].texture       = depth;
                bgr[1].before.layout = depthInit ? VriLayout_DepthStencilAttachment : VriLayout_Undefined;
                bgr[1].before.stages = VriPipelineStage_None;
                bgr[1].after.access  = VriAccess_DepthStencilAttachmentWrite;
                bgr[1].after.layout  = VriLayout_DepthStencilAttachment;
                bgr[1].after.stages  = VriPipelineStage_EarlyFragmentTests;
                bgr[1].aspect        = DepthAspect();
                bn                   = 2;
            }
            VriBarrierGroupDesc g {};
            g.textures   = bgr;
            g.textureNum = bn;
            c.CmdBarrier(cmd, &g);
            depthInit = true;

            VriAttachmentDesc colorRT {};
            colorRT.view                    = bbView;
            colorRT.loadOp                  = VriAttachmentLoadOp_Clear;
            colorRT.storeOp                 = VriAttachmentStoreOp_Store;
            colorRT.clearValue.color.f32[0] = clearColor[0];
            colorRT.clearValue.color.f32[1] = clearColor[1];
            colorRT.clearValue.color.f32[2] = clearColor[2];
            colorRT.clearValue.color.f32[3] = clearColor[3];
            VriAttachmentDesc depthRT {};
            depthRT.view                          = depthView;
            depthRT.loadOp                        = VriAttachmentLoadOp_Clear;
            depthRT.storeOp                       = VriAttachmentStoreOp_DontCare;
            depthRT.clearValue.depthStencil.depth = 1.0f;
            VriAttachmentsDesc att {};
            att.colors   = &colorRT;
            att.colorNum = 1;
            if (hasDepth)
                att.depth = &depthRT;
            att.renderArea.width  = width;
            att.renderArea.height = height;
            att.layerNum          = 1;
            c.CmdBeginRendering(cmd, &att);
            VriViewport vp {0, 0, float(width), float(height), 0, 1};
            c.CmdSetViewports(cmd, &vp, 1);
            VriRect scis {0, 0, width, height};
            c.CmdSetScissors(cmd, &scis, 1);
            if (onRecord)
                onRecord(cmd);
            guiApi.CmdDrawImgui(cmd, gui, &guiData); // UI on top of the example
            c.CmdEndRendering(cmd);

            const bool capturing = captureBuf && maxFrames != 0 && frameValue >= maxFrames;
            if (capturing)
            {
                VriTextureBarrierDesc toSrc {};
                toSrc.texture       = backbuffer;
                toSrc.before.access = VriAccess_ColorAttachmentWrite;
                toSrc.before.layout = VriLayout_ColorAttachment;
                toSrc.before.stages = VriPipelineStage_ColorAttachmentOutput;
                toSrc.after.access  = VriAccess_CopySourceRead;
                toSrc.after.layout  = VriLayout_CopySource;
                toSrc.after.stages  = VriPipelineStage_Transfer;
                toSrc.aspect        = VriImageAspect_Color;
                VriBarrierGroupDesc gs {};
                gs.textures   = &toSrc;
                gs.textureNum = 1;
                c.CmdBarrier(cmd, &gs);
                VriBufferTextureCopyDesc rc {};
                rc.texture.aspect   = VriImageAspect_Color;
                rc.texture.layerNum = 1;
                c.CmdReadbackTextureToBuffer(cmd, captureBuf, backbuffer, &rc);
                VriTextureBarrierDesc toPresent {};
                toPresent.texture       = backbuffer;
                toPresent.before.access = VriAccess_CopySourceRead;
                toPresent.before.layout = VriLayout_CopySource;
                toPresent.before.stages = VriPipelineStage_Transfer;
                toPresent.after.layout  = VriLayout_Present;
                toPresent.after.stages  = VriPipelineStage_AllCommands;
                toPresent.aspect        = VriImageAspect_Color;
                VriBarrierGroupDesc gp {};
                gp.textures   = &toPresent;
                gp.textureNum = 1;
                c.CmdBarrier(cmd, &gp);
            }
            else
            {
                VriTextureBarrierDesc toPresent {};
                toPresent.texture       = backbuffer;
                toPresent.before.access = VriAccess_ColorAttachmentWrite;
                toPresent.before.layout = VriLayout_ColorAttachment;
                toPresent.before.stages = VriPipelineStage_ColorAttachmentOutput;
                toPresent.after.layout  = VriLayout_Present;
                toPresent.after.stages  = VriPipelineStage_AllCommands;
                toPresent.aspect        = VriImageAspect_Color;
                VriBarrierGroupDesc gp {};
                gp.textures   = &toPresent;
                gp.textureNum = 1;
                c.CmdBarrier(cmd, &gp);
            }
            c.EndCommandBuffer(cmd);

            VriFenceSubmitDesc sig {};
            sig.fence  = fence;
            sig.value  = ++frameValue;
            sig.stages = VriPipelineStage_AllCommands;
            VriQueueSubmitDesc sub {};
            sub.commandBuffers   = &cmd;
            sub.commandBufferNum = 1;
            sub.signalFences     = &sig;
            sub.signalFenceNum   = 1;
            c.QueueSubmit(queue, &sub);
            c.Wait(fence, frameValue);
            swap.Present(swapchain, nullptr, 0);
            c.DestroyDescriptor(bbView);

#if !defined(__EMSCRIPTEN__)
            // Render ImGui's detached windows (each into its own swapchain) after the main window.
            if (wantViewports)
            {
                ImGui::UpdatePlatformWindows();
                ImGui::RenderPlatformWindowsDefault();
            }
#endif

            if (capturing)
            {
                const uint8_t* px =
                    static_cast<const uint8_t*>(c.MapBuffer(captureBuf, 0, uint64_t(width) * height * 4));
#if defined(__EMSCRIPTEN__)
                // Headless self-check: count center pixels that differ from a corner (the
                // cleared background) -> the geometry actually drew. Channel order agnostic.
                const uint8_t* cor   = px + (size_t(8) * width + 8) * 4;
                const uint8_t* ctr   = px + (size_t(height / 2) * width + width / 2) * 4;
                int            drawn = 0;
                const int      x0 = width / 4, x1 = 3 * width / 4, y0 = height / 4, y1 = 3 * height / 4;
                // Also count DISTINCT colors over a coarse grid (quantized to 5 bits/channel):
                // "differs from bg" alone can't tell a correct multi-color image from one that
                // is mostly black (black differs from the bg too) - distinct-color count can.
                bool seen[32768] = {};
                int  distinct    = 0;
                for (int y = y0; y < y1; ++y)
                    for (int x = x0; x < x1; ++x)
                    {
                        const uint8_t* p  = px + (size_t(y) * width + x) * 4;
                        int            d0 = p[0] - cor[0], d1 = p[1] - cor[1], d2 = p[2] - cor[2];
                        if (d0 * d0 + d1 * d1 + d2 * d2 > 900)
                            ++drawn;
                        int key = ((p[0] >> 3) << 10) | ((p[1] >> 3) << 5) | (p[2] >> 3);
                        if (!seen[key])
                        {
                            seen[key] = true;
                            ++distinct;
                        }
                    }
                std::printf(
                    "[%s] %s: %d/%d center px differ from bg; %d distinct colors; center=%u,%u,%u corner=%u,%u,%u\n",
                    name,
                    apiName,
                    drawn,
                    (x1 - x0) * (y1 - y0),
                    distinct,
                    ctr[0],
                    ctr[1],
                    ctr[2],
                    cor[0],
                    cor[1],
                    cor[2]);
                std::fflush(stdout);
#else
                if (capturePath && WriteBmpBGRA(capturePath, px, width, height))
                    std::printf("[%s] %s: wrote %s\n", name, apiName, capturePath);
#endif
                c.UnmapBuffer(captureBuf);
            }
            if (maxFrames != 0 && frameValue - 1 >= maxFrames)
                Finish();
        }

        void Run()
        {
            std::printf("[%s] %s: rendering\n", name, apiName);
            std::fflush(stdout);
#if defined(__EMSCRIPTEN__)
            EM_ASM({
                if (Module.vriReady)
                    Module.vriReady();
            }); // reveal the canvas (hide the loading overlay)
            emscripten_set_main_loop_arg(
                [](void* p) { static_cast<ExampleApp*>(p)->Frame(); }, this, 0, true); // never returns
#else
            while (running)
            {
                SDL_Event e;
                while (SDL_PollEvent(&e))
                {
                    ImGui_ImplSDL3_ProcessEvent(&e); // feed mouse/keyboard to ImGui
                    if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
                        running = false;
                }
                if (!running)
                    break;
                Frame();
            }
            c.DeviceWaitIdle(dev);
            std::printf(
                "[%s] %s: %llu frames presented\n", name, apiName, static_cast<unsigned long long>(frameValue - 1));
#endif
        }

        // Tear down the base objects (desktop only; the web main loop never returns). The
        // example destroys its own resources before calling this.
        void Shutdown()
        {
#if !defined(__EMSCRIPTEN__)
            // Destroy ImGui's detached windows (calls VpDestroy for each) before the renderer/device.
            if (wantViewports)
                ImGui::DestroyPlatformWindows();
#endif
            if (gui)
                guiApi.DestroyImgui(gui);
#if !defined(__EMSCRIPTEN__)
            ImGui_ImplSDL3_Shutdown();
#endif
            ImGui::DestroyContext();
            if (fence)
                c.DestroyFence(fence);
            if (alloc)
                c.DestroyCommandAllocator(alloc);
            if (captureBuf)
                c.DestroyBuffer(captureBuf);
            if (depthView)
                c.DestroyDescriptor(depthView);
            if (depth)
                c.DestroyTexture(depth);
            if (swapchain)
                swap.DestroySwapChain(swapchain);
            if (dev)
                vriDestroyDevice(dev);
#if !defined(__EMSCRIPTEN__)
            if (window)
                SDL_DestroyWindow(window);
            SDL_Quit();
#endif
        }
    };
} // namespace vriex
