/*
 * vpl.hpp - the ergonomic, header-only C++ layer over vplot's C ABI.
 *
 * A thin inline wrapper over the vpl.h C ABI that adds C++ overloading, default
 * arguments and containers so matplotlib code ports across almost verbatim. It
 * inlines straight into the same C calls and adds nothing to the binary. A
 * failing C call becomes a thrown Error carrying vplGetLastError()'s message.
 *
 *     matplotlib                     vpl.hpp
 *     ----------                     -------
 *     fig, ax = plt.subplots()       vplot::Figure fig; auto ax = fig.subplots();
 *     ax.plot(x, y)                  ax.plot(x, y);
 *     ax.plot(y)                     ax.plot(y);
 *     ax.plot(x, y, "r--o")          ax.plot(x, y, "r--o");
 *     ax.plot(x, y, label="sin")     ax.plot(x, y, {.label = "sin"});
 *     ax.bar(x, h, width=0.5)        ax.bar(x, h, 0.5);
 *     ax.set_xlabel("t")             ax.set_xlabel("t");
 *     ax.legend()                    ax.legend();
 *     fig.savefig("out.png")         fig.savefig("out.png");
 *
 * Everything here is a call into vpl.h; use .raw() to reach the C ABI for
 * anything the wrapper does not yet cover.
 */

#ifndef VPL_HPP
#define VPL_HPP

#include "vpl.h"

#include <array>
#include <cstddef>
#include <initializer_list>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

/*
 * NAMESPACE: `vplot`, not `vpl`. The library's implementation classes already
 * live in `namespace vpl` (there is a `vpl::Axes` and a `vpl::Figure`), so a
 * public wrapper of the same qualified name would be an ODR violation. Since
 * matplotlib code names the module rarely, `vplot::Figure` reads just as well.
 */
namespace vplot
{

/* A failing C call, as an exception carrying the ABI's own message. */
class Error : public std::runtime_error
{
  public:
    explicit Error(VplResult code)
        : std::runtime_error(std::string(vplResultToString(code)) + ": " + vplGetLastError()),
          m_code(code)
    {
    }

    VplResult code() const noexcept { return m_code; }

  private:
    VplResult m_code;
};

namespace detail
{

inline void check(VplResult r)
{
    if (r != VplResult_Success) {
        throw Error(r);
    }
}

/* The C ABI's NAN sentinel for set_xbound/set_ybound's "leave this end alone",
   exposed as std::optional/std::nullopt in the public signatures. */
inline double unset_bound() { return std::numeric_limits<double>::quiet_NaN(); }

/*
 * A read-only view of a run of doubles, bindable from a vector, an
 * initializer_list, a std::span, or a raw pointer and length. Lets a method
 * accept `{1, 2, 3}`, `xs`, and `ptr, n` alike without the explicit count the
 * C ABI needs.
 */
class Seq
{
  public:
    Seq(std::initializer_list<double> v) : m_data(v.begin()), m_size(v.size()) {}
    Seq(const std::vector<double> &v) : m_data(v.data()), m_size(v.size()) {}
    Seq(std::span<const double> v) : m_data(v.data()), m_size(v.size()) {}
    Seq(const double *data, std::size_t size) : m_data(data), m_size(size) {}

    const double *data() const noexcept { return m_data; }
    std::size_t size() const noexcept { return m_size; }
    bool empty() const noexcept { return m_size == 0; }

