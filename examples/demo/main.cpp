/*
 * The vplot demo browser -- one panel per feature, modeled on ImGui's own demo
 * window.
 *
 * Pick a demo on the left, tweak its parameters on the right, and the figure
 * re-rasterizes and re-uploads. The figures come from demos.h, the same builders
 * tools/export_demos.cpp uses, so the browser and the exported PNGs stay in sync.
 */

#include "../common/example_app.h"

#include "demos.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{

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
    VplDemoParams params;
    vplDemoDefaults(&params);

    int selected = 0;
    VplFigure *figure = nullptr;
    bool figure_dirty = true;
    bool texture_dirty = true;
    std::string export_status;

    auto rebuild = [&]() {
        if (figure != nullptr) {
            vplDestroyFigure(figure);
            figure = nullptr;
        }
        const VplResult r = vplBuildDemo(static_cast<VplDemoId>(selected), &params, &figure);
        if (r != VplResult_Success) {
            std::fprintf(stderr, "building demo failed: %s (%s)\n", vplResultToString(r),
                         vplGetLastError());
        }
        texture_dirty = true;
    };

    rebuild();
    figure_dirty = false;

    uint32_t fig_w = 0;
    uint32_t fig_h = 0;
    vplFigureGetPixelSize(figure, &fig_w, &fig_h);
    std::vector<uint8_t> pixels(static_cast<size_t>(fig_w) * fig_h * 4u);

    static vriex::ExampleApp app;
    app.Init("vplot_demo", 1400, 900, /*hasDepth*/ false);
    app.SetClearColor(0.10f, 0.10f, 0.12f);

    VriCoreInterface &c = app.c;

    VriTextureDesc td {};
    td.type = VriTextureType_2D;
    td.format = VriFormat_RGBA8_UNORM;
    td.width = fig_w;
    td.height = fig_h;
    td.depth = 1;
    td.mipNum = 1;
    td.layerNum = 1;
    td.sampleNum = 1;
    td.usage = VriTextureUsage_TransferDst | VriTextureUsage_ShaderResource;
    td.memoryLocation = VriMemoryLocation_Device;

    VriTexture *texture = nullptr;
    if (c.CreateTexture(app.dev, &td, &texture) != VriResult_Success) {
        app.Fail("CreateTexture failed");
    }

    VriTextureViewDesc vd {};
    vd.texture = texture;
    vd.viewType = VriTextureViewType_2D;
    vd.format = VriFormat_Unknown;
    vd.aspect = VriImageAspect_Color;

    VriDescriptor *view = nullptr;
    if (c.CreateTextureView(app.dev, &vd, &view) != VriResult_Success) {
        app.Fail("CreateTextureView failed");
    }
    const auto texture_id = static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(view));

    app.onGui = [&]() {
        ImGui::SetNextWindowSize(ImVec2(1340.0f, 840.0f), ImGuiCond_FirstUseEver);
        ImGui::Begin("vplot demo");

        /* ---- catalogue ---- */
        ImGui::BeginChild("catalogue", ImVec2(220.0f, 0.0f), ImGuiChildFlags_Borders);
        ImGui::TextUnformatted("Demos");
        ImGui::Separator();
        for (int i = 0; i < VplDemo_Count; ++i) {
            if (ImGui::Selectable(kVplDemos[i].name, selected == i)) {
                if (selected != i) {
                    selected = i;
                    figure_dirty = true;
                }
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("detail", ImVec2(0.0f, 0.0f));

        const VplDemoInfo &info = kVplDemos[selected];
        ImGui::TextWrapped("%s", info.blurb);
        ImGui::TextDisabled("%s", info.api);
        ImGui::Separator();

        /* ---- per-demo controls ---- */
        bool changed = false;

        /* ImGui has no SliderDouble, so the tweakables go through floats. */
        auto slider = [&](const char *label, double *value, float lo, float hi) {
            float v = static_cast<float>(*value);
            if (ImGui::SliderFloat(label, &v, lo, hi)) {
                *value = v;
                return true;
            }
            return false;
        };

        switch (selected) {
            case VplDemo_Lines:
            case VplDemo_LineStyles:
                changed |= slider("line width (pt)", &params.lineWidth, 0.5f, 6.0f);
                break;
            case VplDemo_Markers:
                changed |= slider("marker size (pt)", &params.markerSize, 2.0f, 20.0f);
                break;
            case VplDemo_FormatStrings:
                if (ImGui::InputText("format", params.format, sizeof(params.format))) {
                    changed = true;
                }
                ImGui::TextDisabled("colour + line style + marker, any order");
                break;
            case VplDemo_ErrorBars:
                changed |= slider("cap size (pt)", &params.capSize, 0.0f, 12.0f);
                break;
            case VplDemo_FillBetween:
                changed |= slider("band alpha", &params.bandAlpha, 0.0f, 1.0f);
                break;
            case VplDemo_Bars:
                changed |= slider("bar width", &params.barWidth, 0.1f, 1.0f);
                break;
            case VplDemo_LogScale:
                changed |= ImGui::Checkbox("log x", reinterpret_cast<bool *>(&params.logX));
                ImGui::SameLine();
                changed |= ImGui::Checkbox("log y", reinterpret_cast<bool *>(&params.logY));
                break;
            case VplDemo_AxisLimits:
                changed |= ImGui::Checkbox("pin x", reinterpret_cast<bool *>(&params.pinX));
                changed |= slider("x min", &params.xlimLo, -1.0f, 6.0f);
                changed |= slider("x max", &params.xlimHi, 0.0f, 8.0f);
                break;
            case VplDemo_Subplots:
                changed |= ImGui::SliderInt("rows", &params.subplotRows, 1, 3);
                changed |= ImGui::SliderInt("cols", &params.subplotCols, 1, 3);
                changed |=
                    ImGui::Checkbox("tight_layout", reinterpret_cast<bool *>(&params.tightLayout));
                break;
            default:
                break;
        }

        if (selected != VplDemo_Subplots) {
            changed |= ImGui::Checkbox("grid", reinterpret_cast<bool *>(&params.showGrid));
            ImGui::SameLine();
            changed |= ImGui::Checkbox("legend", reinterpret_cast<bool *>(&params.showLegend));
            if (params.showLegend) {
                ImGui::SameLine();
                const char *locs[] = {"upper right", "upper left", "lower left",
                                      "lower right"};
                int loc = static_cast<int>(params.legendLoc);
                ImGui::SetNextItemWidth(140.0f);
                if (ImGui::Combo("loc", &loc, locs, 4)) {
                    params.legendLoc = static_cast<VplLegendLoc>(loc);
                    changed = true;
                }
            }
        }

        if (changed) {
            figure_dirty = true;
        }

        /* ---- export ---- */
        ImGui::Separator();
        auto export_as = [&](const char *ext) {
            std::string path = std::string("vplot_") + info.slug + "." + ext;
            const VplResult r = vplFigureSaveFig(figure, path.c_str(), nullptr);
#if defined(__EMSCRIPTEN__)
            /* On the web the file lands in Emscripten's in-memory filesystem, invisible to
             * the user -- so hand the bytes to the browser as a download, then unlink them. */
            if (r == VplResult_Success) {
                const char *mime = std::strcmp(ext, "svg") == 0   ? "image/svg+xml"
                                   : std::strcmp(ext, "pdf") == 0 ? "application/pdf"
                                                                  : "image/png";
                /* clang-format off */
                EM_ASM({
                    var path = UTF8ToString($0);
                    var mime = UTF8ToString($1);
                    try {
                        var blob = new Blob([FS.readFile(path)], { type: mime });
                        var url  = URL.createObjectURL(blob);
                        var a    = document.createElement('a');
                        a.href = url; a.download = path;
                        document.body.appendChild(a); a.click(); a.remove();
                        URL.revokeObjectURL(url);
                        FS.unlink(path);
                    } catch (err) { console.error('vplot export download failed:', err); }
                }, path.c_str(), mime);
                /* clang-format on */
                export_status = std::string("downloaded ") + path;
            } else {
                export_status = describe_save(r, path.c_str());
            }
#else
            export_status = describe_save(r, path.c_str());
#endif
        };
        if (ImGui::Button("Export SVG")) {
            export_as("svg");
        }
        ImGui::SameLine();
        if (ImGui::Button("Export PDF")) {
            export_as("pdf");
        }
        ImGui::SameLine();
        if (ImGui::Button("Export PNG")) {
            export_as("png");
        }
        if (!export_status.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", export_status.c_str());
        }

        ImGui::Separator();
        ImGui::Image(texture_id,
                     ImVec2(static_cast<float>(fig_w), static_cast<float>(fig_h)));

        ImGui::EndChild();
        ImGui::End();
    };

    app.onUpdate = [&](uint64_t) {
        if (figure_dirty) {
            rebuild();
            figure_dirty = false;
        }
        if (!texture_dirty || figure == nullptr) {
            return;
        }
        texture_dirty = false;

        if (vplFigureRenderRGBA(figure, pixels.data(), pixels.size(), nullptr) !=
            VplResult_Success) {
            return;
        }
        app.BeginUpload();
        app.UploadTexture(texture, pixels.data(), fig_w, fig_h, 1, fig_w * fig_h * 4u);
        app.EndUpload();
    };

    app.SetupCapture();
    app.Run();

    app.guiApi.FreeImguiTexture(app.gui, view);
    c.DestroyDescriptor(view);
    c.DestroyTexture(texture);

    if (figure != nullptr) {
        vplDestroyFigure(figure);
    }
    app.Shutdown();
    return 0;
}
