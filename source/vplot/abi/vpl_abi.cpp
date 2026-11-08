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
#include <cstddef>
#include <cstdio>
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
    if (axes == nullptr || values == nullptr) {
        set_error("axes or values is null");
        return VplResult_InvalidArgument;
    }
    if (rows < 2 || cols < 2) {
        set_error("contour needs a grid of at least 2x2");
        return VplResult_InvalidArgument;
    }
    if (levels != nullptr && levelCount == 0) {
        set_error("levelCount is 0");
        return VplResult_InvalidArgument;
    }
    if (levels != nullptr) {
        for (size_t i = 1; i < levelCount; ++i) {
            if (!(levels[i] > levels[i - 1])) {
                set_error("contour levels must be increasing");
                return VplResult_InvalidArgument;
            }
        }
    }

    vpl::ContourExtend extend = vpl::ContourExtend::Neither;
    if (desc != nullptr && desc->structSize >= offsetof(VplFieldDesc, extend) + sizeof(desc->extend) &&
        (desc->set & VplFieldField_Extend) != 0) {
        switch (desc->extend) {
        case VplContourExtend_Neither: extend = vpl::ContourExtend::Neither; break;
        case VplContourExtend_Min: extend = vpl::ContourExtend::Min; break;
        case VplContourExtend_Max: extend = vpl::ContourExtend::Max; break;
        case VplContourExtend_Both: extend = vpl::ContourExtend::Both; break;
        default: set_error("unknown VplContourExtend"); return VplResult_InvalidArgument;
        }
    }

    /* contour(Z, N): pick the levels from the data. */
    std::vector<double> auto_levels;
    if (levels == nullptr) {
        auto_levels = vpl::Axes::auto_contour_levels(values, rows * cols, levelCount,
                                                     filled ? extend : vpl::ContourExtend::Neither);
        if (auto_levels.empty()) {
            set_error("no finite values to choose contour levels from");
            return VplResult_InvalidArgument;
        }
        levels = auto_levels.data();
        levelCount = auto_levels.size();
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
                              ex[3], extend)
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

VplResult VPL_CALL vplContourGetLevels(const VplContour *contour,
                                       double *buffer,
                                       size_t bufferCount,
                                       size_t *outCount)
{
    VPL_TRY
    if (contour == nullptr) {
        set_error("contour is null");
        return VplResult_InvalidArgument;
    }
    const vpl::ContourSet &cs = *reinterpret_cast<const vpl::ContourSet *>(contour);
    if (outCount != nullptr) {
        *outCount = cs.levels.size();
    }
    if (buffer == nullptr) {
        return VplResult_Success;
    }
    if (bufferCount < cs.levels.size()) {
        set_error("buffer is too small for the levels");
        return VplResult_BufferTooSmall;
    }
    std::copy(cs.levels.begin(), cs.levels.end(), buffer);
    return VplResult_Success;
    VPL_CATCH
}

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

/* No negative extents -- matplotlib raises on those. NaN is allowed and
   skips that point's bar. */
bool valid_extents(const double *e, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        if (e[i] < 0.0) {
            return false;
        }
    }
    return true;
}

bool equals_ignore_case(const char *a, const char *b)
{
    for (; *a != '\0' && *b != '\0'; ++a, ++b) {
        if (std::tolower(static_cast<unsigned char>(*a)) !=
            std::tolower(static_cast<unsigned char>(*b))) {
            return false;
        }
    }
    return *a == *b;
}

} // namespace