  private:
    const double *m_data;
    std::size_t m_size;
};

} // namespace detail

/*
 * Optional styling for a line -- matplotlib's plot() keyword arguments. Every
 * field is a std::optional, so `{}` sets nothing and the C defaults (tab10
 * cycle, 1.5-point width, ...) apply. Use with designated initialisers:
 *
 *     ax.plot(x, y, {.color = "C1", .linewidth = 2.0, .label = "sin"});
 */
struct LineOpts
{
    std::optional<std::string> color;     /* colour spec: "C1", "#ff0000", "r", ... */
    std::optional<double> linewidth;      /* points */
    std::optional<std::string> linestyle; /* "-", "--", "-.", ":", "none" */
    std::optional<std::string> marker;    /* "o", "s", "^", "+", "x", ... */
    std::optional<double> markersize;     /* points */
    std::optional<std::string> label;     /* legend entry */
};

/* Tick styling for one axis -- matplotlib's tick_params. Every field optional,
   so {} changes nothing and the rcParam defaults hold. */
struct TickOpts
{
    std::optional<VplTickDirection> direction;
    std::optional<double> length;   /* points */
    std::optional<double> width;    /* points */
    std::optional<double> pad;      /* points */
    std::optional<double> labelsize;
    std::optional<bool> ticks_visible;
    std::optional<bool> labels_visible;
    /* tick_params(colors=, labelcolor=), as RGBA rather than a colour spec. */
    std::optional<std::array<float, 4>> color;
    std::optional<std::array<float, 4>> labelcolor;
};

/* Optional styling for a filled patch -- bar(), fill_between() and the rest.
   No edgecolor field: setting linewidth draws the edge in matplotlib's default
   patch edge colour; reach through .raw() for a specific edge colour. */
struct PatchOpts
{
    std::optional<std::string> facecolor;
    std::optional<double> linewidth; /* the edge width; 0 draws no edge */
    std::optional<double> alpha;
    std::optional<std::string> label;
};

namespace detail
{

inline VplMarker marker_from(const std::string &m)
{
    if (m == "o") return VplMarker_Circle;
    if (m == "s") return VplMarker_Square;
    if (m == "^") return VplMarker_TriangleUp;
    if (m == "v") return VplMarker_TriangleDown;
    if (m == "D") return VplMarker_Diamond;
    if (m == "+") return VplMarker_Plus;
    if (m == "x") return VplMarker_Cross;
    if (m == "*") return VplMarker_Star;
    return VplMarker_None; /* "" / "none" */
}

inline VplLineStyle linestyle_from(const std::string &s)
{
    if (s == "--") return VplLineStyle_Dashed;
    if (s == "-.") return VplLineStyle_DashDot;
    if (s == ":") return VplLineStyle_Dotted;
    if (s == "none" || s == "None" || s.empty()) return VplLineStyle_None;
    return VplLineStyle_Solid; /* "-" */
}

inline VplScale scale_from(const std::string &s)
{
    return (s == "log") ? VplScale_Log : VplScale_Linear;
}

inline VplSpine spine_from(const std::string &s)
{
    if (s == "left") return VplSpine_Left;
    if (s == "right") return VplSpine_Right;
    if (s == "top") return VplSpine_Top;
    return VplSpine_Bottom;
}

inline VplColormap cmap_from(const std::string &s)
{
    if (s == "magma") return VplColormap_Magma;
    if (s == "inferno") return VplColormap_Inferno;
    if (s == "plasma") return VplColormap_Plasma;
    if (s == "gray" || s == "grey" || s == "binary") return VplColormap_Gray;
    return VplColormap_Viridis;
}

inline VplStepWhere step_where_from(const std::string &s)
{
    if (s == "post" || s == "steps-post") return VplStepWhere_Post;
    if (s == "mid" || s == "steps-mid") return VplStepWhere_Mid;
    return VplStepWhere_Pre;
}

/* Fill a VplLineDesc from a format string and the options struct, exactly as
   matplotlib layers them: the format first, then the keywords override it. */
inline VplLineDesc line_desc(const char *fmt, const LineOpts &o)
{
    VplLineDesc d{};
    d.structSize = sizeof(d);

    if (fmt != nullptr && fmt[0] != '\0') {
        d.set |= VplLineField_Format;
        d.format = fmt;
    }
    if (o.color) {
        d.set |= VplLineField_ColorSpec;
        d.colorSpec = o.color->c_str();
    }
    if (o.linewidth) {
        d.set |= VplLineField_Width;
        d.width = *o.linewidth;
    }
    if (o.linestyle) {
        d.set |= VplLineField_LineStyle;
        d.linestyle = linestyle_from(*o.linestyle);
    }
    if (o.marker) {
        d.set |= VplLineField_Marker;
        d.marker = marker_from(*o.marker);
    }
    if (o.markersize) {
        d.set |= VplLineField_MarkerSize;
        d.markersize = *o.markersize;
    }
    if (o.label) {
        d.set |= VplLineField_Label;
        d.label = o.label->c_str();
    }
    return d;
}

inline VplPatchDesc patch_desc(const PatchOpts &o)
{
    VplPatchDesc d{};
    d.structSize = sizeof(d);
    if (o.facecolor) {
        d.set |= VplPatchField_FaceColorSpec;
        d.faceColorSpec = o.facecolor->c_str();
    }
    if (o.linewidth) {
        d.set |= VplPatchField_EdgeWidth;
        d.edgeWidth = *o.linewidth;
    }
    if (o.alpha) {
        d.set |= VplPatchField_Alpha;
        d.alpha = *o.alpha;
    }
    if (o.label) {
        d.set |= VplPatchField_Label;
        d.label = o.label->c_str();
    }
    return d;
}

} // namespace detail

/* Non-owning handles to the objects a plotting call returns, for the calls that
   take one back -- bar_label(patch), clabel(contour), errorbar then set_xerr.
   Each is a thin pointer with a .raw() escape hatch. */
class Line
{
  public:
    explicit Line(VplLine *raw) : m_raw(raw) {}
    VplLine *raw() const noexcept { return m_raw; }
  private:
    VplLine *m_raw;
};
class Patch
{
  public:
    explicit Patch(VplPatch *raw) : m_raw(raw) {}
    VplPatch *raw() const noexcept { return m_raw; }
  private:
    VplPatch *m_raw;
};
class Image
{
  public:
    explicit Image(VplImage *raw) : m_raw(raw) {}
    VplImage *raw() const noexcept { return m_raw; }
  private:
    VplImage *m_raw;
};
class Contour
{
  public:
    explicit Contour(VplContour *raw) : m_raw(raw) {}
    VplContour *raw() const noexcept { return m_raw; }
    /* clabel(): label the contour lines in place. */
    void clabel() { detail::check(vplContourClabel(m_raw, nullptr)); }
  private:
    VplContour *m_raw;
};

/*
 * A non-owning handle to an axes. Copyable and cheap -- the axes itself lives
 * in the figure and dies with it, as in the C ABI and in matplotlib.
 */
class Axes
{
  public:
    explicit Axes(VplAxes *raw) : m_raw(raw) {}

    VplAxes *raw() const noexcept { return m_raw; }

