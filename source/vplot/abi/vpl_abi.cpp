/*
 * The C ABI implementation.
 *
 * Every exported function wraps its body in a try/catch that turns any
 * exception into a VplResult; an exception escaping across the boundary is
 * undefined behaviour in the consumer's module.
 */

#include "vpl/vpl.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "agg_renderer.h"
#include "colors.h"
#include "figure.h"
#include "font.h"
#include "pdf_renderer.h"
#include "png_writer.h"
#include "svg_renderer.h"

namespace
{

thread_local std::string g_last_error;

void set_error(const char *what)
{
    g_last_error = what != nullptr ? what : "";
}

/* One font engine for the process, so the face is parsed once rather than per
   figure. matplotlib likewise caches faces globally. */
vpl::FontEngine &font_engine()
{
    static vpl::FontEngine engine;
    static bool tried_default = false;
    if (!engine.ok() && !tried_default) {
        tried_default = true;
        engine.load_default_face();
    }
    return engine;
}

struct FigureImpl
{
    vpl::Figure figure;
    std::unique_ptr<vpl::AggRenderer> renderer;
    unsigned renderer_width = 0;
    unsigned renderer_height = 0;
};

FigureImpl *impl(VplFigure *f) { return reinterpret_cast<FigureImpl *>(f); }
const FigureImpl *impl(const VplFigure *f) { return reinterpret_cast<const FigureImpl *>(f); }

vpl::Axes *axes_of(VplAxes *a) { return reinterpret_cast<vpl::Axes *>(a); }
const vpl::Axes *axes_of(const VplAxes *a) { return reinterpret_cast<const vpl::Axes *>(a); }

/* Axes do not know their own figure, but pan and zoom need the canvas size to
   turn pixels into data. Each axes records the size it was last rendered at;
   figure defaults apply until the first render. */
struct AxesGeometry
{
    double width = 640.0;
    double height = 480.0;
    double dpi = 100.0;
};

std::vector<std::pair<const vpl::Axes *, AxesGeometry>> &geometry_registry()
{
    static std::vector<std::pair<const vpl::Axes *, AxesGeometry>> registry;
    return registry;
}

void remember_geometry(const vpl::Axes *ax, double w, double h, double dpi)
{
    for (auto &entry : geometry_registry()) {
        if (entry.first == ax) {
            entry.second = AxesGeometry{w, h, dpi};
            return;
        }
    }
    geometry_registry().emplace_back(ax, AxesGeometry{w, h, dpi});
}

AxesGeometry geometry_for(const vpl::Axes *ax)
{
    for (const auto &entry : geometry_registry()) {
        if (entry.first == ax) {
            return entry.second;
        }
    }
    return AxesGeometry{};
}

void forget_geometry(const vpl::Figure &fig)
{
    auto &registry = geometry_registry();
    for (const auto &ax : fig.axes) {
        for (std::size_t i = 0; i < registry.size();) {
            if (registry[i].first == ax.get()) {
                registry.erase(registry.begin() + static_cast<std::ptrdiff_t>(i));
            } else {
                ++i;
            }
        }
    }
}

} // namespace

#define VPL_TRY try {
#define VPL_CATCH                                                                                  \
    }                                                                                              \
    catch (const std::bad_alloc &) { set_error("out of memory"); return VplResult_OutOfMemory; }    \
    catch (const std::invalid_argument &e) { set_error(e.what()); return VplResult_InvalidArgument; } \
    catch (const std::exception &e) { set_error(e.what()); return VplResult_InternalError; }        \
    catch (...) { set_error("unknown error"); return VplResult_InternalError; }

