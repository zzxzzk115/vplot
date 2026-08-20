/*
 * vplot embedded in an ImGui panel, rendered through VRI.
 *
 * The embedding is straightforward: vplot rasterizes the figure to an RGBA
 * buffer on the CPU, that buffer is uploaded as a texture, and ImGui draws it --
 * the same approach matplotlib's Qt, GTK and Wx backends take. The on-screen
 * raster is identical to what a PDF or PNG export contains, so there is no
 * second rendering path to keep in sync.
 *
 * Re-rasterization happens only when the view changes, not per frame.
 */

#include "../common/example_app.h"

#include "vpl/vpl.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace
{

constexpr double kPi = 3.14159265358979323846;

void check(VplResult r, const char *what)
{
    if (r != VplResult_Success) {
        std::fprintf(stderr, "%s failed: %s (%s)\n", what, vplResultToString(r),
                     vplGetLastError());
        std::exit(1);
    }
}

std::string describe_save(VplResult r, const char *path)
{
    if (r == VplResult_Success) {
        return std::string("wrote ") + path;
    }
    return std::string("failed: ") + vplResultToString(r) + " (" + vplGetLastError() + ")";
}

} // namespace

int main(int, char **)
{
    /* ---- Build the figure through the C ABI ---------------------------- */

    VplFigureDesc figure_desc {};
    figure_desc.structSize = sizeof(figure_desc);
    figure_desc.widthInches = 6.4;
    figure_desc.heightInches = 4.8;
    figure_desc.dpi = 100.0;

    VplFigure *figure = nullptr;
    check(vplCreateFigure(&figure_desc, &figure), "vplCreateFigure");

    VplAxes *axes = nullptr;
    check(vplFigureAddSubplot(figure, 1, 1, 1, &axes), "vplFigureAddSubplot");

    std::vector<double> xs, sine, damped;
    for (int i = 0; i <= 400; ++i) {
        const double t = i * (4.0 * kPi) / 400.0;
        xs.push_back(t);
        sine.push_back(std::sin(t));
        damped.push_back(std::sin(t) * std::exp(-t / 8.0));
    }

    check(vplAxesPlot(axes, xs.data(), sine.data(), xs.size(), nullptr, nullptr),
          "vplAxesPlot(sine)");
    check(vplAxesPlot(axes, xs.data(), damped.data(), xs.size(), nullptr, nullptr),
          "vplAxesPlot(damped)");
    check(vplAxesSetTitle(axes, "vplot in VRI + ImGui"), "vplAxesSetTitle");
    check(vplAxesSetXLabel(axes, "t"), "vplAxesSetXLabel");
    check(vplAxesSetYLabel(axes, "amplitude"), "vplAxesSetYLabel");

    uint32_t fig_w = 0;
    uint32_t fig_h = 0;
    check(vplFigureGetPixelSize(figure, &fig_w, &fig_h), "vplFigureGetPixelSize");

    std::vector<uint8_t> pixels(static_cast<size_t>(fig_w) * fig_h * 4u);

    /* ---- Bring up VRI -------------------------------------------------- */

    static vriex::ExampleApp app;
    app.Init("vplot_imgui", 1280, 860, /*hasDepth*/ false);
    app.SetClearColor(0.11f, 0.11f, 0.13f);

    VriCoreInterface &c = app.c;

    VriTextureDesc texture_desc {};
    texture_desc.type = VriTextureType_2D;
    texture_desc.format = VriFormat_RGBA8_UNORM;
    texture_desc.width = fig_w;
    texture_desc.height = fig_h;
    texture_desc.depth = 1;
    texture_desc.mipNum = 1;
    texture_desc.layerNum = 1;
    texture_desc.sampleNum = 1;
    texture_desc.usage = VriTextureUsage_TransferDst | VriTextureUsage_ShaderResource;
    texture_desc.memoryLocation = VriMemoryLocation_Device;

    VriTexture *texture = nullptr;
    if (c.CreateTexture(app.dev, &texture_desc, &texture) != VriResult_Success) {
        app.Fail("CreateTexture failed");
    }

    VriTextureViewDesc view_desc {};
    view_desc.texture = texture;
    view_desc.viewType = VriTextureViewType_2D;
    view_desc.format = VriFormat_Unknown;
    view_desc.aspect = VriImageAspect_Color;

    VriDescriptor *texture_view = nullptr;
    if (c.CreateTextureView(app.dev, &view_desc, &texture_view) != VriResult_Success) {
        app.Fail("CreateTextureView failed");
    }

    /* ImGui identifies textures by the view; the descriptor set it caches stays
       valid as the texture's contents change, so re-uploading does not require
       re-registering. */
    const auto texture_id = static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(texture_view));

    bool needs_render = true;
    std::string export_status;

    /* ---- Frame hooks ---------------------------------------------------- */

    app.onGui = [&]() {
        ImGui::SetNextWindowSize(ImVec2(static_cast<float>(fig_w) + 16.0f,
                                        static_cast<float>(fig_h) + 96.0f),
                                 ImGuiCond_FirstUseEver);
        ImGui::Begin("vplot");

        ImGui::TextUnformatted("Drag to pan, wheel to zoom.");
        ImGui::SameLine();
        if (ImGui::Button("Autoscale")) {
            vplAxesAutoscale(axes, 1, VplAxisSel_Both, -1);
            needs_render = true;
        }

        /* Export writes whatever is on screen right now, including any pan or
           zoom -- the vector backends walk the same figure the raster one does,
           so there is no "what you see" versus "what you get" gap. */
        ImGui::SameLine();
        if (ImGui::Button("Export SVG")) {
            export_status = describe_save(vplFigureSaveFig(figure, "vplot_figure.svg", nullptr),
                                          "vplot_figure.svg");
        }
        ImGui::SameLine();
        if (ImGui::Button("Export PDF")) {
            export_status = describe_save(vplFigureSaveFig(figure, "vplot_figure.pdf", nullptr),
                                          "vplot_figure.pdf");
        }
        ImGui::SameLine();
        if (ImGui::Button("Export PNG @2x")) {
            /* 200 dpi for slides and posters, where the vector formats are
               awkward but the on-screen 100 dpi raster is too coarse. */
            VplSaveDesc desc {};
            desc.structSize = sizeof(desc);
            desc.format = VplFormat_PNG;
            desc.dpi = 200.0;
            export_status = describe_save(
                vplFigureSaveFig(figure, "vplot_figure@2x.png", &desc), "vplot_figure@2x.png");
        }

        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::Image(texture_id,
                     ImVec2(static_cast<float>(fig_w), static_cast<float>(fig_h)));

        /* vplot takes pixel coordinates with the origin at the image's top left
           and y increasing downward -- which is exactly what ImGui reports, so
           the mouse position passes straight through. */
        if (ImGui::IsItemHovered()) {
            const ImGuiIO &io = ImGui::GetIO();
            const double mx = io.MousePos.x - origin.x;
            const double my = io.MousePos.y - origin.y;

            if (io.MouseWheel != 0.0f) {
                vplAxesZoomAt(axes, mx, my, io.MouseWheel);
                needs_render = true;
            }
        }

        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            const ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
            if (delta.x != 0.0f || delta.y != 0.0f) {
                vplAxesPan(axes, 0.0, 0.0, delta.x, delta.y);
                ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
                needs_render = true;
            }
        }

        double xmin = 0.0, ymin = 0.0, xmax = 0.0, ymax = 0.0;
        vplAxesGetViewLimits(axes, &xmin, &ymin, &xmax, &ymax);
        ImGui::Text("x [%.3f, %.3f]   y [%.3f, %.3f]", xmin, xmax, ymin, ymax);

        if (!export_status.empty()) {
            ImGui::SameLine();
            ImGui::TextUnformatted(("|  " + export_status).c_str());
        }

        ImGui::End();
    };

    /* onUpdate runs after onGui and before the swapchain acquire, which is the
       right place for a blocking upload. */
    app.onUpdate = [&](uint64_t) {
        if (!needs_render) {
            return;
        }
        needs_render = false;

        const VplResult r =
            vplFigureRenderRGBA(figure, pixels.data(), pixels.size(), nullptr);
        if (r != VplResult_Success) {
            std::fprintf(stderr, "vplFigureRenderRGBA failed: %s (%s)\n",
                         vplResultToString(r), vplGetLastError());
            return;
        }

        app.BeginUpload();
        app.UploadTexture(texture, pixels.data(), fig_w, fig_h, 1,
                          fig_w * fig_h * 4u);
        app.EndUpload();
    };

    app.SetupCapture();
    app.Run();

    /* ---- Teardown ------------------------------------------------------- */

    app.guiApi.FreeImguiTexture(app.gui, texture_view);
    c.DestroyDescriptor(texture_view);
    c.DestroyTexture(texture);

    vplDestroyFigure(figure);
    app.Shutdown();
    return 0;
}