    /* ax.lines / ax.patches / ax.images: the artists already added, in order --
       the same handles a plotting call returned, retrievable again if the
       caller did not keep them. */
    std::vector<Line> lines() const
    {
        std::size_t n = 0;
        detail::check(vplAxesGetLineCount(m_raw, &n));
        std::vector<Line> out;
        out.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            VplLine *l = nullptr;
            detail::check(vplAxesGetLine(m_raw, i, &l));
            out.push_back(Line(l));
        }
        return out;
    }
    std::vector<Patch> patches() const
    {
        std::size_t n = 0;
        detail::check(vplAxesGetPatchCount(m_raw, &n));
        std::vector<Patch> out;
        out.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            VplPatch *p = nullptr;
            detail::check(vplAxesGetPatch(m_raw, i, &p));
            out.push_back(Patch(p));
        }
        return out;
    }
    std::vector<Image> images() const
    {
        std::size_t n = 0;
        detail::check(vplAxesGetImageCount(m_raw, &n));
        std::vector<Image> out;
        out.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            VplImage *im = nullptr;
            detail::check(vplAxesGetImage(m_raw, i, &im));
            out.push_back(Image(im));
        }
        return out;
    }

    // ---- plot, in matplotlib's three forms ------------------------------
    void plot(detail::Seq y, const char *fmt = nullptr, const LineOpts &o = {})
    {
        const VplLineDesc d = detail::line_desc(fmt, o);
        detail::check(vplAxesPlotY(m_raw, y.data(), y.size(), &d, nullptr));
    }
    void plot(detail::Seq x, detail::Seq y, const char *fmt = nullptr, const LineOpts &o = {})
    {
        const VplLineDesc d = detail::line_desc(fmt, o);
        detail::check(vplAxesPlot(m_raw, x.data(), y.data(), std::min(x.size(), y.size()),
                                  &d, nullptr));
    }
    /* plot(y, {.label=...}) and plot(x, y, {.label=...}): the fmt-less forms. */
    void plot(detail::Seq y, const LineOpts &o) { plot(y, nullptr, o); }
    void plot(detail::Seq x, detail::Seq y, const LineOpts &o) { plot(x, y, nullptr, o); }

    // ---- the other artists a first port reaches for ---------------------
    void scatter(detail::Seq x, detail::Seq y)
    {
        detail::check(vplAxesScatter(m_raw, x.data(), y.data(),
                                     std::min(x.size(), y.size()),
                                     /*sizes=*/nullptr, /*colors=*/nullptr,
                                     /*desc=*/nullptr, /*out=*/nullptr));
    }

    void bar(detail::Seq x, detail::Seq height, double width = 0.8, const PatchOpts &o = {})
    {
        const VplPatchDesc d = detail::patch_desc(o);
        detail::check(vplAxesBar(m_raw, x.data(), height.data(), width, /*bottom=*/nullptr,
                                 std::min(x.size(), height.size()), &d, nullptr));
    }

    void fill_between(detail::Seq x, detail::Seq y1, detail::Seq y2, const PatchOpts &o = {},
                      const std::vector<std::uint8_t> &where = {}, bool interpolate = false,
                      VplFillStep step = VplFillStep_None)
    {
        const VplPatchDesc d = detail::patch_desc(o);
        const std::size_t n = std::min({x.size(), y1.size(), y2.size()});
        detail::check(vplAxesFillBetween(m_raw, x.data(), y1.data(), y2.data(), n,
                                         where.empty() ? nullptr : where.data(),
                                         interpolate ? 1 : 0, step, &d, nullptr));
    }

    // ---- the furniture --------------------------------------------------
    void set_xlabel(const std::string &s) { detail::check(vplAxesSetXLabel(m_raw, s.c_str())); }
    void set_ylabel(const std::string &s) { detail::check(vplAxesSetYLabel(m_raw, s.c_str())); }
    void set_title(const std::string &s) { detail::check(vplAxesSetTitle(m_raw, s.c_str())); }

    void set_xlim(double left, double right)
    {
        detail::check(vplAxesSetXLim(m_raw, left, right));
    }
    void set_ylim(double bottom, double top)
    {
        detail::check(vplAxesSetYLim(m_raw, bottom, top));
    }

    /* set_xbound / set_ybound: like set_xlim but leaves the current orientation
       alone, and std::nullopt for either end means "leave this end where it is"
       (matplotlib's lower=None/upper=None). */
    void set_xbound(std::optional<double> lower, std::optional<double> upper)
    {
        detail::check(vplAxesSetXBound(m_raw, lower.value_or(detail::unset_bound()),
                                       upper.value_or(detail::unset_bound())));
    }
    void set_ybound(std::optional<double> lower, std::optional<double> upper)
    {
        detail::check(vplAxesSetYBound(m_raw, lower.value_or(detail::unset_bound()),
                                       upper.value_or(detail::unset_bound())));
    }

    void grid(bool on = true, VplGridWhich which = VplGridWhich_Major,
              VplGridAxis axis = VplGridAxis_Both)
    {
        detail::check(vplAxesSetGrid(m_raw, on ? 1 : 0, which, axis));
    }

    void legend(VplLegendLoc loc = VplLegendLoc_Best)
    {
        detail::check(vplAxesSetLegend(m_raw, 1, loc));
    }

    /* legend(title=...): a heading centred above the entries. */
    void legend(const std::string &title, VplLegendLoc loc = VplLegendLoc_Best)
    {
        detail::check(vplAxesSetLegend(m_raw, 1, loc));
        detail::check(vplAxesSetLegendTitle(m_raw, title.c_str()));
    }

    /* legend(ncols=...): entries flow down each column before starting the
       next. A separate name to avoid colliding with VplLegendLoc's implicit
       int conversion. */
    void legend_ncols(int ncols) { detail::check(vplAxesSetLegendNCols(m_raw, ncols)); }

    // ---- lines: bars, error bars, steps, stems, stairs ------------------
    Patch bar_label(const Patch &bars)
    {
        detail::check(vplAxesBarLabel(m_raw, bars.raw()));
        return bars;
    }
    /* bar_label(fmt=, label_type=, padding=). fmt is a single %-style
       floating-point conversion (f/F/g/G/e/E); anything else is rejected. */
    Patch bar_label(const Patch &bars, const std::string &fmt,
                    VplBarLabelType labelType = VplBarLabelType_Edge, double padding = 0.0)
    {
        detail::check(vplAxesBarLabelFull(m_raw, bars.raw(), fmt.c_str(), labelType, padding));
        return bars;
    }
    Patch barh(detail::Seq y, detail::Seq width, double height = 0.8,
               const PatchOpts &o = {})
    {
        const VplPatchDesc d = detail::patch_desc(o);
        VplPatch *out = nullptr;
        detail::check(vplAxesBarH(m_raw, y.data(), width.data(), height, nullptr,
                                  std::min(y.size(), width.size()), &d, &out));
        return Patch(out);
    }
    Patch bar_patch(detail::Seq x, detail::Seq height, double width = 0.8,
                    const PatchOpts &o = {})
    {
        const VplPatchDesc d = detail::patch_desc(o);
        VplPatch *out = nullptr;
        detail::check(vplAxesBar(m_raw, x.data(), height.data(), width, nullptr,
                                 std::min(x.size(), height.size()), &d, &out));
        return Patch(out);
    }
    Line errorbar(detail::Seq x, detail::Seq y, detail::Seq yerr, double capsize = 0.0,
                  const LineOpts &o = {})
    {
        const VplLineDesc d = detail::line_desc(nullptr, o);
        const std::size_t n = std::min({x.size(), y.size(), yerr.size()});
        VplLine *out = nullptr;
        detail::check(vplAxesErrorBar(m_raw, x.data(), y.data(), yerr.data(), n, capsize,
                                      &d, &out));
        return Line(out);
    }
    void set_xerr(const Line &line, detail::Seq xerr)
    {
        detail::check(vplAxesSetXErr(m_raw, line.raw(), xerr.data(), xerr.size()));
    }
    void step(detail::Seq x, detail::Seq y, const std::string &where = "pre",
              const LineOpts &o = {})
    {
        const VplLineDesc d = detail::line_desc(nullptr, o);
        detail::check(vplAxesStep(m_raw, x.data(), y.data(), std::min(x.size(), y.size()),
                                  detail::step_where_from(where), &d, nullptr));
    }
    void stem(detail::Seq x, detail::Seq y, const std::string &linefmt = "",
              const std::string &markerfmt = "", const std::string &basefmt = "",
              double bottom = 0.0)
    {
        detail::check(vplAxesStem(m_raw, x.data(), y.data(), std::min(x.size(), y.size()),
                                  linefmt.empty() ? nullptr : linefmt.c_str(),
                                  markerfmt.empty() ? nullptr : markerfmt.c_str(),
                                  basefmt.empty() ? nullptr : basefmt.c_str(), bottom));
    }
    void stairs(detail::Seq values, detail::Seq edges, const PatchOpts &o = {},
                bool fill = false, detail::Seq baseline = {}, bool horizontal = false)
    {
        const VplPatchDesc d = detail::patch_desc(o);
        detail::check(vplAxesStairs(m_raw, values.data(), edges.data(), values.size(),
                                    horizontal ? 1 : 0, baseline.size() ? baseline.data() : nullptr,
                                    baseline.size(), fill ? 1 : 0, &d, nullptr));
    }
    void stackplot(detail::Seq x, const double *ys, std::size_t series_count,
                   VplStackBaseline baseline = VplStackBaseline_Zero)
    {
        detail::check(vplAxesStackPlot(m_raw, x.data(), ys, series_count, x.size(), baseline));
    }
    Line ecdf(detail::Seq x, const LineOpts &o = {}, bool complementary = false,
              detail::Seq weights = {}, bool horizontal = false)
    {
        const VplLineDesc d = detail::line_desc(nullptr, o);
        VplLine *out = nullptr;
        detail::check(vplAxesEcdf(m_raw, x.data(), x.size(),
                                  weights.size() ? weights.data() : nullptr,
                                  complementary ? 1 : 0, horizontal ? 1 : 0, &d, &out));
        return Line(out);
    }

    // ---- distributions --------------------------------------------------
    Patch hist(detail::Seq x, std::size_t bins = 10, const PatchOpts &o = {},
               bool density = false)
    {
        const VplPatchDesc d = detail::patch_desc(o);
        VplPatch *out = nullptr;
        detail::check(vplAxesHist(m_raw, x.data(), x.size(), bins, density ? 1 : 0, &d, &out));
        return Patch(out);
    }
    void hist2d(detail::Seq x, detail::Seq y, std::size_t xbins = 10, std::size_t ybins = 10)
    {
        detail::check(vplAxesHist2D(m_raw, x.data(), y.data(),
                                    std::min(x.size(), y.size()), xbins, ybins));
    }
    void boxplot(detail::Seq values, const std::vector<std::size_t> &counts)
    {
        detail::check(vplAxesBoxPlot(m_raw, values.data(), counts.data(), counts.size()));
    }
    void violinplot(detail::Seq values, const std::vector<std::size_t> &counts)
    {
        detail::check(vplAxesViolinPlot(m_raw, values.data(), counts.data(), counts.size()));
    }
    void pie(detail::Seq x) { detail::check(vplAxesPie(m_raw, x.data(), x.size())); }

    // ---- regions, spans and rules ---------------------------------------
    void fill(detail::Seq x, detail::Seq y, const PatchOpts &o = {})
    {
        const VplPatchDesc d = detail::patch_desc(o);
        detail::check(vplAxesFill(m_raw, x.data(), y.data(), std::min(x.size(), y.size()),
                                  &d, nullptr));
    }
    void fill_betweenx(detail::Seq y, detail::Seq x1, detail::Seq x2, const PatchOpts &o = {},
                       const std::vector<std::uint8_t> &where = {}, bool interpolate = false,
                       VplFillStep step = VplFillStep_None)
    {
        const VplPatchDesc d = detail::patch_desc(o);
        const std::size_t n = std::min({y.size(), x1.size(), x2.size()});
        detail::check(vplAxesFillBetweenX(m_raw, y.data(), x1.data(), x2.data(), n,
                                          where.empty() ? nullptr : where.data(),
                                          step, interpolate ? 1 : 0, &d, nullptr));
    }
    void broken_barh(detail::Seq x_start, detail::Seq x_width, double y0, double height,
                     const PatchOpts &o = {})
    {
        const VplPatchDesc d = detail::patch_desc(o);
        detail::check(vplAxesBrokenBarH(m_raw, x_start.data(), x_width.data(),
                                        std::min(x_start.size(), x_width.size()), y0, height,
                                        &d, nullptr));
    }
    void eventplot(detail::Seq positions, double lineoffset = 1.0, double linelength = 1.0,
                   bool horizontal = true)
    {
        detail::check(vplAxesEventPlot(m_raw, positions.data(), positions.size(),
                                       lineoffset, linelength, horizontal ? 1 : 0, nullptr,
                                       nullptr));
    }
    void hlines(detail::Seq y, detail::Seq xmin, detail::Seq xmax)
    {
        const std::size_t n = std::min({y.size(), xmin.size(), xmax.size()});
        detail::check(vplAxesHLines(m_raw, y.data(), xmin.data(), xmax.data(), n, nullptr,
                                    nullptr));
    }
    void vlines(detail::Seq x, detail::Seq ymin, detail::Seq ymax)
    {
        const std::size_t n = std::min({x.size(), ymin.size(), ymax.size()});
        detail::check(vplAxesVLines(m_raw, x.data(), ymin.data(), ymax.data(), n, nullptr,
                                    nullptr));
    }
    void axhline(double y, const LineOpts &o = {})
    {
        const VplLineDesc d = detail::line_desc(nullptr, o);
        detail::check(vplAxesAxHLine(m_raw, y, &d));
    }
    void axvline(double x, const LineOpts &o = {})
    {
        const VplLineDesc d = detail::line_desc(nullptr, o);
        detail::check(vplAxesAxVLine(m_raw, x, &d));
    }
    void axhspan(double ymin, double ymax) { detail::check(vplAxesAxHSpan(m_raw, ymin, ymax)); }
    void axvspan(double xmin, double xmax) { detail::check(vplAxesAxVSpan(m_raw, xmin, xmax)); }
    void axline(double x, double y, double slope, const LineOpts &o = {})
    {
        const VplLineDesc d = detail::line_desc(nullptr, o);
        detail::check(vplAxesAxLine(m_raw, x, y, slope, /*vertical=*/0, &d));
    }
    void arrow(double x, double y, double dx, double dy, double width = 0.001,
               const PatchOpts &o = {})
    {
        const VplPatchDesc d = detail::patch_desc(o);
        detail::check(vplAxesArrow(m_raw, x, y, dx, dy, width, &d, nullptr));
    }

    // ---- text -----------------------------------------------------------
    void text(double x, double y, const std::string &s)
    {
        detail::check(vplAxesText(m_raw, x, y, s.c_str(), nullptr));
    }
    void annotate(const std::string &s, double x, double y, double text_x, double text_y)
    {
        detail::check(vplAxesAnnotate(m_raw, s.c_str(), x, y, text_x, text_y, nullptr));
    }

    // ---- 2D fields ------------------------------------------------------
    void imshow(detail::Seq values, std::size_t rows, std::size_t cols,
                const std::string &cmap = "viridis")
    {
        VplFieldDesc d{};
        d.structSize = sizeof(d);
        d.set = VplFieldField_Colormap;
        d.cmap = detail::cmap_from(cmap);
        detail::check(vplAxesImshow(m_raw, values.data(), rows, cols, &d, nullptr));
    }
    void matshow(detail::Seq values, std::size_t rows, std::size_t cols)
    {
        detail::check(vplAxesMatShow(m_raw, values.data(), rows, cols, nullptr, nullptr));
    }
    void spy(detail::Seq values, std::size_t rows, std::size_t cols)
    {
        detail::check(vplAxesSpy(m_raw, values.data(), rows, cols, nullptr, nullptr));
    }
    void pcolormesh(detail::Seq x_edges, detail::Seq y_edges, detail::Seq c,
                    std::size_t rows, std::size_t cols)
    {
        detail::check(vplAxesPcolorMesh(m_raw, x_edges.data(), y_edges.data(), c.data(),
                                        rows, cols, nullptr, nullptr));
    }
    Contour contour(detail::Seq values, std::size_t rows, std::size_t cols,
                    detail::Seq levels)
    {
        VplContour *out = nullptr;
        detail::check(vplAxesContour(m_raw, values.data(), rows, cols, levels.data(),
                                     levels.size(), nullptr, &out));
        return Contour(out);
    }
    Contour contourf(detail::Seq values, std::size_t rows, std::size_t cols,
                     detail::Seq levels)
    {
        VplContour *out = nullptr;
        detail::check(vplAxesContourF(m_raw, values.data(), rows, cols, levels.data(),
                                      levels.size(), nullptr, &out));
        return Contour(out);
    }
    void hexbin(detail::Seq x, detail::Seq y, std::size_t gridsize = 30)
    {
        detail::check(vplAxesHexbin(m_raw, x.data(), y.data(), std::min(x.size(), y.size()),
                                    gridsize));
    }

    // ---- vector fields --------------------------------------------------
    void quiver(detail::Seq x, detail::Seq y, detail::Seq u, detail::Seq v)
    {
        const std::size_t n = std::min({x.size(), y.size(), u.size(), v.size()});
        detail::check(vplAxesQuiver(m_raw, x.data(), y.data(), u.data(), v.data(), n,
                                    nullptr));
    }
    void barbs(detail::Seq x, detail::Seq y, detail::Seq u, detail::Seq v)
    {
        const std::size_t n = std::min({x.size(), y.size(), u.size(), v.size()});
        detail::check(vplAxesBarbs(m_raw, x.data(), y.data(), u.data(), v.data(), n,
                                   nullptr));
    }
    void streamplot(detail::Seq x, detail::Seq y, detail::Seq u, detail::Seq v,
                    std::size_t rows, std::size_t cols, double density = 1.0)
    {
        detail::check(vplAxesStreamPlot(m_raw, x.data(), y.data(), u.data(), v.data(), rows,
                                        cols, density));
    }

    // ---- triangulated fields --------------------------------------------
    void triplot(detail::Seq x, detail::Seq y)
    {
        detail::check(vplAxesTriplot(m_raw, x.data(), y.data(), std::min(x.size(), y.size())));
    }
    Contour tricontour(detail::Seq x, detail::Seq y, detail::Seq z, detail::Seq levels)
    {
        const std::size_t n = std::min({x.size(), y.size(), z.size()});
        VplContour *out = nullptr;
        detail::check(vplAxesTriContour(m_raw, x.data(), y.data(), z.data(), n,
                                        levels.data(), levels.size(), nullptr, &out));
        return Contour(out);
    }
    Contour tricontourf(detail::Seq x, detail::Seq y, detail::Seq z, detail::Seq levels)
    {
        const std::size_t n = std::min({x.size(), y.size(), z.size()});
        VplContour *out = nullptr;
        detail::check(vplAxesTriContourF(m_raw, x.data(), y.data(), z.data(), n,
                                         levels.data(), levels.size(), nullptr, &out));
        return Contour(out);
    }
    void tripcolor(detail::Seq x, detail::Seq y, detail::Seq z)
    {
        const std::size_t n = std::min({x.size(), y.size(), z.size()});
        detail::check(vplAxesTriPcolor(m_raw, x.data(), y.data(), z.data(), n));
    }

    // ---- spectral -------------------------------------------------------
    // Each takes an optional VplSpectralDesc for the window/detrend/sides/pad_to
    // knobs; pass nullptr (the default) for matplotlib's defaults.
    void psd(detail::Seq x, std::size_t nfft = 256, double fs = 2.0, std::size_t noverlap = 128,
             const VplSpectralDesc *desc = nullptr)
    {
        detail::check(vplAxesPsd(m_raw, x.data(), x.size(), nfft, fs, noverlap, desc));
    }
    void csd(detail::Seq x, detail::Seq y, std::size_t nfft = 256, double fs = 2.0,
             std::size_t noverlap = 128, const VplSpectralDesc *desc = nullptr)
    {
        detail::check(vplAxesCsd(m_raw, x.data(), y.data(), std::min(x.size(), y.size()),
                                 nfft, fs, noverlap, desc));
    }
    void cohere(detail::Seq x, detail::Seq y, std::size_t nfft = 256, double fs = 2.0,
                std::size_t noverlap = 128, const VplSpectralDesc *desc = nullptr)
    {
        detail::check(vplAxesCohere(m_raw, x.data(), y.data(), std::min(x.size(), y.size()),
                                    nfft, fs, noverlap, desc));
    }
    void magnitude_spectrum(detail::Seq x, double fs = 2.0, bool db = false,
                            const VplSpectralDesc *desc = nullptr)
    {
        detail::check(vplAxesMagnitudeSpectrum(m_raw, x.data(), x.size(), fs, db ? 1 : 0, desc));
    }
    void angle_spectrum(detail::Seq x, double fs = 2.0, const VplSpectralDesc *desc = nullptr)
    {
        detail::check(vplAxesAngleSpectrum(m_raw, x.data(), x.size(), fs, desc));
    }
    void phase_spectrum(detail::Seq x, double fs = 2.0, const VplSpectralDesc *desc = nullptr)
    {
        detail::check(vplAxesPhaseSpectrum(m_raw, x.data(), x.size(), fs, desc));
    }
    void specgram(detail::Seq x, std::size_t nfft = 256, double fs = 2.0,
                  std::size_t noverlap = 128, const VplSpectralDesc *desc = nullptr,
                  VplSpecMode mode = VplSpecMode_Psd, VplSpecScale scale = VplSpecScale_Default,
                  const double *xextent = nullptr, VplColormap cmap = VplColormap_Viridis)
    {
        detail::check(vplAxesSpecgram(m_raw, x.data(), x.size(), nfft, fs, noverlap, desc, mode,
                                      scale, xextent, cmap));
    }
    void xcorr(detail::Seq x, detail::Seq y, int maxlags = 10, bool normed = true)
    {
        detail::check(vplAxesXCorr(m_raw, x.data(), y.data(), std::min(x.size(), y.size()),
                                   maxlags, normed ? 1 : 0));
    }
    void acorr(detail::Seq x, int maxlags = 10, bool normed = true)
    {
        detail::check(vplAxesACorr(m_raw, x.data(), x.size(), maxlags, normed ? 1 : 0));
    }

    // ---- scales, ticks, spines, and the rest of the furniture -----------
    void set_xscale(const std::string &s)
    {
        detail::check(vplAxesSetXScale(m_raw, detail::scale_from(s)));
    }
    void set_yscale(const std::string &s)
    {
        detail::check(vplAxesSetYScale(m_raw, detail::scale_from(s)));
    }
    void set_xticks(detail::Seq ticks)
    {
        detail::check(vplAxesSetXTicks(m_raw, ticks.data(), nullptr, ticks.size()));
    }
    void set_yticks(detail::Seq ticks)
    {
        detail::check(vplAxesSetYTicks(m_raw, ticks.data(), nullptr, ticks.size()));
    }
    void set_xticks(detail::Seq ticks, const std::vector<const char *> &labels)
    {
        detail::check(vplAxesSetXTicks(m_raw, ticks.data(), labels.data(), ticks.size()));
    }
    void set_yticks(detail::Seq ticks, const std::vector<const char *> &labels)
    {
        detail::check(vplAxesSetYTicks(m_raw, ticks.data(), labels.data(), ticks.size()));
    }
    void minorticks_on() { detail::check(vplAxesSetMinorTicks(m_raw, 1, 1)); }
    void minorticks_off() { detail::check(vplAxesSetMinorTicks(m_raw, 0, 0)); }
    void invert_xaxis(bool inverted = true)
    {
        detail::check(vplAxesSetXInverted(m_raw, inverted ? 1 : 0));
    }
    void invert_yaxis(bool inverted = true)
    {
        detail::check(vplAxesSetYInverted(m_raw, inverted ? 1 : 0));
    }
    void set_spine_visible(const std::string &spine, bool visible)
    {
        detail::check(vplAxesSetSpineVisible(m_raw, detail::spine_from(spine),
                                             visible ? 1 : 0));
    }
    void set_axis_off() { detail::check(vplAxesSetAxisOn(m_raw, 0)); }
    void set_axis_on() { detail::check(vplAxesSetAxisOn(m_raw, 1)); }
    void set_frame_on(bool on) { detail::check(vplAxesSetFrameOn(m_raw, on ? 1 : 0)); }
    void set_axisbelow(VplAxisBelow mode) { detail::check(vplAxesSetAxisBelow(m_raw, mode)); }
    void set_facecolor(const std::string &spec)
    {
        detail::check(vplAxesSetFaceColor(m_raw, spec.c_str()));
    }
    void set_aspect(double aspect) { detail::check(vplAxesSetAspect(m_raw, aspect)); }
    void set_adjustable(const std::string &mode)
    {
        detail::check(vplAxesSetAdjustable(
            m_raw, mode == "datalim" ? VplAdjustable_Datalim : VplAdjustable_Box));
    }
    void set_box_aspect(double a) { detail::check(vplAxesSetBoxAspect(m_raw, a)); }
    void set_anchor(double x, double y) { detail::check(vplAxesSetAnchor(m_raw, x, y)); }
    void margins(double x, double y) { detail::check(vplAxesSetMargins(m_raw, x, y)); }
    void set_xmargin(double m) { detail::check(vplAxesSetXMargin(m_raw, m)); }
    void set_ymargin(double m) { detail::check(vplAxesSetYMargin(m_raw, m)); }
    void set_position(double left, double bottom, double width, double height)
    {
        detail::check(vplAxesSetPosition(m_raw, left, bottom, width, height));
    }
    void set_prop_cycle(const std::vector<const char *> &colors)
    {
        detail::check(vplAxesSetPropCycle(m_raw, colors.data(), colors.size()));
    }
    /* set_prop_cycle(color=, linestyle=, marker=, linewidth=): every property
       zipped together, as matplotlib's Cycler adds them. Leave any vector empty
       to omit that property from the cycle. */
    void set_prop_cycle_full(const std::vector<const char *> &colors,
                             const std::vector<VplLineStyle> &linestyles = {},
                             const std::vector<VplMarker> &markers = {},
                             const std::vector<double> &linewidths = {})
    {
        detail::check(vplAxesSetPropCycleFull(
            m_raw, colors.empty() ? nullptr : colors.data(), colors.size(),
            linestyles.empty() ? nullptr : linestyles.data(), linestyles.size(),
            markers.empty() ? nullptr : markers.data(), markers.size(),
            linewidths.empty() ? nullptr : linewidths.data(), linewidths.size()));
    }
    void ticklabel_format(VplAxis axis, bool use_offset, bool scientific, int lo = -5,
                          int hi = 6)
    {
        detail::check(vplAxesTickLabelFormat(m_raw, axis, use_offset ? 1 : 0,
                                             scientific ? 1 : 0, lo, hi));
    }
    void locator_params(VplAxis axis, int nbins)
    {
        detail::check(vplAxesLocatorParams(m_raw, axis, nbins));
    }
    /* locator_params(steps=, tight=), alongside the nbins-only form above. An
       empty `steps` vector means "leave the steps alone". */
    void locator_params(VplAxis axis, int nbins, const std::vector<double> &steps, int tight = -1)
    {
        detail::check(vplAxesLocatorParamsFull(m_raw, axis, nbins,
                                               steps.empty() ? nullptr : steps.data(),
                                               steps.size(), tight));
    }
    void autoscale(int enable = 1, VplAxisSel axis = VplAxisSel_Both, int tight = -1)
    {
        detail::check(vplAxesAutoscale(m_raw, enable, axis, tight));
    }
    void set_use_sticky_edges(bool on) { detail::check(vplAxesSetUseStickyEdges(m_raw, on ? 1 : 0)); }
    void add_sticky_edge_x(double v) { detail::check(vplAxesAddStickyEdgeX(m_raw, v)); }
    void add_sticky_edge_y(double v) { detail::check(vplAxesAddStickyEdgeY(m_raw, v)); }
    void set_autoscale_on(bool on) { detail::check(vplAxesSetAutoscale(m_raw, on ? 1 : 0)); }
    void set_autoscalex_on(bool on) { detail::check(vplAxesSetAutoscaleX(m_raw, on ? 1 : 0)); }
    void set_autoscaley_on(bool on) { detail::check(vplAxesSetAutoscaleY(m_raw, on ? 1 : 0)); }
    void set_view_limits(double xmin, double ymin, double xmax, double ymax)
    {
        detail::check(vplAxesSetViewLimits(m_raw, xmin, ymin, xmax, ymax));
    }
    void update_datalim(double xmin, double ymin, double xmax, double ymax)
    {
        detail::check(vplAxesUpdateDataLim(m_raw, xmin, ymin, xmax, ymax));
    }
    void sharex(const Axes &other) { detail::check(vplAxesShareX(m_raw, other.raw())); }
    void sharey(const Axes &other) { detail::check(vplAxesShareY(m_raw, other.raw())); }
    void label_outer(bool remove_inner_ticks = false)
    {
        detail::check(vplAxesLabelOuter(m_raw, remove_inner_ticks ? 1 : 0));
    }
    void indicate_inset(double x, double y, double width, double height, const Axes &inset)
    {
        detail::check(vplAxesIndicateInset(m_raw, x, y, width, height, inset.raw()));
    }
    void indicate_inset_zoom(const Axes &inset)
    {
        detail::check(vplAxesIndicateInsetZoom(m_raw, inset.raw()));
    }

    void tick_params(VplAxis axis, const TickOpts &o)
    {
        /* Start from the current settings so an unset field is left alone, as
           matplotlib's keyword-by-keyword tick_params does. VplAxis_Both reads
           x for the base. */
        VplTickParams p{};
        detail::check(vplAxesGetTickParams(
            m_raw, axis == VplAxis_Y ? VplAxis_Y : VplAxis_X, &p));
        if (o.direction) p.direction = *o.direction;
        if (o.length) p.length = *o.length;
        if (o.width) p.width = *o.width;
        if (o.pad) p.pad = *o.pad;
        if (o.labelsize) p.labelSize = *o.labelsize;
        if (o.ticks_visible) p.ticksVisible = *o.ticks_visible ? 1 : 0;
        if (o.labels_visible) p.labelsVisible = *o.labels_visible ? 1 : 0;
        if (o.color) {
            for (int i = 0; i < 4; ++i) p.color[i] = (*o.color)[static_cast<std::size_t>(i)];
        }
        if (o.labelcolor) {
            for (int i = 0; i < 4; ++i) {
                p.labelColor[i] = (*o.labelcolor)[static_cast<std::size_t>(i)];
            }
        }
        detail::check(vplAxesSetTickParams(m_raw, axis, &p));
    }

    /* Box-and-whisker plots from statistics you already have -- bxp. Each group
       is (q1, median, q3, whisker low, whisker high); fliers optional. */
    void bxp(const std::vector<VplBoxStats> &stats)
    {
        detail::check(vplAxesBxp(m_raw, stats.data(), stats.size()));
    }

    /* Bars grouped by category -- grouped_bar. `heights` is datasetCount rows of
       groupCount values, row-major. */
    void grouped_bar(const double *heights, std::size_t dataset_count,
                     std::size_t group_count,
                     const std::vector<const char *> &tick_labels = {},
                     const std::vector<const char *> &labels = {},
                     double group_spacing = 1.5, double bar_spacing = 0.0)
    {
        detail::check(vplAxesGroupedBar(
            m_raw, heights, dataset_count, group_count,
            tick_labels.empty() ? nullptr : tick_labels.data(),
            labels.empty() ? nullptr : labels.data(), group_spacing, bar_spacing));
    }

    /* Wedge labels for the last pie(), placed `distance` out from the centre. */
    void pie_label(const std::vector<const char *> &labels, double distance = 1.1,
                   double font_size = 10.0)
    {
        detail::check(vplAxesPieLabel(m_raw, labels.data(), labels.size(), distance,
                                      font_size));
    }

    /* A reference arrow and caption for the last quiver() -- quiverkey. */
    void quiverkey(double x, double y, double magnitude, const std::string &label,
                   double font_size = 10.0)
    {
        detail::check(vplAxesQuiverKey(m_raw, x, y, magnitude, label.c_str(), font_size));
    }

    /* A table of text -- table. `cell_text` is rows*cols strings, row-major. */
    void table(const std::vector<const char *> &cell_text, std::size_t rows,
               std::size_t cols)
    {
        detail::check(vplAxesTable(m_raw, cell_text.data(), rows, cols, nullptr));
    }

  private:
    VplAxes *m_raw;
};