extern "C" {

const char *VPL_CALL vplGetLastError(void)
{
    return g_last_error.c_str();
}

const char *VPL_CALL vplResultToString(VplResult result)
{
    switch (result) {
        case VplResult_Success: return "Success";
        case VplResult_InvalidArgument: return "InvalidArgument";
        case VplResult_OutOfMemory: return "OutOfMemory";
        case VplResult_FontUnavailable: return "FontUnavailable";
        case VplResult_BufferTooSmall: return "BufferTooSmall";
        case VplResult_InternalError: return "InternalError";
    }
    return "Unknown";
}

VplResult VPL_CALL vplSetFontPath(const char *ttfPath)
{
    VPL_TRY
    if (ttfPath == nullptr) {
        set_error("ttfPath is null");
        return VplResult_InvalidArgument;
    }
    if (!font_engine().load_face(ttfPath)) {
        set_error("could not load font face");
        return VplResult_FontUnavailable;
    }
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplCreateFigure(const VplFigureDesc *desc, VplFigure **outFigure)
{
    VPL_TRY
    if (outFigure == nullptr) {
        set_error("outFigure is null");
        return VplResult_InvalidArgument;
    }
    *outFigure = nullptr;

    auto fig = std::make_unique<FigureImpl>();

    if (desc != nullptr) {
        if (desc->structSize < sizeof(VplFigureDesc)) {
            /* An older struct (smaller size) is fine; its tail fields stay
               default. A structSize of 0 is rejected as an uninitialized
               struct. */
            if (desc->structSize == 0) {
                set_error("VplFigureDesc.structSize is 0");
                return VplResult_InvalidArgument;
            }
        }
        if (desc->widthInches > 0.0) {
            fig->figure.width_inch = desc->widthInches;
        }
        if (desc->heightInches > 0.0) {
            fig->figure.height_inch = desc->heightInches;
        }
        if (desc->dpi > 0.0) {
            fig->figure.dpi = desc->dpi;
        }
        const bool face_set = desc->faceColor[0] != 0.0f || desc->faceColor[1] != 0.0f ||
                              desc->faceColor[2] != 0.0f || desc->faceColor[3] != 0.0f;
        if (face_set) {
            fig->figure.facecolor = vpl::Color{desc->faceColor[0], desc->faceColor[1],
                                               desc->faceColor[2], desc->faceColor[3]};
        }
    }

    if (fig->figure.pixel_width() == 0 || fig->figure.pixel_height() == 0) {
        set_error("figure size rounds to zero pixels");
        return VplResult_InvalidArgument;
    }

    *outFigure = reinterpret_cast<VplFigure *>(fig.release());
    return VplResult_Success;
    VPL_CATCH
}

void VPL_CALL vplDestroyFigure(VplFigure *figure)
{
    if (figure == nullptr) {
        return;
    }
    try {
        FigureImpl *f = impl(figure);
        forget_geometry(f->figure);
        delete f;
    } catch (...) {
        /* A destructor must not throw across the ABI. */
    }
}

VplResult VPL_CALL vplFigureGetPixelSize(const VplFigure *figure,
                                         uint32_t *outWidth,
                                         uint32_t *outHeight)
{
    VPL_TRY
    if (figure == nullptr) {
        set_error("figure is null");
        return VplResult_InvalidArgument;
    }
    const vpl::Figure &f = impl(figure)->figure;
    if (outWidth != nullptr) {
        *outWidth = f.pixel_width();
    }
    if (outHeight != nullptr) {
        *outHeight = f.pixel_height();
    }
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplFigureRenderRGBA(VplFigure *figure,
                                       uint8_t *buffer,
                                       size_t bufferSize,
                                       size_t *outRequiredSize)
{
    VPL_TRY
    if (figure == nullptr) {
        set_error("figure is null");
        return VplResult_InvalidArgument;
    }

    FigureImpl *f = impl(figure);
    const unsigned w = f->figure.pixel_width();
    const unsigned h = f->figure.pixel_height();
    const size_t required = static_cast<size_t>(w) * h * 4u;

    if (outRequiredSize != nullptr) {
        *outRequiredSize = required;
    }
    if (buffer == nullptr) {
        return VplResult_Success;
    }
    if (bufferSize < required) {
        set_error("buffer too small for the figure's pixel size");
        return VplResult_BufferTooSmall;
    }

    if (!f->renderer || f->renderer_width != w || f->renderer_height != h) {
        f->renderer = std::make_unique<vpl::AggRenderer>(w, h, f->figure.dpi);
        f->renderer_width = w;
        f->renderer_height = h;
    }

    vpl::FontEngine &fonts = font_engine();
    f->renderer->set_font_engine(fonts.ok() ? &fonts : nullptr);

    f->renderer->clear(f->figure.background());
    f->figure.draw(*f->renderer);

    for (const auto &ax : f->figure.axes) {
        remember_geometry(ax.get(), w, h, f->figure.dpi);
    }

    std::memcpy(buffer, f->renderer->pixels(), required);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplFigureSaveFig(VplFigure *figure,
                                    const char *path,
                                    const VplSaveDesc *desc)
{
    VPL_TRY
    if (figure == nullptr || path == nullptr) {
        set_error("figure or path is null");
        return VplResult_InvalidArgument;
    }

    VplFormat format = desc != nullptr ? desc->format : VplFormat_Auto;
    if (format == VplFormat_Auto) {
        const std::string p = path;
        const std::size_t dot = p.find_last_of('.');
        std::string ext = dot == std::string::npos ? "" : p.substr(dot + 1);
        for (char &ch : ext) {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        if (ext == "svg") {
            format = VplFormat_SVG;
        } else if (ext == "pdf") {
            format = VplFormat_PDF;
        } else if (ext == "png") {
            format = VplFormat_PNG;
        } else {
            set_error("cannot infer format from the file extension; set VplSaveDesc.format");
            return VplResult_InvalidArgument;
        }
    }

    FigureImpl *f = impl(figure);
    vpl::FontEngine &fonts = font_engine();
    vpl::FontEngine *font_ptr = fonts.ok() ? &fonts : nullptr;

    /* Vector output is measured in points, so the canvas is inches * 72 rather
       than inches * dpi. */
    const double w_pt = f->figure.width_inch * 72.0;
    const double h_pt = f->figure.height_inch * 72.0;

    bool written = false;
    if (format == VplFormat_PNG) {
        /* Raster export uses the same renderer as the live view, at the
           figure's own dpi. VplSaveDesc::dpi overrides it for a
           higher-resolution copy. */
        const double dpi = (desc != nullptr && desc->dpi > 0.0) ? desc->dpi : f->figure.dpi;
        const double saved_dpi = f->figure.dpi;
        f->figure.dpi = dpi;

        const unsigned w = f->figure.pixel_width();
        const unsigned h = f->figure.pixel_height();

        vpl::AggRenderer renderer(w, h, dpi);
        renderer.set_font_engine(font_ptr);
        renderer.clear(f->figure.background());
        f->figure.draw(renderer);

        f->figure.dpi = saved_dpi;

        written = vpl::write_png(path, renderer.pixels(), w, h, dpi);
    } else if (format == VplFormat_SVG) {
        vpl::SvgRenderer renderer(w_pt, h_pt, font_ptr);
        renderer.begin(f->figure.background());
        f->figure.draw(renderer);
        renderer.finish();
        written = renderer.save(path);
    } else {
        vpl::PdfRenderer renderer(w_pt, h_pt, font_ptr);
        renderer.begin(f->figure.background());
        f->figure.draw(renderer);
        renderer.finish();
        written = renderer.save(path);
    }

    if (!written) {
        set_error("could not write the output file");
        return VplResult_InternalError;
    }
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplFigureTightLayout(VplFigure *figure)
{
    VPL_TRY
    if (figure == nullptr) {
        set_error("figure is null");
        return VplResult_InvalidArgument;
    }

    FigureImpl *f = impl(figure);

    /* Measuring needs a renderer with the right dpi and font metrics but no
       drawn frame; a scratch one avoids disturbing the cached render target. */
    vpl::AggRenderer probe(f->figure.pixel_width(), f->figure.pixel_height(),
                           f->figure.dpi);
    vpl::FontEngine &fonts = font_engine();
    probe.set_font_engine(fonts.ok() ? &fonts : nullptr);

    f->figure.tight_layout(probe);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplFigureAddSubplot(VplFigure *figure,
                                       uint32_t nrows,
                                       uint32_t ncols,
                                       uint32_t index,
                                       VplAxes **outAxes)
{
    VPL_TRY
    if (figure == nullptr || outAxes == nullptr) {
        set_error("figure or outAxes is null");
        return VplResult_InvalidArgument;
    }
    if (nrows == 0 || ncols == 0 || index == 0 || index > nrows * ncols) {
        set_error("subplot index out of range for the given grid");
        return VplResult_InvalidArgument;
    }

    FigureImpl *f = impl(figure);
    vpl::Axes &ax = f->figure.add_subplot(nrows, ncols, index);
    remember_geometry(&ax, f->figure.pixel_width(), f->figure.pixel_height(), f->figure.dpi);

    *outAxes = reinterpret_cast<VplAxes *>(&ax);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplFigureTwinX(VplFigure *figure, VplAxes *base, VplAxes **outAxes)
{
    VPL_TRY
    if (figure == nullptr || base == nullptr || outAxes == nullptr) {
        set_error("figure, base or outAxes is null");
        return VplResult_InvalidArgument;
    }

    FigureImpl *f = impl(figure);
    vpl::Axes &ax = f->figure.twinx(*axes_of(base));
    remember_geometry(&ax, f->figure.pixel_width(), f->figure.pixel_height(), f->figure.dpi);

    *outAxes = reinterpret_cast<VplAxes *>(&ax);
    return VplResult_Success;
    VPL_CATCH
}


namespace
{

/* Shared line styling for plot, errorbar and the reference lines. */
VplResult apply_line_desc(vpl::Line2D &line, const VplLineDesc *desc)
{
    if (desc == nullptr) {
        return VplResult_Success;
    }
    if (desc->structSize == 0) {
        set_error("VplLineDesc.structSize is 0");
        return VplResult_InvalidArgument;
    }

    /* Format string is applied first so explicit fields win, matching
       matplotlib's plot("r--", color="blue"). */
    if ((desc->set & VplLineField_Format) != 0 && desc->format != nullptr) {
        vpl::Color c;
        vpl::LineStyle ls = line.linestyle;
        vpl::MarkerStyle mk = line.marker;
        bool has_c = false, has_ls = false, has_mk = false;
        if (!vpl::parse_format(desc->format, c, has_c, ls, has_ls, mk, has_mk)) {
            set_error("could not parse the format string");
            return VplResult_InvalidArgument;
        }
        if (has_c) {
            line.color = c;
        }
        if (has_ls) {
            line.linestyle = ls;
        }
        if (has_mk) {
            line.marker = mk;
        }
    }

    if ((desc->set & VplLineField_ColorSpec) != 0 && desc->colorSpec != nullptr) {
        vpl::Color c;
        if (!vpl::parse_color(desc->colorSpec, c)) {
            set_error("unrecognized colour spec");
            return VplResult_InvalidArgument;
        }
        line.color = c;
    }

    return VplResult_Success;
}

} // namespace

VplResult VPL_CALL vplAxesPlotY(VplAxes *axes,
                                const double *y,
                                size_t count,
                                const VplLineDesc *desc,
                                VplLine **outLine)
{
    VPL_TRY
    if (axes == nullptr || y == nullptr) {
        set_error("axes or y is null");
        return VplResult_InvalidArgument;
    }
    if (count == 0) {
        set_error("count is 0");
        return VplResult_InvalidArgument;
    }

    /* Default x is 0..n-1, matching matplotlib's np.arange(len(y)). */
    std::vector<double> index(count);
    for (size_t i = 0; i < count; ++i) {
        index[i] = static_cast<double>(i);
    }
    return vplAxesPlot(axes, index.data(), y, count, desc, outLine);
    VPL_CATCH
}

VplResult VPL_CALL vplAxesPlot(VplAxes *axes,
                               const double *x,
                               const double *y,
                               size_t count,
                               const VplLineDesc *desc,
                               VplLine **outLine)
{
    VPL_TRY
    if (axes == nullptr || x == nullptr || y == nullptr) {
        set_error("axes, x or y is null");
        return VplResult_InvalidArgument;
    }
    if (count == 0) {
        set_error("count is 0");
        return VplResult_InvalidArgument;
    }

    vpl::Line2D &line = axes_of(axes)->plot(x, y, count);

    {
        const VplResult r = apply_line_desc(line, desc);
        if (r != VplResult_Success) {
            return r;
        }
    }

    if (desc != nullptr) {
        if ((desc->set & VplLineField_Color) != 0) {
            line.color = vpl::Color{desc->color[0], desc->color[1], desc->color[2],
                                    desc->color[3]};
        }
        if ((desc->set & VplLineField_Width) != 0) {
            line.linewidth = desc->width;
        }
        if ((desc->set & VplLineField_Label) != 0 && desc->label != nullptr) {
            line.label = desc->label;
        }
        if ((desc->set & VplLineField_LineStyle) != 0) {
            switch (desc->linestyle) {
                case VplLineStyle_Dashed: line.linestyle = vpl::LineStyle::Dashed; break;
                case VplLineStyle_DashDot: line.linestyle = vpl::LineStyle::DashDot; break;
                case VplLineStyle_Dotted: line.linestyle = vpl::LineStyle::Dotted; break;
                case VplLineStyle_None: line.linestyle = vpl::LineStyle::None; break;
                case VplLineStyle_Solid: line.linestyle = vpl::LineStyle::Solid; break;
                default:
                    set_error("unknown VplLineStyle");
                    return VplResult_InvalidArgument;
            }
        }
        if ((desc->set & VplLineField_Marker) != 0) {
            switch (desc->marker) {
                case VplMarker_None: line.marker = vpl::MarkerStyle::None; break;
                case VplMarker_Circle: line.marker = vpl::MarkerStyle::Circle; break;
                case VplMarker_Square: line.marker = vpl::MarkerStyle::Square; break;
                case VplMarker_TriangleUp: line.marker = vpl::MarkerStyle::TriangleUp; break;
                case VplMarker_TriangleDown:
                    line.marker = vpl::MarkerStyle::TriangleDown;
                    break;
                case VplMarker_Diamond: line.marker = vpl::MarkerStyle::Diamond; break;
                case VplMarker_Plus: line.marker = vpl::MarkerStyle::Plus; break;
                case VplMarker_Cross: line.marker = vpl::MarkerStyle::Cross; break;
                case VplMarker_Star: line.marker = vpl::MarkerStyle::Star; break;
                default:
                    set_error("unknown VplMarker");
                    return VplResult_InvalidArgument;
            }
        }
        if ((desc->set & VplLineField_MarkerSize) != 0) {
            if (!(desc->markersize >= 0.0)) {
                set_error("markersize must be non-negative");
                return VplResult_InvalidArgument;
            }
            line.markersize = desc->markersize;
        }
    }

    if (outLine != nullptr) {
        *outLine = reinterpret_cast<VplLine *>(&line);
    }
    return VplResult_Success;
    VPL_CATCH
}

namespace
{

vpl::MarkerStyle to_marker(VplMarker m, bool &ok)
{
    ok = true;
    switch (m) {
        case VplMarker_None: return vpl::MarkerStyle::None;
        case VplMarker_Circle: return vpl::MarkerStyle::Circle;
        case VplMarker_Square: return vpl::MarkerStyle::Square;
        case VplMarker_TriangleUp: return vpl::MarkerStyle::TriangleUp;
        case VplMarker_TriangleDown: return vpl::MarkerStyle::TriangleDown;
        case VplMarker_Diamond: return vpl::MarkerStyle::Diamond;
        case VplMarker_Plus: return vpl::MarkerStyle::Plus;
        case VplMarker_Cross: return vpl::MarkerStyle::Cross;
        case VplMarker_Star: return vpl::MarkerStyle::Star;
        default: break;
    }
    ok = false;
    return vpl::MarkerStyle::Circle;
}

} // namespace

VplResult VPL_CALL vplAxesScatter(VplAxes *axes,
                                  const double *x,
                                  const double *y,
                                  size_t count,
                                  const double *sizes,
                                  const float *colors,
                                  const VplScatterDesc *desc,
                                  VplScatter **outScatter)
{
    VPL_TRY
    if (axes == nullptr || x == nullptr || y == nullptr) {
        set_error("axes, x or y is null");
        return VplResult_InvalidArgument;
    }
    if (count == 0) {
        set_error("count is 0");
        return VplResult_InvalidArgument;
    }

    vpl::ScatterCollection &sc = axes_of(axes)->scatter(x, y, count);

    if (sizes != nullptr) {
        sc.sizes.assign(sizes, sizes + count);
        for (double s : sc.sizes) {
            if (!(s >= 0.0)) {
                set_error("scatter sizes must be non-negative");
                return VplResult_InvalidArgument;
            }
        }
    }
    if (colors != nullptr) {
        sc.colors.resize(count);
        for (size_t i = 0; i < count; ++i) {
            sc.colors[i] = vpl::Color{colors[4 * i], colors[4 * i + 1], colors[4 * i + 2],
                                      colors[4 * i + 3]};
        }
    }

    if (desc != nullptr) {
        if (desc->structSize == 0) {
            set_error("VplScatterDesc.structSize is 0");
            return VplResult_InvalidArgument;
        }
        if ((desc->set & VplScatterField_ColorSpec) != 0 && desc->colorSpec != nullptr) {
            vpl::Color c;
            if (!vpl::parse_color(desc->colorSpec, c)) {
                set_error("unrecognized colour spec");
                return VplResult_InvalidArgument;
            }
            sc.color = c;
        }
        if ((desc->set & VplScatterField_Color) != 0) {
            sc.color = vpl::Color{desc->color[0], desc->color[1], desc->color[2],
                                  desc->color[3]};
        }
        if ((desc->set & VplScatterField_Marker) != 0) {
            bool ok = false;
            sc.marker = to_marker(desc->marker, ok);
            if (!ok) {
                set_error("unknown VplMarker");
                return VplResult_InvalidArgument;
            }
        }
        if ((desc->set & VplScatterField_Size) != 0) {
            if (!(desc->size >= 0.0)) {
                set_error("scatter size must be non-negative");
                return VplResult_InvalidArgument;
            }
            sc.size = desc->size;
        }
        if ((desc->set & VplScatterField_EdgeColor) != 0) {
            sc.edgecolor = vpl::Color{desc->edgeColor[0], desc->edgeColor[1],
                                      desc->edgeColor[2], desc->edgeColor[3]};
            sc.has_edgecolor = true;
        }
        if ((desc->set & VplScatterField_EdgeWidth) != 0) {
            if (!(desc->edgeWidth >= 0.0)) {
                set_error("edgeWidth must be non-negative");
                return VplResult_InvalidArgument;
            }
            sc.linewidth = desc->edgeWidth;
        }
        if ((desc->set & VplScatterField_Alpha) != 0) {
            if (!(desc->alpha >= 0.0 && desc->alpha <= 1.0)) {
                set_error("alpha must be within [0, 1]");
                return VplResult_InvalidArgument;
            }
            sc.alpha = desc->alpha;
        }
        if ((desc->set & VplScatterField_Label) != 0 && desc->label != nullptr) {
            sc.label = desc->label;
        }
    }

    if (outScatter != nullptr) {
        *outScatter = reinterpret_cast<VplScatter *>(&sc);
    }
    return VplResult_Success;
    VPL_CATCH
}

namespace
{

bool to_colormap(VplColormap c, vpl::Colormap &out)
{
    switch (c) {
        case VplColormap_Viridis: out = vpl::Colormap::Viridis; return true;
        case VplColormap_Magma: out = vpl::Colormap::Magma; return true;
        case VplColormap_Inferno: out = vpl::Colormap::Inferno; return true;
        case VplColormap_Plasma: out = vpl::Colormap::Plasma; return true;
        case VplColormap_Gray: out = vpl::Colormap::Gray; return true;
        default: return false;
    }
}

bool to_fill_step(VplFillStep s, vpl::FillStep &out)
{
    switch (s) {
        case VplFillStep_None: out = vpl::FillStep::None; return true;
        case VplFillStep_Pre: out = vpl::FillStep::Pre; return true;
        case VplFillStep_Post: out = vpl::FillStep::Post; return true;
        case VplFillStep_Mid: out = vpl::FillStep::Mid; return true;
        default: return false;
    }
}

/* Read a VplSpectralDesc into the core options. NULL or a zeroed struct means
   all defaults, since every zero field is already its matplotlib default. */
bool to_spectral_options(const VplSpectralDesc *desc, vpl::SpectralOptions &out)
{
    if (desc == nullptr) {
        return true;
    }
    if (desc->structSize == 0) {
        set_error("VplSpectralDesc.structSize is 0");
        return false;
    }
    switch (desc->window) {
        case VplSpectralWindow_Hanning: out.window = vpl::SpectralWindow::Hanning; break;
        case VplSpectralWindow_Hamming: out.window = vpl::SpectralWindow::Hamming; break;
        case VplSpectralWindow_Blackman: out.window = vpl::SpectralWindow::Blackman; break;
        case VplSpectralWindow_Bartlett: out.window = vpl::SpectralWindow::Bartlett; break;
        case VplSpectralWindow_Boxcar: out.window = vpl::SpectralWindow::Boxcar; break;
        default: set_error("unknown VplSpectralWindow"); return false;
    }
    switch (desc->detrend) {
        case VplSpectralDetrend_None: out.detrend = vpl::SpectralDetrend::None; break;
        case VplSpectralDetrend_Mean: out.detrend = vpl::SpectralDetrend::Mean; break;
        case VplSpectralDetrend_Linear: out.detrend = vpl::SpectralDetrend::Linear; break;
        default: set_error("unknown VplSpectralDetrend"); return false;
    }
    out.sides = desc->twoSided ? vpl::SpectralSides::TwoSided : vpl::SpectralSides::OneSided;
    out.pad_to = desc->padTo;
    return true;
}

} // namespace

VplResult VPL_CALL vplFigureAddAxes(VplFigure *figure,
                                    double left,
                                    double bottom,
                                    double width,
                                    double height,
                                    VplAxes **outAxes)
{
    VPL_TRY
    if (figure == nullptr || outAxes == nullptr) {
        set_error("figure or outAxes is null");
        return VplResult_InvalidArgument;
    }
    if (!(width > 0.0) || !(height > 0.0)) {
        set_error("width and height must both be positive");
        return VplResult_InvalidArgument;
    }

    FigureImpl *f = impl(figure);
    vpl::Axes &ax = f->figure.add_axes(left, bottom, width, height);
    remember_geometry(&ax, f->figure.pixel_width(), f->figure.pixel_height(), f->figure.dpi);

    *outAxes = reinterpret_cast<VplAxes *>(&ax);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplFigureInsetAxes(VplFigure *figure,
                                      const VplAxes *parent,
                                      double x,
                                      double y,
                                      double width,
                                      double height,
                                      VplAxes **outAxes)
{
    VPL_TRY
    if (figure == nullptr || parent == nullptr || outAxes == nullptr) {
        set_error("figure, parent or outAxes is null");
        return VplResult_InvalidArgument;
    }
    if (!(width > 0.0) || !(height > 0.0)) {
        set_error("width and height must both be positive");
        return VplResult_InvalidArgument;
    }

    FigureImpl *f = impl(figure);
    vpl::Axes &ax = f->figure.inset_axes(*axes_of(parent), x, y, width, height);
    remember_geometry(&ax, f->figure.pixel_width(), f->figure.pixel_height(), f->figure.dpi);
    *outAxes = reinterpret_cast<VplAxes *>(&ax);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesIndicateInset(VplAxes *axes,
                                        double x,
                                        double y,
                                        double width,
                                        double height,
                                        const VplAxes *inset)
{
    VPL_TRY
    if (axes == nullptr || inset == nullptr) {
        set_error("axes or inset is null");
        return VplResult_InvalidArgument;
    }
    vpl::Axes *ax = axes_of(axes);
    ax->indicate_inset(x, y, width, height, *axes_of(inset), ax->position);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesIndicateInsetZoom(VplAxes *axes, const VplAxes *inset)
{
    VPL_TRY
    if (axes == nullptr || inset == nullptr) {
        set_error("axes or inset is null");
        return VplResult_InvalidArgument;
    }
    vpl::Axes *ax = axes_of(axes);
    const vpl::Axes *in = axes_of(inset);
    const vpl::Bbox view = in->view_limits();
    ax->indicate_inset(view.x0, view.y0, view.x1 - view.x0, view.y1 - view.y0, *in,
                       ax->position);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplFigureSecondaryXAxis(VplFigure *figure,
                                           const VplAxes *base,
                                           int top,
                                           double scale,
                                           double offset,
                                           VplAxes **outAxes)
{
    VPL_TRY
    if (figure == nullptr || base == nullptr || outAxes == nullptr) {
        set_error("figure, base or outAxes is null");
        return VplResult_InvalidArgument;
    }
    if (scale == 0.0) {
        set_error("scale must be nonzero");
        return VplResult_InvalidArgument;
    }
    FigureImpl *f = impl(figure);
    vpl::Axes &ax = f->figure.secondary_xaxis(*axes_of(base), top != 0, scale, offset);
    remember_geometry(&ax, f->figure.pixel_width(), f->figure.pixel_height(), f->figure.dpi);
    *outAxes = reinterpret_cast<VplAxes *>(&ax);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplFigureSecondaryYAxis(VplFigure *figure,
                                           const VplAxes *base,
                                           int right,
                                           double scale,
                                           double offset,
                                           VplAxes **outAxes)
{
    VPL_TRY
    if (figure == nullptr || base == nullptr || outAxes == nullptr) {
        set_error("figure, base or outAxes is null");
        return VplResult_InvalidArgument;
    }
    if (scale == 0.0) {
        set_error("scale must be nonzero");
        return VplResult_InvalidArgument;
    }
    FigureImpl *f = impl(figure);
    vpl::Axes &ax = f->figure.secondary_yaxis(*axes_of(base), right != 0, scale, offset);
    remember_geometry(&ax, f->figure.pixel_width(), f->figure.pixel_height(), f->figure.dpi);
    *outAxes = reinterpret_cast<VplAxes *>(&ax);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplFigureGetSubplotParams(const VplFigure *figure,
                                             VplSubplotParams *outParams)
{
    VPL_TRY
    if (figure == nullptr || outParams == nullptr) {
        set_error("figure or outParams is null");
        return VplResult_InvalidArgument;
    }
    const vpl::Figure::SubplotParams &p =
        impl(const_cast<VplFigure *>(figure))->figure.subplot_params;
    outParams->left = p.left;
    outParams->bottom = p.bottom;
    outParams->right = p.right;
    outParams->top = p.top;
    outParams->wspace = p.wspace;
    outParams->hspace = p.hspace;
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplFigureSubplotsAdjust(VplFigure *figure,
                                           const VplSubplotParams *params)
{
    VPL_TRY
    if (figure == nullptr || params == nullptr) {
        set_error("figure or params is null");
        return VplResult_InvalidArgument;
    }
    if (!(params->right > params->left) || !(params->top > params->bottom)) {
        set_error("subplot bounds must satisfy right > left and top > bottom");
        return VplResult_InvalidArgument;
    }
    if (params->wspace < 0.0 || params->hspace < 0.0) {
        set_error("wspace and hspace must not be negative");
        return VplResult_InvalidArgument;
    }

    vpl::Figure &fig = impl(figure)->figure;
    fig.subplot_params.left = params->left;
    fig.subplot_params.bottom = params->bottom;
    fig.subplot_params.right = params->right;
    fig.subplot_params.top = params->top;
    fig.subplot_params.wspace = params->wspace;
    fig.subplot_params.hspace = params->hspace;
    fig.subplots_adjust();
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplFigureSuptitle(VplFigure *figure, const char *text, double x, double y,
                                     double fontsize)
{
    VPL_TRY
    if (figure == nullptr) {
        set_error("figure is null");
        return VplResult_InvalidArgument;
    }
    impl(figure)->figure.suptitle(text == nullptr ? "" : text, x, y, fontsize);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplFigureSupXLabel(VplFigure *figure, const char *text, double x, double y,
                                      double fontsize)
{
    VPL_TRY
    if (figure == nullptr) {
        set_error("figure is null");
        return VplResult_InvalidArgument;
    }
    impl(figure)->figure.supxlabel(text == nullptr ? "" : text, x, y, fontsize);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplFigureSupYLabel(VplFigure *figure, const char *text, double x, double y,
                                      double fontsize)
{
    VPL_TRY
    if (figure == nullptr) {
        set_error("figure is null");
        return VplResult_InvalidArgument;
    }
    impl(figure)->figure.supylabel(text == nullptr ? "" : text, x, y, fontsize);
    return VplResult_Success;
    VPL_CATCH
}

namespace
{

/* Turn a handle array into a core-axes vector. NULL array or zero count yields
   an empty vector, which the core reads as every axes on the figure. */
std::vector<vpl::Axes *> axes_span(VplAxes *const *axes, size_t count)
{
    std::vector<vpl::Axes *> out;
    for (size_t i = 0; axes != nullptr && i < count; ++i) {
        if (axes[i] != nullptr) {
            out.push_back(axes_of(axes[i]));
        }
    }
    return out;
}

} // namespace

VplResult VPL_CALL vplFigureAlignXLabels(VplFigure *figure, VplAxes *const *axes, size_t count)
{
    VPL_TRY
    if (figure == nullptr) {
        set_error("figure is null");
        return VplResult_InvalidArgument;
    }
    impl(figure)->figure.align_xlabels(axes_span(axes, count));
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplFigureAlignYLabels(VplFigure *figure, VplAxes *const *axes, size_t count)
{
    VPL_TRY
    if (figure == nullptr) {
        set_error("figure is null");
        return VplResult_InvalidArgument;
    }
    impl(figure)->figure.align_ylabels(axes_span(axes, count));
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplFigureAlignLabels(VplFigure *figure, VplAxes *const *axes, size_t count)
{
    VPL_TRY
    if (figure == nullptr) {
        set_error("figure is null");
        return VplResult_InvalidArgument;
    }
    impl(figure)->figure.align_labels(axes_span(axes, count));
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplFigureAlignTitles(VplFigure *figure, VplAxes *const *axes, size_t count)
{
    VPL_TRY
    if (figure == nullptr) {
        set_error("figure is null");
        return VplResult_InvalidArgument;
    }
    impl(figure)->figure.align_titles(axes_span(axes, count));
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplFigureSubplots(VplFigure *figure, uint32_t nrows, uint32_t ncols,
                                     VplAxes **outAxes)
{
    VPL_TRY
    if (figure == nullptr || outAxes == nullptr) {
        set_error("figure or outAxes is null");
        return VplResult_InvalidArgument;
    }
    if (nrows == 0 || ncols == 0) {
        set_error("nrows and ncols must both be nonzero");
        return VplResult_InvalidArgument;
    }

    FigureImpl *f = impl(figure);
    /* The core writes vpl::Axes*; reinterpret each into a handle in place. */
    std::vector<vpl::Axes *> grid(static_cast<std::size_t>(nrows) * ncols);
    f->figure.subplots(nrows, ncols, grid.data());
    for (std::size_t i = 0; i < grid.size(); ++i) {
        remember_geometry(grid[i], f->figure.pixel_width(), f->figure.pixel_height(), f->figure.dpi);
        outAxes[i] = reinterpret_cast<VplAxes *>(grid[i]);
    }
    return VplResult_Success;
    VPL_CATCH
}

namespace
{
/* Links every other axes in its group (row, column, or whole grid) to the
   group's first member, whose view_limits() the followers read from. Mirrors
   matplotlib's 'all'/'row'/'col' grouping for both sharex and sharey; is_x
   selects which field of Axes is written. */
void link_share_group(const std::vector<vpl::Axes *> &grid, uint32_t nrows, uint32_t ncols,
                      VplShareMode mode, bool is_x)
{
    if (mode == VplShareMode_None) {
        return;
    }
    for (uint32_t r = 0; r < nrows; ++r) {
        for (uint32_t c = 0; c < ncols; ++c) {
            vpl::Axes *leader = nullptr;
            switch (mode) {
                case VplShareMode_All: leader = grid[0]; break;
                case VplShareMode_Row: leader = grid[r * ncols + 0]; break;
                case VplShareMode_Col: leader = grid[0 * ncols + c]; break;
                default: return;
            }
            vpl::Axes *ax = grid[r * ncols + c];
            if (ax == leader) {
                continue;
            }
            if (is_x) {
                ax->shared_x = leader;
            } else {
                ax->shared_y = leader;
            }
        }
    }
}
} // namespace

VplResult VPL_CALL vplFigureSubplotsShared(VplFigure *figure, uint32_t nrows, uint32_t ncols,
                                           VplShareMode shareX, VplShareMode shareY,
                                           VplAxes **outAxes)
{
    VPL_TRY
    if (figure == nullptr || outAxes == nullptr) {
        set_error("figure or outAxes is null");
        return VplResult_InvalidArgument;
    }
    if (nrows == 0 || ncols == 0) {
        set_error("nrows and ncols must both be nonzero");
        return VplResult_InvalidArgument;
    }

    FigureImpl *f = impl(figure);
    std::vector<vpl::Axes *> grid(static_cast<std::size_t>(nrows) * ncols);
    f->figure.subplots(nrows, ncols, grid.data());

    link_share_group(grid, nrows, ncols, shareX, /*is_x=*/true);
    link_share_group(grid, nrows, ncols, shareY, /*is_x=*/false);

    /* matplotlib's subplots() strips interior tick labels when either axis is
       shared, the same edge rule label_outer applies. */
    if (shareX != VplShareMode_None || shareY != VplShareMode_None) {
        for (vpl::Axes *ax : grid) {
            ax->label_outer();
        }
    }

    for (std::size_t i = 0; i < grid.size(); ++i) {
        remember_geometry(grid[i], f->figure.pixel_width(), f->figure.pixel_height(), f->figure.dpi);
        outAxes[i] = reinterpret_cast<VplAxes *>(grid[i]);
    }
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplFigureGetSizeInches(const VplFigure *figure, double *outWidth,
                                          double *outHeight)
{
    VPL_TRY
    if (figure == nullptr) {
        set_error("figure is null");
        return VplResult_InvalidArgument;
    }
    const FigureImpl *f = impl(figure);
    if (outWidth != nullptr) {
        *outWidth = f->figure.width_inch;
    }
    if (outHeight != nullptr) {
        *outHeight = f->figure.height_inch;
    }
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplFigureSetSizeInches(VplFigure *figure, double width, double height)
{
    VPL_TRY
    if (figure == nullptr) {
        set_error("figure is null");
        return VplResult_InvalidArgument;
    }
    if (!(width > 0.0) || !(height > 0.0)) {
        set_error("width and height must both be positive");
        return VplResult_InvalidArgument;
    }
    impl(figure)->figure.width_inch = width;
    impl(figure)->figure.height_inch = height;
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplFigureSetFigWidth(VplFigure *figure, double width)
{
    VPL_TRY
    if (figure == nullptr) {
        set_error("figure is null");
        return VplResult_InvalidArgument;
    }
    if (!(width > 0.0)) {
        set_error("width must be positive");
        return VplResult_InvalidArgument;
    }
    impl(figure)->figure.width_inch = width;
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplFigureSetFigHeight(VplFigure *figure, double height)
{
    VPL_TRY
    if (figure == nullptr) {
        set_error("figure is null");
        return VplResult_InvalidArgument;
    }
    if (!(height > 0.0)) {
        set_error("height must be positive");
        return VplResult_InvalidArgument;
    }
    impl(figure)->figure.height_inch = height;
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplFigureGetDpi(const VplFigure *figure, double *outDpi)
{
    VPL_TRY
    if (figure == nullptr || outDpi == nullptr) {
        set_error("figure or outDpi is null");
        return VplResult_InvalidArgument;
    }
    *outDpi = impl(figure)->figure.dpi;
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplFigureSetDpi(VplFigure *figure, double dpi)
{
    VPL_TRY
    if (figure == nullptr) {
        set_error("figure is null");
        return VplResult_InvalidArgument;
    }
    if (!(dpi > 0.0)) {
        set_error("dpi must be positive");
        return VplResult_InvalidArgument;
    }
    impl(figure)->figure.dpi = dpi;
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplFigureGetAxesCount(const VplFigure *figure, size_t *outCount)
{
    VPL_TRY
    if (figure == nullptr || outCount == nullptr) {
        set_error("figure or outCount is null");
        return VplResult_InvalidArgument;
    }
    *outCount = impl(figure)->figure.axes.size();
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplFigureGetAxes(VplFigure *figure, size_t index, VplAxes **outAxes)
{
    VPL_TRY
    if (figure == nullptr || outAxes == nullptr) {
        set_error("figure or outAxes is null");
        return VplResult_InvalidArgument;
    }
    FigureImpl *f = impl(figure);
    if (index >= f->figure.axes.size()) {
        set_error("axes index out of range");
        return VplResult_InvalidArgument;
    }
    *outAxes = reinterpret_cast<VplAxes *>(f->figure.axes[index].get());
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplFigureClear(VplFigure *figure)
{
    VPL_TRY
    if (figure == nullptr) {
        set_error("figure is null");
        return VplResult_InvalidArgument;
    }
    /* Drop every axes and the figure-level labels together, as clf does. Any
       VplAxes handle from this figure is dangling after this returns. */
    FigureImpl *f = impl(figure);
    f->figure.axes.clear();
    f->figure.suptitle("");
    f->figure.supxlabel("");
    f->figure.supylabel("");
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplFigureTwinY(VplFigure *figure, VplAxes *base, VplAxes **outAxes)
{
    VPL_TRY
    if (figure == nullptr || base == nullptr || outAxes == nullptr) {
        set_error("figure, base or outAxes is null");
        return VplResult_InvalidArgument;
    }

    FigureImpl *f = impl(figure);
    vpl::Axes &ax = f->figure.twiny(*axes_of(base));
    remember_geometry(&ax, f->figure.pixel_width(), f->figure.pixel_height(), f->figure.dpi);

    *outAxes = reinterpret_cast<VplAxes *>(&ax);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplFigureColorbar(VplFigure *figure,
                                     VplAxes *parent,
                                     VplColormap cmap,
                                     double vmin,
                                     double vmax,
                                     VplAxes **outAxes)
{
    VPL_TRY
    if (figure == nullptr || parent == nullptr || outAxes == nullptr) {
        set_error("figure, parent or outAxes is null");
        return VplResult_InvalidArgument;
    }
    if (!(vmax > vmin)) {
        set_error("colorbar needs vmax greater than vmin");
        return VplResult_InvalidArgument;
    }

    vpl::Colormap map = vpl::Colormap::Viridis;
    if (!to_colormap(cmap, map)) {
        set_error("unknown colormap");
        return VplResult_InvalidArgument;
    }

    FigureImpl *f = impl(figure);
    vpl::Axes &ax = f->figure.colorbar(*axes_of(parent), map, vmin, vmax);
    remember_geometry(&ax, f->figure.pixel_width(), f->figure.pixel_height(), f->figure.dpi);

    *outAxes = reinterpret_cast<VplAxes *>(&ax);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplFigureColorbarOriented(VplFigure *figure, VplAxes *parent,
                                             VplColormap cmap, double vmin, double vmax,
                                             int horizontal, VplAxes **outAxes)
{
    VPL_TRY
    if (figure == nullptr || parent == nullptr || outAxes == nullptr) {
        set_error("figure, parent or outAxes is null");
        return VplResult_InvalidArgument;
    }
    if (!(vmax > vmin)) {
        set_error("colorbar needs vmax greater than vmin");
        return VplResult_InvalidArgument;
    }

    vpl::Colormap map = vpl::Colormap::Viridis;
    if (!to_colormap(cmap, map)) {
        set_error("unknown colormap");
        return VplResult_InvalidArgument;
    }

    FigureImpl *f = impl(figure);
    vpl::Axes &ax = f->figure.colorbar(*axes_of(parent), map, vmin, vmax, 0.15, -1.0, 20.0,
                                       horizontal != 0);
    remember_geometry(&ax, f->figure.pixel_width(), f->figure.pixel_height(), f->figure.dpi);

    *outAxes = reinterpret_cast<VplAxes *>(&ax);
    return VplResult_Success;
    VPL_CATCH
}

namespace
{

/* Shared styling for field artists (imshow, pcolormesh, matshow, spy):
   colormap, value limits, extent, alpha, label. */
VplResult apply_field_desc(vpl::ImageGrid &img, const VplFieldDesc *desc)
{
    if (desc == nullptr) {
        return VplResult_Success;
    }
    if (desc->structSize == 0) {
        set_error("VplFieldDesc.structSize is 0");
        return VplResult_InvalidArgument;
    }
    if ((desc->set & VplFieldField_Colormap) != 0 && !to_colormap(desc->cmap, img.cmap)) {
        set_error("unknown VplColormap");
        return VplResult_InvalidArgument;
    }
    if ((desc->set & VplFieldField_VLimits) != 0) {
        if (!(desc->vmax > desc->vmin)) {
            set_error("vmax must exceed vmin");
            return VplResult_InvalidArgument;
        }
        img.vmin = desc->vmin;
        img.vmax = desc->vmax;
        img.has_vlimits = true;
    }
    if ((desc->set & VplFieldField_Extent) != 0) {
        img.x0 = desc->extent[0];
        img.x1 = desc->extent[1];
        img.y0 = desc->extent[2];
        img.y1 = desc->extent[3];
    }
    if ((desc->set & VplFieldField_Alpha) != 0) {
        if (!(desc->alpha >= 0.0 && desc->alpha <= 1.0)) {
            set_error("alpha must be within [0, 1]");
            return VplResult_InvalidArgument;
        }
        img.alpha = desc->alpha;
    }
    if ((desc->set & VplFieldField_Label) != 0 && desc->label != nullptr) {
        img.label = desc->label;
    }
    return VplResult_Success;
}

} // namespace

VplResult VPL_CALL vplAxesImshow(VplAxes *axes,
                                 const double *values,
                                 size_t rows,
                                 size_t cols,
                                 const VplFieldDesc *desc,
                                 VplImage **outImage)
{
    VPL_TRY
    if (axes == nullptr || values == nullptr) {
        set_error("axes or values is null");
        return VplResult_InvalidArgument;
    }
    if (rows == 0 || cols == 0) {
        set_error("rows and cols must both be non-zero");
        return VplResult_InvalidArgument;
    }

    vpl::ImageGrid &img = axes_of(axes)->imshow(values, rows, cols);

    const VplResult r = apply_field_desc(img, desc);
    if (r != VplResult_Success) {
        return r;
    }

    if (outImage != nullptr) {
        *outImage = reinterpret_cast<VplImage *>(&img);
    }
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplFigureFigImage(VplFigure *figure, const double *X, size_t rows,
                                     size_t cols, double xo, double yo,
                                     const VplFieldDesc *desc)
{
    VPL_TRY
    if (figure == nullptr || X == nullptr) {
        set_error("figure or X is null");
        return VplResult_InvalidArgument;
    }
    if (rows == 0 || cols == 0) {
        set_error("rows and cols must both be non-zero");
        return VplResult_InvalidArgument;
    }

    vpl::FigureImage &img = impl(figure)->figure.figimage(X, rows, cols, xo, yo);

    /* Same colormap/limits/alpha fields as an axes image; extent and label
       have no meaning for a figure-pixel blit. */
    if (desc != nullptr) {
        if (desc->structSize == 0) {
            set_error("VplFieldDesc.structSize is 0");
            return VplResult_InvalidArgument;
        }
        if ((desc->set & VplFieldField_Colormap) != 0 && !to_colormap(desc->cmap, img.cmap)) {
            set_error("unknown VplColormap");
            return VplResult_InvalidArgument;
        }
        if ((desc->set & VplFieldField_VLimits) != 0) {
            if (!(desc->vmax > desc->vmin)) {
                set_error("vmax must exceed vmin");
                return VplResult_InvalidArgument;
            }
            img.vmin = desc->vmin;
            img.vmax = desc->vmax;
            img.has_vlimits = true;
        }
        if ((desc->set & VplFieldField_Alpha) != 0) {
            if (!(desc->alpha >= 0.0 && desc->alpha <= 1.0)) {
                set_error("alpha must be within [0, 1]");
                return VplResult_InvalidArgument;
            }
            img.alpha = desc->alpha;
        }
    }
    return VplResult_Success;
    VPL_CATCH
}

namespace
{

/* Shared contour/contourf body: they differ only by one call and the filled
   flag; validation, extent, colormap and label are identical. */
VplResult contour_impl(VplAxes *axes,
                       const double *values,
                       size_t rows,
                       size_t cols,
                       const double *levels,
                       size_t levelCount,
                       const VplFieldDesc *desc,
                       VplContour **outContour,
                       bool filled)
{
    VPL_TRY
    if (axes == nullptr || values == nullptr || levels == nullptr) {
        set_error("axes, values or levels is null");
        return VplResult_InvalidArgument;
    }
    if (rows < 2 || cols < 2) {
        set_error("contour needs a grid of at least 2x2");
        return VplResult_InvalidArgument;
    }
    if (levelCount == 0) {
        set_error("levelCount is 0");
        return VplResult_InvalidArgument;
    }

    /* The extent must be applied before extraction, since marching squares
       emits its segments directly in data coordinates. */
    double ex[4] = {0.0, 0.0, 0.0, 0.0};
    if (desc != nullptr && desc->structSize != 0 &&
        (desc->set & VplFieldField_Extent) != 0) {
        ex[0] = desc->extent[0];
        ex[1] = desc->extent[1];
        ex[2] = desc->extent[2];
        ex[3] = desc->extent[3];
        if (!(ex[1] != ex[0]) || !(ex[3] != ex[2])) {
            set_error("contour extent must have non-zero width and height");
            return VplResult_InvalidArgument;
        }
    }

    if (filled && levelCount < 2) {
        set_error("filled contours need at least 2 levels to have a band between");
        return VplResult_InvalidArgument;
    }

    vpl::Axes *ax = axes_of(axes);
    vpl::ContourSet &cs =
        filled ? ax->contourf(values, rows, cols, levels, levelCount, ex[0], ex[1], ex[2],
                              ex[3])
               : ax->contour(values, rows, cols, levels, levelCount, ex[0], ex[1], ex[2],
                             ex[3]);

    if (desc != nullptr) {
        if (desc->structSize == 0) {
            set_error("VplFieldDesc.structSize is 0");
            return VplResult_InvalidArgument;
        }
        if ((desc->set & VplFieldField_Colormap) != 0 &&
            !to_colormap(desc->cmap, cs.cmap)) {
            set_error("unknown VplColormap");
            return VplResult_InvalidArgument;
        }
        if ((desc->set & VplFieldField_ColorSpec) != 0 && desc->colorSpec != nullptr) {
            vpl::Color c;
            if (!vpl::parse_color(desc->colorSpec, c)) {
                set_error("unrecognized colour spec");
                return VplResult_InvalidArgument;
            }
            cs.color = c;
            cs.has_color = true;
        }
        if ((desc->set & VplFieldField_LineWidth) != 0) {
            if (!(desc->lineWidth > 0.0)) {
                set_error("lineWidth must be positive");
                return VplResult_InvalidArgument;
            }
            cs.linewidth = desc->lineWidth;
        }
        /* Extent was consumed above, before extraction. */
        if ((desc->set & VplFieldField_Label) != 0 && desc->label != nullptr) {
            cs.label = desc->label;
        }
    }

    if (outContour != nullptr) {
        *outContour = reinterpret_cast<VplContour *>(&cs);
    }
    return VplResult_Success;
    VPL_CATCH
}

} // namespace

VplResult VPL_CALL vplAxesContour(VplAxes *axes,
                                  const double *values,
                                  size_t rows,
                                  size_t cols,
                                  const double *levels,
                                  size_t levelCount,
                                  const VplFieldDesc *desc,
                                  VplContour **outContour)
{
    return contour_impl(axes, values, rows, cols, levels, levelCount, desc, outContour,
                        /*filled=*/false);
}

VplResult VPL_CALL vplAxesContourF(VplAxes *axes,
                                   const double *values,
                                   size_t rows,
                                   size_t cols,
                                   const double *levels,
                                   size_t levelCount,
                                   const VplFieldDesc *desc,
                                   VplContour **outContour)
{
    return contour_impl(axes, values, rows, cols, levels, levelCount, desc, outContour,
                        /*filled=*/true);
}

VplResult VPL_CALL vplContourClabel(VplContour *contour, const VplClabelDesc *desc)
{
    VPL_TRY
    if (contour == nullptr) {
        set_error("contour is null");
        return VplResult_InvalidArgument;
    }

    vpl::ContourSet &cs = *reinterpret_cast<vpl::ContourSet *>(contour);
    cs.labeled = true;

    if (desc != nullptr) {
        if ((desc->set & VplClabelField_FontSize) != 0) {
            cs.label_size = desc->fontSize;
        }
        if ((desc->set & VplClabelField_Inline) != 0) {
            cs.label_inline = desc->inlineLabels != 0;
        }
        if ((desc->set & VplClabelField_InlineSpacing) != 0) {
            cs.label_inline_spacing = desc->inlineSpacing;
        }
        if ((desc->set & VplClabelField_RightSideUp) != 0) {
            cs.rightside_up = desc->rightSideUp != 0;
        }
        if ((desc->set & VplClabelField_ColorSpec) != 0 && desc->colorSpec != nullptr) {
            vpl::Color c;
            if (!vpl::parse_color(desc->colorSpec, c)) {
                set_error("unrecognized colour spec");
                return VplResult_InvalidArgument;
            }
            cs.label_color = c;
            cs.label_has_color = true;
        }
    }

    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesErrorBar(VplAxes *axes,
                                   const double *x,
                                   const double *y,
                                   const double *yerr,
                                   size_t count,
                                   double capsize,
                                   const VplLineDesc *desc,
                                   VplLine **outLine)
{
    VPL_TRY
    if (axes == nullptr || x == nullptr || y == nullptr) {
        set_error("axes, x or y is null");
        return VplResult_InvalidArgument;
    }
    if (count == 0) {
        set_error("count is 0");
        return VplResult_InvalidArgument;
    }
    if (!(capsize >= 0.0)) {
        set_error("capsize must be non-negative");
        return VplResult_InvalidArgument;
    }

    vpl::Line2D &line = axes_of(axes)->errorbar(x, y, yerr, count);
    line.capsize = capsize;

    const VplResult r = apply_line_desc(line, desc);
    if (r != VplResult_Success) {
        return r;
    }
    if (desc != nullptr) {
        if ((desc->set & VplLineField_Color) != 0) {
            line.color = vpl::Color{desc->color[0], desc->color[1], desc->color[2],
                                    desc->color[3]};
        }
        if ((desc->set & VplLineField_Width) != 0) {
            line.linewidth = desc->width;
        }
        if ((desc->set & VplLineField_Label) != 0 && desc->label != nullptr) {
            line.label = desc->label;
        }
    }

    if (outLine != nullptr) {
        *outLine = reinterpret_cast<VplLine *>(&line);
    }
    return VplResult_Success;
    VPL_CATCH
}

namespace
{

VplResult apply_patch_desc(vpl::PolyPatch &patch, const VplPatchDesc *desc)
{
    if (desc == nullptr) {
        return VplResult_Success;
    }
    if (desc->structSize == 0) {
        set_error("VplPatchDesc.structSize is 0");
        return VplResult_InvalidArgument;
    }

    if ((desc->set & VplPatchField_FaceColorSpec) != 0 && desc->faceColorSpec != nullptr) {
        vpl::Color c;
        if (!vpl::parse_color(desc->faceColorSpec, c)) {
            set_error("unrecognized face colour spec");
            return VplResult_InvalidArgument;
        }
        patch.facecolor = c;
    }
    if ((desc->set & VplPatchField_FaceColor) != 0) {
        patch.facecolor = vpl::Color{desc->faceColor[0], desc->faceColor[1],
                                     desc->faceColor[2], desc->faceColor[3]};
    }
    if ((desc->set & VplPatchField_EdgeColor) != 0) {
        patch.edgecolor = vpl::Color{desc->edgeColor[0], desc->edgeColor[1],
                                     desc->edgeColor[2], desc->edgeColor[3]};
    }
    if ((desc->set & VplPatchField_EdgeWidth) != 0) {
        if (!(desc->edgeWidth >= 0.0)) {
            set_error("edgeWidth must be non-negative");
            return VplResult_InvalidArgument;
        }
        patch.linewidth = desc->edgeWidth;
    }
    if ((desc->set & VplPatchField_Alpha) != 0) {
        if (!(desc->alpha >= 0.0 && desc->alpha <= 1.0)) {
            set_error("alpha must be within [0, 1]");
            return VplResult_InvalidArgument;
        }
        patch.alpha = desc->alpha;
    }
    if ((desc->set & VplPatchField_Label) != 0 && desc->label != nullptr) {
        patch.label = desc->label;
    }
    return VplResult_Success;
}

} // namespace

VplResult VPL_CALL vplAxesFillBetween(VplAxes *axes,
                                      const double *x,
                                      const double *y1,
                                      const double *y2,
                                      size_t count,
                                      const uint8_t *where,
                                      int32_t interpolate,
                                      VplFillStep step,
                                      const VplPatchDesc *desc,
                                      VplPatch **outPatch)
{
    VPL_TRY
    if (axes == nullptr || x == nullptr || y1 == nullptr || y2 == nullptr) {
        set_error("axes, x, y1 or y2 is null");
        return VplResult_InvalidArgument;
    }
    if (count < 2) {
        set_error("fill_between needs at least 2 points");
        return VplResult_InvalidArgument;
    }

    vpl::FillStep fs;
    if (!to_fill_step(step, fs)) {
        set_error("unknown VplFillStep");
        return VplResult_InvalidArgument;
    }
    vpl::PolyPatch &patch =
        axes_of(axes)->fill_between(x, y1, y2, count, where, interpolate != 0, fs);
    const VplResult r = apply_patch_desc(patch, desc);
    if (r != VplResult_Success) {
        return r;
    }

    if (outPatch != nullptr) {
        *outPatch = reinterpret_cast<VplPatch *>(&patch);
    }
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesBar(VplAxes *axes,
                              const double *x,
                              const double *height,
                              double width,
                              const double *bottom,
                              size_t count,
                              const VplPatchDesc *desc,
                              VplPatch **outPatch)
{
    VPL_TRY
    if (axes == nullptr || x == nullptr || height == nullptr) {
        set_error("axes, x or height is null");
        return VplResult_InvalidArgument;
    }
    if (count == 0) {
        set_error("count is 0");
        return VplResult_InvalidArgument;
    }
    if (!(width > 0.0)) {
        set_error("bar width must be positive");
        return VplResult_InvalidArgument;
    }

    vpl::PolyPatch &patch = axes_of(axes)->bar(x, height, count, width, bottom);
    const VplResult r = apply_patch_desc(patch, desc);
    if (r != VplResult_Success) {
        return r;
    }

    if (outPatch != nullptr) {
        *outPatch = reinterpret_cast<VplPatch *>(&patch);
    }
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesStep(VplAxes *axes,
                               const double *x,
                               const double *y,
                               size_t count,
                               VplStepWhere where,
                               const VplLineDesc *desc,
                               VplLine **outLine)
{
    VPL_TRY
    if (axes == nullptr || x == nullptr || y == nullptr) {
        set_error("axes, x or y is null");
        return VplResult_InvalidArgument;
    }
    if (count == 0) {
        set_error("count is 0");
        return VplResult_InvalidArgument;
    }

    vpl::Axes::StepWhere w = vpl::Axes::StepWhere::Pre;
    switch (where) {
        case VplStepWhere_Pre: w = vpl::Axes::StepWhere::Pre; break;
        case VplStepWhere_Post: w = vpl::Axes::StepWhere::Post; break;
        case VplStepWhere_Mid: w = vpl::Axes::StepWhere::Mid; break;
        default:
            set_error("unknown VplStepWhere");
            return VplResult_InvalidArgument;
    }

    vpl::Line2D &line = axes_of(axes)->step(x, y, count, w);

    const VplResult r = apply_line_desc(line, desc);
    if (r != VplResult_Success) {
        return r;
    }
    if (desc != nullptr) {
        if ((desc->set & VplLineField_Color) != 0) {
            line.color = vpl::Color{desc->color[0], desc->color[1], desc->color[2],
                                    desc->color[3]};
        }
        if ((desc->set & VplLineField_Width) != 0) {
            line.linewidth = desc->width;
        }
        if ((desc->set & VplLineField_Label) != 0 && desc->label != nullptr) {
            line.label = desc->label;
        }
    }

    if (outLine != nullptr) {
        *outLine = reinterpret_cast<VplLine *>(&line);
    }
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesBarH(VplAxes *axes,
                               const double *y,
                               const double *width,
                               double height,
                               const double *left,
                               size_t count,
                               const VplPatchDesc *desc,
                               VplPatch **outPatch)
{
    VPL_TRY
    if (axes == nullptr || y == nullptr || width == nullptr) {
        set_error("axes, y or width is null");
        return VplResult_InvalidArgument;
    }
    if (count == 0) {
        set_error("count is 0");
        return VplResult_InvalidArgument;
    }
    if (!(height > 0.0)) {
        set_error("bar height must be positive");
        return VplResult_InvalidArgument;
    }

    vpl::PolyPatch &patch = axes_of(axes)->barh(y, width, count, height, left);
    const VplResult r = apply_patch_desc(patch, desc);
    if (r != VplResult_Success) {
        return r;
    }
    if (outPatch != nullptr) {
        *outPatch = reinterpret_cast<VplPatch *>(&patch);
    }
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesFillBetweenX(VplAxes *axes,
                                       const double *y,
                                       const double *x1,
                                       const double *x2,
                                       size_t count,
                                       const uint8_t *where,
                                       VplFillStep step,
                                       int32_t interpolate,
                                       const VplPatchDesc *desc,
                                       VplPatch **outPatch)
{
    VPL_TRY
    if (axes == nullptr || y == nullptr || x1 == nullptr || x2 == nullptr) {
        set_error("axes, y, x1 or x2 is null");
        return VplResult_InvalidArgument;
    }
    if (count == 0) {
        set_error("count is 0");
        return VplResult_InvalidArgument;
    }

    vpl::FillStep fs;
    if (!to_fill_step(step, fs)) {
        set_error("unknown VplFillStep");
        return VplResult_InvalidArgument;
    }
    vpl::PolyPatch &patch =
        axes_of(axes)->fill_betweenx(y, x1, x2, count, where, interpolate != 0, fs);
    const VplResult r = apply_patch_desc(patch, desc);
    if (r != VplResult_Success) {
        return r;
    }
    if (outPatch != nullptr) {
        *outPatch = reinterpret_cast<VplPatch *>(&patch);
    }
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesAxHSpan(VplAxes *axes, double ymin, double ymax)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    axes_of(axes)->axhspan(ymin, ymax);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesAxVSpan(VplAxes *axes, double xmin, double xmax)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    axes_of(axes)->axvspan(xmin, xmax);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesHist(VplAxes *axes,
                               const double *values,
                               size_t count,
                               size_t bins,
                               int32_t density,
                               const VplPatchDesc *desc,
                               VplPatch **outPatch)
{
    VPL_TRY
    if (axes == nullptr || values == nullptr) {
        set_error("axes or values is null");
        return VplResult_InvalidArgument;
    }
    if (count == 0) {
        set_error("count is 0");
        return VplResult_InvalidArgument;
    }

    vpl::PolyPatch &patch =
        axes_of(axes)->hist(values, count, bins == 0 ? 10 : bins, density != 0);
    const VplResult r = apply_patch_desc(patch, desc);
    if (r != VplResult_Success) {
        return r;
    }

    if (outPatch != nullptr) {
        *outPatch = reinterpret_cast<VplPatch *>(&patch);
    }
    return VplResult_Success;
    VPL_CATCH
}

namespace
{

/* Shared by the x and y tick setters, which differ only in which vectors they
   fill. Passing count 0 hands the axis back to its locator. */
VplResult set_fixed_ticks(std::vector<double> &positions,
                          std::vector<std::string> &labels,
                          bool &in_force,
                          const double *ticks,
                          const char *const *tick_labels,
                          size_t count)
{
    if (count != 0 && ticks == nullptr) {
        set_error("ticks is null but count is not 0");
        return VplResult_InvalidArgument;
    }

    /* A null pointer hands the axis back to its locator; a pointer with a count
       of zero means no ticks at all, which is what set_xticks([]) does. */
    in_force = ticks != nullptr;
    positions.assign(ticks, ticks + count);
    labels.clear();
    if (tick_labels != nullptr) {
        for (size_t i = 0; i < count; ++i) {
            labels.emplace_back(tick_labels[i] != nullptr ? tick_labels[i] : "");
        }
    }
    return VplResult_Success;
}

} // namespace

VplResult VPL_CALL vplAxesSetXTicks(VplAxes *axes,
                                    const double *ticks,
                                    const char *const *labels,
                                    size_t count)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    vpl::Axes *ax = axes_of(axes);
    return set_fixed_ticks(ax->xticks_fixed, ax->xticklabels_fixed,
                           ax->xticks_fixed_set, ticks, labels, count);
    VPL_CATCH
}

VplResult VPL_CALL vplAxesSetYTicks(VplAxes *axes,
                                    const double *ticks,
                                    const char *const *labels,
                                    size_t count)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    vpl::Axes *ax = axes_of(axes);
    return set_fixed_ticks(ax->yticks_fixed, ax->yticklabels_fixed,
                           ax->yticks_fixed_set, ticks, labels, count);
    VPL_CATCH
}

namespace
{

/* Shared by vplAxesGetXTicks/GetYTicks: run the locator against the axes'
   last-known pixel geometry and hand back one of the two resulting arrays. */
VplResult get_ticks_impl(const vpl::Axes *ax, bool want_x, double *buffer,
                         size_t bufferCount, size_t *outCount)
{
    if (ax == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    const AxesGeometry geom = geometry_for(ax);
    const vpl::Bbox box = ax->display_box(geom.width, geom.height);
    const double px_per_pt = geom.dpi / 72.0;

    std::vector<double> xticks, yticks;
    std::vector<std::string> xlabels, ylabels;
    ax->compute_ticks(box, px_per_pt, xticks, yticks, xlabels, ylabels);
    const std::vector<double> &ticks = want_x ? xticks : yticks;

    if (outCount != nullptr) {
        *outCount = ticks.size();
    }
    if (buffer == nullptr) {
        return VplResult_Success;
    }
    if (bufferCount < ticks.size()) {
        set_error("buffer too small");
        return VplResult_BufferTooSmall;
    }
    std::copy(ticks.begin(), ticks.end(), buffer);
    return VplResult_Success;
}

} // namespace

VplResult VPL_CALL vplAxesGetXTicks(const VplAxes *axes, double *buffer, size_t bufferCount,
                                    size_t *outCount)
{
    VPL_TRY
    return get_ticks_impl(axes_of(axes), /*want_x=*/true, buffer, bufferCount, outCount);
    VPL_CATCH
}

VplResult VPL_CALL vplAxesGetYTicks(const VplAxes *axes, double *buffer, size_t bufferCount,
                                    size_t *outCount)
{
    VPL_TRY
    return get_ticks_impl(axes_of(axes), /*want_x=*/false, buffer, bufferCount, outCount);
    VPL_CATCH
}

namespace
{

VplTickDirection tick_direction_to_abi(vpl::TickDirection d)
{
    switch (d) {
        case vpl::TickDirection::In:
            return VplTickDirection_In;
        case vpl::TickDirection::InOut:
            return VplTickDirection_InOut;
        case vpl::TickDirection::Out:
        default:
            return VplTickDirection_Out;
    }
}

VplTickParams tick_params_to_abi(const vpl::TickParams &t)
{
    VplTickParams out;
    out.direction = tick_direction_to_abi(t.direction);
    out.length = t.size;
    out.width = t.width;
    out.pad = t.pad;
    out.labelSize = t.label_size;
    out.ticksVisible = t.ticks_visible ? 1 : 0;
    out.labelsVisible = t.labels_visible ? 1 : 0;
    out.color[0] = static_cast<float>(t.color.r);
    out.color[1] = static_cast<float>(t.color.g);
    out.color[2] = static_cast<float>(t.color.b);
    out.color[3] = static_cast<float>(t.color.a);
    out.labelColor[0] = static_cast<float>(t.label_color.r);
    out.labelColor[1] = static_cast<float>(t.label_color.g);
    out.labelColor[2] = static_cast<float>(t.label_color.b);
    out.labelColor[3] = static_cast<float>(t.label_color.a);
    return out;
}

VplResult tick_params_from_abi(const VplTickParams &in, vpl::TickParams &out)
{
    switch (in.direction) {
        case VplTickDirection_Out:
            out.direction = vpl::TickDirection::Out;
            break;
        case VplTickDirection_In:
            out.direction = vpl::TickDirection::In;
            break;
        case VplTickDirection_InOut:
            out.direction = vpl::TickDirection::InOut;
            break;
        default:
            set_error("unknown tick direction");
            return VplResult_InvalidArgument;
    }
    if (!(in.length >= 0.0) || !(in.width >= 0.0) || !(in.labelSize > 0.0)) {
        set_error("tick length and width must be >= 0 and label size > 0");
        return VplResult_InvalidArgument;
    }
    out.size = in.length;
    out.width = in.width;
    out.pad = in.pad;
    out.label_size = in.labelSize;
    out.ticks_visible = in.ticksVisible != 0;
    out.labels_visible = in.labelsVisible != 0;
    out.color = vpl::Color{in.color[0], in.color[1], in.color[2], in.color[3]};
    out.label_color =
        vpl::Color{in.labelColor[0], in.labelColor[1], in.labelColor[2], in.labelColor[3]};
    return VplResult_Success;
}

} // namespace

VplResult VPL_CALL vplAxesGetTickParams(const VplAxes *axes,
                                        VplAxis axis,
                                        VplTickParams *outParams)
{
    VPL_TRY
    if (axes == nullptr || outParams == nullptr) {
        set_error("axes or outParams is null");
        return VplResult_InvalidArgument;
    }

    const vpl::Axes *ax = axes_of(const_cast<VplAxes *>(axes));
    switch (axis) {
        case VplAxis_X:
            *outParams = tick_params_to_abi(ax->xtick);
            return VplResult_Success;
        case VplAxis_Y:
            *outParams = tick_params_to_abi(ax->ytick);
            return VplResult_Success;
        default:
            /* The two axes can differ, so "both" has no single answer. */
            set_error("vplAxesGetTickParams needs one axis, not both");
            return VplResult_InvalidArgument;
    }
    VPL_CATCH
}

VplResult VPL_CALL vplAxesSetTickParams(VplAxes *axes,
                                        VplAxis axis,
                                        const VplTickParams *params)
{
    VPL_TRY
    if (axes == nullptr || params == nullptr) {
        set_error("axes or params is null");
        return VplResult_InvalidArgument;
    }
    if (axis != VplAxis_X && axis != VplAxis_Y && axis != VplAxis_Both) {
        set_error("unknown axis");
        return VplResult_InvalidArgument;
    }

    /* Validate into a scratch copy first so a rejected call leaves both axes
       unchanged rather than half-applied. */
    vpl::TickParams parsed;
    const VplResult r = tick_params_from_abi(*params, parsed);
    if (r != VplResult_Success) {
        return r;
    }

    vpl::Axes *ax = axes_of(axes);
    if (axis == VplAxis_X || axis == VplAxis_Both) {
        ax->xtick = parsed;
    }
    if (axis == VplAxis_Y || axis == VplAxis_Both) {
        ax->ytick = parsed;
    }
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesSetXInverted(VplAxes *axes, int inverted)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    axes_of(axes)->x_inverted = inverted != 0;
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesSetYInverted(VplAxes *axes, int inverted)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    axes_of(axes)->y_inverted = inverted != 0;
    return VplResult_Success;
    VPL_CATCH
}

namespace
{

vpl::SegmentCollection *segments_of(VplSegments *s)
{
    return reinterpret_cast<vpl::SegmentCollection *>(s);
}

VplResult apply_segments_desc(vpl::SegmentCollection &sc, const VplSegmentsDesc *desc)
{
    if (desc == nullptr) {
        return VplResult_Success;
    }
    if (desc->color != nullptr && !vpl::parse_color(desc->color, sc.color)) {
        set_error("unrecognised color");
        return VplResult_InvalidArgument;
    }
    if (desc->lineWidth > 0.0) {
        sc.linewidth = desc->lineWidth;
    }
    switch (desc->lineStyle) {
        case VplLineStyle_Solid: sc.linestyle = vpl::LineStyle::Solid; break;
        case VplLineStyle_Dashed: sc.linestyle = vpl::LineStyle::Dashed; break;
        case VplLineStyle_DashDot: sc.linestyle = vpl::LineStyle::DashDot; break;
        case VplLineStyle_Dotted: sc.linestyle = vpl::LineStyle::Dotted; break;
        case VplLineStyle_None: sc.linestyle = vpl::LineStyle::None; break;
        default:
            set_error("unknown line style");
            return VplResult_InvalidArgument;
    }
    if (desc->label != nullptr) {
        sc.label = desc->label;
    }
    return VplResult_Success;
}

} // namespace

VplResult VPL_CALL vplAxesHLines(VplAxes *axes,
                                 const double *y,
                                 const double *xmin,
                                 const double *xmax,
                                 size_t count,
                                 const VplSegmentsDesc *desc,
                                 VplSegments **outSegments)
{
    VPL_TRY
    if (axes == nullptr || y == nullptr || xmin == nullptr || xmax == nullptr) {
        set_error("axes, y, xmin or xmax is null");
        return VplResult_InvalidArgument;
    }
    if (count == 0) {
        set_error("count is 0");
        return VplResult_InvalidArgument;
    }

    vpl::SegmentCollection &sc = axes_of(axes)->hlines(y, xmin, xmax, count);
    const VplResult r = apply_segments_desc(sc, desc);
    if (r != VplResult_Success) {
        return r;
    }
    if (outSegments != nullptr) {
        *outSegments = reinterpret_cast<VplSegments *>(&sc);
    }
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesVLines(VplAxes *axes,
                                 const double *x,
                                 const double *ymin,
                                 const double *ymax,
                                 size_t count,
                                 const VplSegmentsDesc *desc,
                                 VplSegments **outSegments)
{
    VPL_TRY
    if (axes == nullptr || x == nullptr || ymin == nullptr || ymax == nullptr) {
        set_error("axes, x, ymin or ymax is null");
        return VplResult_InvalidArgument;
    }
    if (count == 0) {
        set_error("count is 0");
        return VplResult_InvalidArgument;
    }

    vpl::SegmentCollection &sc = axes_of(axes)->vlines(x, ymin, ymax, count);
    const VplResult r = apply_segments_desc(sc, desc);
    if (r != VplResult_Success) {
        return r;
    }
    if (outSegments != nullptr) {
        *outSegments = reinterpret_cast<VplSegments *>(&sc);
    }
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesEventPlot(VplAxes *axes,
                                    const double *positions,
                                    size_t count,
                                    double lineOffset,
                                    double lineLength,
                                    int horizontal,
                                    const VplSegmentsDesc *desc,
                                    VplSegments **outSegments)
{
    VPL_TRY
    if (axes == nullptr || positions == nullptr) {
        set_error("axes or positions is null");
        return VplResult_InvalidArgument;
    }
    if (count == 0) {
        set_error("count is 0");
        return VplResult_InvalidArgument;
    }
    if (!(lineLength > 0.0)) {
        set_error("lineLength must be positive");
        return VplResult_InvalidArgument;
    }

    vpl::SegmentCollection &sc = axes_of(axes)->eventplot(
        positions, count, lineOffset, lineLength, horizontal != 0);
    const VplResult r = apply_segments_desc(sc, desc);
    if (r != VplResult_Success) {
        return r;
    }
    if (outSegments != nullptr) {
        *outSegments = reinterpret_cast<VplSegments *>(&sc);
    }
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesStem(VplAxes *axes, const double *x, const double *y, size_t count,
                               const char *linefmt, const char *markerfmt, const char *basefmt,
                               double bottom)
{
    VPL_TRY
    if (axes == nullptr || x == nullptr || y == nullptr) {
        set_error("axes, x or y is null");
        return VplResult_InvalidArgument;
    }
    if (count == 0) {
        set_error("count is 0");
        return VplResult_InvalidArgument;
    }

    vpl::StemStyle style;
    style.bottom = bottom;

    vpl::Color c;
    vpl::LineStyle ls = vpl::LineStyle::Solid;
    vpl::MarkerStyle mk = vpl::MarkerStyle::Circle;
    bool hc = false, hls = false, hmk = false;

    /* linefmt gives the stalks their colour and dash; the marker colour then
       follows the stalks unless markerfmt overrides it. */
    if (linefmt != nullptr && linefmt[0] != '\0') {
        if (!vpl::parse_format(linefmt, c, hc, ls, hls, mk, hmk)) {
            set_error("could not parse linefmt");
            return VplResult_InvalidArgument;
        }
        if (hc) {
            style.line_color = c;
        }
        if (hls) {
            style.line_style = ls;
        }
    }
    style.marker_color = style.line_color;
    if (markerfmt != nullptr && markerfmt[0] != '\0') {
        hc = hls = hmk = false;
        if (!vpl::parse_format(markerfmt, c, hc, ls, hls, mk, hmk)) {
            set_error("could not parse markerfmt");
            return VplResult_InvalidArgument;
        }
        if (hmk) {
            style.marker = mk;
        }
        if (hc) {
            style.marker_color = c;
        }
    }
    if (basefmt != nullptr && basefmt[0] != '\0') {
        hc = hls = hmk = false;
        if (!vpl::parse_format(basefmt, c, hc, ls, hls, mk, hmk)) {
            set_error("could not parse basefmt");
            return VplResult_InvalidArgument;
        }
        if (hc) {
            style.base_color = c;
        }
        if (hls) {
            style.base_style = ls;
        }
    }

    axes_of(axes)->stem(x, y, count, style);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesStairs(VplAxes *axes,
                                 const double *values,
                                 const double *edges,
                                 size_t count,
                                 int32_t horizontal,
                                 const double *baseline,
                                 size_t baselineCount,
                                 int32_t fill,
                                 const VplPatchDesc *desc,
                                 VplPatch **outPatch)
{
    VPL_TRY
    if (axes == nullptr || values == nullptr || edges == nullptr) {
        set_error("axes, values or edges is null");
        return VplResult_InvalidArgument;
    }
    if (count == 0) {
        set_error("count is 0");
        return VplResult_InvalidArgument;
    }
    if (baselineCount != 0 && baselineCount != 1) {
        set_error("baselineCount must be 0 (default 0 baseline) or 1 (a scalar)");
        return VplResult_InvalidArgument;
    }

    vpl::PolyPatch &patch =
        axes_of(axes)->stairs(values, edges, count, fill != 0, baseline, baselineCount,
                              horizontal != 0);
    const VplResult r = apply_patch_desc(patch, desc);
    if (r != VplResult_Success) {
        return r;
    }
    if (outPatch != nullptr) {
        *outPatch = reinterpret_cast<VplPatch *>(&patch);
    }
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesFill(VplAxes *axes,
                               const double *x,
                               const double *y,
                               size_t count,
                               const VplPatchDesc *desc,
                               VplPatch **outPatch)
{
    VPL_TRY
    if (axes == nullptr || x == nullptr || y == nullptr) {
        set_error("axes, x or y is null");
        return VplResult_InvalidArgument;
    }
    if (count < 3) {
        set_error("a polygon needs at least 3 points");
        return VplResult_InvalidArgument;
    }

    vpl::PolyPatch &patch = axes_of(axes)->fill(x, y, count);
    const VplResult r = apply_patch_desc(patch, desc);
    if (r != VplResult_Success) {
        return r;
    }
    if (outPatch != nullptr) {
        *outPatch = reinterpret_cast<VplPatch *>(&patch);
    }
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesBrokenBarH(VplAxes *axes,
                                     const double *xStart,
                                     const double *xWidth,
                                     size_t count,
                                     double y0,
                                     double height,
                                     const VplPatchDesc *desc,
                                     VplPatch **outPatch)
{
    VPL_TRY
    if (axes == nullptr || xStart == nullptr || xWidth == nullptr) {
        set_error("axes, xStart or xWidth is null");
        return VplResult_InvalidArgument;
    }
    if (count == 0) {
        set_error("count is 0");
        return VplResult_InvalidArgument;
    }
    if (!(height > 0.0)) {
        set_error("height must be positive");
        return VplResult_InvalidArgument;
    }

    vpl::PolyPatch &patch = axes_of(axes)->broken_barh(xStart, xWidth, count, y0, height);
    const VplResult r = apply_patch_desc(patch, desc);
    if (r != VplResult_Success) {
        return r;
    }
    if (outPatch != nullptr) {
        *outPatch = reinterpret_cast<VplPatch *>(&patch);
    }
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesStackPlot(VplAxes *axes,
                                    const double *x,
                                    const double *ys,
                                    size_t seriesCount,
                                    size_t count,
                                    VplStackBaseline baseline)
{
    VPL_TRY
    if (axes == nullptr || x == nullptr || ys == nullptr) {
        set_error("axes, x or ys is null");
        return VplResult_InvalidArgument;
    }
    if (seriesCount == 0 || count == 0) {
        set_error("seriesCount and count must both be non-zero");
        return VplResult_InvalidArgument;
    }
    vpl::StackBaseline b;
    switch (baseline) {
        case VplStackBaseline_Zero: b = vpl::StackBaseline::Zero; break;
        case VplStackBaseline_Sym: b = vpl::StackBaseline::Sym; break;
        case VplStackBaseline_Wiggle: b = vpl::StackBaseline::Wiggle; break;
        case VplStackBaseline_WeightedWiggle: b = vpl::StackBaseline::WeightedWiggle; break;
        default: set_error("unknown VplStackBaseline"); return VplResult_InvalidArgument;
    }
    axes_of(axes)->stackplot(x, ys, seriesCount, count, b);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesBoxPlot(VplAxes *axes,
                                  const double *values,
                                  const size_t *counts,
                                  size_t groupCount)
{
    VPL_TRY
    if (axes == nullptr || values == nullptr || counts == nullptr) {
        set_error("axes, values or counts is null");
        return VplResult_InvalidArgument;
    }
    if (groupCount == 0) {
        set_error("groupCount is 0");
        return VplResult_InvalidArgument;
    }
    for (size_t g = 0; g < groupCount; ++g) {
        if (counts[g] == 0) {
            set_error("every group needs at least one value");
            return VplResult_InvalidArgument;
        }
    }
    axes_of(axes)->boxplot(values, counts, groupCount);
    return VplResult_Success;
    VPL_CATCH
}

namespace
{

/* Fill-a-buffer / report-required-length helper for the string accessors. The
   reported length includes the null terminator. */
VplResult copy_out(const std::string &s, char *buffer, size_t bufferSize,
                   size_t *outRequiredSize)
{
    const size_t required = s.size() + 1;
    if (outRequiredSize != nullptr) {
        *outRequiredSize = required;
    }
    if (buffer == nullptr) {
        return VplResult_Success;
    }
    if (bufferSize < required) {
        set_error("buffer too small");
        return VplResult_BufferTooSmall;
    }
    std::memcpy(buffer, s.c_str(), required);
    return VplResult_Success;
}

/* Every label a legend would show, in the order it would show them. */
std::vector<std::string> legend_labels_of(const vpl::Axes &ax)
{
    std::vector<std::string> out;
    for (const auto &l : ax.lines) {
        if (!l->label.empty()) {
            out.push_back(l->label);
        }
    }
    for (const auto &p : ax.patches) {
        if (!p->label.empty()) {
            out.push_back(p->label);
        }
    }
    for (const auto &s : ax.collections) {
        if (!s->label.empty()) {
            out.push_back(s->label);
        }
    }
    return out;
}

} // namespace

VplResult VPL_CALL vplAxesQuiver(VplAxes *axes,
                                 const double *x,
                                 const double *y,
                                 const double *u,
                                 const double *v,
                                 size_t count,
                                 const VplQuiverDesc *desc)
{
    VPL_TRY
    if (axes == nullptr || x == nullptr || y == nullptr || u == nullptr || v == nullptr) {
        set_error("axes or one of x, y, u, v is null");
        return VplResult_InvalidArgument;
    }
    if (count == 0) {
        set_error("count is 0");
        return VplResult_InvalidArgument;
    }
    if (desc != nullptr && desc->structSize != sizeof(VplQuiverDesc)) {
        set_error("VplQuiverDesc.structSize does not match this header");
        return VplResult_InvalidArgument;
    }

    vpl::QuiverField &q = axes_of(axes)->quiver(x, y, u, v, count);
    if (desc == nullptr) {
        return VplResult_Success;
    }
    if ((desc->set & VplQuiverField_Scale) != 0) {
        if (!(desc->scale > 0.0)) {
            set_error("scale must be positive");
            return VplResult_InvalidArgument;
        }
        q.scale = desc->scale;
        q.has_scale = true;
    }
    if ((desc->set & VplQuiverField_Width) != 0) {
        q.width = desc->width;
        q.has_width = true;
    }
    if ((desc->set & VplQuiverField_ColorSpec) != 0 && desc->colorSpec != nullptr) {
        vpl::Color c;
        if (!vpl::parse_color(desc->colorSpec, c)) {
            set_error("unrecognized colour spec");
            return VplResult_InvalidArgument;
        }
        q.color = c;
    }
    if ((desc->set & VplQuiverField_Pivot) != 0) {
        switch (desc->pivot) {
            case VplQuiverPivot_Middle: q.pivot = vpl::QuiverPivot::Middle; break;
            case VplQuiverPivot_Tip: q.pivot = vpl::QuiverPivot::Tip; break;
            default: q.pivot = vpl::QuiverPivot::Tail; break;
        }
    }
    if ((desc->set & VplQuiverField_Label) != 0 && desc->label != nullptr) {
        q.label = desc->label;
    }
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesQuiverKey(VplAxes *axes,
                                    double x,
                                    double y,
                                    double magnitude,
                                    const char *label,
                                    double fontSize)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    vpl::Axes *ax = axes_of(axes);
    if (ax->quivers.empty()) {
        set_error("no quiver field to key; call vplAxesQuiver first");
        return VplResult_InvalidArgument;
    }
    vpl::QuiverKey &k = ax->quiverkey(*ax->quivers.back(), x, y, magnitude,
                                      label != nullptr ? label : "");
    if (fontSize > 0.0) {
        k.fontsize = fontSize;
    }
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesBarbs(VplAxes *axes,
                                const double *x,
                                const double *y,
                                const double *u,
                                const double *v,
                                size_t count,
                                const VplBarbsDesc *desc)
{
    VPL_TRY
    if (axes == nullptr || x == nullptr || y == nullptr || u == nullptr || v == nullptr) {
        set_error("axes or one of x, y, u, v is null");
        return VplResult_InvalidArgument;
    }
    if (count == 0) {
        set_error("count is 0");
        return VplResult_InvalidArgument;
    }
    if (desc != nullptr && desc->structSize != sizeof(VplBarbsDesc)) {
        set_error("VplBarbsDesc.structSize does not match this header");
        return VplResult_InvalidArgument;
    }

    vpl::BarbField &b = axes_of(axes)->barbs(x, y, u, v, count);
    if (desc == nullptr) {
        return VplResult_Success;
    }
    if ((desc->set & VplBarbsField_Length) != 0) {
        if (!(desc->length > 0.0)) {
            set_error("length must be positive");
            return VplResult_InvalidArgument;
        }
        b.length = desc->length;
    }
    if ((desc->set & VplBarbsField_Increments) != 0) {
        if (!(desc->half > 0.0) || !(desc->full > 0.0) || !(desc->flag > 0.0)) {
            set_error("half, full and flag must all be positive");
            return VplResult_InvalidArgument;
        }
        b.half = desc->half;
        b.full = desc->full;
        b.flag = desc->flag;
    }
    if ((desc->set & VplBarbsField_Rounding) != 0) {
        b.rounding = desc->rounding != 0;
    }
    if ((desc->set & VplBarbsField_Flip) != 0) {
        b.flip = desc->flip != 0;
    }
    if ((desc->set & VplBarbsField_FillEmpty) != 0) {
        b.fill_empty = desc->fillEmpty != 0;
    }
    if ((desc->set & VplBarbsField_PivotMiddle) != 0) {
        b.pivot_middle = desc->pivotMiddle != 0;
    }
    if ((desc->set & VplBarbsField_ColorSpec) != 0 && desc->colorSpec != nullptr) {
        vpl::Color c;
        if (!vpl::parse_color(desc->colorSpec, c)) {
            set_error("unrecognized colour spec");
            return VplResult_InvalidArgument;
        }
        b.color = c;
    }
    if ((desc->set & VplBarbsField_LineWidth) != 0) {
        b.linewidth = desc->lineWidth;
    }
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesGroupedBar(VplAxes *axes,
                                     const double *heights,
                                     size_t datasetCount,
                                     size_t groupCount,
                                     const char *const *tickLabels,
                                     const char *const *labels,
                                     double groupSpacing,
                                     double barSpacing)
{
    VPL_TRY
    if (axes == nullptr || heights == nullptr) {
        set_error("axes or heights is null");
        return VplResult_InvalidArgument;
    }
    if (datasetCount == 0 || groupCount == 0) {
        set_error("datasetCount and groupCount must both be nonzero");
        return VplResult_InvalidArgument;
    }

    std::vector<std::string> ticks;
    std::vector<std::string> names;
    if (tickLabels != nullptr) {
        ticks.resize(groupCount);
        for (size_t g = 0; g < groupCount; ++g) {
            ticks[g] = tickLabels[g] != nullptr ? tickLabels[g] : "";
        }
    }
    if (labels != nullptr) {
        names.resize(datasetCount);
        for (size_t i = 0; i < datasetCount; ++i) {
            names[i] = labels[i] != nullptr ? labels[i] : "";
        }
    }

    axes_of(axes)->grouped_bar(heights, datasetCount, groupCount,
                               ticks.empty() ? nullptr : ticks.data(),
                               names.empty() ? nullptr : names.data(), groupSpacing,
                               barSpacing);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesXCorr(VplAxes *axes,
                                const double *x,
                                const double *y,
                                size_t count,
                                int maxLags,
                                int normed)
{
    VPL_TRY
    if (axes == nullptr || x == nullptr || y == nullptr) {
        set_error("axes, x or y is null");
        return VplResult_InvalidArgument;
    }
    if (maxLags < 1 || static_cast<size_t>(maxLags) >= count) {
        set_error("maxLags must be at least 1 and less than count");
        return VplResult_InvalidArgument;
    }
    axes_of(axes)->xcorr(x, y, count, maxLags, normed != 0);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesACorr(VplAxes *axes,
                                const double *x,
                                size_t count,
                                int maxLags,
                                int normed)
{
    return vplAxesXCorr(axes, x, x, count, maxLags, normed);
}

namespace
{

/* Shared validation for the seven spectral entry points: they differ in what
   they draw and not in what they reject. */
VplResult check_spectral(const VplAxes *axes, const double *x, size_t count, size_t nfft,
                         double fs, size_t noverlap)
{
    if (axes == nullptr || x == nullptr) {
        set_error("axes or x is null");
        return VplResult_InvalidArgument;
    }
    if (nfft == 0 || count < nfft) {
        set_error("count must be at least nfft, and nfft nonzero");
        return VplResult_InvalidArgument;
    }
    if (noverlap >= nfft) {
        set_error("noverlap must be less than nfft");
        return VplResult_InvalidArgument;
    }
    if (!(fs > 0.0)) {
        set_error("fs must be positive");
        return VplResult_InvalidArgument;
    }
    return VplResult_Success;
}

} // namespace

VplResult VPL_CALL vplAxesPsd(VplAxes *axes, const double *x, size_t count, size_t nfft,
                              double fs, size_t noverlap, const VplSpectralDesc *desc)
{
    VPL_TRY
    const VplResult bad = check_spectral(axes, x, count, nfft, fs, noverlap);
    if (bad != VplResult_Success) {
        return bad;
    }
    vpl::SpectralOptions opts;
    if (!to_spectral_options(desc, opts)) {
        return VplResult_InvalidArgument;
    }
    axes_of(axes)->psd(x, count, nfft, fs, noverlap, opts);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesCsd(VplAxes *axes, const double *x, const double *y,
                              size_t count, size_t nfft, double fs, size_t noverlap,
                              const VplSpectralDesc *desc)
{
    VPL_TRY
    const VplResult bad = check_spectral(axes, x, count, nfft, fs, noverlap);
    if (bad != VplResult_Success) {
        return bad;
    }
    if (y == nullptr) {
        set_error("y is null");
        return VplResult_InvalidArgument;
    }
    vpl::SpectralOptions opts;
    if (!to_spectral_options(desc, opts)) {
        return VplResult_InvalidArgument;
    }
    axes_of(axes)->csd(x, y, count, nfft, fs, noverlap, opts);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesCohere(VplAxes *axes, const double *x, const double *y,
                                 size_t count, size_t nfft, double fs, size_t noverlap,
                                 const VplSpectralDesc *desc)
{
    VPL_TRY
    const VplResult bad = check_spectral(axes, x, count, nfft, fs, noverlap);
    if (bad != VplResult_Success) {
        return bad;
    }
    if (y == nullptr) {
        set_error("y is null");
        return VplResult_InvalidArgument;
    }
    vpl::SpectralOptions opts;
    if (!to_spectral_options(desc, opts)) {
        return VplResult_InvalidArgument;
    }
    axes_of(axes)->cohere(x, y, count, nfft, fs, noverlap, opts);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesMagnitudeSpectrum(VplAxes *axes, const double *x, size_t count,
                                            double fs, int scaleDb, const VplSpectralDesc *desc)
{
    VPL_TRY
    const VplResult bad = check_spectral(axes, x, count, count, fs, 0);
    if (bad != VplResult_Success) {
        return bad;
    }
    vpl::SpectralOptions opts;
    if (!to_spectral_options(desc, opts)) {
        return VplResult_InvalidArgument;
    }
    axes_of(axes)->magnitude_spectrum(x, count, fs, scaleDb != 0, opts);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesAngleSpectrum(VplAxes *axes, const double *x, size_t count,
                                        double fs, const VplSpectralDesc *desc)
{
    VPL_TRY
    const VplResult bad = check_spectral(axes, x, count, count, fs, 0);
    if (bad != VplResult_Success) {
        return bad;
    }
    vpl::SpectralOptions opts;
    if (!to_spectral_options(desc, opts)) {
        return VplResult_InvalidArgument;
    }
    axes_of(axes)->angle_spectrum(x, count, fs, opts);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesPhaseSpectrum(VplAxes *axes, const double *x, size_t count,
                                        double fs, const VplSpectralDesc *desc)
{
    VPL_TRY
    const VplResult bad = check_spectral(axes, x, count, count, fs, 0);
    if (bad != VplResult_Success) {
        return bad;
    }
    vpl::SpectralOptions opts;
    if (!to_spectral_options(desc, opts)) {
        return VplResult_InvalidArgument;
    }
    axes_of(axes)->phase_spectrum(x, count, fs, opts);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesSpecgram(VplAxes *axes, const double *x, size_t count,
                                   size_t nfft, double fs, size_t noverlap,
                                   const VplSpectralDesc *desc, VplSpecMode mode,
                                   VplSpecScale scale, const double *xextent, VplColormap cmap)
{
    VPL_TRY
    const VplResult bad = check_spectral(axes, x, count, nfft, fs, noverlap);
    if (bad != VplResult_Success) {
        return bad;
    }
    vpl::SpectralOptions opts;
    if (!to_spectral_options(desc, opts)) {
        return VplResult_InvalidArgument;
    }
    vpl::SpectralMode m;
    switch (mode) {
        case VplSpecMode_Psd: m = vpl::SpectralMode::Psd; break;
        case VplSpecMode_Magnitude: m = vpl::SpectralMode::Magnitude; break;
        case VplSpecMode_Angle: m = vpl::SpectralMode::Angle; break;
        case VplSpecMode_Phase: m = vpl::SpectralMode::Phase; break;
        default: set_error("unknown VplSpecMode"); return VplResult_InvalidArgument;
    }
    vpl::SpecgramScale sc;
    switch (scale) {
        case VplSpecScale_Default: sc = vpl::SpecgramScale::Default; break;
        case VplSpecScale_Linear: sc = vpl::SpecgramScale::Linear; break;
        case VplSpecScale_Db: sc = vpl::SpecgramScale::Db; break;
        default: set_error("unknown VplSpecScale"); return VplResult_InvalidArgument;
    }
    if (sc == vpl::SpecgramScale::Db &&
        (m == vpl::SpectralMode::Angle || m == vpl::SpectralMode::Phase)) {
        set_error("cannot use dB scale with the angle or phase mode");
        return VplResult_InvalidArgument;
    }
    vpl::Colormap cm;
    if (!to_colormap(cmap, cm)) {
        set_error("unknown VplColormap");
        return VplResult_InvalidArgument;
    }
    axes_of(axes)->specgram(x, count, nfft, fs, noverlap, opts, m, sc, xextent, cm);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesStreamPlot(VplAxes *axes,
                                     const double *x,
                                     const double *y,
                                     const double *u,
                                     const double *v,
                                     size_t rows,
                                     size_t cols,
                                     double density)
{
    VPL_TRY
    if (axes == nullptr || x == nullptr || y == nullptr || u == nullptr || v == nullptr) {
        set_error("axes or one of x, y, u, v is null");
        return VplResult_InvalidArgument;
    }
    if (rows < 2 || cols < 2) {
        set_error("rows and cols must both be at least 2");
        return VplResult_InvalidArgument;
    }
    if (!(density > 0.0)) {
        set_error("density must be positive");
        return VplResult_InvalidArgument;
    }
    for (size_t i = 1; i < cols; ++i) {
        if (!(x[i] > x[i - 1])) {
            set_error("x must be strictly increasing");
            return VplResult_InvalidArgument;
        }
    }
    for (size_t i = 1; i < rows; ++i) {
        if (!(y[i] > y[i - 1])) {
            set_error("y must be strictly increasing");
            return VplResult_InvalidArgument;
        }
    }
    axes_of(axes)->streamplot(x, y, u, v, rows, cols, density);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesPieLabel(VplAxes *axes,
                                   const char *const *labels,
                                   size_t count,
                                   double distance,
                                   double fontSize)
{
    VPL_TRY
    if (axes == nullptr || labels == nullptr) {
        set_error("axes or labels is null");
        return VplResult_InvalidArgument;
    }
    if (count == 0) {
        set_error("count is 0");
        return VplResult_InvalidArgument;
    }
    std::vector<std::string> text(count);
    for (size_t i = 0; i < count; ++i) {
        text[i] = labels[i] != nullptr ? labels[i] : "";
    }
    axes_of(axes)->pie_label(text.data(), count, distance,
                             fontSize > 0.0 ? fontSize : 10.0);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesSetAxisOn(VplAxes *axes, int on)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    axes_of(axes)->axis_on = (on != 0);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesGetAxisOn(const VplAxes *axes, int *outOn)
{
    VPL_TRY
    if (axes == nullptr || outOn == nullptr) {
        set_error("axes or outOn is null");
        return VplResult_InvalidArgument;
    }
    *outOn = axes_of(axes)->axis_on ? 1 : 0;
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesSetAxisBelow(VplAxes *axes, VplAxisBelow mode)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    vpl::AxisBelow ab;
    switch (mode) {
        case VplAxisBelow_On: ab = vpl::AxisBelow::On; break;
        case VplAxisBelow_Line: ab = vpl::AxisBelow::Line; break;
        case VplAxisBelow_Off: ab = vpl::AxisBelow::Off; break;
        default:
            set_error("mode is not a valid VplAxisBelow");
            return VplResult_InvalidArgument;
    }
    axes_of(axes)->axisbelow = ab;
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesGetAxisBelow(const VplAxes *axes, VplAxisBelow *outMode)
{
    VPL_TRY
    if (axes == nullptr || outMode == nullptr) {
        set_error("axes or outMode is null");
        return VplResult_InvalidArgument;
    }
    switch (axes_of(axes)->axisbelow) {
        case vpl::AxisBelow::On: *outMode = VplAxisBelow_On; break;
        case vpl::AxisBelow::Line: *outMode = VplAxisBelow_Line; break;
        case vpl::AxisBelow::Off: *outMode = VplAxisBelow_Off; break;
    }
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesShareX(VplAxes *axes, const VplAxes *other)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    axes_of(axes)->shared_x = axes_of(other);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesShareY(VplAxes *axes, const VplAxes *other)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    axes_of(axes)->shared_y = axes_of(other);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesLabelOuter(VplAxes *axes, int removeInnerTicks)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    axes_of(axes)->label_outer(removeInnerTicks != 0);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesSetMargins(VplAxes *axes, double xmargin, double ymargin)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    if (xmargin < 0.0 || ymargin < 0.0) {
        set_error("margins must not be negative");
        return VplResult_InvalidArgument;
    }
    vpl::Axes *ax = axes_of(axes);
    ax->xmargin = xmargin;
    ax->ymargin = ymargin;
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesGetMargins(const VplAxes *axes, double *outXMargin,
                                     double *outYMargin)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    const vpl::Axes *ax = axes_of(axes);
    if (outXMargin != nullptr) {
        *outXMargin = ax->xmargin;
    }
    if (outYMargin != nullptr) {
        *outYMargin = ax->ymargin;
    }
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesSetXMargin(VplAxes *axes, double margin)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    /* matplotlib allows a margin down to -0.5, past which the near and far
       edges would cross. */
    if (!(margin > -0.5)) {
        set_error("margin must be greater than -0.5");
        return VplResult_InvalidArgument;
    }
    axes_of(axes)->xmargin = margin;
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesSetYMargin(VplAxes *axes, double margin)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    if (!(margin > -0.5)) {
        set_error("margin must be greater than -0.5");
        return VplResult_InvalidArgument;
    }
    axes_of(axes)->ymargin = margin;
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesGetXMargin(const VplAxes *axes, double *outMargin)
{
    VPL_TRY
    if (axes == nullptr || outMargin == nullptr) {
        set_error("axes or outMargin is null");
        return VplResult_InvalidArgument;
    }
    *outMargin = axes_of(axes)->xmargin;
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesGetYMargin(const VplAxes *axes, double *outMargin)
{
    VPL_TRY
    if (axes == nullptr || outMargin == nullptr) {
        set_error("axes or outMargin is null");
        return VplResult_InvalidArgument;
    }
    *outMargin = axes_of(axes)->ymargin;
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesSetPosition(VplAxes *axes, double left, double bottom,
                                      double width, double height)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    vpl::Axes *ax = axes_of(axes);
    ax->position = vpl::Bbox(left, bottom, left + width, bottom + height);
    /* An explicit position takes the axes out of the grid, so subplots_adjust
       and tight_layout leave it where it was put -- the same as add_axes. */
    ax->grid_rows = 0;
    ax->grid_cols = 0;
    ax->grid_index = 0;
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesGetPosition(const VplAxes *axes, double *outLeft, double *outBottom,
                                      double *outWidth, double *outHeight)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    const vpl::Bbox &p = axes_of(axes)->position;
    if (outLeft != nullptr) {
        *outLeft = p.x0;
    }
    if (outBottom != nullptr) {
        *outBottom = p.y0;
    }
    if (outWidth != nullptr) {
        *outWidth = p.x1 - p.x0;
    }
    if (outHeight != nullptr) {
        *outHeight = p.y1 - p.y0;
    }
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesGetXLabel(const VplAxes *axes, char *buffer, size_t bufferSize,
                                    size_t *outRequiredSize)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    return copy_out(axes_of(axes)->xlabel, buffer, bufferSize, outRequiredSize);
    VPL_CATCH
}

VplResult VPL_CALL vplAxesGetYLabel(const VplAxes *axes, char *buffer, size_t bufferSize,
                                    size_t *outRequiredSize)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    return copy_out(axes_of(axes)->ylabel, buffer, bufferSize, outRequiredSize);
    VPL_CATCH
}

VplResult VPL_CALL vplAxesGetXScale(const VplAxes *axes, VplScale *outScale)
{
    VPL_TRY
    if (axes == nullptr || outScale == nullptr) {
        set_error("axes or outScale is null");
        return VplResult_InvalidArgument;
    }
    *outScale = axes_of(axes)->xscale == vpl::ScaleType::Log ? VplScale_Log : VplScale_Linear;
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesGetYScale(const VplAxes *axes, VplScale *outScale)
{
    VPL_TRY
    if (axes == nullptr || outScale == nullptr) {
        set_error("axes or outScale is null");
        return VplResult_InvalidArgument;
    }
    *outScale = axes_of(axes)->yscale == vpl::ScaleType::Log ? VplScale_Log : VplScale_Linear;
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesSetFrameOn(VplAxes *axes, int on)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    vpl::Axes *ax = axes_of(axes);
    /* frame_on governs all four spines AND the background together, which is
       what makes it different from turning each spine off by hand. */
    ax->spine_left = ax->spine_right = ax->spine_top = ax->spine_bottom = (on != 0);
    ax->patch_visible = (on != 0);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesGetFrameOn(const VplAxes *axes, int *outOn)
{
    VPL_TRY
    if (axes == nullptr || outOn == nullptr) {
        set_error("axes or outOn is null");
        return VplResult_InvalidArgument;
    }
    const vpl::Axes *ax = axes_of(axes);
    *outOn = (ax->spine_left || ax->spine_right || ax->spine_top || ax->spine_bottom) ? 1 : 0;
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesSetAspect(VplAxes *axes, double aspect)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    if (aspect < 0.0) {
        set_error("aspect must not be negative; 0 means auto");
        return VplResult_InvalidArgument;
    }
    axes_of(axes)->aspect = aspect;
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesGetAspect(const VplAxes *axes, double *outAspect)
{
    VPL_TRY
    if (axes == nullptr || outAspect == nullptr) {
        set_error("axes or outAspect is null");
        return VplResult_InvalidArgument;
    }
    *outAspect = axes_of(axes)->aspect;
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesHasData(const VplAxes *axes, int *outHasData)
{
    VPL_TRY
    if (axes == nullptr || outHasData == nullptr) {
        set_error("axes or outHasData is null");
        return VplResult_InvalidArgument;
    }
    const vpl::Axes *ax = axes_of(axes);
    *outHasData = (!ax->lines.empty() || !ax->patches.empty() || !ax->collections.empty() ||
                   !ax->images.empty() || !ax->contours.empty() || !ax->segment_collections.empty() ||
                   !ax->quivers.empty() || !ax->barb_fields.empty() || !ax->streams.empty())
                      ? 1
                      : 0;
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesGetTitle(const VplAxes *axes,
                                   char *buffer,
                                   size_t bufferSize,
                                   size_t *outRequiredSize)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    return copy_out(axes_of(axes)->title, buffer, bufferSize, outRequiredSize);
    VPL_CATCH
}

VplResult VPL_CALL vplFigureGetSupTitle(const VplFigure *figure, char *buffer,
                                        size_t bufferSize, size_t *outRequiredSize)
{
    VPL_TRY
    if (figure == nullptr) {
        set_error("figure is null");
        return VplResult_InvalidArgument;
    }
    return copy_out(impl(figure)->figure.figure_title, buffer, bufferSize, outRequiredSize);
    VPL_CATCH
}

VplResult VPL_CALL vplFigureGetSupXLabel(const VplFigure *figure, char *buffer,
                                         size_t bufferSize, size_t *outRequiredSize)
{
    VPL_TRY
    if (figure == nullptr) {
        set_error("figure is null");
        return VplResult_InvalidArgument;
    }
    return copy_out(impl(figure)->figure.figure_xlabel, buffer, bufferSize, outRequiredSize);
    VPL_CATCH
}

VplResult VPL_CALL vplFigureGetSupYLabel(const VplFigure *figure, char *buffer,
                                         size_t bufferSize, size_t *outRequiredSize)
{
    VPL_TRY
    if (figure == nullptr) {
        set_error("figure is null");
        return VplResult_InvalidArgument;
    }
    return copy_out(impl(figure)->figure.figure_ylabel, buffer, bufferSize, outRequiredSize);
    VPL_CATCH
}

VplResult VPL_CALL vplAxesGetLegendLabelCount(const VplAxes *axes, size_t *outCount)
{
    VPL_TRY
    if (axes == nullptr || outCount == nullptr) {
        set_error("axes or outCount is null");
        return VplResult_InvalidArgument;
    }
    *outCount = legend_labels_of(*axes_of(axes)).size();
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesGetLegendLabel(const VplAxes *axes,
                                         size_t index,
                                         char *buffer,
                                         size_t bufferSize,
                                         size_t *outRequiredSize)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    const std::vector<std::string> labels = legend_labels_of(*axes_of(axes));
    if (index >= labels.size()) {
        set_error("index is out of range");
        return VplResult_InvalidArgument;
    }
    return copy_out(labels[index], buffer, bufferSize, outRequiredSize);
    VPL_CATCH
}

VplResult VPL_CALL vplAxesGetLineCount(const VplAxes *axes, size_t *outCount)
{
    VPL_TRY
    if (axes == nullptr || outCount == nullptr) {
        set_error("axes or outCount is null");
        return VplResult_InvalidArgument;
    }
    *outCount = axes_of(axes)->lines.size();
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesGetLine(const VplAxes *axes, size_t index, VplLine **outLine)
{
    VPL_TRY
    if (axes == nullptr || outLine == nullptr) {
        set_error("axes or outLine is null");
        return VplResult_InvalidArgument;
    }
    const auto &lines = axes_of(axes)->lines;
    if (index >= lines.size()) {
        set_error("index is out of range");
        return VplResult_InvalidArgument;
    }
    *outLine = reinterpret_cast<VplLine *>(lines[index].get());
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesGetPatchCount(const VplAxes *axes, size_t *outCount)
{
    VPL_TRY
    if (axes == nullptr || outCount == nullptr) {
        set_error("axes or outCount is null");
        return VplResult_InvalidArgument;
    }
    *outCount = axes_of(axes)->patches.size();
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesGetPatch(const VplAxes *axes, size_t index, VplPatch **outPatch)
{
    VPL_TRY
    if (axes == nullptr || outPatch == nullptr) {
        set_error("axes or outPatch is null");
        return VplResult_InvalidArgument;
    }
    const auto &patches = axes_of(axes)->patches;
    if (index >= patches.size()) {
        set_error("index is out of range");
        return VplResult_InvalidArgument;
    }
    *outPatch = reinterpret_cast<VplPatch *>(patches[index].get());
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesGetImageCount(const VplAxes *axes, size_t *outCount)
{
    VPL_TRY
    if (axes == nullptr || outCount == nullptr) {
        set_error("axes or outCount is null");
        return VplResult_InvalidArgument;
    }
    *outCount = axes_of(axes)->images.size();
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesGetImage(const VplAxes *axes, size_t index, VplImage **outImage)
{
    VPL_TRY
    if (axes == nullptr || outImage == nullptr) {
        set_error("axes or outImage is null");
        return VplResult_InvalidArgument;
    }
    const auto &images = axes_of(axes)->images;
    if (index >= images.size()) {
        set_error("index is out of range");
        return VplResult_InvalidArgument;
    }
    *outImage = reinterpret_cast<VplImage *>(images[index].get());
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesTable(VplAxes *axes,
                                const char *const *cellText,
                                size_t rows,
                                size_t cols,
                                const VplTableDesc *desc)
{
    VPL_TRY
    if (axes == nullptr || cellText == nullptr) {
        set_error("axes or cellText is null");
        return VplResult_InvalidArgument;
    }
    if (rows == 0 || cols == 0) {
        set_error("rows and cols must both be nonzero");
        return VplResult_InvalidArgument;
    }
    if (desc != nullptr && desc->structSize != sizeof(VplTableDesc)) {
        set_error("VplTableDesc.structSize does not match this header");
        return VplResult_InvalidArgument;
    }

    /* A null entry is an empty cell, not an error. */
    std::vector<std::string> cells(rows * cols);
    for (size_t i = 0; i < rows * cols; ++i) {
        cells[i] = cellText[i] != nullptr ? cellText[i] : "";
    }

    vpl::Table &t = axes_of(axes)->table(cells.data(), rows, cols);

    if (desc == nullptr) {
        return VplResult_Success;
    }

    auto to_align = [](VplHAlign a) {
        switch (a) {
            case VplHAlign_Left: return vpl::HAlign::Left;
            case VplHAlign_Right: return vpl::HAlign::Right;
            default: return vpl::HAlign::Center;
        }
    };

    if ((desc->set & VplTableField_ColLabels) != 0 && desc->colLabels != nullptr) {
        t.col_labels.resize(cols);
        for (size_t c = 0; c < cols; ++c) {
            t.col_labels[c] = desc->colLabels[c] != nullptr ? desc->colLabels[c] : "";
        }
    }
    if ((desc->set & VplTableField_RowLabels) != 0 && desc->rowLabels != nullptr) {
        t.row_labels.resize(rows);
        for (size_t r = 0; r < rows; ++r) {
            t.row_labels[r] = desc->rowLabels[r] != nullptr ? desc->rowLabels[r] : "";
        }
    }
    if ((desc->set & VplTableField_ColWidths) != 0 && desc->colWidths != nullptr) {
        t.col_widths.assign(desc->colWidths, desc->colWidths + cols);
    }
    if ((desc->set & VplTableField_FontSize) != 0) {
        t.fontsize = desc->fontSize;
    }
    if ((desc->set & VplTableField_Loc) != 0) {
        if (desc->loc < VplTableLoc_Best || desc->loc > VplTableLoc_Bottom) {
            set_error("loc is out of range");
            return VplResult_InvalidArgument;
        }
        t.loc = static_cast<vpl::TableLoc>(desc->loc);
    }
    if ((desc->set & VplTableField_CellLoc) != 0) {
        t.cell_loc = to_align(desc->cellLoc);
    }
    if ((desc->set & VplTableField_RowLoc) != 0) {
        t.row_loc = to_align(desc->rowLoc);
    }
    if ((desc->set & VplTableField_ColLoc) != 0) {
        t.col_loc = to_align(desc->colLoc);
    }
    if ((desc->set & VplTableField_EdgeColorSpec) != 0 && desc->edgeColorSpec != nullptr) {
        vpl::Color c;
        if (!vpl::parse_color(desc->edgeColorSpec, c)) {
            set_error("unrecognized colour spec");
            return VplResult_InvalidArgument;
        }
        t.edgecolor = c;
    }
    if ((desc->set & VplTableField_FaceColorSpec) != 0 && desc->faceColorSpec != nullptr) {
        vpl::Color c;
        if (!vpl::parse_color(desc->faceColorSpec, c)) {
            set_error("unrecognized colour spec");
            return VplResult_InvalidArgument;
        }
        t.facecolor = c;
    }
    if ((desc->set & VplTableField_LineWidth) != 0) {
        t.linewidth = desc->lineWidth;
    }

    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesBxp(VplAxes *axes, const VplBoxStats *stats, size_t groupCount)
{
    VPL_TRY
    if (axes == nullptr || stats == nullptr) {
        set_error("axes or stats is null");
        return VplResult_InvalidArgument;
    }
    if (groupCount == 0) {
        set_error("groupCount is 0");
        return VplResult_InvalidArgument;
    }

    std::vector<vpl::BoxStats> converted(groupCount);
    for (size_t g = 0; g < groupCount; ++g) {
        const VplBoxStats &in = stats[g];
        if (in.structSize != sizeof(VplBoxStats)) {
            set_error("VplBoxStats.structSize does not match this header");
            return VplResult_InvalidArgument;
        }
        if (in.flierCount != 0 && in.fliers == nullptr) {
            set_error("flierCount is nonzero but fliers is null");
            return VplResult_InvalidArgument;
        }
        vpl::BoxStats &out = converted[g];
        out.q1 = in.q1;
        out.med = in.median;
        out.q3 = in.q3;
        out.whislo = in.whisLo;
        out.whishi = in.whisHi;
        out.fliers.assign(in.fliers, in.fliers + in.flierCount);
    }

    axes_of(axes)->bxp(converted.data(), groupCount);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesEcdf(VplAxes *axes,
                               const double *x,
                               size_t count,
                               const double *weights,
                               int32_t complementary,
                               int32_t horizontal,
                               const VplLineDesc *desc,
                               VplLine **outLine)
{
    VPL_TRY
    if (axes == nullptr || x == nullptr) {
        set_error("axes or x is null");
        return VplResult_InvalidArgument;
    }
    if (count == 0) {
        set_error("count is 0");
        return VplResult_InvalidArgument;
    }

    vpl::Line2D &line =
        axes_of(axes)->ecdf(x, count, complementary != 0, weights, horizontal != 0);
    const VplResult r = apply_line_desc(line, desc);
    if (r != VplResult_Success) {
        return r;
    }
    if (outLine != nullptr) {
        *outLine = reinterpret_cast<VplLine *>(&line);
    }
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesArrow(VplAxes *axes,
                                double x,
                                double y,
                                double dx,
                                double dy,
                                double width,
                                const VplPatchDesc *desc,
                                VplPatch **outPatch)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    if (!(width > 0.0)) {
        set_error("arrow width must be positive");
        return VplResult_InvalidArgument;
    }
    if (dx == 0.0 && dy == 0.0) {
        set_error("an arrow needs a direction: dx and dy are both 0");
        return VplResult_InvalidArgument;
    }

    vpl::PolyPatch &patch = axes_of(axes)->arrow(x, y, dx, dy, width);
    const VplResult r = apply_patch_desc(patch, desc);
    if (r != VplResult_Success) {
        return r;
    }
    if (outPatch != nullptr) {
        *outPatch = reinterpret_cast<VplPatch *>(&patch);
    }
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesSetXErr(VplAxes *axes,
                                  VplLine *line,
                                  const double *xerr,
                                  size_t count)
{
    VPL_TRY
    if (axes == nullptr || line == nullptr || xerr == nullptr) {
        set_error("axes, line or xerr is null");
        return VplResult_InvalidArgument;
    }

    vpl::Line2D *l = reinterpret_cast<vpl::Line2D *>(line);
    if (count != l->xdata.size()) {
        set_error("xerr must have one entry per point");
        return VplResult_InvalidArgument;
    }
    l->xerr.assign(xerr, xerr + count);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesPcolorMesh(VplAxes *axes,
                                     const double *X,
                                     const double *Y,
                                     const double *C,
                                     size_t rows,
                                     size_t cols,
                                     const VplFieldDesc *desc,
                                     VplImage **outImage)
{
    VPL_TRY
    if (axes == nullptr || X == nullptr || Y == nullptr || C == nullptr) {
        set_error("axes, X, Y or C is null");
        return VplResult_InvalidArgument;
    }
    if (rows == 0 || cols == 0) {
        set_error("rows and cols must both be non-zero");
        return VplResult_InvalidArgument;
    }

    vpl::ImageGrid &img = axes_of(axes)->pcolormesh(C, rows, cols, X, Y);
    const VplResult r = apply_field_desc(img, desc);
    if (r != VplResult_Success) {
        return r;
    }
    if (outImage != nullptr) {
        *outImage = reinterpret_cast<VplImage *>(&img);
    }
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesMatShow(VplAxes *axes,
                                  const double *values,
                                  size_t rows,
                                  size_t cols,
                                  const VplFieldDesc *desc,
                                  VplImage **outImage)
{
    VPL_TRY
    if (axes == nullptr || values == nullptr) {
        set_error("axes or values is null");
        return VplResult_InvalidArgument;
    }
    if (rows == 0 || cols == 0) {
        set_error("rows and cols must both be non-zero");
        return VplResult_InvalidArgument;
    }

    vpl::ImageGrid &img = axes_of(axes)->matshow(values, rows, cols);
    const VplResult r = apply_field_desc(img, desc);
    if (r != VplResult_Success) {
        return r;
    }
    if (outImage != nullptr) {
        *outImage = reinterpret_cast<VplImage *>(&img);
    }
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesSpy(VplAxes *axes,
                              const double *values,
                              size_t rows,
                              size_t cols,
                              const VplFieldDesc *desc,
                              VplImage **outImage)
{
    VPL_TRY
    if (axes == nullptr || values == nullptr) {
        set_error("axes or values is null");
        return VplResult_InvalidArgument;
    }
    if (rows == 0 || cols == 0) {
        set_error("rows and cols must both be non-zero");
        return VplResult_InvalidArgument;
    }

    vpl::ImageGrid &img = axes_of(axes)->spy(values, rows, cols);
    const VplResult r = apply_field_desc(img, desc);
    if (r != VplResult_Success) {
        return r;
    }
    if (outImage != nullptr) {
        *outImage = reinterpret_cast<VplImage *>(&img);
    }
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesHist2D(VplAxes *axes,
                                 const double *x,
                                 const double *y,
                                 size_t count,
                                 size_t xBins,
                                 size_t yBins)
{
    VPL_TRY
    if (axes == nullptr || x == nullptr || y == nullptr) {
        set_error("axes, x or y is null");
        return VplResult_InvalidArgument;
    }
    if (count == 0) {
        set_error("count is 0");
        return VplResult_InvalidArgument;
    }
    if (xBins == 0 || yBins == 0) {
        set_error("xBins and yBins must both be non-zero");
        return VplResult_InvalidArgument;
    }
    axes_of(axes)->hist2d(x, y, count, xBins, yBins);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesHexbin(VplAxes *axes,
                                 const double *x,
                                 const double *y,
                                 size_t count,
                                 size_t gridsize)
{
    VPL_TRY
    if (axes == nullptr || x == nullptr || y == nullptr) {
        set_error("axes, x or y is null");
        return VplResult_InvalidArgument;
    }
    if (count == 0) {
        set_error("count is 0");
        return VplResult_InvalidArgument;
    }
    /* 0 means "the default", which is matplotlib's gridsize of 100. */
    axes_of(axes)->hexbin(x, y, count, gridsize == 0 ? 100u : gridsize);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesTriplot(VplAxes *axes,
                                  const double *x,
                                  const double *y,
                                  size_t count)
{
    VPL_TRY
    if (axes == nullptr || x == nullptr || y == nullptr) {
        set_error("axes, x or y is null");
        return VplResult_InvalidArgument;
    }
    if (count < 3) {
        set_error("a triangulation needs at least 3 points");
        return VplResult_InvalidArgument;
    }
    axes_of(axes)->triplot(x, y, count);
    return VplResult_Success;
    VPL_CATCH
}

/* tricontour and tricontourf differ only in which core call they make: the
   validation, the desc handling and the handle-out are identical. */
static VplResult tricontour_impl(VplAxes *axes,
                                 const double *x,
                                 const double *y,
                                 const double *z,
                                 size_t count,
                                 const double *levels,
                                 size_t levelCount,
                                 const VplFieldDesc *desc,
                                 VplContour **outContour,
                                 bool filled)
{
    VPL_TRY
    if (axes == nullptr || x == nullptr || y == nullptr || z == nullptr ||
        levels == nullptr) {
        set_error("axes, x, y, z or levels is null");
        return VplResult_InvalidArgument;
    }
    if (count < 3) {
        set_error("a triangulation needs at least 3 points");
        return VplResult_InvalidArgument;
    }
    if (levelCount == 0) {
        set_error("levelCount is 0");
        return VplResult_InvalidArgument;
    }

    if (filled && levelCount < 2) {
        set_error("a filled contour needs at least 2 levels");
        return VplResult_InvalidArgument;
    }

    vpl::Axes *ax = axes_of(axes);
    vpl::ContourSet &cs =
        filled ? ax->tricontourf(x, y, z, count, levels, levelCount)
               : ax->tricontour(x, y, z, count, levels, levelCount);

    if (desc != nullptr) {
        if (desc->structSize == 0) {
            set_error("VplFieldDesc.structSize is 0");
            return VplResult_InvalidArgument;
        }
        if ((desc->set & VplFieldField_Colormap) != 0 &&
            !to_colormap(desc->cmap, cs.cmap)) {
            set_error("unknown VplColormap");
            return VplResult_InvalidArgument;
        }
        if ((desc->set & VplFieldField_ColorSpec) != 0 && desc->colorSpec != nullptr) {
            vpl::Color c;
            if (!vpl::parse_color(desc->colorSpec, c)) {
                set_error("unrecognized colour spec");
                return VplResult_InvalidArgument;
            }
            cs.color = c;
            cs.has_color = true;
        }
        if ((desc->set & VplFieldField_LineWidth) != 0) {
            if (!(desc->lineWidth > 0.0)) {
                set_error("lineWidth must be positive");
                return VplResult_InvalidArgument;
            }
            cs.linewidth = desc->lineWidth;
        }
        if ((desc->set & VplFieldField_Label) != 0 && desc->label != nullptr) {
            cs.label = desc->label;
        }
    }

    if (outContour != nullptr) {
        *outContour = reinterpret_cast<VplContour *>(&cs);
    }
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesTriContour(VplAxes *axes,
                                     const double *x,
                                     const double *y,
                                     const double *z,
                                     size_t count,
                                     const double *levels,
                                     size_t levelCount,
                                     const VplFieldDesc *desc,
                                     VplContour **outContour)
{
    return tricontour_impl(axes, x, y, z, count, levels, levelCount, desc, outContour,
                           /*filled=*/false);
}

VplResult VPL_CALL vplAxesTriContourF(VplAxes *axes,
                                      const double *x,
                                      const double *y,
                                      const double *z,
                                      size_t count,
                                      const double *levels,
                                      size_t levelCount,
                                      const VplFieldDesc *desc,
                                      VplContour **outContour)
{
    return tricontour_impl(axes, x, y, z, count, levels, levelCount, desc, outContour,
                           /*filled=*/true);
}

VplResult VPL_CALL vplAxesTriPcolor(VplAxes *axes,
                                    const double *x,
                                    const double *y,
                                    const double *z,
                                    size_t count)
{
    VPL_TRY
    if (axes == nullptr || x == nullptr || y == nullptr || z == nullptr) {
        set_error("axes, x, y or z is null");
        return VplResult_InvalidArgument;
    }
    if (count < 3) {
        set_error("a triangulation needs at least 3 points");
        return VplResult_InvalidArgument;
    }
    axes_of(axes)->tripcolor(x, y, z, count);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesPie(VplAxes *axes, const double *x, size_t count)
{
    VPL_TRY
    if (axes == nullptr || x == nullptr) {
        set_error("axes or x is null");
        return VplResult_InvalidArgument;
    }
    if (count == 0) {
        set_error("count is 0");
        return VplResult_InvalidArgument;
    }
    axes_of(axes)->pie(x, count);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesViolinPlot(VplAxes *axes,
                                     const double *values,
                                     const size_t *counts,
                                     size_t groupCount)
{
    VPL_TRY
    if (axes == nullptr || values == nullptr || counts == nullptr) {
        set_error("axes, values or counts is null");
        return VplResult_InvalidArgument;
    }
    if (groupCount == 0) {
        set_error("groupCount is 0");
        return VplResult_InvalidArgument;
    }
    for (size_t g = 0; g < groupCount; ++g) {
        if (counts[g] < 2) {
            set_error("a density needs at least two values per group");
            return VplResult_InvalidArgument;
        }
    }
    axes_of(axes)->violinplot(values, counts, groupCount);
    return VplResult_Success;
    VPL_CATCH
}

namespace
{

/* The label half of get_ticks_impl: the same locator run, handing back the
   strings instead of the positions. */
VplResult get_tick_labels_impl(const vpl::Axes *ax, bool want_x, char *const *labels,
                               size_t labelCount, size_t bufferSize, size_t *outCount)
{
    if (ax == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    const AxesGeometry geom = geometry_for(ax);
    const vpl::Bbox box = ax->display_box(geom.width, geom.height);
    const double px_per_pt = geom.dpi / 72.0;

    std::vector<double> xticks, yticks;
    std::vector<std::string> xlabels, ylabels;
    ax->compute_ticks(box, px_per_pt, xticks, yticks, xlabels, ylabels);
    const std::vector<std::string> &text = want_x ? xlabels : ylabels;

    if (outCount != nullptr) {
        *outCount = text.size();
    }
    if (labels == nullptr) {
        return VplResult_Success;
    }
    if (bufferSize == 0) {
        set_error("bufferSize is 0");
        return VplResult_InvalidArgument;
    }
    if (labelCount < text.size()) {
        /* Refuse rather than write through pointers the caller may not have
           allocated. */
        set_error("too few label pointers for the number of ticks");
        return VplResult_InvalidArgument;
    }

    for (size_t i = 0; i < text.size(); ++i) {
        if (labels[i] == nullptr) {
            set_error("a label buffer is null");
            return VplResult_InvalidArgument;
        }
        const size_t n = text[i].size() < bufferSize - 1 ? text[i].size() : bufferSize - 1;
        std::memcpy(labels[i], text[i].data(), n);
        labels[i][n] = '\0';
    }
    return VplResult_Success;
}

void write_rgba(const vpl::Color &c, double *out)
{
    out[0] = c.r;
    out[1] = c.g;
    out[2] = c.b;
    out[3] = c.a;
}

} // namespace

VplResult VPL_CALL vplAxesSetAutoscaleX(VplAxes *axes, int on)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    axes_of(axes)->set_autoscale_x(on != 0);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesSetAutoscaleY(VplAxes *axes, int on)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    axes_of(axes)->set_autoscale_y(on != 0);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesSetAutoscale(VplAxes *axes, int on)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    vpl::Axes *ax = axes_of(axes);
    ax->set_autoscale_x(on != 0);
    ax->set_autoscale_y(on != 0);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesGetAutoscaleX(const VplAxes *axes, int *outOn)
{
    VPL_TRY
    if (axes == nullptr || outOn == nullptr) {
        set_error("axes or outOn is null");
        return VplResult_InvalidArgument;
    }
    *outOn = axes_of(const_cast<VplAxes *>(axes))->autoscale_x_on() ? 1 : 0;
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesGetAutoscaleY(const VplAxes *axes, int *outOn)
{
    VPL_TRY
    if (axes == nullptr || outOn == nullptr) {
        set_error("axes or outOn is null");
        return VplResult_InvalidArgument;
    }
    *outOn = axes_of(const_cast<VplAxes *>(axes))->autoscale_y_on() ? 1 : 0;
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesGetAutoscale(const VplAxes *axes, int *outOn)
{
    VPL_TRY
    if (axes == nullptr || outOn == nullptr) {
        set_error("axes or outOn is null");
        return VplResult_InvalidArgument;
    }
    const vpl::Axes *ax = axes_of(const_cast<VplAxes *>(axes));
    /* Both, since matplotlib's get_autoscale_on is the AND of the two. */
    *outOn = (ax->autoscale_x_on() && ax->autoscale_y_on()) ? 1 : 0;
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesSetFaceColor(VplAxes *axes, const char *colorSpec)
{
    VPL_TRY
    if (axes == nullptr || colorSpec == nullptr) {
        set_error("axes or colorSpec is null");
        return VplResult_InvalidArgument;
    }
    vpl::Color c;
    if (!vpl::parse_color(colorSpec, c)) {
        set_error("unrecognised color");
        return VplResult_InvalidArgument;
    }
    axes_of(axes)->facecolor = c;
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesGetFaceColor(const VplAxes *axes, double *outRgba)
{
    VPL_TRY
    if (axes == nullptr || outRgba == nullptr) {
        set_error("axes or outRgba is null");
        return VplResult_InvalidArgument;
    }
    write_rgba(axes_of(const_cast<VplAxes *>(axes))->facecolor, outRgba);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesGetDataRatio(const VplAxes *axes, double *outRatio)
{
    VPL_TRY
    if (axes == nullptr || outRatio == nullptr) {
        set_error("axes or outRatio is null");
        return VplResult_InvalidArgument;
    }
    *outRatio = axes_of(const_cast<VplAxes *>(axes))->data_ratio();
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesUpdateDataLim(VplAxes *axes, double xmin, double ymin,
                                        double xmax, double ymax)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    if (!std::isfinite(xmin) || !std::isfinite(ymin) || !std::isfinite(xmax) ||
        !std::isfinite(ymax)) {
        set_error("the rectangle must be finite");
        return VplResult_InvalidArgument;
    }
    axes_of(axes)->extra_datalim.push_back(vpl::Bbox(xmin, ymin, xmax, ymax));
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesGetXTickLabels(const VplAxes *axes, char *const *labels,
                                         size_t labelCount, size_t bufferSize,
                                         size_t *outCount)
{
    VPL_TRY
    return get_tick_labels_impl(axes_of(const_cast<VplAxes *>(axes)), true, labels,
                                labelCount, bufferSize, outCount);
    VPL_CATCH
}

VplResult VPL_CALL vplAxesGetYTickLabels(const VplAxes *axes, char *const *labels,
                                         size_t labelCount, size_t bufferSize,
                                         size_t *outCount)
{
    VPL_TRY
    return get_tick_labels_impl(axes_of(const_cast<VplAxes *>(axes)), false, labels,
                                labelCount, bufferSize, outCount);
    VPL_CATCH
}

VplResult VPL_CALL vplFigureGetFaceColor(const VplFigure *figure, double *outRgba)
{
    VPL_TRY
    if (figure == nullptr || outRgba == nullptr) {
        set_error("figure or outRgba is null");
        return VplResult_InvalidArgument;
    }
    write_rgba(impl(const_cast<VplFigure *>(figure))->figure.facecolor, outRgba);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplFigureGetEdgeColor(const VplFigure *figure, double *outRgba)
{
    VPL_TRY
    if (figure == nullptr || outRgba == nullptr) {
        set_error("figure or outRgba is null");
        return VplResult_InvalidArgument;
    }
    write_rgba(impl(const_cast<VplFigure *>(figure))->figure.edgecolor, outRgba);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplFigureDelAxes(VplFigure *figure, VplAxes *axes)
{
    VPL_TRY
    if (figure == nullptr || axes == nullptr) {
        set_error("figure or axes is null");
        return VplResult_InvalidArgument;
    }
    if (!impl(figure)->figure.remove_axes(axes_of(axes))) {
        set_error("that axes does not belong to this figure");
        return VplResult_InvalidArgument;
    }
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplFigureGca(VplFigure *figure, VplAxes **outAxes)
{
    VPL_TRY
    if (figure == nullptr || outAxes == nullptr) {
        set_error("figure or outAxes is null");
        return VplResult_InvalidArgument;
    }

    FigureImpl *f = impl(figure);
    if (f->figure.axes.empty()) {
        /* gca adds an axes when there is none, matching pyplot. */
        vpl::Axes &ax = f->figure.add_subplot();
        remember_geometry(&ax, f->figure.pixel_width(), f->figure.pixel_height(),
                          f->figure.dpi);
    }
    if (f->figure.current_axes >= f->figure.axes.size()) {
        f->figure.current_axes = f->figure.axes.size() - 1;
    }
    *outAxes = reinterpret_cast<VplAxes *>(f->figure.axes[f->figure.current_axes].get());
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplFigureSca(VplFigure *figure, VplAxes *axes)
{
    VPL_TRY
    if (figure == nullptr || axes == nullptr) {
        set_error("figure or axes is null");
        return VplResult_InvalidArgument;
    }

    FigureImpl *f = impl(figure);
    const vpl::Axes *want = axes_of(axes);
    for (std::size_t i = 0; i < f->figure.axes.size(); ++i) {
        if (f->figure.axes[i].get() == want) {
            f->figure.current_axes = i;
            return VplResult_Success;
        }
    }
    set_error("that axes does not belong to this figure");
    return VplResult_InvalidArgument;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesSetAdjustable(VplAxes *axes, VplAdjustable adjustable)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    if (adjustable == VplAdjustable_Datalim) {
        /* Refused rather than silently downgraded to 'box', which would place
           the data differently than 'datalim' would. */
        set_error("adjustable='datalim' is not implemented; only 'box' is supported");
        return VplResult_InvalidArgument;
    }
    if (adjustable != VplAdjustable_Box) {
        set_error("unknown adjustable");
        return VplResult_InvalidArgument;
    }
    axes_of(axes)->adjustable_datalim = false;
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesGetAdjustable(const VplAxes *axes, VplAdjustable *outAdjustable)
{
    VPL_TRY
    if (axes == nullptr || outAdjustable == nullptr) {
        set_error("axes or outAdjustable is null");
        return VplResult_InvalidArgument;
    }
    *outAdjustable = axes_of(const_cast<VplAxes *>(axes))->adjustable_datalim
                         ? VplAdjustable_Datalim
                         : VplAdjustable_Box;
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesSetBoxAspect(VplAxes *axes, double boxAspect)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    if (boxAspect < 0.0 || !std::isfinite(boxAspect)) {
        set_error("boxAspect must be finite and not negative (0 clears it)");
        return VplResult_InvalidArgument;
    }
    axes_of(axes)->box_aspect = boxAspect;
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesGetBoxAspect(const VplAxes *axes, double *outBoxAspect)
{
    VPL_TRY
    if (axes == nullptr || outBoxAspect == nullptr) {
        set_error("axes or outBoxAspect is null");
        return VplResult_InvalidArgument;
    }
    *outBoxAspect = axes_of(const_cast<VplAxes *>(axes))->box_aspect;
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesSetAnchor(VplAxes *axes, double x, double y)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    if (!(x >= 0.0 && x <= 1.0) || !(y >= 0.0 && y <= 1.0)) {
        set_error("anchor must lie within [0, 1] in both directions");
        return VplResult_InvalidArgument;
    }
    vpl::Axes *ax = axes_of(axes);
    ax->anchor_x = x;
    ax->anchor_y = y;
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesGetAnchor(const VplAxes *axes, double *outX, double *outY)
{
    VPL_TRY
    if (axes == nullptr || outX == nullptr || outY == nullptr) {
        set_error("axes, outX or outY is null");
        return VplResult_InvalidArgument;
    }
    const vpl::Axes *ax = axes_of(const_cast<VplAxes *>(axes));
    *outX = ax->anchor_x;
    *outY = ax->anchor_y;
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesLocatorParams(VplAxes *axes, VplAxis axis, int nbins)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    if (axis != VplAxis_X && axis != VplAxis_Y && axis != VplAxis_Both) {
        set_error("unknown axis");
        return VplResult_InvalidArgument;
    }
    if (nbins < 0) {
        set_error("nbins must not be negative (0 restores the derived value)");
        return VplResult_InvalidArgument;
    }

    vpl::Axes *ax = axes_of(axes);
    if (axis == VplAxis_X || axis == VplAxis_Both) {
        ax->x_nbins = nbins;
    }
    if (axis == VplAxis_Y || axis == VplAxis_Both) {
        ax->y_nbins = nbins;
    }
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesLocatorParamsFull(VplAxes *axes, VplAxis axis, int nbins,
                                            const double *steps, size_t stepsCount, int tight)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    if (axis != VplAxis_X && axis != VplAxis_Y && axis != VplAxis_Both) {
        set_error("unknown axis");
        return VplResult_InvalidArgument;
    }
    if (nbins < 0) {
        set_error("nbins must not be negative (0 restores the derived value)");
        return VplResult_InvalidArgument;
    }
    if ((steps == nullptr) != (stepsCount == 0)) {
        set_error("steps and stepsCount must both be given or both be empty");
        return VplResult_InvalidArgument;
    }
    for (size_t i = 0; i < stepsCount; ++i) {
        if (steps[i] < 1.0 || steps[i] > 10.0) {
            set_error("steps must be an increasing sequence of numbers between 1 and 10 inclusive");
            return VplResult_InvalidArgument;
        }
        if (i > 0 && steps[i] <= steps[i - 1]) {
            set_error("steps must be an increasing sequence of numbers between 1 and 10 inclusive");
            return VplResult_InvalidArgument;
        }
    }
    if (tight != -1 && tight != 0 && tight != 1) {
        set_error("tight must be -1 (leave alone), 0 or 1");
        return VplResult_InvalidArgument;
    }

    vpl::Axes *ax = axes_of(axes);
    const bool ax_x = axis == VplAxis_X || axis == VplAxis_Both;
    const bool ax_y = axis == VplAxis_Y || axis == VplAxis_Both;
    if (ax_x) {
        ax->x_nbins = nbins;
    }
    if (ax_y) {
        ax->y_nbins = nbins;
    }
    if (steps != nullptr) {
        const std::vector<double> s(steps, steps + stepsCount);
        if (ax_x) {
            ax->x_tick_steps = s;
        }
        if (ax_y) {
            ax->y_tick_steps = s;
        }
    }
    /* tight is accepted and range-checked but otherwise unused. It selects
       whether autoscale_view expands the view via the locator's view_limits(),
       and MaxNLocator (the only locator driven here) inherits the base no-op,
       so the result is identical for tight=True/False/unset -- as in
       matplotlib. */
    (void)tight;
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesSetPropCycle(VplAxes *axes,
                                       const char *const *colorSpecs,
                                       size_t count)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    if (colorSpecs == nullptr) {
        /* set_prop_cycle replaces the whole cycle, so any linestyle/marker/
           linewidth cycling from an earlier call is cleared too. */
        axes_of(axes)->prop_cycle = vpl::Axes::PropCycle{};
        return VplResult_Success;
    }
    if (count == 0) {
        set_error("a property cycle needs at least one colour");
        return VplResult_InvalidArgument;
    }

    /* Parse into a scratch list first so a bad spec leaves the existing cycle
       intact. */
    std::vector<vpl::Color> parsed;
    parsed.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        vpl::Color c;
        if (colorSpecs[i] == nullptr || !vpl::parse_color(colorSpecs[i], c)) {
            set_error("unrecognised color in the property cycle");
            return VplResult_InvalidArgument;
        }
        parsed.push_back(c);
    }
    axes_of(axes)->prop_cycle = vpl::Axes::PropCycle{};
    axes_of(axes)->prop_cycle.colors = std::move(parsed);
    return VplResult_Success;
    VPL_CATCH
}

namespace
{
bool to_line_style(VplLineStyle s, vpl::LineStyle &out)
{
    switch (s) {
        case VplLineStyle_Solid: out = vpl::LineStyle::Solid; return true;
        case VplLineStyle_Dashed: out = vpl::LineStyle::Dashed; return true;
        case VplLineStyle_DashDot: out = vpl::LineStyle::DashDot; return true;
        case VplLineStyle_Dotted: out = vpl::LineStyle::Dotted; return true;
        case VplLineStyle_None: out = vpl::LineStyle::None; return true;
        default: return false;
    }
}
} // namespace

VplResult VPL_CALL vplAxesSetPropCycleFull(VplAxes *axes, const char *const *colorSpecs,
                                           size_t colorCount, const VplLineStyle *linestyles,
                                           size_t linestyleCount, const VplMarker *markers,
                                           size_t markerCount, const double *linewidths,
                                           size_t linewidthCount)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    if (colorSpecs == nullptr && linestyles == nullptr && markers == nullptr &&
        linewidths == nullptr) {
        axes_of(axes)->prop_cycle = vpl::Axes::PropCycle{};
        return VplResult_Success;
    }

    /* When more than one property is given, their lists must be the same
       length (matplotlib's rule); only the present (non-NULL) ones must agree. */
    size_t common = 0;
    bool have_common = false;
    auto check_length = [&](bool present, size_t n) {
        if (!present) {
            return true;
        }
        if (!have_common) {
            common = n;
            have_common = true;
            return true;
        }
        return n == common;
    };
    if (!check_length(colorSpecs != nullptr, colorCount) ||
        !check_length(linestyles != nullptr, linestyleCount) ||
        !check_length(markers != nullptr, markerCount) ||
        !check_length(linewidths != nullptr, linewidthCount)) {
        set_error("prop cycle arrays must all be the same length");
        return VplResult_InvalidArgument;
    }
    if (have_common && common == 0) {
        set_error("a property cycle needs at least one entry");
        return VplResult_InvalidArgument;
    }

    /* Parse into a scratch cycle first so an error leaves the existing cycle
       intact. */
    vpl::Axes::PropCycle parsed;
    if (colorSpecs != nullptr) {
        parsed.colors.reserve(colorCount);
        for (size_t i = 0; i < colorCount; ++i) {
            vpl::Color c;
            if (colorSpecs[i] == nullptr || !vpl::parse_color(colorSpecs[i], c)) {
                set_error("unrecognised color in the property cycle");
                return VplResult_InvalidArgument;
            }
            parsed.colors.push_back(c);
        }
    }
    if (linestyles != nullptr) {
        parsed.linestyles.reserve(linestyleCount);
        for (size_t i = 0; i < linestyleCount; ++i) {
            vpl::LineStyle ls;
            if (!to_line_style(linestyles[i], ls)) {
                set_error("unknown VplLineStyle in the property cycle");
                return VplResult_InvalidArgument;
            }
            parsed.linestyles.push_back(ls);
        }
    }
    if (markers != nullptr) {
        parsed.markers.reserve(markerCount);
        for (size_t i = 0; i < markerCount; ++i) {
            bool ok = false;
            const vpl::MarkerStyle m = to_marker(markers[i], ok);
            if (!ok) {
                set_error("unknown VplMarker in the property cycle");
                return VplResult_InvalidArgument;
            }
            parsed.markers.push_back(m);
        }
    }
    if (linewidths != nullptr) {
        for (size_t i = 0; i < linewidthCount; ++i) {
            if (!(linewidths[i] >= 0.0)) {
                set_error("linewidths in the property cycle must be >= 0");
                return VplResult_InvalidArgument;
            }
        }
        parsed.linewidths.assign(linewidths, linewidths + linewidthCount);
    }

    axes_of(axes)->prop_cycle = std::move(parsed);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplFigureSetFaceColor(VplFigure *figure, const char *colorSpec)
{
    VPL_TRY
    if (figure == nullptr || colorSpec == nullptr) {
        set_error("figure or colorSpec is null");
        return VplResult_InvalidArgument;
    }
    vpl::Color c;
    if (!vpl::parse_color(colorSpec, c)) {
        set_error("unrecognised color");
        return VplResult_InvalidArgument;
    }
    impl(figure)->figure.facecolor = c;
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplFigureSetEdgeColor(VplFigure *figure, const char *colorSpec)
{
    VPL_TRY
    if (figure == nullptr || colorSpec == nullptr) {
        set_error("figure or colorSpec is null");
        return VplResult_InvalidArgument;
    }
    vpl::Color c;
    if (!vpl::parse_color(colorSpec, c)) {
        set_error("unrecognised color");
        return VplResult_InvalidArgument;
    }
    impl(figure)->figure.edgecolor = c;
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplFigureSetLineWidth(VplFigure *figure, double width)
{
    VPL_TRY
    if (figure == nullptr) {
        set_error("figure is null");
        return VplResult_InvalidArgument;
    }
    if (width < 0.0 || !std::isfinite(width)) {
        set_error("width must be finite and not negative");
        return VplResult_InvalidArgument;
    }
    impl(figure)->figure.linewidth = width;
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplFigureSetFrameOn(VplFigure *figure, int on)
{
    VPL_TRY
    if (figure == nullptr) {
        set_error("figure is null");
        return VplResult_InvalidArgument;
    }
    impl(figure)->figure.frameon = on != 0;
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplFigureGetFrameOn(const VplFigure *figure, int *outOn)
{
    VPL_TRY
    if (figure == nullptr || outOn == nullptr) {
        set_error("figure or outOn is null");
        return VplResult_InvalidArgument;
    }
    *outOn = impl(const_cast<VplFigure *>(figure))->figure.frameon ? 1 : 0;
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplFigureGetLineWidth(const VplFigure *figure, double *outWidth)
{
    VPL_TRY
    if (figure == nullptr || outWidth == nullptr) {
        set_error("figure or outWidth is null");
        return VplResult_InvalidArgument;
    }
    *outWidth = impl(const_cast<VplFigure *>(figure))->figure.linewidth;
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesTickLabelFormat(VplAxes *axes,
                                          VplAxis axis,
                                          int useOffset,
                                          int scientific,
                                          int sciLimitLo,
                                          int sciLimitHi)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    if (axis != VplAxis_X && axis != VplAxis_Y && axis != VplAxis_Both) {
        set_error("unknown axis");
        return VplResult_InvalidArgument;
    }
    if (sciLimitHi < sciLimitLo) {
        set_error("sciLimitHi must not be below sciLimitLo");
        return VplResult_InvalidArgument;
    }

    vpl::TickFormatOptions opts;
    opts.use_offset = useOffset != 0;
    opts.scientific = scientific != 0;
    opts.sci_limit_lo = sciLimitLo;
    opts.sci_limit_hi = sciLimitHi;

    vpl::Axes *ax = axes_of(axes);
    if (axis == VplAxis_X || axis == VplAxis_Both) {
        ax->x_tick_format = opts;
    }
    if (axis == VplAxis_Y || axis == VplAxis_Both) {
        ax->y_tick_format = opts;
    }
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesSetSpineVisible(VplAxes *axes, VplSpine spine, int visible)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }

    vpl::Axes *ax = axes_of(axes);
    const bool on = visible != 0;
    switch (spine) {
        case VplSpine_Left: ax->spine_left = on; break;
        case VplSpine_Right: ax->spine_right = on; break;
        case VplSpine_Top: ax->spine_top = on; break;
        case VplSpine_Bottom: ax->spine_bottom = on; break;
        default:
            set_error("unknown spine");
            return VplResult_InvalidArgument;
    }
    return VplResult_Success;
    VPL_CATCH
}

namespace
{

VplResult apply_text_desc(vpl::TextArtist &t, const VplTextDesc *desc)
{
    if (desc == nullptr) {
        return VplResult_Success;
    }
    if (desc->structSize == 0) {
        set_error("VplTextDesc.structSize is 0");
        return VplResult_InvalidArgument;
    }

    if ((desc->set & VplTextField_ColorSpec) != 0 && desc->colorSpec != nullptr) {
        vpl::Color c;
        if (!vpl::parse_color(desc->colorSpec, c)) {
            set_error("unrecognized colour spec");
            return VplResult_InvalidArgument;
        }
        t.color = c;
    }
    if ((desc->set & VplTextField_Color) != 0) {
        t.color = vpl::Color{desc->color[0], desc->color[1], desc->color[2], desc->color[3]};
    }
    if ((desc->set & VplTextField_Size) != 0) {
        if (!(desc->size > 0.0)) {
            set_error("text size must be positive");
            return VplResult_InvalidArgument;
        }
        t.size = desc->size;
    }
    if ((desc->set & VplTextField_Rotation) != 0) {
        t.rotation = desc->rotation;
    }
    if ((desc->set & VplTextField_Align) != 0) {
        switch (desc->halign) {
            case VplHAlign_Left: t.halign = vpl::HAlign::Left; break;
            case VplHAlign_Center: t.halign = vpl::HAlign::Center; break;
            case VplHAlign_Right: t.halign = vpl::HAlign::Right; break;
            default:
                set_error("unknown VplHAlign");
                return VplResult_InvalidArgument;
        }
        switch (desc->valign) {
            case VplVAlign_Baseline: t.valign = vpl::VAlign::Baseline; break;
            case VplVAlign_Bottom: t.valign = vpl::VAlign::Bottom; break;
            case VplVAlign_Center: t.valign = vpl::VAlign::Center; break;
            case VplVAlign_Top: t.valign = vpl::VAlign::Top; break;
            default:
                set_error("unknown VplVAlign");
                return VplResult_InvalidArgument;
        }
    }
    return VplResult_Success;
}

} // namespace

VplResult VPL_CALL vplAxesText(VplAxes *axes,
                               double x,
                               double y,
                               const char *text,
                               const VplTextDesc *desc)
{
    VPL_TRY
    if (axes == nullptr || text == nullptr) {
        set_error("axes or text is null");
        return VplResult_InvalidArgument;
    }
    vpl::TextArtist &t = axes_of(axes)->text(x, y, text);
    return apply_text_desc(t, desc);
    VPL_CATCH
}

VplResult VPL_CALL vplAxesAnnotate(VplAxes *axes,
                                   const char *text,
                                   double x,
                                   double y,
                                   double textX,
                                   double textY,
                                   const VplTextDesc *desc)
{
    VPL_TRY
    if (axes == nullptr || text == nullptr) {
        set_error("axes or text is null");
        return VplResult_InvalidArgument;
    }
    vpl::TextArtist &t = axes_of(axes)->text(textX, textY, text);
    t.has_arrow = true;
    t.arrow_x = x;
    t.arrow_y = y;
    return apply_text_desc(t, desc);
    VPL_CATCH
}

namespace
{

/* Shared styling for axhline, axvline and axline. Reference lines reuse
   VplLineDesc, but only the styling fields apply -- a label would need a legend
   handle the reference line does not have. */
VplResult apply_reference_desc(vpl::ReferenceLine &ref, const VplLineDesc *desc)
{
    if (desc != nullptr) {
        vpl::Line2D scratch;
        scratch.color = ref.color;
        scratch.linewidth = ref.linewidth;
        scratch.linestyle = ref.linestyle;

        const VplResult r = apply_line_desc(scratch, desc);
        if (r != VplResult_Success) {
            return r;
        }
        if ((desc->set & VplLineField_Color) != 0) {
            scratch.color = vpl::Color{desc->color[0], desc->color[1], desc->color[2],
                                       desc->color[3]};
        }
        if ((desc->set & VplLineField_Width) != 0) {
            scratch.linewidth = desc->width;
        }
        if ((desc->set & VplLineField_LineStyle) != 0) {
            switch (desc->linestyle) {
                case VplLineStyle_Dashed: scratch.linestyle = vpl::LineStyle::Dashed; break;
                case VplLineStyle_DashDot: scratch.linestyle = vpl::LineStyle::DashDot; break;
                case VplLineStyle_Dotted: scratch.linestyle = vpl::LineStyle::Dotted; break;
                case VplLineStyle_None: scratch.linestyle = vpl::LineStyle::None; break;
                case VplLineStyle_Solid: scratch.linestyle = vpl::LineStyle::Solid; break;
                default:
                    set_error("unknown VplLineStyle");
                    return VplResult_InvalidArgument;
            }
        }

        ref.color = scratch.color;
        ref.linewidth = scratch.linewidth;
        ref.linestyle = scratch.linestyle;
    }

    return VplResult_Success;
}

VplResult add_reference_line(VplAxes *axes, double value, bool horizontal,
                             const VplLineDesc *desc)
{
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }

    vpl::Axes *ax = axes_of(axes);
    vpl::ReferenceLine &ref = horizontal ? ax->axhline(value) : ax->axvline(value);
    return apply_reference_desc(ref, desc);
}

} // namespace

VplResult VPL_CALL vplAxesAxHLine(VplAxes *axes, double y, const VplLineDesc *desc)
{
    VPL_TRY
    return add_reference_line(axes, y, true, desc);
    VPL_CATCH
}

VplResult VPL_CALL vplAxesAxVLine(VplAxes *axes, double x, const VplLineDesc *desc)
{
    VPL_TRY
    return add_reference_line(axes, x, false, desc);
    VPL_CATCH
}

VplResult VPL_CALL vplAxesAxLine(VplAxes *axes,
                                 double x,
                                 double y,
                                 double slope,
                                 int vertical,
                                 const VplLineDesc *desc)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    if (vertical == 0 && !std::isfinite(slope)) {
        set_error("slope must be finite unless vertical is set");
        return VplResult_InvalidArgument;
    }

    vpl::ReferenceLine &line = axes_of(axes)->axline(x, y, slope, vertical != 0);
    return apply_reference_desc(line, desc);
    VPL_CATCH
}

VplResult VPL_CALL vplAxesBarLabel(VplAxes *axes, const VplPatch *bars)
{
    VPL_TRY
    if (axes == nullptr || bars == nullptr) {
        set_error("axes or bars is null");
        return VplResult_InvalidArgument;
    }
    axes_of(axes)->bar_label(*reinterpret_cast<const vpl::PolyPatch *>(bars));
    return VplResult_Success;
    VPL_CATCH
}

namespace
{
/*
 * bar_label's fmt reaches std::snprintf as the format string with only one
 * double argument, so this is a security check: %s or %n would read or write
 * through bogus va_arg values. Accepts exactly one %-conversion (plain flags/
 * width/precision, ending in f/F/g/G/e/E) plus any literal "%%" escapes.
 */
bool is_safe_double_format(const char *fmt)
{
    if (fmt == nullptr) {
        return false;
    }
    int conversions = 0;
    for (const char *p = fmt; *p != '\0'; ++p) {
        if (*p != '%') {
            continue;
        }
        ++p;
        if (*p == '%') {
            continue; /* a literal percent, not a conversion */
        }
        ++conversions;
        if (conversions > 1) {
            return false;
        }
        /* flags */
        while (*p == '-' || *p == '+' || *p == ' ' || *p == '#' || *p == '0') {
            ++p;
        }
        /* width */
        while (*p >= '0' && *p <= '9') {
            ++p;
        }
        /* precision */
        if (*p == '.') {
            ++p;
            while (*p >= '0' && *p <= '9') {
                ++p;
            }
        }
        switch (*p) {
            case 'f': case 'F': case 'g': case 'G': case 'e': case 'E': break;
            default: return false; /* anything else, including %s/%d/%n/end-of-string */
        }
    }
    return conversions == 1;
}

vpl::BarLabelType bar_label_type_from(VplBarLabelType t, bool &ok)
{
    ok = true;
    switch (t) {
        case VplBarLabelType_Edge: return vpl::BarLabelType::Edge;
        case VplBarLabelType_Center: return vpl::BarLabelType::Center;
        default: break;
    }
    ok = false;
    return vpl::BarLabelType::Edge;
}
} // namespace

VplResult VPL_CALL vplAxesBarLabelFull(VplAxes *axes, const VplPatch *bars, const char *fmt,
                                       VplBarLabelType labelType, double padding)
{
    VPL_TRY
    if (axes == nullptr || bars == nullptr) {
        set_error("axes or bars is null");
        return VplResult_InvalidArgument;
    }
    if (!is_safe_double_format(fmt)) {
        set_error(
            "fmt must have exactly one %-conversion, ending in f/F/g/G/e/E");
        return VplResult_InvalidArgument;
    }
    bool ok = false;
    const vpl::BarLabelType lt = bar_label_type_from(labelType, ok);
    if (!ok) {
        set_error("unknown VplBarLabelType");
        return VplResult_InvalidArgument;
    }
    axes_of(axes)->bar_label(*reinterpret_cast<const vpl::PolyPatch *>(bars), fmt, lt, padding);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesSetXLim(VplAxes *axes, double left, double right)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    if (!(right > left)) {
        set_error("x limits must satisfy right > left");
        return VplResult_InvalidArgument;
    }
    axes_of(axes)->set_xlim(left, right);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesSetYLim(VplAxes *axes, double bottom, double top)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    if (!(top > bottom)) {
        set_error("y limits must satisfy top > bottom");
        return VplResult_InvalidArgument;
    }
    axes_of(axes)->set_ylim(bottom, top);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesSetXBound(VplAxes *axes, double lower, double upper)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    if (!std::isnan(lower) && !std::isnan(upper) && !(upper > lower)) {
        set_error("x bounds must satisfy upper > lower when both are given");
        return VplResult_InvalidArgument;
    }
    axes_of(axes)->set_xbound(lower, upper);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesSetYBound(VplAxes *axes, double lower, double upper)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    if (!std::isnan(lower) && !std::isnan(upper) && !(upper > lower)) {
        set_error("y bounds must satisfy upper > lower when both are given");
        return VplResult_InvalidArgument;
    }
    axes_of(axes)->set_ybound(lower, upper);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesSetTitle(VplAxes *axes, const char *title)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    axes_of(axes)->title = title != nullptr ? title : "";
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesSetXLabel(VplAxes *axes, const char *label)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    axes_of(axes)->xlabel = label != nullptr ? label : "";
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesSetYLabel(VplAxes *axes, const char *label)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    axes_of(axes)->ylabel = label != nullptr ? label : "";
    return VplResult_Success;
    VPL_CATCH
}

namespace
{

VplResult set_scale(VplAxes *axes, VplScale scale, bool is_x)
{
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    vpl::ScaleType s;
    switch (scale) {
        case VplScale_Linear: s = vpl::ScaleType::Linear; break;
        case VplScale_Log: s = vpl::ScaleType::Log; break;
        default:
            set_error("unknown VplScale");
            return VplResult_InvalidArgument;
    }
    if (is_x) {
        axes_of(axes)->xscale = s;
    } else {
        axes_of(axes)->yscale = s;
    }
    /* A scale change invalidates any pinned view, whose limits were chosen for
       the old mapping. */
    axes_of(axes)->autoscale();
    return VplResult_Success;
}

} // namespace

VplResult VPL_CALL vplAxesSetXScale(VplAxes *axes, VplScale scale)
{
    VPL_TRY
    return set_scale(axes, scale, true);
    VPL_CATCH
}

VplResult VPL_CALL vplAxesSetYScale(VplAxes *axes, VplScale scale)
{
    VPL_TRY
    return set_scale(axes, scale, false);
    VPL_CATCH
}

VplResult VPL_CALL vplAxesSetGrid(VplAxes *axes, int visible, VplGridWhich which,
                                  VplGridAxis axis)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    vpl::GridWhich w;
    switch (which) {
        case VplGridWhich_Major: w = vpl::GridWhich::Major; break;
        case VplGridWhich_Minor: w = vpl::GridWhich::Minor; break;
        case VplGridWhich_Both: w = vpl::GridWhich::Both; break;
        default: set_error("unknown VplGridWhich"); return VplResult_InvalidArgument;
    }
    vpl::GridAxis a;
    switch (axis) {
        case VplGridAxis_Both: a = vpl::GridAxis::Both; break;
        case VplGridAxis_X: a = vpl::GridAxis::X; break;
        case VplGridAxis_Y: a = vpl::GridAxis::Y; break;
        default: set_error("unknown VplGridAxis"); return VplResult_InvalidArgument;
    }
    axes_of(axes)->set_grid(visible != 0, w, a);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesSetMinorTicks(VplAxes *axes, int onX, int onY)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    axes_of(axes)->minor_x_visible = onX != 0;
    axes_of(axes)->minor_y_visible = onY != 0;
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesSetLegend(VplAxes *axes, int visible, VplLegendLoc loc)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    vpl::LegendLoc l;
    switch (loc) {
        case VplLegendLoc_Best: l = vpl::LegendLoc::Best; break;
        case VplLegendLoc_UpperRight: l = vpl::LegendLoc::UpperRight; break;
        case VplLegendLoc_UpperLeft: l = vpl::LegendLoc::UpperLeft; break;
        case VplLegendLoc_LowerLeft: l = vpl::LegendLoc::LowerLeft; break;
        case VplLegendLoc_LowerRight: l = vpl::LegendLoc::LowerRight; break;
        case VplLegendLoc_Right: l = vpl::LegendLoc::Right; break;
        case VplLegendLoc_CenterLeft: l = vpl::LegendLoc::CenterLeft; break;
        case VplLegendLoc_CenterRight: l = vpl::LegendLoc::CenterRight; break;
        case VplLegendLoc_LowerCenter: l = vpl::LegendLoc::LowerCenter; break;
        case VplLegendLoc_UpperCenter: l = vpl::LegendLoc::UpperCenter; break;
        case VplLegendLoc_Center: l = vpl::LegendLoc::Center; break;
        default:
            set_error("unknown VplLegendLoc");
            return VplResult_InvalidArgument;
    }
    axes_of(axes)->legend_visible = visible != 0;
    axes_of(axes)->legend_loc = l;
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesSetLegendTitle(VplAxes *axes, const char *title)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    axes_of(axes)->legend_title = title != nullptr ? title : "";
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesSetLegendNCols(VplAxes *axes, int ncols)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    if (ncols < 1) {
        set_error("ncols must be at least 1");
        return VplResult_InvalidArgument;
    }
    axes_of(axes)->legend_ncols = ncols;
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesSetViewLimits(VplAxes *axes,
                                        double xmin, double ymin,
                                        double xmax, double ymax)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    if (!(xmax > xmin) || !(ymax > ymin)) {
        set_error("view limits must satisfy xmax > xmin and ymax > ymin");
        return VplResult_InvalidArgument;
    }
    axes_of(axes)->set_view_limits(vpl::Bbox(xmin, ymin, xmax, ymax));
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesGetViewLimits(const VplAxes *axes,
                                        double *outXmin, double *outYmin,
                                        double *outXmax, double *outYmax)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    const vpl::Bbox view = axes_of(axes)->view_limits();
    if (outXmin != nullptr) { *outXmin = view.x0; }
    if (outYmin != nullptr) { *outYmin = view.y0; }
    if (outXmax != nullptr) { *outXmax = view.x1; }
    if (outYmax != nullptr) { *outYmax = view.y1; }
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesAutoscale(VplAxes *axes, int enable, VplAxisSel axis, int tight)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    if (axis != VplAxisSel_Both && axis != VplAxisSel_X && axis != VplAxisSel_Y) {
        set_error("unknown VplAxisSel");
        return VplResult_InvalidArgument;
    }
    const bool ax_x = axis != VplAxisSel_Y;
    const bool ax_y = axis != VplAxisSel_X;
    axes_of(axes)->autoscale(enable, ax_x, ax_y, tight);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesSetUseStickyEdges(VplAxes *axes, int on)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    axes_of(axes)->use_sticky_edges = on != 0;
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesGetUseStickyEdges(const VplAxes *axes, int *outOn)
{
    VPL_TRY
    if (axes == nullptr || outOn == nullptr) {
        set_error("axes or outOn is null");
        return VplResult_InvalidArgument;
    }
    *outOn = axes_of(axes)->use_sticky_edges ? 1 : 0;
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesAddStickyEdgeX(VplAxes *axes, double value)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    axes_of(axes)->sticky_x.push_back(value);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesAddStickyEdgeY(VplAxes *axes, double value)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }
    axes_of(axes)->sticky_y.push_back(value);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesPan(VplAxes *axes,
                              double fromX, double fromY,
                              double toX, double toY)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }

    vpl::Axes *ax = axes_of(axes);
    const AxesGeometry geom = geometry_for(ax);
    const vpl::Bbox box = ax->display_box(geom.width, geom.height);
    const vpl::Bbox view = ax->view_limits();

    if (box.width() <= 0.0 || box.height() <= 0.0) {
        set_error("axes has no area to pan in");
        return VplResult_InvalidArgument;
    }

    /* Incoming pixels are top-down; display space is y-up. */
    const double dx_px = toX - fromX;
    const double dy_px = (geom.height - toY) - (geom.height - fromY);

    const double dx_data = dx_px * view.width() / box.width();
    const double dy_data = dy_px * view.height() / box.height();

    /* Dragging the content right moves the view window left. */
    ax->set_view_limits(vpl::Bbox(view.x0 - dx_data, view.y0 - dy_data,
                                  view.x1 - dx_data, view.y1 - dy_data));
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesZoomAt(VplAxes *axes,
                                 double pixelX, double pixelY,
                                 double steps)
{
    VPL_TRY
    if (axes == nullptr) {
        set_error("axes is null");
        return VplResult_InvalidArgument;
    }

    vpl::Axes *ax = axes_of(axes);
    const AxesGeometry geom = geometry_for(ax);
    const vpl::Bbox box = ax->display_box(geom.width, geom.height);
    const vpl::Bbox view = ax->view_limits();

    if (box.width() <= 0.0 || box.height() <= 0.0) {
        set_error("axes has no area to zoom in");
        return VplResult_InvalidArgument;
    }

    /* The data point under the cursor stays put. */
    const double display_x = pixelX;
    const double display_y = geom.height - pixelY;

    const double anchor_x = view.x0 + (display_x - box.x0) / box.width() * view.width();
    const double anchor_y = view.y0 + (display_y - box.y0) / box.height() * view.height();

    /* 0.9 per step matches the feel of matplotlib's scroll zoom. */
    const double factor = std::pow(0.9, steps);

    ax->set_view_limits(vpl::Bbox(anchor_x + (view.x0 - anchor_x) * factor,
                                  anchor_y + (view.y0 - anchor_y) * factor,
                                  anchor_x + (view.x1 - anchor_x) * factor,
                                  anchor_y + (view.y1 - anchor_y) * factor));
    return VplResult_Success;
    VPL_CATCH
}

} /* extern "C" */