VplResult VPL_CALL vplAxesErrorBarFull(VplAxes *axes,
                                       const double *x,
                                       const double *y,
                                       size_t count,
                                       const double *yerrLower,
                                       const double *yerrUpper,
                                       const double *xerrLower,
                                       const double *xerrUpper,
                                       const VplErrorBarDesc *eb,
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
    if ((yerrLower == nullptr && yerrUpper != nullptr) ||
        (xerrLower == nullptr && xerrUpper != nullptr)) {
        set_error("an upper error array needs its lower one");
        return VplResult_InvalidArgument;
    }
    for (const double *e : {yerrLower, yerrUpper, xerrLower, xerrUpper}) {
        if (e != nullptr && !valid_extents(e, count)) {
            set_error("error extents must not be negative");
            return VplResult_InvalidArgument;
        }
    }

    vpl::Color ecolor;
    bool has_ecolor = false;
    if (eb != nullptr) {
        if (eb->structSize == 0) {
            set_error("VplErrorBarDesc.structSize is 0");
            return VplResult_InvalidArgument;
        }
        if (!(eb->capsize >= 0.0) || !(eb->capthick >= 0.0) || !(eb->elinewidth >= 0.0)) {
            set_error("capsize, capthick and elinewidth must be non-negative");
            return VplResult_InvalidArgument;
        }
        if (eb->structSize >= offsetof(VplErrorBarDesc, eLineStyle) + sizeof(eb->eLineStyle) &&
            (eb->eLineStyle < VplLineStyle_Solid || eb->eLineStyle > VplLineStyle_None)) {
            set_error("unknown eLineStyle");
            return VplResult_InvalidArgument;
        }
        if (eb->eColorSpec != nullptr) {
            if (!vpl::parse_color(eb->eColorSpec, ecolor)) {
                set_error("unrecognized ecolor spec");
                return VplResult_InvalidArgument;
            }
            has_ecolor = true;
        } else if (eb->hasEColor != 0) {
            ecolor = vpl::Color{eb->eColor[0], eb->eColor[1], eb->eColor[2], eb->eColor[3]};
            has_ecolor = true;
        }
    }

    /* fmt='none' is not a format the line parser knows: strip it and draw
       only the bars. */
    VplLineDesc line_desc{};
    bool bars_only = false;
    if (desc != nullptr) {
        line_desc = *desc;
        if ((desc->set & VplLineField_Format) != 0 && desc->format != nullptr &&
            equals_ignore_case(desc->format, "none")) {
            bars_only = true;
            line_desc.set &= ~static_cast<uint32_t>(VplLineField_Format);
        }
    }

    vpl::Line2D &line = axes_of(axes)->errorbar(x, y, yerrLower, count);
    if (yerrUpper != nullptr) {
        line.yerr_upper.assign(yerrUpper, yerrUpper + count);
    }
    if (xerrLower != nullptr) {
        line.xerr.assign(xerrLower, xerrLower + count);
    }
    if (xerrUpper != nullptr) {
        line.xerr_upper.assign(xerrUpper, xerrUpper + count);
    }
    if (eb != nullptr) {
        line.capsize = eb->capsize;
        line.capthick = eb->capthick > 0.0 ? eb->capthick : -1.0;
        line.elinewidth = eb->elinewidth;
        line.errorevery_offset = eb->errorEveryOffset;
        line.errorevery_step = eb->errorEveryStep == 0 ? 1 : eb->errorEveryStep;
        line.barsabove = eb->barsAbove != 0;
        if (eb->structSize >= offsetof(VplErrorBarDesc, eLineStyle) + sizeof(eb->eLineStyle)) {
            static const vpl::LineStyle styles[] = {vpl::LineStyle::Solid, vpl::LineStyle::Dashed,
                                                    vpl::LineStyle::DashDot, vpl::LineStyle::Dotted,
                                                    vpl::LineStyle::None};
            line.elinestyle = styles[eb->eLineStyle];
        }
        if (eb->structSize >= offsetof(VplErrorBarDesc, xuplims) + sizeof(eb->xuplims)) {
            const auto take = [count](const uint8_t *src, std::vector<std::uint8_t> &dst) {
                if (src != nullptr) {
                    dst.assign(src, src + count);
                }
            };
            take(eb->lolims, line.lolims);
            take(eb->uplims, line.uplims);
            take(eb->xlolims, line.xlolims);
            take(eb->xuplims, line.xuplims);
        }
    }

    const VplResult r = apply_line_desc(line, desc != nullptr ? &line_desc : nullptr);
    if (r != VplResult_Success) {
        return r;
    }
    if (bars_only) {
        /* matplotlib drops the data line and its cycle colour: the bars fall
           back to C0 unless a colour was given. */
        line.errorbars_only = true;
        if ((line_desc.set & (VplLineField_Color | VplLineField_ColorSpec)) == 0) {
            line.color = vpl::Color::rgb(0.121569, 0.466667, 0.705882);
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
    }
    line.has_ecolor = has_ecolor;
    line.ecolor = ecolor;

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

VplResult VPL_CALL vplAxesHistFull(VplAxes *axes,
                                   const double *const *datasets,
                                   const size_t *counts,
                                   const double *const *weights,
                                   size_t datasetCount,
                                   const VplHistDesc *hist,
                                   const VplPatchDesc *descs,
                                   double *outTops,
                                   double *outEdges,
                                   VplPatch **outPatches)
{
    VPL_TRY
    if (axes == nullptr || datasets == nullptr || counts == nullptr) {
        set_error("axes, datasets or counts is null");
        return VplResult_InvalidArgument;
    }
    if (datasetCount == 0) {
        set_error("datasetCount is 0");
        return VplResult_InvalidArgument;
    }
    for (size_t i = 0; i < datasetCount; ++i) {
        if (counts[i] != 0 && datasets[i] == nullptr) {
            set_error("a dataset with a non-zero count is null");
            return VplResult_InvalidArgument;
        }
    }

    vpl::HistOptions o;
    if (hist != nullptr) {
        if (hist->structSize == 0) {
            set_error("VplHistDesc.structSize is 0");
            return VplResult_InvalidArgument;
        }
        o.bins = hist->bins == 0 ? 10 : hist->bins;
        if (hist->binEdges != nullptr) {
            if (hist->binEdgeCount < 2) {
                set_error("binEdges needs at least two edges");
                return VplResult_InvalidArgument;
            }
            for (size_t k = 0; k < hist->binEdgeCount; ++k) {
                if (!std::isfinite(hist->binEdges[k]) ||
                    (k > 0 && !(hist->binEdges[k] > hist->binEdges[k - 1]))) {
                    set_error("binEdges must be finite and strictly increasing");
                    return VplResult_InvalidArgument;
                }
            }
            o.edges = hist->binEdges;
            o.edge_count = hist->binEdgeCount;
        }
        if (hist->hasRange != 0) {
            if (!std::isfinite(hist->rangeMin) || !std::isfinite(hist->rangeMax) ||
                hist->rangeMin > hist->rangeMax) {
                set_error("range must be finite with rangeMin <= rangeMax");
                return VplResult_InvalidArgument;
            }
            o.has_range = true;
            o.range_lo = hist->rangeMin;
            o.range_hi = hist->rangeMax;
        }
        o.density = hist->density != 0;
        o.cumulative = hist->cumulative > 0 ? 1 : (hist->cumulative < 0 ? -1 : 0);
        switch (hist->histtype) {
        case VplHistType_Bar: o.type = vpl::HistType::Bar; break;
        case VplHistType_BarStacked: o.type = vpl::HistType::BarStacked; break;
        case VplHistType_Step: o.type = vpl::HistType::Step; break;
        case VplHistType_StepFilled: o.type = vpl::HistType::StepFilled; break;
        default: set_error("unknown histtype"); return VplResult_InvalidArgument;
        }
        switch (hist->align) {
        case VplHistAlign_Mid: o.align = vpl::HistAlign::Mid; break;
        case VplHistAlign_Left: o.align = vpl::HistAlign::Left; break;
        case VplHistAlign_Right: o.align = vpl::HistAlign::Right; break;
        default: set_error("unknown align"); return VplResult_InvalidArgument;
        }
        o.horizontal = hist->horizontal != 0;
        if (!std::isfinite(hist->rwidth) || hist->rwidth < 0.0) {
            set_error("rwidth must be finite and non-negative");
            return VplResult_InvalidArgument;
        }
        if (hist->rwidth > 0.0) {
            o.rwidth = hist->rwidth;
        }
        o.stacked = hist->stacked != 0;
        if (!std::isfinite(hist->bottom)) {
            set_error("bottom must be finite");
            return VplResult_InvalidArgument;
        }
        o.bottom = hist->bottom;
        o.log = hist->log != 0;
    }

    std::vector<double> tops;
    std::vector<double> edges;
    const std::vector<vpl::PolyPatch *> made =
        axes_of(axes)->hist(datasets, counts, weights, datasetCount, o, &tops, &edges);

    for (size_t i = 0; i < made.size(); ++i) {
        if (descs != nullptr) {
            const VplResult r = apply_patch_desc(*made[i], &descs[i]);
            if (r != VplResult_Success) {
                return r;
            }
        }
        if (outPatches != nullptr) {
            outPatches[i] = reinterpret_cast<VplPatch *>(made[i]);
        }
    }
    if (outTops != nullptr) {
        std::copy(tops.begin(), tops.end(), outTops);
    }
    if (outEdges != nullptr) {
        std::copy(edges.begin(), edges.end(), outEdges);
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
    /* The struct has grown: accept the original size, and read the later
       fields only when the caller's structSize covers them. */
    constexpr size_t kQuiverDescV1 = offsetof(VplQuiverDesc, label) + sizeof(const char *);
    if (desc != nullptr && (desc->structSize < kQuiverDescV1 ||
                            desc->structSize > sizeof(VplQuiverDesc))) {
        set_error("VplQuiverDesc.structSize does not match this header");
        return VplResult_InvalidArgument;
    }
    const bool desc_v2 = desc != nullptr &&
                         desc->structSize >= offsetof(VplQuiverDesc, colors) + sizeof(const float *);
    const bool desc_v3 = desc != nullptr && desc->structSize >= sizeof(VplQuiverDesc);
    const auto to_units = [](VplQuiverUnits u, vpl::QuiverUnits &out) {
        switch (u) {
        case VplQuiverUnits_Width: out = vpl::QuiverUnits::Width; return true;
        case VplQuiverUnits_Height: out = vpl::QuiverUnits::Height; return true;
        case VplQuiverUnits_Dots: out = vpl::QuiverUnits::Dots; return true;
        case VplQuiverUnits_Inches: out = vpl::QuiverUnits::Inches; return true;
        case VplQuiverUnits_X: out = vpl::QuiverUnits::X; return true;
        case VplQuiverUnits_Y: out = vpl::QuiverUnits::Y; return true;
        case VplQuiverUnits_XY: out = vpl::QuiverUnits::XY; return true;
        default: return false;
        }
    };
    vpl::QuiverUnits units = vpl::QuiverUnits::Width;
    vpl::QuiverUnits scale_units = vpl::QuiverUnits::Width;
    vpl::QuiverAngles angles = vpl::QuiverAngles::UV;
    if (desc_v3) {
        if ((desc->set & VplQuiverField_Units) != 0 && !to_units(desc->units, units)) {
            set_error("unknown VplQuiverUnits");
            return VplResult_InvalidArgument;
        }
        if ((desc->set & VplQuiverField_ScaleUnits) != 0 &&
            !to_units(desc->scaleUnits, scale_units)) {
            set_error("unknown VplQuiverUnits");
            return VplResult_InvalidArgument;
        }
        if ((desc->set & VplQuiverField_Head) != 0 &&
            (!(desc->headWidth >= 0.0) || !(desc->headLength > 0.0) ||
             !(desc->headAxisLength >= 0.0) || !(desc->minShaft >= 0.0) ||
             !(desc->minLength >= 0.0) || !std::isfinite(desc->headWidth + desc->headLength +
                                                         desc->headAxisLength +
                                                         desc->minShaft + desc->minLength))) {
            set_error("head sizes must be finite and non-negative, headLength positive");
            return VplResult_InvalidArgument;
        }
        if ((desc->set & VplQuiverField_Angles) != 0) {
            switch (desc->angles) {
            case VplQuiverAngles_UV: angles = vpl::QuiverAngles::UV; break;
            case VplQuiverAngles_XY: angles = vpl::QuiverAngles::XY; break;
            case VplQuiverAngles_Fixed:
                if (desc->angleDegrees == nullptr) {
                    set_error("VplQuiverAngles_Fixed needs angleDegrees");
                    return VplResult_InvalidArgument;
                }
                angles = vpl::QuiverAngles::Fixed;
                break;
            default: set_error("unknown VplQuiverAngles"); return VplResult_InvalidArgument;
            }
        }
    }
    if (desc_v2 && (desc->set & VplQuiverField_C) != 0) {
        if (desc->C == nullptr) {
            set_error("VplQuiverField_C needs C");
            return VplResult_InvalidArgument;
        }
        if (desc->hasVLimits != 0 && !(desc->vmax > desc->vmin)) {
            set_error("vmin must be below vmax");
            return VplResult_InvalidArgument;
        }
    }
    if (desc_v2 && (desc->set & VplQuiverField_Colors) != 0 && desc->colors == nullptr) {
        set_error("VplQuiverField_Colors needs colors");
        return VplResult_InvalidArgument;
    }
    vpl::Colormap quiver_cmap = vpl::Colormap::Viridis;
    if (desc_v2 && (desc->set & VplQuiverField_C) != 0 && !to_colormap(desc->cmap, quiver_cmap)) {
        set_error("unknown VplColormap");
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
    if (desc_v2 && (desc->set & VplQuiverField_C) != 0) {
        q.c.assign(desc->C, desc->C + count);
        q.cmap = quiver_cmap;
        q.has_vlimits = desc->hasVLimits != 0;
        q.vmin = desc->vmin;
        q.vmax = desc->vmax;
    }
    if (desc_v2 && (desc->set & VplQuiverField_Colors) != 0) {
        for (size_t i = 0; i < count; ++i) {
            const float *c = desc->colors + 4 * i;
            q.colors.push_back(vpl::Color{c[0], c[1], c[2], c[3]});
        }
    }
    if (desc_v3) {
        q.units = units;
        q.has_scale_units = (desc->set & VplQuiverField_ScaleUnits) != 0;
        q.scale_units = scale_units;
        q.angles = angles;
        if (angles == vpl::QuiverAngles::Fixed) {
            q.angle_degrees.assign(desc->angleDegrees, desc->angleDegrees + count);
        }
        if ((desc->set & VplQuiverField_Head) != 0) {
            q.headwidth = desc->headWidth;
            q.headlength = desc->headLength;
            q.headaxislength = desc->headAxisLength;
            q.minshaft = desc->minShaft;
            q.minlength = desc->minLength;
        }
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
    /* The struct has grown: the original size is still accepted, and the
       later fields read only when structSize covers them. */
    constexpr size_t kBarbsDescV1 = offsetof(VplBarbsDesc, lineWidth) + sizeof(double);
    if (desc != nullptr &&
        (desc->structSize < kBarbsDescV1 || desc->structSize > sizeof(VplBarbsDesc))) {
        set_error("VplBarbsDesc.structSize does not match this header");
        return VplResult_InvalidArgument;
    }
    const bool barbs_v2 = desc != nullptr && desc->structSize >= sizeof(VplBarbsDesc);
    vpl::Color barb_color, flag_color;
    vpl::Colormap barb_cmap = vpl::Colormap::Viridis;
    if (barbs_v2) {
        if ((desc->set & VplBarbsField_Sizes) != 0 &&
            (!(desc->spacing >= 0.0) || !(desc->height >= 0.0) || !(desc->width >= 0.0) ||
             !(desc->emptyBarb >= 0.0) ||
             !std::isfinite(desc->spacing + desc->height + desc->width + desc->emptyBarb))) {
            set_error("barb sizes must be finite and non-negative");
            return VplResult_InvalidArgument;
        }
        if ((desc->set & VplBarbsField_BarbColor) != 0 &&
            (desc->barbColorSpec == nullptr || !vpl::parse_color(desc->barbColorSpec, barb_color))) {
            set_error("unrecognized barbColorSpec");
            return VplResult_InvalidArgument;
        }
        if ((desc->set & VplBarbsField_FlagColor) != 0 &&
            (desc->flagColorSpec == nullptr || !vpl::parse_color(desc->flagColorSpec, flag_color))) {
            set_error("unrecognized flagColorSpec");
            return VplResult_InvalidArgument;
        }
        if ((desc->set & VplBarbsField_C) != 0) {
            if (desc->C == nullptr || !to_colormap(desc->cmap, barb_cmap)) {
                set_error("VplBarbsField_C needs C and a known colormap");
                return VplResult_InvalidArgument;
            }
            if (desc->hasVLimits != 0 && !(desc->vmax > desc->vmin)) {
                set_error("vmin must be below vmax");
                return VplResult_InvalidArgument;
            }
        }
        if ((desc->set & VplBarbsField_Flips) != 0 && desc->flips == nullptr) {
            set_error("VplBarbsField_Flips needs flips");
            return VplResult_InvalidArgument;
        }
        if ((desc->set & VplBarbsField_PivotValue) != 0 && !std::isfinite(desc->pivotValue)) {
            set_error("pivotValue must be finite");
            return VplResult_InvalidArgument;
        }
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
    if (barbs_v2) {
        if ((desc->set & VplBarbsField_Sizes) != 0) {
            b.spacing = desc->spacing;
            b.height = desc->height;
            b.width = desc->width;
            b.emptybarb = desc->emptyBarb;
        }
        if ((desc->set & VplBarbsField_BarbColor) != 0) {
            b.has_barbcolor = true;
            b.barbcolor = barb_color;
        }
        if ((desc->set & VplBarbsField_FlagColor) != 0) {
            b.has_flagcolor = true;
            b.flagcolor = flag_color;
        }
        if ((desc->set & VplBarbsField_C) != 0) {
            b.c.assign(desc->C, desc->C + count);
            b.cmap = barb_cmap;
            b.has_vlimits = desc->hasVLimits != 0;
            b.vmin = desc->vmin;
            b.vmax = desc->vmax;
        }
        if ((desc->set & VplBarbsField_Flips) != 0) {
            b.flips.assign(desc->flips, desc->flips + count);
        }
        if ((desc->set & VplBarbsField_PivotValue) != 0) {
            b.has_pivot_value = true;
            b.pivot_value = desc->pivotValue;
        }
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

VplResult VPL_CALL vplAxesXCorrFull(VplAxes *axes,
                                    const double *x,
                                    const double *y,
                                    size_t count,
                                    int maxLags,
                                    int normed,
                                    VplSpectralDetrend detrend,
                                    int32_t useVlines,
                                    const VplLineDesc *desc,
                                    VplLine **outLine)
{
    VPL_TRY
    if (outLine != nullptr) {
        *outLine = nullptr;
    }
    if (axes == nullptr || x == nullptr || y == nullptr) {
        set_error("axes, x or y is null");
        return VplResult_InvalidArgument;
    }
    if (count < 2) {
        set_error("count must be at least 2");
        return VplResult_InvalidArgument;
    }
    if (maxLags == 0) {
        maxLags = static_cast<int>(count - 1);
    }
    if (maxLags < 1 || static_cast<size_t>(maxLags) >= count) {
        set_error("maxLags must be at least 1 and less than count");
        return VplResult_InvalidArgument;
    }
    vpl::SpectralDetrend how = vpl::SpectralDetrend::None;
    switch (detrend) {
    case VplSpectralDetrend_None: how = vpl::SpectralDetrend::None; break;
    case VplSpectralDetrend_Mean: how = vpl::SpectralDetrend::Mean; break;
    case VplSpectralDetrend_Linear: how = vpl::SpectralDetrend::Linear; break;
    default: set_error("unknown VplSpectralDetrend"); return VplResult_InvalidArgument;
    }
    if (useVlines != 0 && desc != nullptr) {
        set_error("desc styles the marker form only; pass NULL with useVlines");
        return VplResult_InvalidArgument;
    }
    vpl::Line2D *line =
        axes_of(axes)->xcorr(x, y, count, maxLags, normed != 0, how, useVlines != 0);
    if (line != nullptr) {
        const VplResult r = apply_line_desc(*line, desc);
        if (r != VplResult_Success) {
            return r;
        }
        if (outLine != nullptr) {
            *outLine = reinterpret_cast<VplLine *>(line);
        }
    }
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesACorrFull(VplAxes *axes,
                                    const double *x,
                                    size_t count,
                                    int maxLags,
                                    int normed,
                                    VplSpectralDetrend detrend,
                                    int32_t useVlines,
                                    const VplLineDesc *desc,
                                    VplLine **outLine)
{
    return vplAxesXCorrFull(axes, x, x, count, maxLags, normed, detrend, useVlines, desc,
                            outLine);
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

/* One replacement field of Python's format mini-language applied to a
   number: [[fill]align][sign][z][#][0][width][grouping][.precision][type]. */
static bool py_format_number(double v, const std::string &spec, std::string &out)
{
    std::size_t i = 0;
    char fill = ' ';
    char align = '\0';
    const auto is_align = [](char c) { return c == '<' || c == '>' || c == '^' || c == '='; };
    if (spec.size() >= 2 && is_align(spec[1])) {
        fill = spec[0];
        align = spec[1];
        i = 2;
    } else if (!spec.empty() && is_align(spec[0])) {
        align = spec[0];
        i = 1;
    }
    char sign = '-';
    if (i < spec.size() && (spec[i] == '+' || spec[i] == '-' || spec[i] == ' ')) {
        sign = spec[i++];
    }
    if (i < spec.size() && spec[i] == 'z') {
        ++i;
    }
    if (i < spec.size() && spec[i] == '#') {
        ++i;
    }
    if (i < spec.size() && spec[i] == '0') {
        if (align == '\0') {
            fill = '0';
            align = '=';
        }
        ++i;
    }
    std::size_t width = 0;
    while (i < spec.size() && spec[i] >= '0' && spec[i] <= '9') {
        width = width * 10 + static_cast<std::size_t>(spec[i++] - '0');
    }
    char group = '\0';
    if (i < spec.size() && (spec[i] == ',' || spec[i] == '_')) {
        group = spec[i++];
    }
    int precision = -1;
    if (i < spec.size() && spec[i] == '.') {
        ++i;
        precision = 0;
        bool any = false;
        while (i < spec.size() && spec[i] >= '0' && spec[i] <= '9') {
            precision = precision * 10 + (spec[i++] - '0');
            any = true;
        }
        if (!any || precision > 100) {
            return false;
        }
    }
    char type = '\0';
    if (i < spec.size()) {
        type = spec[i++];
    }
    if (i != spec.size() || !std::isfinite(v) || width > 256) {
        return false;
    }

    const bool negative = std::signbit(v) && v != 0.0;
    const double a = std::fabs(v);
    char buf[512];
    std::string body;
    std::string suffix;
    switch (type) {
    case 'd':
    case 'n':
        if (precision >= 0 || a != std::floor(a) || a > 9.0e15) {
            return false; /* Python: 'd' needs an integer */
        }
        std::snprintf(buf, sizeof(buf), "%.0f", a);
        body = buf;
        break;
    case '%':
        std::snprintf(buf, sizeof(buf), "%.*f", precision < 0 ? 6 : precision, a * 100.0);
        body = buf;
        suffix = "%";
        break;
    case 'f':
    case 'F':
    case 'e':
    case 'E':
    case 'g':
    case 'G': {
        const char f[5] = {'%', '.', '*', type, '\0'};
        std::snprintf(buf, sizeof(buf), f, precision < 0 ? 6 : precision, a);
        body = buf;
        break;
    }
    case '\0':
        if (precision >= 0) {
            std::snprintf(buf, sizeof(buf), "%.*g", precision == 0 ? 1 : precision, a);
            body = buf;
        } else if (a == std::floor(a) && a < 1.0e16) {
            std::snprintf(buf, sizeof(buf), "%.0f", a);
            body = buf;
        } else {
            /* The shortest digits that read back exactly, as Python's repr. */
            for (int p = 1; p <= 17; ++p) {
                std::snprintf(buf, sizeof(buf), "%.*g", p, a);
                if (std::strtod(buf, nullptr) == a) {
                    break;
                }
            }
            body = buf;
        }
        break;
    default: return false;
    }

    if (group != '\0') {
        std::size_t end = body.find_first_of(".eE");
        if (end == std::string::npos) {
            end = body.size();
        }
        std::string grouped;
        for (std::size_t k = 0; k < end; ++k) {
            if (k != 0 && (end - k) % 3 == 0) {
                grouped += group;
            }
            grouped += body[k];
        }
        body = grouped + body.substr(end);
    }
    body += suffix;

    std::string sgn;
    if (negative) {
        sgn = "-";
    } else if (sign == '+') {
        sgn = "+";
    } else if (sign == ' ') {
        sgn = " ";
    }
    const std::size_t len = sgn.size() + body.size();
    const std::size_t pad = width > len ? width - len : 0;
    switch (align == '\0' ? '>' : align) {
    case '<': out = sgn + body + std::string(pad, fill); break;
    case '^': out = std::string(pad / 2, fill) + sgn + body + std::string(pad - pad / 2, fill); break;
    case '=': out = sgn + std::string(pad, fill) + body; break;
    default: out = std::string(pad, fill) + sgn + body; break;
    }
    return true;
}

/* Python's str.format over the two pie_label fields, {absval} and {frac}. */
static bool py_format_pie(const char *fmt, double absval, double frac, std::string &out)
{
    out.clear();
    for (const char *p = fmt; *p != '\0'; ++p) {
        if (*p == '{' && p[1] == '{') {
            out += '{';
            ++p;
            continue;
        }
        if (*p == '}') {
            if (p[1] != '}') {
                return false; /* a single '}' is an error in Python */
            }
            out += '}';
            ++p;
            continue;
        }
        if (*p != '{') {
            out += *p;
            continue;
        }
        const char *close = std::strchr(p, '}');
        if (close == nullptr) {
            return false;
        }
        const std::string field(p + 1, close);
        const std::size_t colon = field.find(':');
        const std::string name = field.substr(0, colon);
        const std::string spec = colon == std::string::npos ? "" : field.substr(colon + 1);
        double v;
        if (name == "absval") {
            v = absval;
        } else if (name == "frac") {
            v = frac;
        } else {
            return false;
        }
        std::string piece;
        if (!py_format_number(v, spec, piece)) {
            return false;
        }
        out += piece;
        p = close;
    }
    return true;
}

VplResult VPL_CALL vplAxesPieLabelFormat(VplAxes *axes,
                                         const char *format,
                                         double distance,
                                         double fontSize,
                                         VplPieLabelAlign alignment,
                                         int32_t rotate)
{
    VPL_TRY
    if (axes == nullptr || format == nullptr) {
        set_error("axes or format is null");
        return VplResult_InvalidArgument;
    }
    vpl::PieLabelAlign align = vpl::PieLabelAlign::Auto;
    switch (alignment) {
    case VplPieLabelAlign_Auto: align = vpl::PieLabelAlign::Auto; break;
    case VplPieLabelAlign_Center: align = vpl::PieLabelAlign::Center; break;
    case VplPieLabelAlign_Outer: align = vpl::PieLabelAlign::Outer; break;
    default: set_error("unknown alignment"); return VplResult_InvalidArgument;
    }
    vpl::Axes *ax = axes_of(axes);
    std::vector<std::string> text(ax->pie_wedges.size());
    for (size_t i = 0; i < text.size(); ++i) {
        const auto &w = ax->pie_wedges[i];
        if (!py_format_pie(format, w.value, w.frac, text[i])) {
            set_error("format must use {absval} and {frac} with a supported format spec");
            return VplResult_InvalidArgument;
        }
    }
    ax->pie_label(text.data(), text.size(), distance, fontSize > 0.0 ? fontSize : 10.0, align,
                  rotate != 0);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesPieLabelFull(VplAxes *axes,
                                       const char *const *labels,
                                       size_t count,
                                       double distance,
                                       double fontSize,
                                       VplPieLabelAlign alignment,
                                       int32_t rotate)
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
    vpl::PieLabelAlign align = vpl::PieLabelAlign::Auto;
    switch (alignment) {
    case VplPieLabelAlign_Auto: align = vpl::PieLabelAlign::Auto; break;
    case VplPieLabelAlign_Center: align = vpl::PieLabelAlign::Center; break;
    case VplPieLabelAlign_Outer: align = vpl::PieLabelAlign::Outer; break;
    default: set_error("unknown alignment"); return VplResult_InvalidArgument;
    }
    std::vector<std::string> text(count);
    for (size_t i = 0; i < count; ++i) {
        text[i] = labels[i] != nullptr ? labels[i] : "";
    }
    axes_of(axes)->pie_label(text.data(), count, distance, fontSize > 0.0 ? fontSize : 10.0,
                             align, rotate != 0);
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

namespace
{

/* Whether a caller's VplBoxDesc is new enough to hold `field`. */
#define VPL_BOX_HAS(desc, field)                                                                    \
    ((desc)->structSize >= offsetof(VplBoxDesc, field) + sizeof((desc)->field))

void set_error_s(const std::string &message)
{
    set_error(message.c_str());
}

/* One VplBoxProps into vpl::BoxElementProps. `name` is for the message;
   `patch_box` allows the patch-only fields. */
VplResult box_props_from(const VplBoxProps *p, const char *name, bool patch_box,
                         vpl::BoxElementProps &out)
{
    if (p == nullptr) {
        return VplResult_Success;
    }
    if (p->structSize < sizeof(VplBoxProps)) {
        set_error_s(std::string(name) + ".structSize is too small");
        return VplResult_InvalidArgument;
    }
    auto color = [&](uint32_t bit, const char *spec, std::optional<vpl::Color> &dst) {
        if ((p->set & bit) == 0) {
            return true;
        }
        vpl::Color c;
        if (spec == nullptr || !vpl::parse_color(spec, c)) {
            set_error_s(std::string(name) + ": unrecognized colour spec");
            return false;
        }
        dst = c;
        return true;
    };
    const uint32_t patch_only = VplBoxProps_FaceColor | VplBoxProps_EdgeColor | VplBoxProps_Alpha;
    if (!patch_box && (p->set & patch_only) != 0) {
        set_error_s(std::string(name) +
                  ": faceColor, edgeColor and alpha apply to a patch_artist box only");
        return VplResult_InvalidArgument;
    }
    if (!color(VplBoxProps_Color, p->colorSpec, out.color) ||
        !color(VplBoxProps_MarkerFaceColor, p->markerFaceColorSpec, out.markerfacecolor) ||
        !color(VplBoxProps_MarkerEdgeColor, p->markerEdgeColorSpec, out.markeredgecolor) ||
        !color(VplBoxProps_FaceColor, p->faceColorSpec, out.facecolor) ||
        !color(VplBoxProps_EdgeColor, p->edgeColorSpec, out.edgecolor)) {
        return VplResult_InvalidArgument;
    }
    auto size = [&](uint32_t bit, double v, std::optional<double> &dst) {
        if ((p->set & bit) == 0) {
            return true;
        }
        if (!(v >= 0.0) || !std::isfinite(v)) {
            set_error_s(std::string(name) + ": sizes must be finite and non-negative");
            return false;
        }
        dst = v;
        return true;
    };
    if (!size(VplBoxProps_LineWidth, p->lineWidth, out.linewidth) ||
        !size(VplBoxProps_MarkerSize, p->markerSize, out.markersize) ||
        !size(VplBoxProps_MarkerEdgeWidth, p->markerEdgeWidth, out.markeredgewidth)) {
        return VplResult_InvalidArgument;
    }
    if ((p->set & VplBoxProps_Alpha) != 0) {
        if (!(p->alpha >= 0.0 && p->alpha <= 1.0)) {
            set_error_s(std::string(name) + ": alpha must be in [0, 1]");
            return VplResult_InvalidArgument;
        }
        out.alpha = p->alpha;
    }
    if ((p->set & VplBoxProps_LineStyle) != 0) {
        vpl::LineStyle ls;
        if (!to_line_style(p->lineStyle, ls)) {
            set_error_s(std::string(name) + ": unknown line style");
            return VplResult_InvalidArgument;
        }
        if (patch_box && ls != vpl::LineStyle::Solid) {
            set_error_s(std::string(name) + ": a patch_artist box is drawn solid");
            return VplResult_InvalidArgument;
        }
        out.linestyle = ls;
    }
    if ((p->set & VplBoxProps_Marker) != 0) {
        bool ok = false;
        out.marker = to_marker(p->marker, ok);
        if (!ok) {
            set_error_s(std::string(name) + ": unknown marker");
            return VplResult_InvalidArgument;
        }
    }
    return VplResult_Success;
}

/* VplBoxDesc to vpl::BoxOptions; `labels` and `legend` own the strings
   options points at. */
VplResult box_options_from(const VplBoxDesc *desc, size_t groupCount, vpl::BoxOptions &o,
                           std::vector<std::string> &labels, std::vector<std::string> &legend)
{
    if (desc == nullptr) {
        return VplResult_Success;
    }
    if (desc->structSize == 0) {
        set_error("VplBoxDesc.structSize is 0");
        return VplResult_InvalidArgument;
    }
    o.patch_artist = desc->patchArtist != 0;
    if (VPL_BOX_HAS(desc, legendLabels)) {
        if (desc->capWidths != nullptr) {
            for (size_t g = 0; g < groupCount; ++g) {
                if (!(desc->capWidths[g] >= 0.0) || !std::isfinite(desc->capWidths[g])) {
                    set_error("capWidths must be finite and non-negative");
                    return VplResult_InvalidArgument;
                }
            }
            o.capwidths = desc->capWidths;
        }
        const struct
        {
            const VplBoxProps *p;
            const char *name;
            vpl::BoxElementProps *dst;
            bool patch;
        } props[] = {
            {desc->boxProps, "boxProps", &o.boxprops, o.patch_artist},
            {desc->whiskerProps, "whiskerProps", &o.whiskerprops, false},
            {desc->capProps, "capProps", &o.capprops, false},
            {desc->medianProps, "medianProps", &o.medianprops, false},
            {desc->meanProps, "meanProps", &o.meanprops, false},
            {desc->flierProps, "flierProps", &o.flierprops, false},
        };
        for (const auto &e : props) {
            const VplResult r = box_props_from(e.p, e.name, e.patch, *e.dst);
            if (r != VplResult_Success) {
                return r;
            }
        }
        if (desc->legendLabels != nullptr) {
            for (size_t g = 0; g < groupCount; ++g) {
                legend.emplace_back(desc->legendLabels[g] != nullptr ? desc->legendLabels[g] : "");
            }
            o.legend_labels = legend.data();
        }
    }
    if (desc->widths != nullptr) {
        for (size_t g = 0; g < groupCount; ++g) {
            if (!(desc->widths[g] > 0.0) || !std::isfinite(desc->widths[g])) {
                set_error("widths must be positive");
                return VplResult_InvalidArgument;
            }
        }
    }
    if (desc->positions != nullptr) {
        for (size_t g = 0; g < groupCount; ++g) {
            if (!std::isfinite(desc->positions[g])) {
                set_error("positions must be finite");
                return VplResult_InvalidArgument;
            }
        }
    }
    if (desc->labels != nullptr) {
        for (size_t g = 0; g < groupCount; ++g) {
            labels.emplace_back(desc->labels[g] != nullptr ? desc->labels[g] : "");
        }
        o.labels = labels.data();
    }
    o.positions = desc->positions;
    o.widths = desc->widths;
    o.horizontal = desc->horizontal != 0;
    o.shownotches = desc->showNotches != 0;
    o.showmeans = desc->showMeans != 0;
    o.meanline = desc->meanLine != 0;
    o.showcaps = desc->hideCaps == 0;
    o.showbox = desc->hideBox == 0;
    o.showfliers = desc->hideFliers == 0;
    o.manage_ticks = desc->noManageTicks == 0;
    return VplResult_Success;
}

} // namespace

VplResult VPL_CALL vplAxesBoxPlotFull(VplAxes *axes,
                                      const double *values,
                                      const size_t *counts,
                                      size_t groupCount,
                                      const VplBoxDesc *desc)
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
    vpl::BoxOptions o;
    std::vector<std::string> labels, legend;
    const VplResult r = box_options_from(desc, groupCount, o, labels, legend);
    if (r != VplResult_Success) {
        return r;
    }
    double whis = 1.5;
    const double *pct = nullptr;
    vpl::BoxStatsOptions so;
    if (desc != nullptr && VPL_BOX_HAS(desc, sym)) {
        /* boxplot, unlike bxp, maps a patch box's color to its edge. */
        if (o.patch_artist && o.boxprops.color) {
            o.boxprops.edgecolor = o.boxprops.color;
            o.boxprops.color.reset();
        }
        if (desc->bootstrap < 0) {
            set_error("bootstrap must be non-negative");
            return VplResult_InvalidArgument;
        }
        so.bootstrap = desc->bootstrap;
        so.autorange = desc->autorange != 0;
        so.usermedians = desc->userMedians;
        so.conf_intervals = desc->confIntervals;
        if (desc->sym != nullptr) {
            if (desc->sym[0] == '\0') {
                /* sym='': invisible fliers, and none drawn. */
                o.showfliers = false;
            } else {
                vpl::Color c;
                vpl::LineStyle ls = vpl::LineStyle::None;
                vpl::MarkerStyle mk = vpl::MarkerStyle::Circle;
                bool hc = false, hls = false, hmk = false;
                if (!vpl::parse_format(desc->sym, c, hc, ls, hls, mk, hmk)) {
                    set_error("could not parse sym");
                    return VplResult_InvalidArgument;
                }
                if (hmk) {
                    o.flierprops.marker = mk;
                }
                if (hc) {
                    /* A coloured sym means filled markers. */
                    o.flierprops.color = c;
                    o.flierprops.markerfacecolor = c;
                    o.flierprops.markeredgecolor = c;
                }
            }
        }
    }
    if (desc != nullptr) {
        if (desc->whis < 0.0 || !std::isfinite(desc->whis)) {
            set_error("whis must be non-negative");
            return VplResult_InvalidArgument;
        }
        if (desc->whis > 0.0) {
            whis = desc->whis;
        }
        if (desc->hasWhisPercentiles != 0) {
            const double lo = desc->whisPercentiles[0];
            const double hi = desc->whisPercentiles[1];
            if (!(lo >= 0.0 && lo <= hi && hi <= 100.0)) {
                set_error("whisPercentiles must satisfy 0 <= lo <= hi <= 100");
                return VplResult_InvalidArgument;
            }
            pct = desc->whisPercentiles;
        }
    }
    so.whis = whis;
    so.whis_percentiles = pct;
    axes_of(axes)->boxplot(values, counts, groupCount, o, so);
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesBxpFull(VplAxes *axes,
                                  const VplBoxStats *stats,
                                  size_t groupCount,
                                  const double *means,
                                  const double *ciLo,
                                  const double *ciHi,
                                  const VplBoxDesc *desc)
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
    vpl::BoxOptions o;
    std::vector<std::string> labels, legend;
    const VplResult r = box_options_from(desc, groupCount, o, labels, legend);
    if (r != VplResult_Success) {
        return r;
    }
    if (desc != nullptr && VPL_BOX_HAS(desc, sym) &&
        (desc->userMedians != nullptr || desc->confIntervals != nullptr || desc->bootstrap != 0 ||
         desc->autorange != 0 || desc->sym != nullptr)) {
        set_error("userMedians, confIntervals, bootstrap, autorange and sym are boxplot's; "
                  "bxp takes the statistics directly");
        return VplResult_InvalidArgument;
    }
    if (o.showmeans && means == nullptr) {
        set_error("showMeans needs the means");
        return VplResult_InvalidArgument;
    }
    if (o.shownotches && (ciLo == nullptr || ciHi == nullptr)) {
        set_error("showNotches needs ciLo and ciHi");
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
        if (means != nullptr) {
            out.mean = means[g];
        }
        if (ciLo != nullptr && ciHi != nullptr) {
            out.cilo = ciLo[g];
            out.cihi = ciHi[g];
        }
    }
    axes_of(axes)->bxp(converted.data(), groupCount, o);
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

VplResult VPL_CALL vplAxesHexbinFull(VplAxes *axes,
                                     const double *x,
                                     const double *y,
                                     size_t count,
                                     const VplHexbinDesc *desc)
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
    vpl::HexbinOptions o;
    if (desc != nullptr) {
        if (desc->structSize == 0) {
            set_error("VplHexbinDesc.structSize is 0");
            return VplResult_InvalidArgument;
        }
        o.nx = desc->gridsizeX == 0 ? 100 : desc->gridsizeX;
        o.ny = desc->gridsizeY;
        o.C = desc->C;
        switch (desc->reduce) {
        case VplHexReduce_Mean: o.reduce = vpl::HexReduce::Mean; break;
        case VplHexReduce_Sum: o.reduce = vpl::HexReduce::Sum; break;
        case VplHexReduce_Max: o.reduce = vpl::HexReduce::Max; break;
        case VplHexReduce_Min: o.reduce = vpl::HexReduce::Min; break;
        case VplHexReduce_Median: o.reduce = vpl::HexReduce::Median; break;
        case VplHexReduce_Std: o.reduce = vpl::HexReduce::Std; break;
        default: set_error("unknown VplHexReduce"); return VplResult_InvalidArgument;
        }
        o.mincnt = desc->mincnt;
        if (desc->hasExtent != 0) {
            const double *e = desc->extent;
            if (!std::isfinite(e[0]) || !std::isfinite(e[1]) || !std::isfinite(e[2]) ||
                !std::isfinite(e[3]) || !(e[1] > e[0]) || !(e[3] > e[2])) {
                set_error("extent must be finite with xmin < xmax and ymin < ymax");
                return VplResult_InvalidArgument;
            }
            o.has_extent = true;
            std::copy(e, e + 4, o.extent);
        }
        if (!to_colormap(desc->cmap, o.cmap)) {
            set_error("unknown VplColormap");
            return VplResult_InvalidArgument;
        }
        if (desc->hasVLimits != 0) {
            if (!(desc->vmax > desc->vmin) || (desc->logBins != 0 && !(desc->vmin > 0.0))) {
                set_error("vmin must be below vmax (and positive under logBins)");
                return VplResult_InvalidArgument;
            }
            o.has_vlimits = true;
            o.vmin = desc->vmin;
            o.vmax = desc->vmax;
        }
        o.log_bins = desc->logBins != 0;
        o.bins = desc->bins;
        if (desc->structSize >= offsetof(VplHexbinDesc, marginals) + sizeof(desc->marginals)) {
            o.xlog = desc->xLog != 0;
            o.ylog = desc->yLog != 0;
            o.marginals = desc->marginals != 0;
        }
    }
    for (size_t i = 0; i < count; ++i) {
        if ((o.xlog && !(x[i] > 0.0)) || (o.ylog && !(y[i] > 0.0))) {
            set_error("a log-scaled hexbin axis needs positive data");
            return VplResult_InvalidArgument;
        }
    }
    axes_of(axes)->hexbin(x, y, count, o);
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

/* A VplTriangulation resolved to the triangles that are drawn: the given
   ones less the masked, or the Delaunay triangulation. `source` maps each kept
   triangle to its index in the caller's list, for per-triangle values;
   `total` is the length of that list. */
static VplResult resolve_triangulation(const double *x,
                                       const double *y,
                                       size_t count,
                                       const VplTriangulation *tri,
                                       std::vector<vpl::Triangle> &tris,
                                       std::vector<size_t> &source,
                                       size_t &total)
{
    tris.clear();
    source.clear();
    if (tri != nullptr && tri->structSize == 0) {
        set_error("VplTriangulation.structSize is 0");
        return VplResult_InvalidArgument;
    }
    if (tri == nullptr || tri->triangles == nullptr) {
        if (tri != nullptr && tri->mask != nullptr) {
            set_error("a triangulation mask needs explicit triangles");
            return VplResult_InvalidArgument;
        }
        tris = vpl::delaunay(x, y, count);
        total = tris.size();
        for (size_t i = 0; i < tris.size(); ++i) {
            source.push_back(i);
        }
        return VplResult_Success;
    }
    if (tri->triangleCount == 0) {
        set_error("triangleCount is 0");
        return VplResult_InvalidArgument;
    }
    total = tri->triangleCount;
    for (size_t i = 0; i < tri->triangleCount; ++i) {
        const int32_t *t = tri->triangles + 3 * i;
        for (int k = 0; k < 3; ++k) {
            if (t[k] < 0 || static_cast<size_t>(t[k]) >= count) {
                set_error("a triangle index is out of range");
                return VplResult_InvalidArgument;
            }
        }
        if (tri->mask != nullptr && tri->mask[i] != 0) {
            continue;
        }
        tris.push_back(vpl::Triangle{t[0], t[1], t[2]});
        source.push_back(i);
    }
    return VplResult_Success;
}

VplResult VPL_CALL vplTriangulate(const double *x,
                                  const double *y,
                                  size_t count,
                                  int32_t *triangles,
                                  size_t triangleCapacity,
                                  size_t *outTriangleCount)
{
    VPL_TRY
    if (x == nullptr || y == nullptr) {
        set_error("x or y is null");
        return VplResult_InvalidArgument;
    }
    const std::vector<vpl::Triangle> tris = vpl::delaunay(x, y, count);
    if (outTriangleCount != nullptr) {
        *outTriangleCount = tris.size();
    }
    if (triangles == nullptr) {
        return VplResult_Success;
    }
    if (triangleCapacity < tris.size()) {
        set_error("buffer is too small for the triangles");
        return VplResult_BufferTooSmall;
    }
    for (size_t i = 0; i < tris.size(); ++i) {
        triangles[3 * i] = tris[i].a;
        triangles[3 * i + 1] = tris[i].b;
        triangles[3 * i + 2] = tris[i].c;
    }
    return VplResult_Success;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesTriplotFull(VplAxes *axes,
                                      const double *x,
                                      const double *y,
                                      size_t count,
                                      const VplTriangulation *tri,
                                      const char *fmt)
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
    vpl::TriplotStyle style;
    if (fmt != nullptr && fmt[0] != '\0') {
        vpl::Color c;
        bool hc = false, hls = false, hmk = false;
        vpl::LineStyle ls = vpl::LineStyle::Solid;
        vpl::MarkerStyle mk = vpl::MarkerStyle::None;
        if (!vpl::parse_format(fmt, c, hc, ls, hls, mk, hmk)) {
            set_error("could not parse fmt");
            return VplResult_InvalidArgument;
        }
        style.has_color = hc;
        style.color = c;
        style.marker = hmk ? mk : vpl::MarkerStyle::None;
        /* A marker with no line style means markers only, as in plot. */
        style.linestyle = hls ? ls : (hmk ? vpl::LineStyle::None : vpl::LineStyle::Solid);
    }
    std::vector<vpl::Triangle> tris;
    std::vector<size_t> source;
    size_t total = 0;
    const VplResult r = resolve_triangulation(x, y, count, tri, tris, source, total);
    if (r != VplResult_Success) {
        return r;
    }
    axes_of(axes)->triplot(x, y, count, tris, style);
    return VplResult_Success;
    VPL_CATCH
}

/* tricontour and tricontourf differ only in which core call they make: the
   validation, the desc handling and the handle-out are identical. The legacy
   entries pass tri = NULL and always give levels. */
static VplResult tricontour_impl(VplAxes *axes,
                                 const double *x,
                                 const double *y,
                                 const double *z,
                                 size_t count,
                                 const VplTriangulation *tri,
                                 const double *levels,
                                 size_t levelCount,
                                 const VplFieldDesc *desc,
                                 VplContour **outContour,
                                 bool filled)
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
    if (levels != nullptr && levelCount == 0) {
        set_error("levelCount is 0");
        return VplResult_InvalidArgument;
    }
    if (levels != nullptr) {
        for (size_t i = 1; i < levelCount; ++i) {
            if (!(levels[i] > levels[i - 1])) {
                set_error("contour levels must be increasing");
                return VplResult_InvalidArgument;
            }
        }
    }
    if (desc != nullptr && desc->structSize == 0) {
        set_error("VplFieldDesc.structSize is 0");
        return VplResult_InvalidArgument;
    }

    vpl::ContourExtend extend = vpl::ContourExtend::Neither;
    if (filled && desc != nullptr &&
        desc->structSize >= offsetof(VplFieldDesc, extend) + sizeof(desc->extend) &&
        (desc->set & VplFieldField_Extend) != 0) {
        switch (desc->extend) {
        case VplContourExtend_Neither: extend = vpl::ContourExtend::Neither; break;
        case VplContourExtend_Min: extend = vpl::ContourExtend::Min; break;
        case VplContourExtend_Max: extend = vpl::ContourExtend::Max; break;
        case VplContourExtend_Both: extend = vpl::ContourExtend::Both; break;
        default: set_error("unknown VplContourExtend"); return VplResult_InvalidArgument;
        }
    }

    std::vector<double> auto_levels;
    if (levels == nullptr) {
        auto_levels = vpl::Axes::auto_contour_levels(z, count, levelCount, extend);
        if (auto_levels.empty()) {
            set_error("no finite values to choose contour levels from");
            return VplResult_InvalidArgument;
        }
        levels = auto_levels.data();
        levelCount = auto_levels.size();
    }

    if (filled && levelCount < 2) {
        set_error("a filled contour needs at least 2 levels");
        return VplResult_InvalidArgument;
    }

    std::vector<vpl::Triangle> tris;
    std::vector<size_t> source;
    size_t total = 0;
    const VplResult r = resolve_triangulation(x, y, count, tri, tris, source, total);
    if (r != VplResult_Success) {
        return r;
    }

    vpl::Axes *ax = axes_of(axes);
    vpl::ContourSet &cs =
        filled ? ax->tricontourf(x, y, z, count, tris, levels, levelCount, extend)
               : ax->tricontour(x, y, z, count, tris, levels, levelCount);

    if (desc != nullptr) {
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
    if (levels == nullptr) {
        set_error("levels is null");
        return VplResult_InvalidArgument;
    }
    return tricontour_impl(axes, x, y, z, count, nullptr, levels, levelCount, desc,
                           outContour, /*filled=*/false);
}

VplResult VPL_CALL vplAxesTriContourFull(VplAxes *axes,
                                         const double *x,
                                         const double *y,
                                         const double *z,
                                         size_t count,
                                         const VplTriangulation *tri,
                                         const double *levels,
                                         size_t levelCount,
                                         const VplFieldDesc *desc,
                                         VplContour **outContour)
{
    return tricontour_impl(axes, x, y, z, count, tri, levels, levelCount, desc, outContour,
                           /*filled=*/false);
}

VplResult VPL_CALL vplAxesTriContourFFull(VplAxes *axes,
                                          const double *x,
                                          const double *y,
                                          const double *z,
                                          size_t count,
                                          const VplTriangulation *tri,
                                          const double *levels,
                                          size_t levelCount,
                                          const VplFieldDesc *desc,
                                          VplContour **outContour)
{
    return tricontour_impl(axes, x, y, z, count, tri, levels, levelCount, desc, outContour,
                           /*filled=*/true);
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
    if (levels == nullptr) {
        set_error("levels is null");
        return VplResult_InvalidArgument;
    }
    return tricontour_impl(axes, x, y, z, count, nullptr, levels, levelCount, desc,
                           outContour, /*filled=*/true);
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

VplResult VPL_CALL vplAxesTriPcolorFull(VplAxes *axes,
                                        const double *x,
                                        const double *y,
                                        size_t count,
                                        const VplTriangulation *tri,
                                        const double *c,
                                        size_t cCount,
                                        int32_t cAtTriangles,
                                        const VplFieldDesc *desc)
{
    VPL_TRY
    if (axes == nullptr || x == nullptr || y == nullptr || c == nullptr) {
        set_error("axes, x, y or c is null");
        return VplResult_InvalidArgument;
    }
    if (count < 3) {
        set_error("a triangulation needs at least 3 points");
        return VplResult_InvalidArgument;
    }
    std::vector<vpl::Triangle> tris;
    std::vector<size_t> source;
    size_t total = 0;
    const VplResult r = resolve_triangulation(x, y, count, tri, tris, source, total);
    if (r != VplResult_Success) {
        return r;
    }
    /* Points first on a tie, unless facecolors= is asked for. */
    const bool at_points = cAtTriangles == 0 && cCount == count;
    const bool at_faces = !at_points && cCount == total;
    if (!at_points && !at_faces) {
        set_error("cCount must match the number of points or of triangles");
        return VplResult_InvalidArgument;
    }

    vpl::TripcolorOptions o;
    if (desc != nullptr) {
        if (desc->structSize == 0) {
            set_error("VplFieldDesc.structSize is 0");
            return VplResult_InvalidArgument;
        }
        if ((desc->set & VplFieldField_Colormap) != 0 && !to_colormap(desc->cmap, o.cmap)) {
            set_error("unknown VplColormap");
            return VplResult_InvalidArgument;
        }
        if ((desc->set & VplFieldField_VLimits) != 0) {
            if (!(desc->vmax > desc->vmin)) {
                set_error("vmin must be below vmax");
                return VplResult_InvalidArgument;
            }
            o.has_vlimits = true;
            o.vmin = desc->vmin;
            o.vmax = desc->vmax;
        }
        if ((desc->set & VplFieldField_Alpha) != 0) {
            if (!(desc->alpha >= 0.0 && desc->alpha <= 1.0)) {
                set_error("alpha must be in [0, 1]");
                return VplResult_InvalidArgument;
            }
            o.alpha = desc->alpha;
        }
    }

    std::vector<double> facevals;
    facevals.reserve(tris.size());
    for (size_t i = 0; i < tris.size(); ++i) {
        const vpl::Triangle &t = tris[i];
        facevals.push_back(at_points ? (c[t.a] + c[t.b] + c[t.c]) / 3.0 : c[source[i]]);
    }
    axes_of(axes)->tripcolor(x, y, count, tris, facevals, o);
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

VplResult VPL_CALL vplAxesViolinPlotFull(VplAxes *axes,
                                         const double *values,
                                         const size_t *counts,
                                         size_t groupCount,
                                         const VplViolinDesc *desc)
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
    vpl::ViolinOptions o;
    if (desc != nullptr) {
        if (desc->structSize == 0) {
            set_error("VplViolinDesc.structSize is 0");
            return VplResult_InvalidArgument;
        }
        if ((desc->quantiles == nullptr) != (desc->quantileCounts == nullptr)) {
            set_error("quantiles and quantileCounts go together");
            return VplResult_InvalidArgument;
        }
        if (desc->quantiles != nullptr) {
            size_t total = 0;
            for (size_t g = 0; g < groupCount; ++g) {
                total += desc->quantileCounts[g];
            }
            for (size_t k = 0; k < total; ++k) {
                if (!(desc->quantiles[k] >= 0.0 && desc->quantiles[k] <= 1.0)) {
                    set_error("quantiles must lie in [0, 1]");
                    return VplResult_InvalidArgument;
                }
            }
        }
        if (desc->width < 0.0 || !std::isfinite(desc->width)) {
            set_error("width must be non-negative");
            return VplResult_InvalidArgument;
        }
        switch (desc->bwMethod) {
        case VplViolinBandwidth_Scott: o.bandwidth = vpl::ViolinOptions::Bandwidth::Scott; break;
        case VplViolinBandwidth_Silverman:
            o.bandwidth = vpl::ViolinOptions::Bandwidth::Silverman;
            break;
        case VplViolinBandwidth_Scalar:
            if (!(desc->bwFactor > 0.0) || !std::isfinite(desc->bwFactor)) {
                set_error("bwFactor must be positive");
                return VplResult_InvalidArgument;
            }
            o.bandwidth = vpl::ViolinOptions::Bandwidth::Scalar;
            o.bw_factor = desc->bwFactor;
            break;
        default: set_error("unknown VplViolinBandwidth"); return VplResult_InvalidArgument;
        }
        switch (desc->side) {
        case VplViolinSide_Both: o.side = vpl::ViolinOptions::Side::Both; break;
        case VplViolinSide_Low: o.side = vpl::ViolinOptions::Side::Low; break;
        case VplViolinSide_High: o.side = vpl::ViolinOptions::Side::High; break;
        default: set_error("unknown VplViolinSide"); return VplResult_InvalidArgument;
        }
        for (int which = 0; which < 2; ++which) {
            const char *const *specs = which == 0 ? desc->faceColorSpecs : desc->lineColorSpecs;
            const size_t n = which == 0 ? desc->faceColorCount : desc->lineColorCount;
            std::vector<vpl::Color> &out = which == 0 ? o.facecolors : o.linecolors;
            for (size_t i = 0; specs != nullptr && i < n; ++i) {
                vpl::Color c;
                if (specs[i] == nullptr || !vpl::parse_color(specs[i], c)) {
                    set_error("unrecognized colour spec");
                    return VplResult_InvalidArgument;
                }
                out.push_back(c);
            }
        }
        o.positions = desc->positions;
        o.widths = desc->widths;
        o.width = desc->width > 0.0 ? desc->width : 0.5;
        o.showmeans = desc->showMeans != 0;
        o.showextrema = desc->hideExtrema == 0;
        o.showmedians = desc->showMedians != 0;
        o.quantiles = desc->quantiles;
        o.quantile_counts = desc->quantileCounts;
        o.points = desc->points == 0 ? 100 : desc->points;
        o.horizontal = desc->horizontal != 0;
    }
    axes_of(axes)->violinplot(values, counts, groupCount, o);
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
    ax->anchor_code.clear();
    return VplResult_Success;
    VPL_CATCH
}

namespace
{

/* matplotlib's Bbox.coefs: each anchor name and the fractions it stands for. */
struct AnchorCode
{
    const char *name;
    double x, y;
};
constexpr AnchorCode kAnchorCodes[] = {
    {"C", 0.5, 0.5}, {"SW", 0.0, 0.0}, {"S", 0.5, 0.0}, {"SE", 1.0, 0.0}, {"E", 1.0, 0.5},
    {"NE", 1.0, 1.0}, {"N", 0.5, 1.0}, {"NW", 0.0, 1.0}, {"W", 0.0, 0.5},
};

} // namespace

VplResult VPL_CALL vplAxesSetAnchorCode(VplAxes *axes, const char *code)
{
    VPL_TRY
    if (axes == nullptr || code == nullptr) {
        set_error("axes or code is null");
        return VplResult_InvalidArgument;
    }
    for (const AnchorCode &a : kAnchorCodes) {
        if (std::strcmp(a.name, code) == 0) {
            vpl::Axes *ax = axes_of(axes);
            ax->anchor_x = a.x;
            ax->anchor_y = a.y;
            ax->anchor_code = a.name;
            return VplResult_Success;
        }
    }
    set_error("anchor must be one of C, SW, S, SE, E, NE, N, NW, W");
    return VplResult_InvalidArgument;
    VPL_CATCH
}

VplResult VPL_CALL vplAxesGetAnchorCode(const VplAxes *axes, const char **outCode)
{
    VPL_TRY
    if (axes == nullptr || outCode == nullptr) {
        set_error("axes or outCode is null");
        return VplResult_InvalidArgument;
    }
    const vpl::Axes *ax = axes_of(axes);
    *outCode = nullptr;
    for (const AnchorCode &a : kAnchorCodes) {
        if (ax->anchor_code == a.name) {
            *outCode = a.name;
        }
    }
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

VplResult VPL_CALL vplAxesPieFull(VplAxes *axes, const double *x, size_t count,
                                  const VplPieDesc *desc)
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
    double sum = 0.0;
    for (size_t i = 0; i < count; ++i) {
        if (!std::isfinite(x[i]) || x[i] < 0.0) {
            set_error("wedge sizes must be finite and non-negative");
            return VplResult_InvalidArgument;
        }
        sum += x[i];
    }
    if (!(sum > 0.0)) {
        set_error("all wedge sizes are zero");
        return VplResult_InvalidArgument;
    }

    vpl::PieOptions o;
    double label_distance = 1.1;
    double pct_distance = 0.6;
    double font_size = 10.0;
    bool rotate = false;
    const char *const *labels = nullptr;
    const char *autopct = nullptr;
    if (desc != nullptr) {
        if (desc->structSize == 0) {
            set_error("VplPieDesc.structSize is 0");
            return VplResult_InvalidArgument;
        }
        if (desc->noNormalize != 0 && sum > 1.0) {
            set_error("an unnormalized pie cannot sum to more than 1");
            return VplResult_InvalidArgument;
        }
        if (!(desc->radius >= 0.0) || !std::isfinite(desc->radius)) {
            set_error("radius must be positive");
            return VplResult_InvalidArgument;
        }
        if (desc->autopct != nullptr && !is_safe_double_format(desc->autopct)) {
            set_error("autopct must have exactly one %-conversion, ending in f/F/g/G/e/E");
            return VplResult_InvalidArgument;
        }
        if (desc->explode != nullptr) {
            for (size_t i = 0; i < count; ++i) {
                if (!std::isfinite(desc->explode[i])) {
                    set_error("explode must be finite");
                    return VplResult_InvalidArgument;
                }
            }
        }
        if (desc->colorSpecs != nullptr) {
            for (size_t i = 0; i < desc->colorCount; ++i) {
                vpl::Color c;
                if (desc->colorSpecs[i] == nullptr || !vpl::parse_color(desc->colorSpecs[i], c)) {
                    set_error("unrecognized colour spec");
                    return VplResult_InvalidArgument;
                }
                o.colors.push_back(c);
            }
        }
        o.explode = desc->explode;
        o.startangle = desc->startAngle;
        o.radius = desc->radius > 0.0 ? desc->radius : 1.0;
        o.counterclock = desc->clockwise == 0;
        o.normalize = desc->noNormalize == 0;
        o.center_x = desc->centerX;
        o.center_y = desc->centerY;
        o.frame = desc->frame != 0;
        if (desc->structSize >= offsetof(VplPieDesc, shadowShade) + sizeof(desc->shadowShade)) {
            if (!(desc->wedgeWidth >= 0.0) || !std::isfinite(desc->wedgeWidth) ||
                !(desc->wedgeLineWidth >= 0.0)) {
                set_error("wedgeWidth and wedgeLineWidth must be non-negative");
                return VplResult_InvalidArgument;
            }
            o.width = desc->wedgeWidth;
            if (desc->wedgeEdgeColorSpec != nullptr) {
                if (!vpl::parse_color(desc->wedgeEdgeColorSpec, o.edgecolor)) {
                    set_error("unrecognized wedge edge colour spec");
                    return VplResult_InvalidArgument;
                }
                o.has_edgecolor = true;
            }
            if (desc->wedgeLineWidth > 0.0) {
                o.linewidth = desc->wedgeLineWidth;
            }
            o.shadow = desc->shadow != 0;
            if (desc->hasShadowParams != 0) {
                if (!(desc->shadowShade >= 0.0 && desc->shadowShade <= 1.0) ||
                    !std::isfinite(desc->shadowOx) || !std::isfinite(desc->shadowOy)) {
                    set_error("shadowShade must be in [0, 1] and the offsets finite");
                    return VplResult_InvalidArgument;
                }
                o.shadow_ox = desc->shadowOx;
                o.shadow_oy = desc->shadowOy;
                o.shadow_shade = desc->shadowShade;
            }
        }
        labels = desc->labels;
        autopct = desc->autopct;
        rotate = desc->rotateLabels != 0;
        if (desc->labelDistance != 0.0) {
            label_distance = desc->labelDistance;
        }
        if (desc->pctDistance != 0.0) {
            pct_distance = desc->pctDistance;
        }
        if (desc->fontSize > 0.0) {
            font_size = desc->fontSize;
        }
    }

    vpl::Axes *ax = axes_of(axes);
    ax->pie(x, count, o);

    /* The wedge labels go outside, outer-aligned; the percentages inside,
       centred -- both through pie_label, as matplotlib's pie does it. */
    if (labels != nullptr) {
        std::vector<std::string> text(count);
        for (size_t i = 0; i < count; ++i) {
            text[i] = labels[i] != nullptr ? labels[i] : "";
        }
        ax->pie_label(text.data(), count, label_distance, font_size, vpl::PieLabelAlign::Outer,
                      rotate);
    }
    if (autopct != nullptr) {
        std::vector<std::string> text(count);
        for (size_t i = 0; i < count; ++i) {
            char buf[128];
            std::snprintf(buf, sizeof(buf), autopct, 100.0 * ax->pie_wedges[i].frac);
            text[i] = buf;
        }
        ax->pie_label(text.data(), count, pct_distance, font_size, vpl::PieLabelAlign::Center,
                      false);
    }
    return VplResult_Success;
    VPL_CATCH
}

} /* extern "C" */