/*
 * An owning figure. Non-copyable, movable; frees itself in the destructor, so
 * there is no vplDestroyFigure to call by hand.
 */
class Figure
{
  public:
    Figure(double width_inches = 6.4, double height_inches = 4.8, double dpi = 100.0)
    {
        VplFigureDesc desc{};
        desc.structSize = sizeof(desc);
        desc.widthInches = width_inches;
        desc.heightInches = height_inches;
        desc.dpi = dpi;
        detail::check(vplCreateFigure(&desc, &m_raw));
    }

    ~Figure()
    {
        if (m_raw != nullptr) {
            vplDestroyFigure(m_raw);
        }
    }

    Figure(const Figure &) = delete;
    Figure &operator=(const Figure &) = delete;

    Figure(Figure &&other) noexcept : m_raw(other.m_raw) { other.m_raw = nullptr; }
    Figure &operator=(Figure &&other) noexcept
    {
        if (this != &other) {
            if (m_raw != nullptr) {
                vplDestroyFigure(m_raw);
            }
            m_raw = other.m_raw;
            other.m_raw = nullptr;
        }
        return *this;
    }

    VplFigure *raw() const noexcept { return m_raw; }

    /* add_subplot(nrows, ncols, index), 1-based like matplotlib's. */
    Axes add_subplot(unsigned nrows = 1, unsigned ncols = 1, unsigned index = 1)
    {
        VplAxes *ax = nullptr;
        detail::check(vplFigureAddSubplot(m_raw, nrows, ncols, index, &ax));
        return Axes(ax);
    }

    /* subplots() with one panel -- the `fig, ax = plt.subplots()` idiom. For a
       grid, use the vector overload below. */
    Axes subplots() { return add_subplot(1, 1, 1); }

    /* subplots(nrows, ncols): every panel, row-major, as matplotlib returns
       them flattened. Built in one C call, which also creates the shared grid. */
    std::vector<Axes> subplots(unsigned nrows, unsigned ncols)
    {
        std::vector<VplAxes *> raw(static_cast<std::size_t>(nrows) * ncols, nullptr);
        detail::check(vplFigureSubplots(m_raw, nrows, ncols, raw.data()));
        std::vector<Axes> out;
        out.reserve(raw.size());
        for (VplAxes *ax : raw) {
            out.push_back(Axes(ax));
        }
        return out;
    }

    /* subplots(nrows, ncols, sharex=, sharey=): the grid form with sharing
       wired in, matplotlib's bool-or-{'none','all','row','col'} as
       VplShareMode. Interior tick labels are stripped automatically, the
       same as plt.subplots() does. */
    std::vector<Axes> subplots(unsigned nrows, unsigned ncols, VplShareMode shareX,
                               VplShareMode shareY)
    {
        std::vector<VplAxes *> raw(static_cast<std::size_t>(nrows) * ncols, nullptr);
        detail::check(vplFigureSubplotsShared(m_raw, nrows, ncols, shareX, shareY, raw.data()));
        std::vector<Axes> out;
        out.reserve(raw.size());
        for (VplAxes *ax : raw) {
            out.push_back(Axes(ax));
        }
        return out;
    }

    void suptitle(const std::string &s, double x = 0.5, double y = 0.98, double fontsize = 0.0)
    {
        detail::check(vplFigureSuptitle(m_raw, s.c_str(), x, y, fontsize));
    }

    void tight_layout() { detail::check(vplFigureTightLayout(m_raw)); }

    void savefig(const std::string &path) { detail::check(vplFigureSaveFig(m_raw, path.c_str(), nullptr)); }

    /* An axes at an explicit position, in figure fractions -- add_axes. */
    Axes add_axes(double left, double bottom, double width, double height)
    {
        VplAxes *ax = nullptr;
        detail::check(vplFigureAddAxes(m_raw, left, bottom, width, height, &ax));
        return Axes(ax);
    }

    /* An inset inside `parent`, its rectangle in the parent's axes fractions. */
    Axes inset_axes(const Axes &parent, double x, double y, double width, double height)
    {
        VplAxes *ax = nullptr;
        detail::check(vplFigureInsetAxes(m_raw, parent.raw(), x, y, width, height, &ax));
        return Axes(ax);
    }

    /* A second axes sharing one of `base`'s axes -- twinx shares x, twiny y. */
    Axes twinx(const Axes &base)
    {
        VplAxes *ax = nullptr;
        detail::check(vplFigureTwinX(m_raw, base.raw(), &ax));
        return Axes(ax);
    }
    Axes twiny(const Axes &base)
    {
        VplAxes *ax = nullptr;
        detail::check(vplFigureTwinY(m_raw, base.raw(), &ax));
        return Axes(ax);
    }

    /* A colorbar strip beside `parent`, over the given value range. */
    Axes colorbar(const Axes &parent, double vmin, double vmax,
                  const std::string &cmap = "viridis")
    {
        VplAxes *ax = nullptr;
        detail::check(vplFigureColorbar(m_raw, parent.raw(), detail::cmap_from(cmap), vmin,
                                        vmax, &ax));
        return Axes(ax);
    }

    /* colorbar(..., orientation='horizontal'): the strip runs below `parent`
       instead of beside it. A separate name so a string-literal cmap argument
       cannot bind to a `bool horizontal` overload by mistake. */
    Axes colorbar_horizontal(const Axes &parent, double vmin, double vmax,
                             const std::string &cmap = "viridis")
    {
        VplAxes *ax = nullptr;
        detail::check(vplFigureColorbarOriented(m_raw, parent.raw(), detail::cmap_from(cmap),
                                                vmin, vmax, /*horizontal=*/1, &ax));
        return Axes(ax);
    }

    /* A secondary axis with its own affine scale of the base -- secondary_xaxis
       / secondary_yaxis. `scale` and `offset` map base value v to scale*v +
       offset. */
    Axes secondary_xaxis(const Axes &base, bool top, double scale, double offset)
    {
        VplAxes *ax = nullptr;
        detail::check(
            vplFigureSecondaryXAxis(m_raw, base.raw(), top ? 1 : 0, scale, offset, &ax));
        return Axes(ax);
    }
    Axes secondary_yaxis(const Axes &base, bool right, double scale, double offset)
    {
        VplAxes *ax = nullptr;
        detail::check(
            vplFigureSecondaryYAxis(m_raw, base.raw(), right ? 1 : 0, scale, offset, &ax));
        return Axes(ax);
    }

    void delaxes(const Axes &ax) { detail::check(vplFigureDelAxes(m_raw, ax.raw())); }
    void clear() { detail::check(vplFigureClear(m_raw)); }

    void supxlabel(const std::string &s, double x = 0.5, double y = 0.01, double fontsize = 0.0)
    {
        detail::check(vplFigureSupXLabel(m_raw, s.c_str(), x, y, fontsize));
    }
    void supylabel(const std::string &s, double x = 0.02, double y = 0.5, double fontsize = 0.0)
    {
        detail::check(vplFigureSupYLabel(m_raw, s.c_str(), x, y, fontsize));
    }

    /* Line up axis labels or titles across a shared row or column. An empty list
       aligns every axes on the figure -- matplotlib's axs=None default. */
    void align_xlabels(const std::vector<Axes> &axs = {})
    {
        const std::vector<VplAxes *> raw = raw_handles(axs);
        detail::check(vplFigureAlignXLabels(m_raw, raw.empty() ? nullptr : raw.data(), raw.size()));
    }
    void align_ylabels(const std::vector<Axes> &axs = {})
    {
        const std::vector<VplAxes *> raw = raw_handles(axs);
        detail::check(vplFigureAlignYLabels(m_raw, raw.empty() ? nullptr : raw.data(), raw.size()));
    }
    void align_labels(const std::vector<Axes> &axs = {})
    {
        const std::vector<VplAxes *> raw = raw_handles(axs);
        detail::check(vplFigureAlignLabels(m_raw, raw.empty() ? nullptr : raw.data(), raw.size()));
    }
    void align_titles(const std::vector<Axes> &axs = {})
    {
        const std::vector<VplAxes *> raw = raw_handles(axs);
        detail::check(vplFigureAlignTitles(m_raw, raw.empty() ? nullptr : raw.data(), raw.size()));
    }

    /* An image straight onto the figure's pixels -- figimage. One scalar per
       device pixel through the colormap, at lower-left corner (xo, yo). */
    void figimage(detail::Seq values, std::size_t rows, std::size_t cols, double xo = 0.0,
                  double yo = 0.0, const std::string &cmap = "viridis")
    {
        VplFieldDesc d{};
        d.structSize = sizeof(d);
        d.set = VplFieldField_Colormap;
        d.cmap = detail::cmap_from(cmap);
        detail::check(vplFigureFigImage(m_raw, values.data(), rows, cols, xo, yo, &d));
    }

    void subplots_adjust(double left, double bottom, double right, double top,
                         double wspace, double hspace)
    {
        VplSubplotParams p{};
        p.left = left;
        p.bottom = bottom;
        p.right = right;
        p.top = top;
        p.wspace = wspace;
        p.hspace = hspace;
        detail::check(vplFigureSubplotsAdjust(m_raw, &p));
    }

    void set_size_inches(double w, double h) { detail::check(vplFigureSetSizeInches(m_raw, w, h)); }
    void set_figwidth(double w) { detail::check(vplFigureSetFigWidth(m_raw, w)); }
    void set_figheight(double h) { detail::check(vplFigureSetFigHeight(m_raw, h)); }
    void set_dpi(double dpi) { detail::check(vplFigureSetDpi(m_raw, dpi)); }
    void set_facecolor(const std::string &spec) { detail::check(vplFigureSetFaceColor(m_raw, spec.c_str())); }
    void set_edgecolor(const std::string &spec) { detail::check(vplFigureSetEdgeColor(m_raw, spec.c_str())); }
    void set_linewidth(double w) { detail::check(vplFigureSetLineWidth(m_raw, w)); }
    void set_frameon(bool on) { detail::check(vplFigureSetFrameOn(m_raw, on ? 1 : 0)); }

  private:
    static std::vector<VplAxes *> raw_handles(const std::vector<Axes> &axs)
    {
        std::vector<VplAxes *> raw;
        raw.reserve(axs.size());
        for (const Axes &a : axs) {
            raw.push_back(a.raw());
        }
        return raw;
    }

    VplFigure *m_raw = nullptr;
};

/* Point the library at its font file, the same as vplSetFontPath. Call once
   before creating a figure if the bundled font is not beside the executable. */
inline void set_font_path(const std::string &path)
{
    detail::check(vplSetFontPath(path.c_str()));
}

} // namespace vplot

#endif /* VPL_HPP */
