/*
 * Differential harness comparing vplot's output against matplotlib's.
 *
 * References are produced by tools/gen_references.py (matplotlib required) and
 * live in tests/references/*.rgba. When absent, each case is reported as
 * skipped and the suite still succeeds.
 *
 * The metric is matplotlib's own root-mean-square difference over all channels
 * (see lib/matplotlib/testing/compare.py).
 */

#include "vpl/vpl.h" /* the general errorbar is reached through the ABI */

#include "agg_renderer.h"
#include "mplutils.h" /* CLOSEPOLY */
#include "colors.h"
#include "figure.h"
#include "font.h"
#include "png_writer.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include <stb_image.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace
{

/* Default tolerance, roughly matplotlib's image_comparison default. */
constexpr double kRmsTolerance = 3.0;

/* A case with a known, understood difference carries its own tolerance and a
   note naming the cause, like matplotlib's image_comparison `tol` argument. */
struct KnownGap
{
    const char *name;
    double tolerance;
    const char *cause;
};

constexpr KnownGap kKnownGaps[] = {
    {"tripcolor", 5.0,
     "differing pixels lie only on shared triangle boundaries; the residue is "
     "draw order, which follows Qhull's hull-walk emission order and is not "
     "worth reproducing since the interiors already agree"},
    {"tight_layout", 10.0,
     "a sub-ULP tie in the shared spine position (312.5 vs 312.49999999999994) "
     "lands it in a different pixel column; matplotlib's own rounding noise, "
     "not a rule that can be ported"},
    {"loglog", 6.0,
     "mathtext box model differs: hinted vs unhinted advances, and the layout "
     "box width/height/depth are ceiled and carry a phantom depth that vplot "
     "does not reproduce, shifting each decade label by about a pixel"},
    {"hist_log", 6.0,
     "the bars and outline match; the residue is the decade labels, the same "
     "mathtext box-model gap as loglog"},
    {"hexbin_log_marginals", 5.0,
     "the hexagons and marginal strips match; the residue is the log axis's "
     "decade labels, the same mathtext box-model gap as loglog"},
};

double tolerance_for(const std::string &name, const char **cause)
{
    for (const KnownGap &gap : kKnownGaps) {
        if (name == gap.name) {
            *cause = gap.cause;
            return gap.tolerance;
        }
    }
    *cause = nullptr;
    return kRmsTolerance;
}

struct Reference
{
    unsigned width = 0;
    unsigned height = 0;
    std::vector<uint8_t> rgba;
};

bool load_reference(const std::string &name, Reference &out)
{
    /* The test binary runs from the build directory, so search up to the
       project root. */
    static const char *prefixes[] = {
        "tests/references/", "../tests/references/", "../../tests/references/",
        "../../../tests/references/", "../../../../tests/references/",
    };

    for (const char *prefix : prefixes) {
        const std::string path = std::string(prefix) + name + ".rgba";
        FILE *f = std::fopen(path.c_str(), "rb");
        if (f == nullptr) {
            continue;
        }

        uint32_t header[2] = {0, 0};
        if (std::fread(header, sizeof(uint32_t), 2, f) != 2) {
            std::fclose(f);
            return false;
        }
        out.width = header[0];
        out.height = header[1];

        const std::size_t expected = static_cast<std::size_t>(out.width) * out.height * 4u;
        out.rgba.resize(expected);
        const std::size_t got = std::fread(out.rgba.data(), 1, expected, f);
        std::fclose(f);

        if (got != expected) {
            std::fprintf(stderr, "  %s: truncated (%zu of %zu bytes)\n", path.c_str(), got,
                         expected);
            return false;
        }
        return true;
    }
    return false;
}

/* testing/compare.calculate_rms */
double rms_difference(const uint8_t *a, const uint8_t *b, std::size_t n)
{
    double total = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double d = static_cast<double>(a[i]) - static_cast<double>(b[i]);
        total += d * d;
    }
    return std::sqrt(total / static_cast<double>(n));
}

/*
 * Loads one of matplotlib's own baseline PNGs under
 * lib/matplotlib/tests/baseline_images/, so a reproducible figure can be
 * compared against real matplotlib pixels with only a PNG decoder.
 */
bool load_mpl_baseline(const std::string &relative, Reference &out, std::string &why)
{
    static const char *roots[] = {
        "_reference/matplotlib/lib/matplotlib/tests/baseline_images/",
        "../_reference/matplotlib/lib/matplotlib/tests/baseline_images/",
        "../../../../../_reference/matplotlib/lib/matplotlib/tests/baseline_images/",
        "../../../../../../_reference/matplotlib/lib/matplotlib/tests/baseline_images/",
    };

    for (const char *root : roots) {
        const std::string path = std::string(root) + relative;
        int w = 0;
        int h = 0;
        int channels = 0;
        /* Force 4 channels so the buffer layout matches AggRenderer's. */
        unsigned char *pixels = stbi_load(path.c_str(), &w, &h, &channels, 4);
        if (pixels == nullptr) {
            continue;
        }
        out.width = static_cast<unsigned>(w);
        out.height = static_cast<unsigned>(h);
        out.rgba.assign(pixels, pixels + static_cast<std::size_t>(w) * h * 4);
        stbi_image_free(pixels);
        return true;
    }

    why = "not found under _reference/matplotlib (clone it, or adjust the path)";
    return false;
}

int checked = 0;
int skipped = 0;
int failed = 0;

/*
 * Reports where two renders disagree: per-channel mean/max difference and a
 * count of differing pixels. Writes <prefix>_vplot.png, <prefix>_mpl.png and
 * <prefix>_diff_mask.png next to the binary.
 */
void report_diff(const std::string &prefix, const Reference &ref, const uint8_t *ours,
                 unsigned w, unsigned h, double dpi)
{
    double channel_sum[4] = {0.0, 0.0, 0.0, 0.0};
    int channel_max[4] = {0, 0, 0, 0};
    std::size_t differing = 0;
    const std::size_t pixel_count = static_cast<std::size_t>(w) * h;

    for (std::size_t i = 0; i < pixel_count; ++i) {
        bool any = false;
        for (int ch = 0; ch < 4; ++ch) {
            const int d = std::abs(static_cast<int>(ours[4 * i + ch]) -
                                   static_cast<int>(ref.rgba[4 * i + ch]));
            channel_sum[ch] += d;
            channel_max[ch] = std::max(channel_max[ch], d);
            if (d > 8) {
                any = true;
            }
        }
        if (any) {
            ++differing;
        }
    }

    std::printf("      mean |diff| per channel: R %.2f  G %.2f  B %.2f  A %.2f\n",
                channel_sum[0] / pixel_count, channel_sum[1] / pixel_count,
                channel_sum[2] / pixel_count, channel_sum[3] / pixel_count);
    std::printf("      max  |diff| per channel: R %d  G %d  B %d  A %d\n", channel_max[0],
                channel_max[1], channel_max[2], channel_max[3]);
    std::printf("      pixels differing by >8: %zu of %zu (%.1f%%)\n", differing,
                pixel_count, 100.0 * differing / pixel_count);

    vpl::write_png(prefix + "_vplot.png", ours, w, h, dpi);
    vpl::write_png(prefix + "_mpl.png", ref.rgba.data(), w, h, dpi);

    /* Red marks a differing pixel over a faded copy of our own render. */
    std::vector<uint8_t> mask(static_cast<std::size_t>(w) * h * 4);
    for (std::size_t i = 0; i < pixel_count; ++i) {
        int worst = 0;
        for (int ch = 0; ch < 3; ++ch) {
            worst = std::max(worst, std::abs(static_cast<int>(ours[4 * i + ch]) -
                                             static_cast<int>(ref.rgba[4 * i + ch])));
        }
        if (worst > 8) {
            mask[4 * i + 0] = 255;
            mask[4 * i + 1] = 0;
            mask[4 * i + 2] = 0;
        } else {
            const uint8_t faded = static_cast<uint8_t>(255 - (255 - ours[4 * i]) / 6);
            mask[4 * i + 0] = faded;
            mask[4 * i + 1] = faded;
            mask[4 * i + 2] = faded;
        }
        mask[4 * i + 3] = 255;
    }
    vpl::write_png(prefix + "_diff_mask.png", mask.data(), w, h, dpi);
}

/* rms, tolerance and the diagnostics, for a raster produced any which way. */
void check(const std::string &name, const Reference &ref, const uint8_t *ours, unsigned w,
           unsigned h, double dpi, double tolerance, const char *cause)
{
    const double rms = rms_difference(ours, ref.rgba.data(), ref.rgba.size());
    ++checked;

    if (rms > tolerance) {
        std::printf("  %-16s FAIL rms %.2f (tolerance %.2f)\n", name.c_str(), rms, tolerance);
        ++failed;
    } else if (cause != nullptr) {
        std::printf("  %-16s GAP  rms %.2f (of %.2f allowed) -- %s\n", name.c_str(), rms,
                    tolerance, cause);
    } else {
        std::printf("  %-16s OK   rms %.2f\n", name.c_str(), rms);
    }
    report_diff(name, ref, ours, w, h, dpi);
}

/*
 * Tests the renderer's text placement directly, with no figure layout: the
 * string origin is given, so only where draw_text puts the baseline is under
 * test. matplotlib's origin y counts down from the top here; vplot's display
 * space is y-up, hence the flip.
 */
void compare_text_probe(vpl::FontEngine &fonts)
{
    const std::string name = "text_probe";
    Reference ref;
    if (!load_reference(name, ref)) {
        std::printf("  %-16s SKIP (no reference; run tools/gen_references.py)\n", name.c_str());
        ++skipped;
        return;
    }

    constexpr unsigned w = 320;
    constexpr unsigned h = 120;
    constexpr double dpi = 100.0;

    if (ref.width != w || ref.height != h) {
        std::printf("  %-16s FAIL size: reference %ux%u, vplot %ux%u\n", name.c_str(),
                    ref.width, ref.height, w, h);
        ++failed;
        return;
    }

    vpl::AggRenderer renderer(w, h, dpi);
    renderer.set_font_engine(&fonts);
    renderer.clear(vpl::Color::rgb(1.0, 1.0, 1.0));

    vpl::GraphicsContext gc;
    gc.color = vpl::Color::rgb(0.0, 0.0, 0.0);

    for (const auto &[size, baseline_row] :
         {std::pair{10.0, 90.0}, std::pair{12.0, 40.0}, std::pair{10.0, 65.0}}) {
        vpl::FontProps font;
        font.size = size;
        /* Rotation mode is spelled out; at zero degrees the modes agree. */
        const double origin_x = baseline_row == 65.0 ? 30.37 : 30.0;
        renderer.draw_text(gc, origin_x, h - baseline_row, "Apg 1.00", font, 0.0,
                           vpl::HAlign::Left, vpl::VAlign::Baseline, vpl::RotationMode::Bbox);
    }

    /* Expected to be exact: same glyphs, same font, same origin. */
    check(name, ref, renderer.pixels(), w, h, dpi, 0.01, nullptr);
}

void compare(const std::string &name, vpl::Figure &fig, vpl::FontEngine &fonts,
             bool tight_layout = false)
{
    Reference ref;
    if (!load_reference(name, ref)) {
        std::printf("  %-16s SKIP (no reference; run tools/gen_references.py)\n", name.c_str());
        ++skipped;
        return;
    }

    const unsigned w = fig.pixel_width();
    const unsigned h = fig.pixel_height();

    if (ref.width != w || ref.height != h) {
        std::printf("  %-16s FAIL size: reference %ux%u, vplot %ux%u\n", name.c_str(),
                    ref.width, ref.height, w, h);
        ++failed;
        return;
    }

    vpl::AggRenderer renderer(w, h, fig.dpi);
    renderer.set_font_engine(&fonts);
    /* tight_layout needs font metrics, so it runs against the renderer about
       to draw. */
    if (tight_layout) {
        fig.tight_layout(renderer);
    }
    renderer.clear(fig.facecolor);
    fig.draw(renderer);

    const char *cause = nullptr;
    const double tolerance = tolerance_for(name, &cause);
    check(name, ref, renderer.pixels(), w, h, fig.dpi, tolerance, cause);
}

/* Each builder mirrors the matching case in tools/gen_references.py exactly,
   including how the sample points are computed. */

vpl::Figure make_simple_line()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    constexpr int n = 201;
    std::vector<double> xs(n), ys(n);
    for (int i = 0; i < n; ++i) {
        xs[i] = i * 6.0 / (n - 1);
        ys[i] = std::sin(xs[i]);
    }
    ax.plot(xs.data(), ys.data(), n);
    return fig;
}

vpl::Figure make_labels()
{
    vpl::Figure fig = make_simple_line();
    vpl::Axes &ax = *fig.axes.front();
    ax.title = "vplot baseline";
    ax.xlabel = "t";
    ax.ylabel = "amplitude";
    return fig;
}

vpl::Figure make_styles_markers()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    constexpr int n = 24;
    std::vector<double> xs(n), a(n), b(n);
    for (int i = 0; i < n; ++i) {
        xs[i] = i * 6.0 / (n - 1);
        a[i] = std::sin(xs[i]);
        b[i] = std::cos(xs[i]) * 0.7;
    }

    vpl::Line2D &l0 = ax.plot(xs.data(), a.data(), n);
    l0.linestyle = vpl::LineStyle::Solid;
    l0.marker = vpl::MarkerStyle::Circle;
    l0.label = "sin";

    vpl::Line2D &l1 = ax.plot(xs.data(), b.data(), n);
    l1.linestyle = vpl::LineStyle::Dashed;
    l1.marker = vpl::MarkerStyle::Square;
    l1.label = "cos";

    ax.set_grid(true);
    ax.legend_visible = true;
    ax.legend_loc = vpl::LegendLoc::UpperRight;
    return fig;
}

vpl::Figure make_loglog()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    constexpr int n = 60;
    std::vector<double> xs(n), ys(n);
    for (int i = 0; i < n; ++i) {
        xs[i] = std::pow(10.0, i * 4.0 / (n - 1));
        ys[i] = xs[i] * xs[i];
    }
    ax.plot(xs.data(), ys.data(), n);
    ax.xscale = vpl::ScaleType::Log;
    ax.yscale = vpl::ScaleType::Log;
    return fig;
}

vpl::Figure make_fill_between()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    constexpr int n = 61;
    std::vector<double> xs(n), lo(n), hi(n);
    for (int i = 0; i < n; ++i) {
        xs[i] = i * 6.0 / (n - 1);
        lo[i] = std::sin(xs[i]) - 0.3;
        hi[i] = std::sin(xs[i]) + 0.3;
    }
    ax.fill_between(xs.data(), lo.data(), hi.data(), n);
    return fig;
}

vpl::Figure make_errorbar()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    constexpr int n = 12;
    std::vector<double> xs(n), ys(n), es(n);
    for (int i = 0; i < n; ++i) {
        xs[i] = i * 6.0 / (n - 1);
        ys[i] = std::sin(xs[i]);
        es[i] = 0.1 + 0.05 * i;
    }
    ax.errorbar(xs.data(), ys.data(), es.data(), n);
    return fig;
}

vpl::Figure make_scatter()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    constexpr int n = 40;
    std::vector<double> xs(n), ys(n);
    for (int i = 0; i < n; ++i) {
        xs[i] = i * 6.0 / (n - 1);
        ys[i] = std::sin(xs[i]);
    }
    ax.scatter(xs.data(), ys.data(), n);
    return fig;
}

vpl::Figure make_bar()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    constexpr int n = 7;
    std::vector<double> xs(n), hs(n);
    for (int i = 0; i < n; ++i) {
        xs[i] = static_cast<double>(i);
        hs[i] = 1.0 + 0.5 * i;
    }
    ax.bar(xs.data(), hs.data(), n, 0.8); /* matplotlib's default width */
    return fig;
}

vpl::Figure make_twinx()
{
    vpl::Figure fig;
    vpl::Axes &ax1 = fig.add_subplot();

    constexpr int n = 61;
    std::vector<double> xs(n), a(n), b(n);
    for (int i = 0; i < n; ++i) {
        xs[i] = i * 6.0 / (n - 1);
        a[i] = std::sin(xs[i]);
        b[i] = 100.0 * std::cos(xs[i]);
    }

    vpl::Line2D &l1 = ax1.plot(xs.data(), a.data(), n);
    vpl::parse_color("C0", l1.color);

    vpl::Axes &ax2 = fig.twinx(ax1);
    vpl::Line2D &l2 = ax2.plot(xs.data(), b.data(), n);
    vpl::parse_color("C1", l2.color);

    return fig;
}

vpl::Figure make_colorbar()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    constexpr int n = 61;
    std::vector<double> xs(n), ys(n);
    for (int i = 0; i < n; ++i) {
        xs[i] = i * 6.0 / (n - 1);
        ys[i] = std::sin(xs[i]);
    }
    ax.plot(xs.data(), ys.data(), n);

    fig.colorbar(ax, vpl::Colormap::Viridis, 0.0, 1.0);
    return fig;
}

vpl::Figure make_colorbar_horizontal()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    constexpr int n = 61;
    std::vector<double> xs(n), ys(n);
    for (int i = 0; i < n; ++i) {
        xs[i] = i * 6.0 / (n - 1);
        ys[i] = std::sin(xs[i]);
    }
    ax.plot(xs.data(), ys.data(), n);

    fig.colorbar(ax, vpl::Colormap::Viridis, 0.0, 1.0, 0.15, -1.0, 20.0, /*horizontal=*/true);
    return fig;
}

vpl::Figure make_text()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    constexpr int n = 61;
    std::vector<double> xs(n), ys(n);
    for (int i = 0; i < n; ++i) {
        xs[i] = i * 6.0 / (n - 1);
        ys[i] = std::sin(xs[i]);
    }
    ax.plot(xs.data(), ys.data(), n);

    ax.text(1.0, 0.5, "left baseline");

    vpl::TextArtist &centred = ax.text(3.0, 0.0, "centred");
    centred.halign = vpl::HAlign::Center;
    centred.valign = vpl::VAlign::Center;

    vpl::TextArtist &rotated = ax.text(4.5, -0.5, "rotated");
    rotated.rotation = 30.0;

    return fig;
}

vpl::Figure make_reference_lines()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    constexpr int n = 61;
    std::vector<double> xs(n), ys(n);
    for (int i = 0; i < n; ++i) {
        xs[i] = i * 6.0 / (n - 1);
        ys[i] = std::sin(xs[i]);
    }
    ax.plot(xs.data(), ys.data(), n);
    ax.axhline(0.5);
    ax.axvline(2.0);
    return fig;
}

vpl::Figure make_minor_ticks()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    constexpr int n = 61;
    std::vector<double> xs(n), ys(n);
    for (int i = 0; i < n; ++i) {
        xs[i] = i * 6.0 / (n - 1);
        ys[i] = std::sin(xs[i]);
    }
    ax.plot(xs.data(), ys.data(), n);
    ax.minor_x_visible = true;
    ax.minor_y_visible = true;
    return fig;
}

vpl::Figure make_scatter_varied()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    constexpr int n = 24;
    std::vector<double> xs(n), ys(n);
    for (int i = 0; i < n; ++i) {
        xs[i] = i * 6.0 / (n - 1);
        ys[i] = std::sin(xs[i]);
    }

    vpl::ScatterCollection &sc = ax.scatter(xs.data(), ys.data(), n);
    for (int i = 0; i < n; ++i) {
        sc.sizes.push_back(20.0 + 8.0 * i);
        vpl::Color c;
        vpl::parse_color(i % 3 == 0 ? "C0" : (i % 3 == 1 ? "C1" : "C2"), c);
        sc.colors.push_back(c);
    }
    return fig;
}

vpl::Figure make_annotate()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    constexpr int n = 61;
    std::vector<double> xs(n), ys(n);
    for (int i = 0; i < n; ++i) {
        xs[i] = i * 6.0 / (n - 1);
        ys[i] = std::sin(xs[i]);
    }
    ax.plot(xs.data(), ys.data(), n);

    vpl::TextArtist &note = ax.text(3.0, 0.6, "peak");
    note.has_arrow = true;
    note.arrow_x = 1.5707963267948966;
    note.arrow_y = 1.0;
    return fig;
}

vpl::Figure make_hist()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    constexpr int n = 400;
    std::vector<double> vals(n);
    for (int i = 0; i < n; ++i) {
        vals[i] = std::sin(i * 1.7) + std::sin(i * 0.31);
    }
    ax.hist(vals.data(), n);
    return fig;
}

vpl::Figure make_hist_density()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    constexpr int n = 400;
    std::vector<double> vals(n);
    for (int i = 0; i < n; ++i) {
        vals[i] = std::sin(i * 1.7) + std::sin(i * 0.31);
    }
    ax.hist(vals.data(), n, 10, /*density=*/true);
    return fig;
}

/* Mirrors case_errorbar_full in tools/gen_references.py. The general form is
   reached through the C ABI, since its fmt/ecolor handling lives there. */
vpl::Figure make_errorbar_full()
{
    std::vector<double> x, y, lower, upper, sym;
    for (int i = 0; i < 12; ++i) {
        x.push_back(i * 0.5);
        y.push_back(std::sin(i * 0.5) + 2.0);
        lower.push_back(0.1 + 0.05 * i);
        upper.push_back(0.3 * std::fabs(std::cos(static_cast<double>(i))));
        sym.push_back(0.15);
    }
    const std::size_t n = x.size();

    vpl::Figure fig;
    VplAxes *ax1 = reinterpret_cast<VplAxes *>(&fig.add_subplot(2, 2, 1));
    VplAxes *ax2 = reinterpret_cast<VplAxes *>(&fig.add_subplot(2, 2, 2));
    VplAxes *ax3 = reinterpret_cast<VplAxes *>(&fig.add_subplot(2, 2, 3));
    VplAxes *ax4 = reinterpret_cast<VplAxes *>(&fig.add_subplot(2, 2, 4));

    VplLineDesc ld{};
    ld.structSize = sizeof(ld);
    ld.set = VplLineField_Format;

    VplErrorBarDesc eb{};
    eb.structSize = sizeof(eb);
    eb.eColorSpec = "tab:red";
    eb.elinewidth = 1.0;
    eb.capsize = 4.0;
    eb.capthick = 2.0;
    ld.format = "o";
    vplAxesErrorBarFull(ax1, x.data(), y.data(), n, lower.data(), upper.data(), nullptr,
                        nullptr, &eb, &ld, nullptr);

    eb = VplErrorBarDesc{};
    eb.structSize = sizeof(eb);
    eb.errorEveryOffset = 1;
    eb.errorEveryStep = 3;
    eb.capsize = 3.0;
    ld.format = "s--";
    vplAxesErrorBarFull(ax2, x.data(), y.data(), n, sym.data(), nullptr, upper.data(),
                        lower.data(), &eb, &ld, nullptr);

    std::vector<double> shifted;
    for (double v : y) {
        shifted.push_back(v - 1.0);
    }
    vplAxesPlot(ax3, x.data(), shifted.data(), n, nullptr, nullptr);
    eb = VplErrorBarDesc{};
    eb.structSize = sizeof(eb);
    eb.errorEveryStep = 2;
    eb.capsize = 2.0;
    ld.format = "none";
    vplAxesErrorBarFull(ax3, x.data(), y.data(), n, lower.data(), upper.data(), nullptr,
                        nullptr, &eb, &ld, nullptr);

    eb = VplErrorBarDesc{};
    eb.structSize = sizeof(eb);
    eb.eColorSpec = "k";
    eb.barsAbove = 1;
    ld.format = "o-";
    ld.set = VplLineField_Format | VplLineField_Width;
    ld.width = 3.0;
    vplAxesErrorBarFull(ax4, x.data(), y.data(), n, lower.data(), upper.data(), nullptr,
                        nullptr, &eb, &ld, nullptr);
    return fig;
}

/* Mirrors case_tri_explicit (and _tri_grid) in tools/gen_references.py. */
vpl::Figure make_tri_explicit()
{
    std::vector<double> xs, ys, zs;
    for (int i = 0; i < 6; ++i) {
        for (int j = 0; j < 5; ++j) {
            const double px = i + 0.13 * std::sin(2.7 * (i + j));
            const double py = j + 0.11 * std::cos(1.9 * (i - j));
            xs.push_back(px);
            ys.push_back(py);
            zs.push_back(std::sin(0.8 * px) * std::cos(0.6 * py));
        }
    }
    std::vector<int32_t> tris;
    std::vector<uint8_t> mask;
    for (int i = 0; i < 5; ++i) {
        for (int j = 0; j < 4; ++j) {
            const int32_t a = i * 5 + j, b = (i + 1) * 5 + j, c = (i + 1) * 5 + j + 1,
                          d = i * 5 + j + 1;
            const uint8_t hole = (i == 2 && (j == 1 || j == 2)) ? 1 : 0;
            tris.insert(tris.end(), {a, b, c, a, c, d});
            mask.insert(mask.end(), {hole, hole});
        }
    }
    const size_t ntri = mask.size();
    std::vector<double> fc;
    for (size_t t = 0; t < ntri; ++t) {
        fc.push_back(std::sin(0.7 * static_cast<double>(t)));
    }
    VplTriangulation tri{};
    tri.structSize = sizeof(tri);
    tri.triangles = tris.data();
    tri.triangleCount = ntri;
    tri.mask = mask.data();

    vpl::Figure fig;
    VplAxes *ax1 = reinterpret_cast<VplAxes *>(&fig.add_subplot(2, 2, 1));
    vplAxesTriplotFull(ax1, xs.data(), ys.data(), xs.size(), &tri, "o--");
    VplAxes *ax2 = reinterpret_cast<VplAxes *>(&fig.add_subplot(2, 2, 2));
    vplAxesTriContourFull(ax2, xs.data(), ys.data(), zs.data(), xs.size(), &tri, nullptr, 6,
                          nullptr, nullptr);
    VplAxes *ax3 = reinterpret_cast<VplAxes *>(&fig.add_subplot(2, 2, 3));
    const double lv[4] = {-0.4, -0.1, 0.2, 0.5};
    VplFieldDesc fd{};
    fd.structSize = sizeof(fd);
    fd.set = VplFieldField_Extend | VplFieldField_Colormap;
    fd.extend = VplContourExtend_Both;
    fd.cmap = VplColormap_Plasma;
    vplAxesTriContourFFull(ax3, xs.data(), ys.data(), zs.data(), xs.size(), &tri, lv, 4, &fd,
                           nullptr);
    VplAxes *ax4 = reinterpret_cast<VplAxes *>(&fig.add_subplot(2, 2, 4));
    VplFieldDesc pd{};
    pd.structSize = sizeof(pd);
    pd.set = VplFieldField_Colormap;
    pd.cmap = VplColormap_Magma;
    vplAxesTriPcolorFull(ax4, xs.data(), ys.data(), xs.size(), &tri, fc.data(), fc.size(), 1,
                         &pd);
    return fig;
}

/* Mirrors case_contour_over_mesh: line contours drawn above a later mesh. */
vpl::Figure make_contour_over_mesh()
{
    std::vector<double> xs, ys, zs;
    for (int i = 0; i < 6; ++i) {
        for (int j = 0; j < 5; ++j) {
            const double px = i + 0.13 * std::sin(2.7 * (i + j));
            const double py = j + 0.11 * std::cos(1.9 * (i - j));
            xs.push_back(px);
            ys.push_back(py);
            zs.push_back(std::sin(0.8 * px) * std::cos(0.6 * py));
        }
    }
    std::vector<int32_t> tris;
    for (int i = 0; i < 5; ++i) {
        for (int j = 0; j < 4; ++j) {
            const int32_t a = i * 5 + j, b = (i + 1) * 5 + j, c = (i + 1) * 5 + j + 1,
                          d = i * 5 + j + 1;
            tris.insert(tris.end(), {a, b, c, a, c, d});
        }
    }
    VplTriangulation tri{};
    tri.structSize = sizeof(tri);
    tri.triangles = tris.data();
    tri.triangleCount = tris.size() / 3;

    vpl::Figure fig;
    VplAxes *ax = reinterpret_cast<VplAxes *>(&fig.add_subplot());
    const double lv[4] = {-0.4, -0.1, 0.2, 0.5};
    VplFieldDesc fd{};
    fd.structSize = sizeof(fd);
    fd.set = VplFieldField_ColorSpec;
    fd.colorSpec = "white";
    vplAxesTriContourFull(ax, xs.data(), ys.data(), zs.data(), xs.size(), &tri, lv, 4, &fd,
                          nullptr);
    vplAxesTriPcolorFull(ax, xs.data(), ys.data(), xs.size(), &tri, zs.data(), zs.size(), 0,
                         nullptr);
    return fig;
}

/* Mirrors case_errorbar_limits in tools/gen_references.py. */
vpl::Figure make_errorbar_limits()
{
    const double x[6] = {0, 1, 2, 3, 4, 5};
    const double y[6] = {1.0, 1.8, 1.4, 2.2, 2.0, 2.6};
    const uint8_t lo[6] = {1, 0, 1, 0, 0, 1};
    const uint8_t up[6] = {0, 1, 1, 0, 0, 0};
    const uint8_t xup[6] = {0, 0, 0, 1, 1, 0};
    const double ye[6] = {0.4, 0.4, 0.4, 0.4, 0.4, 0.4};
    const double xe[6] = {0.3, 0.3, 0.3, 0.3, 0.3, 0.3};

    vpl::Figure fig;
    VplAxes *ax1 = reinterpret_cast<VplAxes *>(&fig.add_subplot(2, 1, 1));
    VplErrorBarDesc eb{};
    eb.structSize = sizeof(eb);
    eb.capsize = 3.0;
    eb.eLineStyle = VplLineStyle_Dashed;
    eb.lolims = lo;
    eb.uplims = up;
    VplLineDesc ld{};
    ld.structSize = sizeof(ld);
    ld.set = VplLineField_Format;
    ld.format = "o";
    vplAxesErrorBarFull(ax1, x, y, 6, ye, nullptr, nullptr, nullptr, &eb, &ld, nullptr);

    VplAxes *ax2 = reinterpret_cast<VplAxes *>(&fig.add_subplot(2, 1, 2));
    VplErrorBarDesc eb2{};
    eb2.structSize = sizeof(eb2);
    eb2.eColorSpec = "tab:red";
    eb2.xlolims = up;
    eb2.xuplims = xup;
    ld.format = "s-";
    vplAxesErrorBarFull(ax2, x, y, 6, nullptr, nullptr, xe, nullptr, &eb2, &ld, nullptr);
    return fig;
}

/* Mirrors case_hexbin_log_marginals in tools/gen_references.py. */
vpl::Figure make_hexbin_log_marginals()
{
    std::vector<double> xs, ys, cs, lx;
    for (int i = 0; i < 400; ++i) {
        const double t = i * 0.37;
        const double r = 0.2 + 0.004 * i;
        xs.push_back(2.0 + std::cos(t) * r * 3.0);
        ys.push_back(2.0 + std::sin(t * 1.3) * r * 2.0);
        cs.push_back(std::sin(i * 0.11));
        lx.push_back(std::pow(10.0, 0.5 * xs.back()));
    }
    vpl::Figure fig;
    VplAxes *ax1 = reinterpret_cast<VplAxes *>(&fig.add_subplot(1, 2, 1));
    VplHexbinDesc d{};
    d.structSize = sizeof(d);
    d.gridsizeX = 12;
    d.xLog = 1;
    d.logBins = 1;
    d.cmap = VplColormap_Magma;
    vplAxesHexbinFull(ax1, lx.data(), ys.data(), lx.size(), &d);

    VplAxes *ax2 = reinterpret_cast<VplAxes *>(&fig.add_subplot(1, 2, 2));
    d = VplHexbinDesc{};
    d.structSize = sizeof(d);
    d.gridsizeX = 10;
    d.C = cs.data();
    d.reduce = VplHexReduce_Sum;
    d.marginals = 1;
    vplAxesHexbinFull(ax2, xs.data(), ys.data(), xs.size(), &d);
    return fig;
}

/* Mirrors case_pie_shadow_donut in tools/gen_references.py. */
vpl::Figure make_pie_shadow_donut()
{
    const double sizes[4] = {30, 20, 45, 5};
    const double explode[4] = {0, 0.1, 0, 0};
    vpl::Figure fig;
    VplAxes *ax1 = reinterpret_cast<VplAxes *>(&fig.add_subplot(1, 2, 1));
    VplPieDesc d{};
    d.structSize = sizeof(d);
    d.shadow = 1;
    d.explode = explode;
    d.startAngle = 90.0;
    vplAxesPieFull(ax1, sizes, 4, &d);

    VplAxes *ax2 = reinterpret_cast<VplAxes *>(&fig.add_subplot(1, 2, 2));
    d = VplPieDesc{};
    d.structSize = sizeof(d);
    d.wedgeWidth = 0.4;
    d.wedgeEdgeColorSpec = "w";
    d.wedgeLineWidth = 2.0;
    vplAxesPieFull(ax2, sizes, 4, &d);
    vplAxesPieLabelFormat(ax2, "{absval:d} ({frac:.0%})", 0.8, 0.0, VplPieLabelAlign_Auto, 0);
    return fig;
}

/* Mirrors case_quiver_units in tools/gen_references.py. */
vpl::Figure make_quiver_units()
{
    std::vector<double> xs, ys, us, vs, ds;
    for (int i = 0; i < 5; ++i) {
        for (int j = 0; j < 4; ++j) {
            xs.push_back(i);
            ys.push_back(j);
            us.push_back(0.6 * std::cos(0.7 * i + 0.4 * j));
            vs.push_back(0.5 * std::sin(0.5 * i - 0.6 * j));
            ds.push_back(25.0 * i - 40.0 * j);
        }
    }
    const std::size_t n = xs.size();
    vpl::Figure fig;
    VplQuiverDesc d{};

    VplAxes *ax1 = reinterpret_cast<VplAxes *>(&fig.add_subplot(2, 2, 1));
    d.structSize = sizeof(d);
    d.set = VplQuiverField_Angles | VplQuiverField_ScaleUnits | VplQuiverField_Scale;
    d.angles = VplQuiverAngles_XY;
    d.scaleUnits = VplQuiverUnits_XY;
    d.scale = 1.0;
    vplAxesQuiver(ax1, xs.data(), ys.data(), us.data(), vs.data(), n, &d);

    VplAxes *ax2 = reinterpret_cast<VplAxes *>(&fig.add_subplot(2, 2, 2));
    d = VplQuiverDesc{};
    d.structSize = sizeof(d);
    d.set = VplQuiverField_Units | VplQuiverField_Width;
    d.units = VplQuiverUnits_Inches;
    d.width = 0.02;
    vplAxesQuiver(ax2, xs.data(), ys.data(), us.data(), vs.data(), n, &d);

    VplAxes *ax3 = reinterpret_cast<VplAxes *>(&fig.add_subplot(2, 2, 3));
    d = VplQuiverDesc{};
    d.structSize = sizeof(d);
    d.set = VplQuiverField_Units | VplQuiverField_Width | VplQuiverField_ScaleUnits |
            VplQuiverField_Scale;
    d.units = VplQuiverUnits_Dots;
    d.width = 2.0;
    d.scaleUnits = VplQuiverUnits_Inches;
    d.scale = 2.0;
    vplAxesQuiver(ax3, xs.data(), ys.data(), us.data(), vs.data(), n, &d);

    VplAxes *ax4 = reinterpret_cast<VplAxes *>(&fig.add_subplot(2, 2, 4));
    d = VplQuiverDesc{};
    d.structSize = sizeof(d);
    d.set = VplQuiverField_Angles | VplQuiverField_ScaleUnits | VplQuiverField_Scale |
            VplQuiverField_Pivot;
    d.angles = VplQuiverAngles_Fixed;
    d.angleDegrees = ds.data();
    d.scaleUnits = VplQuiverUnits_X;
    d.scale = 1.0;
    d.pivot = VplQuiverPivot_Middle;
    vplAxesQuiver(ax4, xs.data(), ys.data(), us.data(), vs.data(), n, &d);
    return fig;
}

/* Mirrors case_barbs_options in tools/gen_references.py. */
vpl::Figure make_barbs_options()
{
    const double mags[12] = {0.0, 4.0, 12.0, 23.0, 35.0, 55.0, 65.0, 80.0, 7.0, 18.0, 45.0, 2.0};
    std::vector<double> xs, ys, us, vs, cs;
    std::vector<uint8_t> flips;
    for (int k = 0; k < 12; ++k) {
        xs.push_back(k % 4);
        ys.push_back(k / 4);
        const double a = 0.6 * k;
        us.push_back(mags[k] * std::cos(a));
        vs.push_back(mags[k] * std::sin(a));
        cs.push_back(mags[k]);
        flips.push_back(k % 2 == 1 ? 1 : 0);
    }
    vpl::Figure fig;
    vpl::Axes &a1 = fig.add_subplot(2, 1, 1);
    VplAxes *ax1 = reinterpret_cast<VplAxes *>(&a1);
    VplBarbsDesc d{};
    d.structSize = sizeof(d);
    d.set = VplBarbsField_C | VplBarbsField_Length | VplBarbsField_Sizes;
    d.C = cs.data();
    d.cmap = VplColormap_Plasma;
    d.length = 6.0;
    d.spacing = 0.2;
    d.height = 0.5;
    d.width = 0.25;
    d.emptyBarb = 0.25;
    vplAxesBarbs(ax1, xs.data(), ys.data(), us.data(), vs.data(), 12, &d);
    a1.set_xlim(-0.5, 3.5);
    a1.set_ylim(-0.5, 2.5);

    vpl::Axes &a2 = fig.add_subplot(2, 1, 2);
    VplAxes *ax2 = reinterpret_cast<VplAxes *>(&a2);
    d = VplBarbsDesc{};
    d.structSize = sizeof(d);
    d.set = VplBarbsField_BarbColor | VplBarbsField_FlagColor | VplBarbsField_Flips |
            VplBarbsField_PivotValue | VplBarbsField_FillEmpty;
    d.barbColorSpec = "tab:blue";
    d.flagColorSpec = "tab:red";
    d.flips = flips.data();
    d.pivotValue = -2.0;
    d.fillEmpty = 1;
    vplAxesBarbs(ax2, xs.data(), ys.data(), us.data(), vs.data(), 12, &d);
    a2.set_xlim(-0.5, 3.5);
    a2.set_ylim(-0.5, 2.5);
    return fig;
}

/* Mirrors case_quiver_colors in tools/gen_references.py. */
vpl::Figure make_quiver_colors()
{
    const int g = 6;
    std::vector<double> xs, ys, us, vs, cs;
    for (int i = 0; i < g; ++i) {
        for (int j = 0; j < g; ++j) {
            const double x = -2.0 + 4.0 * j / (g - 1);
            const double y = -2.0 + 4.0 * i / (g - 1);
            xs.push_back(x);
            ys.push_back(y);
            us.push_back(-y * 0.5);
            vs.push_back(x * 0.5);
            cs.push_back(std::hypot(x, y));
        }
    }
    const std::size_t n = xs.size();
    vpl::Figure fig;
    VplAxes *ax1 = reinterpret_cast<VplAxes *>(&fig.add_subplot(1, 2, 1));
    VplAxes *ax2 = reinterpret_cast<VplAxes *>(&fig.add_subplot(1, 2, 2));

    VplQuiverDesc d{};
    d.structSize = sizeof(d);
    d.set = VplQuiverField_C;
    d.C = cs.data();
    d.cmap = VplColormap_Magma;
    vplAxesQuiver(ax1, xs.data(), ys.data(), us.data(), vs.data(), n, &d);

    static const float palette[3][4] = {{0.839216f, 0.152941f, 0.156863f, 1.0f},
                                        {0.172549f, 0.627451f, 0.172549f, 1.0f},
                                        {0.121569f, 0.466667f, 0.705882f, 1.0f}};
    std::vector<float> colors;
    for (std::size_t k = 0; k < n; ++k) {
        colors.insert(colors.end(), palette[k % 3], palette[k % 3] + 4);
    }
    d = VplQuiverDesc{};
    d.structSize = sizeof(d);
    d.set = VplQuiverField_Colors | VplQuiverField_Pivot;
    d.colors = colors.data();
    d.pivot = VplQuiverPivot_Middle;
    vplAxesQuiver(ax2, xs.data(), ys.data(), us.data(), vs.data(), n, &d);
    return fig;
}

/* Mirrors case_boxplot_props in tools/gen_references.py, through the C ABI. */
vpl::Figure make_boxplot_props()
{
    std::vector<double> all;
    std::vector<std::size_t> counts;
    for (int i = 0; i < 60; ++i) {
        all.push_back(std::sin(i * 0.7) * 2.0 + std::cos(i * 0.13));
    }
    all.push_back(6.5);
    all.push_back(-5.0);
    counts.push_back(62);
    for (int i = 0; i < 45; ++i) {
        const double cs = std::cos(i * 0.31);
        all.push_back(cs * cs * cs * 3.0 + 1.0);
    }
    counts.push_back(45);
    for (int i = 0; i < 70; ++i) {
        all.push_back(0.5 * std::sin(i * 1.9) + 0.03 * i);
    }
    all.push_back(4.0);
    counts.push_back(71);
    for (int i = 0; i < 12; ++i) {
        all.push_back(2.0);
    }
    all.push_back(1.0);
    all.push_back(3.5);
    all.push_back(0.5);
    counts.push_back(15);

    const double nan = std::numeric_limits<double>::quiet_NaN();
    vpl::Figure fig;
    vpl::Axes &a1 = fig.add_subplot(1, 2, 1);
    {
        const double medians[4] = {nan, 0.5, nan, nan};
        const double cis[8] = {nan, nan, 0.2, 0.9, nan, 0.8, nan, nan};
        const double capw[4] = {0.1, 0.2, 0.3, 0.4};
        VplBoxProps mean{}, median{}, whisker{}, cap{}, box{}, flier{};
        mean.structSize = median.structSize = whisker.structSize = sizeof(VplBoxProps);
        cap.structSize = box.structSize = flier.structSize = sizeof(VplBoxProps);
        mean.set = VplBoxProps_Color | VplBoxProps_LineWidth;
        mean.colorSpec = "purple";
        mean.lineWidth = 1.5;
        median.set = VplBoxProps_Color | VplBoxProps_LineWidth;
        median.colorSpec = "k";
        median.lineWidth = 2.0;
        whisker.set = VplBoxProps_LineStyle | VplBoxProps_Color;
        whisker.lineStyle = VplLineStyle_Dashed;
        whisker.colorSpec = "0.4";
        cap.set = VplBoxProps_Color | VplBoxProps_LineWidth;
        cap.colorSpec = "tab:red";
        cap.lineWidth = 2.0;
        box.set = VplBoxProps_Color;
        box.colorSpec = "tab:blue";
        flier.set = VplBoxProps_Marker | VplBoxProps_MarkerFaceColor | VplBoxProps_MarkerSize |
                    VplBoxProps_MarkerEdgeColor;
        flier.marker = VplMarker_Diamond;
        flier.markerFaceColorSpec = "tab:green";
        flier.markerSize = 5.0;
        flier.markerEdgeColorSpec = "none";
        VplBoxDesc d{};
        d.structSize = sizeof(d);
        d.showNotches = 1;
        d.userMedians = medians;
        d.confIntervals = cis;
        d.capWidths = capw;
        d.autorange = 1;
        d.showMeans = 1;
        d.meanLine = 1;
        d.meanProps = &mean;
        d.medianProps = &median;
        d.whiskerProps = &whisker;
        d.capProps = &cap;
        d.boxProps = &box;
        d.flierProps = &flier;
        vplAxesBoxPlotFull(reinterpret_cast<VplAxes *>(&a1), all.data(), counts.data(), 4, &d);
    }
    vpl::Axes &a2 = fig.add_subplot(1, 2, 2);
    {
        VplBoxProps box{}, mean{};
        box.structSize = mean.structSize = sizeof(VplBoxProps);
        box.set = VplBoxProps_FaceColor | VplBoxProps_Color | VplBoxProps_LineWidth;
        box.faceColorSpec = "#add8e6";
        box.colorSpec = "navy";
        box.lineWidth = 1.5;
        mean.set = VplBoxProps_Marker | VplBoxProps_MarkerFaceColor | VplBoxProps_MarkerEdgeColor;
        mean.marker = VplMarker_Circle;
        mean.markerFaceColorSpec = "white";
        mean.markerEdgeColorSpec = "k";
        const char *labels[4] = {"A", "B", "C", "D"};
        VplBoxDesc d{};
        d.structSize = sizeof(d);
        d.horizontal = 1;
        d.patchArtist = 1;
        d.boxProps = &box;
        d.sym = "r+";
        d.showMeans = 1;
        d.meanProps = &mean;
        d.legendLabels = labels;
        vplAxesBoxPlotFull(reinterpret_cast<VplAxes *>(&a2), all.data(), counts.data(), 4, &d);
    }
    a2.legend_visible = true;
    a2.legend_loc = vpl::LegendLoc::LowerRight;
    return fig;
}

/* Mirrors case_boxplot_full in tools/gen_references.py. */
vpl::Figure make_boxplot_full()
{
    std::vector<double> a, b, c;
    for (int i = 0; i < 60; ++i) {
        a.push_back(std::sin(i * 0.7) * 2.0 + std::cos(i * 0.13));
    }
    a.push_back(6.5);
    a.push_back(-5.0);
    for (int i = 0; i < 45; ++i) {
        const double cs = std::cos(i * 0.31);
        b.push_back(cs * cs * cs * 3.0 + 1.0);
    }
    for (int i = 0; i < 70; ++i) {
        c.push_back(0.5 * std::sin(i * 1.9) + 0.03 * i);
    }
    c.push_back(4.0);
    std::vector<double> all = a;
    all.insert(all.end(), b.begin(), b.end());
    all.insert(all.end(), c.begin(), c.end());
    const std::size_t counts[] = {a.size(), b.size(), c.size()};

    vpl::Figure fig;
    {
        const double positions[] = {0.5, 2.0, 3.0};
        const double widths[] = {0.3, 0.6, 0.4};
        vpl::BoxOptions o;
        o.positions = positions;
        o.widths = widths;
        o.shownotches = true;
        o.showmeans = true;
        fig.add_subplot(2, 2, 1).boxplot(all.data(), counts, 3, o);
    }
    {
        const std::string labels[] = {"alpha", "beta", "gamma"};
        vpl::BoxOptions o;
        o.horizontal = true;
        o.patch_artist = true;
        o.labels = labels;
        fig.add_subplot(2, 2, 2).boxplot(all.data(), counts, 3, o);
    }
    {
        vpl::BoxOptions o;
        o.showmeans = true;
        o.meanline = true;
        o.showcaps = false;
        const double pct[] = {5.0, 95.0};
        fig.add_subplot(2, 2, 3).boxplot(all.data(), counts, 3, o, 1.5, pct);
    }
    {
        vpl::BoxOptions o;
        o.showfliers = false;
        o.manage_ticks = false;
        fig.add_subplot(2, 2, 4).boxplot(all.data(), counts, 3, o, 0.5);
    }
    return fig;
}

/* Mirrors case_xcorr_full in tools/gen_references.py. */
vpl::Figure make_xcorr_full()
{
    std::vector<double> a, b;
    for (int i = 0; i < 60; ++i) {
        a.push_back(0.05 * i + std::sin(i * 0.6));
        b.push_back(2.0 + std::cos(i * 0.6 + 0.4));
    }
    vpl::Figure fig;
    fig.add_subplot(2, 1, 1).acorr(a.data(), a.size(), 20, true, vpl::SpectralDetrend::Linear,
                                   /*usevlines=*/false);
    fig.add_subplot(2, 1, 2).xcorr(a.data(), b.data(), 30, 29, true, vpl::SpectralDetrend::Mean);
    return fig;
}

/* Mirrors case_violin_full in tools/gen_references.py. */
vpl::Figure make_violin_full()
{
    std::vector<double> a, b, c;
    for (int i = 0; i < 120; ++i) {
        a.push_back(std::sin(i * 0.7) * 2.0 + std::cos(i * 0.13));
    }
    for (int i = 0; i < 90; ++i) {
        const double cs = std::cos(i * 0.31);
        b.push_back(cs * cs * cs * 3.0 + 1.0);
    }
    for (int i = 0; i < 150; ++i) {
        c.push_back(0.5 * std::sin(i * 1.9) + 0.02 * i);
    }
    vpl::Figure fig;
    {
        std::vector<double> all = a;
        all.insert(all.end(), b.begin(), b.end());
        all.insert(all.end(), c.begin(), c.end());
        const std::size_t counts[] = {a.size(), b.size(), c.size()};
        const double positions[] = {0.5, 2.0, 3.0};
        const double widths[] = {0.4, 0.9, 0.6};
        const double quantiles[] = {0.25, 0.75, 0.1, 0.5, 0.9};
        const std::size_t qcounts[] = {2, 0, 3};
        vpl::ViolinOptions o;
        o.positions = positions;
        o.widths = widths;
        o.showmeans = true;
        o.showmedians = true;
        o.quantiles = quantiles;
        o.quantile_counts = qcounts;
        o.bandwidth = vpl::ViolinOptions::Bandwidth::Silverman;
        fig.add_subplot(1, 2, 1).violinplot(all.data(), counts, 3, o);
    }
    {
        std::vector<double> all = a;
        all.insert(all.end(), c.begin(), c.end());
        const std::size_t counts[] = {a.size(), c.size()};
        vpl::ViolinOptions o;
        o.horizontal = true;
        o.side = vpl::ViolinOptions::Side::High;
        o.bandwidth = vpl::ViolinOptions::Bandwidth::Scalar;
        o.bw_factor = 0.3;
        o.points = 40;
        o.facecolors = {vpl::Color::rgb(0.172549, 0.627451, 0.172549)}; /* tab:green */
        o.linecolors = {vpl::Color::rgb(0.0, 0.0, 0.0)};
        fig.add_subplot(1, 2, 2).violinplot(all.data(), counts, 2, o);
    }
    return fig;
}

/* Mirrors case_hexbin_full in tools/gen_references.py. */
vpl::Figure make_hexbin_full()
{
    std::vector<double> xs, ys, cs;
    for (int i = 0; i < 400; ++i) {
        const double t = i * 0.37;
        const double r = 0.2 + 0.004 * i;
        xs.push_back(2.0 + std::cos(t) * r * 3.0);
        ys.push_back(2.0 + std::sin(t * 1.3) * r * 2.0);
        cs.push_back(std::sin(i * 0.11));
    }
    const std::size_t n = xs.size();
    vpl::Figure fig;
    {
        vpl::HexbinOptions o;
        o.nx = 15;
        o.C = cs.data();
        o.mincnt = 2;
        fig.add_subplot(2, 2, 1).hexbin(xs.data(), ys.data(), n, o);
    }
    {
        vpl::HexbinOptions o;
        o.nx = 20;
        o.log_bins = true;
        o.cmap = vpl::Colormap::Magma;
        fig.add_subplot(2, 2, 2).hexbin(xs.data(), ys.data(), n, o);
    }
    {
        vpl::HexbinOptions o;
        o.nx = 12;
        o.ny = 5;
        o.C = cs.data();
        o.reduce = vpl::HexReduce::Sum;
        o.has_extent = true;
        o.extent[0] = 0.0;
        o.extent[1] = 4.0;
        o.extent[2] = 0.0;
        o.extent[3] = 4.0;
        o.cmap = vpl::Colormap::Gray;
        fig.add_subplot(2, 2, 3).hexbin(xs.data(), ys.data(), n, o);
    }
    {
        vpl::HexbinOptions o;
        o.nx = 10;
        o.bins = 4;
        o.has_vlimits = true;
        o.vmin = 0.0;
        o.vmax = 3.0;
        fig.add_subplot(2, 2, 4).hexbin(xs.data(), ys.data(), n, o);
    }
    return fig;
}

/* Mirrors case_contour_auto in tools/gen_references.py, through the C ABI
   where the automatic levels and extend are resolved. */
vpl::Figure make_contour_auto()
{
    const std::size_t rows = 30, cols = 40;
    std::vector<double> z;
    for (std::size_t r = 0; r < rows; ++r) {
        for (std::size_t c = 0; c < cols; ++c) {
            const double dc = static_cast<double>(c) - 25.0;
            const double dr = static_cast<double>(r) - 10.0;
            z.push_back(std::sin(static_cast<double>(c) * 0.15) *
                            std::cos(static_cast<double>(r) * 0.2) +
                        0.6 * std::exp(-(dc * dc + dr * dr) / 60.0));
        }
    }
    vpl::Figure fig;
    VplAxes *ax[4];
    for (unsigned k = 0; k < 4; ++k) {
        ax[k] = reinterpret_cast<VplAxes *>(&fig.add_subplot(2, 2, k + 1));
    }
    vplAxesContour(ax[0], z.data(), rows, cols, nullptr, 0, nullptr, nullptr);
    vplAxesContourF(ax[1], z.data(), rows, cols, nullptr, 5, nullptr, nullptr);

    VplFieldDesc d{};
    d.structSize = sizeof(d);
    d.set = VplFieldField_Extend;
    d.extend = VplContourExtend_Both;
    const double lv[4] = {-0.5, 0.0, 0.5, 1.0};
    vplAxesContourF(ax[2], z.data(), rows, cols, lv, 4, &d, nullptr);

    d.set = VplFieldField_Extend | VplFieldField_Colormap;
    d.extend = VplContourExtend_Min;
    d.cmap = VplColormap_Magma;
    vplAxesContourF(ax[3], z.data(), rows, cols, nullptr, 6, &d, nullptr);
    return fig;
}

/* Mirrors case_pie_full in tools/gen_references.py, through the C ABI where
   the labels and autopct are assembled. */
vpl::Figure make_pie_full()
{
    vpl::Figure fig;
    VplAxes *ax1 = reinterpret_cast<VplAxes *>(&fig.add_subplot(1, 2, 1));
    VplAxes *ax2 = reinterpret_cast<VplAxes *>(&fig.add_subplot(1, 2, 2));

    const double sizes[4] = {15, 30, 45, 10};
    const char *const labels[4] = {"frogs", "hogs", "dogs", "logs"};
    const double explode[4] = {0, 0.1, 0, 0};
    VplPieDesc d{};
    d.structSize = sizeof(d);
    d.labels = labels;
    d.autopct = "%1.1f%%";
    d.explode = explode;
    d.startAngle = 90.0;
    vplAxesPieFull(ax1, sizes, 4, &d);

    const double fracs[3] = {0.3, 0.25, 0.25};
    const char *const labels2[3] = {"a", "bb", "ccc"};
    const char *const colors[3] = {"tab:red", "0.6", "#2ca02c"};
    d = VplPieDesc{};
    d.structSize = sizeof(d);
    d.labels = labels2;
    d.clockwise = 1;
    d.noNormalize = 1;
    d.radius = 0.9;
    d.colorSpecs = colors;
    d.colorCount = 3;
    d.rotateLabels = 1;
    d.labelDistance = 1.05;
    d.autopct = "%.0f";
    d.pctDistance = 0.7;
    vplAxesPieFull(ax2, fracs, 3, &d);
    return fig;
}

/* Mirrors _hist_datasets in tools/gen_references.py. */
struct HistData
{
    std::vector<double> a, b, c;
    HistData()
    {
        for (int i = 0; i < 300; ++i) {
            a.push_back(std::sin(i * 1.7) + std::sin(i * 0.31));
        }
        for (int i = 0; i < 250; ++i) {
            b.push_back(0.8 * std::cos(i * 0.53) + 0.4 * std::sin(i * 2.9) - 0.2);
        }
        for (int i = 0; i < 200; ++i) {
            c.push_back(1.3 * std::sin(i * 0.77));
        }
    }
};

vpl::Figure make_hist_options()
{
    const HistData d;
    vpl::Figure fig;

    {
        vpl::Axes &ax = fig.add_subplot(2, 2, 1);
        std::vector<double> w;
        for (std::size_t i = 0; i < d.a.size(); ++i) {
            const double cs = std::cos(static_cast<double>(i) * 0.9);
            w.push_back(0.5 + 0.5 * cs * cs);
        }
        const double edges[] = {-2.0, -1.2, -0.5, 0.0, 0.3, 1.0, 2.2};
        vpl::HistOptions o;
        o.edges = edges;
        o.edge_count = 7;
        o.rwidth = 0.7;
        o.align = vpl::HistAlign::Left;
        const double *data[] = {d.a.data()};
        const std::size_t counts[] = {d.a.size()};
        const double *weights[] = {w.data()};
        ax.hist(data, counts, weights, 1, o);
    }
    {
        vpl::Axes &ax = fig.add_subplot(2, 2, 2);
        vpl::HistOptions o;
        o.bins = 8;
        const double *data[] = {d.a.data(), d.b.data(), d.c.data()};
        const std::size_t counts[] = {d.a.size(), d.b.size(), d.c.size()};
        ax.hist(data, counts, nullptr, 3, o);
    }
    {
        vpl::Axes &ax = fig.add_subplot(2, 2, 3);
        vpl::HistOptions o;
        o.bins = 12;
        o.has_range = true;
        o.range_lo = -1.5;
        o.range_hi = 1.5;
        o.density = true;
        o.type = vpl::HistType::BarStacked;
        const double *data[] = {d.b.data(), d.c.data()};
        const std::size_t counts[] = {d.b.size(), d.c.size()};
        ax.hist(data, counts, nullptr, 2, o);
    }
    {
        vpl::Axes &ax = fig.add_subplot(2, 2, 4);
        vpl::HistOptions filled;
        filled.type = vpl::HistType::StepFilled;
        filled.cumulative = -1;
        const double *one[] = {d.c.data()};
        const std::size_t one_count[] = {d.c.size()};
        ax.hist(one, one_count, nullptr, 1, filled);

        vpl::HistOptions step;
        step.type = vpl::HistType::Step;
        step.stacked = true;
        const double *two[] = {d.a.data(), d.b.data()};
        const std::size_t two_count[] = {d.a.size(), d.b.size()};
        ax.hist(two, two_count, nullptr, 2, step);
    }
    return fig;
}

vpl::Figure make_hist_horizontal()
{
    const HistData d;
    vpl::Figure fig;
    {
        vpl::Axes &ax = fig.add_subplot(1, 2, 1);
        vpl::HistOptions o;
        o.bins = 9;
        o.horizontal = true;
        o.bottom = 3.0;
        o.rwidth = 0.5;
        const double *data[] = {d.a.data()};
        const std::size_t counts[] = {d.a.size()};
        ax.hist(data, counts, nullptr, 1, o);
    }
    {
        vpl::Axes &ax = fig.add_subplot(1, 2, 2);
        vpl::HistOptions o;
        o.bins = 7;
        o.horizontal = true;
        o.type = vpl::HistType::StepFilled;
        o.cumulative = 1;
        o.density = true;
        o.align = vpl::HistAlign::Right;
        const double *data[] = {d.b.data()};
        const std::size_t counts[] = {d.b.size()};
        ax.hist(data, counts, nullptr, 1, o);
    }
    return fig;
}

vpl::Figure make_hist_log()
{
    std::vector<double> tail;
    for (int i = 0; i < 300; ++i) {
        tail.push_back(std::exp(2.0 * std::sin(i * 0.37) + 1.5 * std::cos(i * 1.3)));
    }
    vpl::Figure fig;
    {
        vpl::Axes &ax = fig.add_subplot(1, 2, 1);
        vpl::HistOptions o;
        o.bins = 25;
        o.log = true;
        const double *data[] = {tail.data()};
        const std::size_t counts[] = {tail.size()};
        ax.hist(data, counts, nullptr, 1, o);
    }
    {
        vpl::Axes &ax = fig.add_subplot(1, 2, 2);
        vpl::HistOptions o;
        o.bins = 15;
        o.type = vpl::HistType::Step;
        o.log = true;
        o.horizontal = true;
        const double *data[] = {tail.data()};
        const std::size_t counts[] = {tail.size()};
        ax.hist(data, counts, nullptr, 1, o);
    }
    return fig;
}

vpl::Figure make_fixed_ticks()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    constexpr int n = 61;
    std::vector<double> xs(n), ys(n);
    for (int i = 0; i < n; ++i) {
        xs[i] = i * 6.0 / (n - 1);
        ys[i] = std::sin(xs[i]);
    }
    ax.plot(xs.data(), ys.data(), n);

    ax.xticks_fixed = {0.0, 1.5707963267948966, 3.141592653589793, 4.71238898038469,
                       6.283185307179586};
    ax.xticklabels_fixed = {"0", "pi/2", "pi", "3pi/2", "2pi"};
    ax.xticks_fixed_set = true;
    ax.yticks_fixed = {-1.0, 0.0, 1.0};
    ax.yticks_fixed_set = true;
    return fig;
}

vpl::Figure make_spines()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    constexpr int n = 61;
    std::vector<double> xs(n), ys(n);
    for (int i = 0; i < n; ++i) {
        xs[i] = i * 6.0 / (n - 1);
        ys[i] = std::sin(xs[i]);
    }
    ax.plot(xs.data(), ys.data(), n);
    ax.spine_top = false;
    ax.spine_right = false;
    return fig;
}

vpl::Figure make_multiline()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    constexpr int n = 61;
    std::vector<double> xs(n), ys(n);
    for (int i = 0; i < n; ++i) {
        xs[i] = i * 6.0 / (n - 1);
        ys[i] = std::sin(xs[i]);
    }
    ax.plot(xs.data(), ys.data(), n);
    ax.title = "two lines\nin a title";

    ax.text(0.5, 0.5, "left\naligned lines");

    vpl::TextArtist &centred = ax.text(4.0, -0.4, "centred\nlines here");
    centred.halign = vpl::HAlign::Center;
    return fig;
}

vpl::Figure make_barh()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    constexpr int n = 7;
    std::vector<double> ys(n), ws(n);
    for (int i = 0; i < n; ++i) {
        ys[i] = static_cast<double>(i);
        ws[i] = 1.0 + 0.5 * i;
    }
    ax.barh(ys.data(), ws.data(), n, 0.8);
    return fig;
}

vpl::Figure make_spans()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    constexpr int n = 61;
    std::vector<double> xs(n), ys(n);
    for (int i = 0; i < n; ++i) {
        xs[i] = i * 6.0 / (n - 1);
        ys[i] = std::sin(xs[i]);
    }
    ax.plot(xs.data(), ys.data(), n);
    ax.axhspan(0.25, 0.75);
    ax.axvspan(1.0, 2.0);
    return fig;
}

vpl::Figure make_step()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    constexpr int n = 15;
    std::vector<double> xs(n), ys(n);
    for (int i = 0; i < n; ++i) {
        xs[i] = i * 6.0 / (n - 1);
        ys[i] = std::sin(xs[i]);
    }
    ax.step(xs.data(), ys.data(), n);
    return fig;
}

vpl::Figure make_fill_betweenx()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    constexpr int n = 61;
    std::vector<double> ys(n), x1(n), x2(n);
    for (int i = 0; i < n; ++i) {
        ys[i] = i * 6.0 / (n - 1);
        x1[i] = std::sin(ys[i]) - 0.3;
        x2[i] = std::sin(ys[i]) + 0.3;
    }
    ax.fill_betweenx(ys.data(), x1.data(), x2.data(), n);
    return fig;
}

vpl::Figure make_twiny()
{
    vpl::Figure fig;
    vpl::Axes &ax1 = fig.add_subplot();

    constexpr int n = 61;
    std::vector<double> xs(n), ys(n), xs2(n);
    for (int i = 0; i < n; ++i) {
        xs[i] = i * 6.0 / (n - 1);
        ys[i] = std::sin(xs[i]);
        xs2[i] = 100.0 * xs[i];
    }

    vpl::Line2D &l1 = ax1.plot(xs.data(), ys.data(), n);
    vpl::parse_color("C0", l1.color);

    vpl::Axes &ax2 = fig.twiny(ax1);
    vpl::Line2D &l2 = ax2.plot(xs2.data(), ys.data(), n);
    vpl::parse_color("C1", l2.color);

    return fig;
}

vpl::Figure make_legend_locs()
{
    vpl::Figure fig;

    constexpr int n = 61;
    std::vector<double> xs(n), ys(n);
    for (int i = 0; i < n; ++i) {
        xs[i] = i * 6.0 / (n - 1);
        ys[i] = std::sin(xs[i]);
    }

    const vpl::LegendLoc locs[4] = {vpl::LegendLoc::UpperCenter, vpl::LegendLoc::CenterLeft,
                                    vpl::LegendLoc::LowerCenter, vpl::LegendLoc::Center};
    for (int k = 0; k < 4; ++k) {
        vpl::Axes &ax = fig.add_subplot(2, 2, static_cast<unsigned>(k + 1));
        vpl::Line2D &line = ax.plot(xs.data(), ys.data(), n);
        line.label = "sin";
        ax.legend_visible = true;
        ax.legend_loc = locs[k];
    }
    return fig;
}

vpl::Figure make_set_bound()
{
    /* Left panel set_xbound(NaN, 5) clips only the upper end; right panel
       set_ybound(-0.5, NaN) is the mirror, with NaN in the other position. */
    vpl::Figure fig;

    constexpr int n = 61;
    std::vector<double> xs(n), ys(n);
    for (int i = 0; i < n; ++i) {
        xs[i] = i * 8.0 / (n - 1);
        ys[i] = std::sin(xs[i]);
    }

    vpl::Axes &ax1 = fig.add_subplot(1, 2, 1);
    ax1.plot(xs.data(), ys.data(), n);
    ax1.set_xbound(std::nan(""), 5.0);

    vpl::Axes &ax2 = fig.add_subplot(1, 2, 2);
    ax2.plot(xs.data(), ys.data(), n);
    ax2.set_ybound(-0.5, std::nan(""));

    return fig;
}

vpl::Figure make_legend_ncols()
{
    /* np.array_split gives the first columns the extra entry (5 into 2 is 3
       then 2). Columns differ in width, exercising per-column sizing rather
       than a single legend-wide maximum. */
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    constexpr int n = 41;
    std::vector<double> xs(n);
    for (int i = 0; i < n; ++i) {
        xs[i] = i * 4.0 / (n - 1);
    }

    const char *labels[5] = {"sin", "a very long cosine label", "x", "y squared", "z"};
    for (int k = 0; k < 5; ++k) {
        std::vector<double> ys(n);
        for (int i = 0; i < n; ++i) {
            ys[i] = std::sin(xs[i] + k);
        }
        ax.plot(xs.data(), ys.data(), n).label = labels[k];
    }
    ax.legend_visible = true;
    ax.legend_ncols = 2;

    return fig;
}

vpl::Figure make_legend_title()
{
    /* Left panel's title is narrower than its widest entry (entries decide the
       box width); right panel's title is wider (the title decides it). */
    vpl::Figure fig;

    constexpr int n = 61;
    std::vector<double> xs(n), ys(n), zs(n), ys2(n), zs2(n);
    for (int i = 0; i < n; ++i) {
        xs[i] = i * 6.0 / (n - 1);
        ys[i] = std::sin(xs[i]);
        zs[i] = std::cos(xs[i]);
        /* Scaled, not identical to ys/zs: identical y-ranges would give
           tight_layout the same margin on both panels and land the shared
           spine on a sub-ULP tie between adjacent pixel columns. */
        ys2[i] = 1.4 * ys[i];
        zs2[i] = 1.4 * zs[i];
    }

    vpl::Axes &ax1 = fig.add_subplot(1, 2, 1);
    ax1.plot(xs.data(), ys.data(), n).label = "sin";
    ax1.plot(xs.data(), zs.data(), n).label = "cos";
    ax1.legend_visible = true;
    ax1.legend_title = "Signals";

    vpl::Axes &ax2 = fig.add_subplot(1, 2, 2);
    ax2.plot(xs.data(), ys2.data(), n).label = "a";
    ax2.plot(xs.data(), zs2.data(), n).label = "b";
    ax2.legend_visible = true;
    ax2.legend_title = "A rather longer legend title";

    return fig;
}

vpl::Figure make_tick_params()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    constexpr int n = 61;
    std::vector<double> xs(n), ys(n);
    for (int i = 0; i < n; ++i) {
        xs[i] = i * 6.0 / (n - 1);
        ys[i] = std::sin(xs[i]);
    }
    ax.plot(xs.data(), ys.data(), n);

    ax.xtick.direction = vpl::TickDirection::In;
    ax.xtick.size = 6.0;
    ax.xtick.width = 1.6;
    ax.xtick.label_size = 8.0;

    ax.ytick.direction = vpl::TickDirection::InOut;
    ax.ytick.size = 8.0;
    ax.ytick.width = 0.8;
    ax.ytick.label_size = 13.0;

    return fig;
}

vpl::Figure make_tick_colors()
{
    /* Three independent colours in one panel: tick mark colour (xtick.color),
       label colour (xtick.labelcolor), and the spine edgecolor, which stays
       black. */
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    constexpr int n = 61;
    std::vector<double> xs(n), ys(n);
    for (int i = 0; i < n; ++i) {
        xs[i] = i * 6.0 / (n - 1);
        ys[i] = std::sin(xs[i]);
    }
    ax.plot(xs.data(), ys.data(), n);

    vpl::parse_color("tab:red", ax.xtick.color);
    vpl::parse_color("tab:green", ax.xtick.label_color);
    vpl::parse_color("tab:blue", ax.ytick.color);
    ax.ytick.label_color = ax.ytick.color;

    return fig;
}

vpl::Figure make_bar_stacked()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    constexpr int n = 7;
    std::vector<double> xs(n), lower(n), upper(n);
    for (int i = 0; i < n; ++i) {
        xs[i] = static_cast<double>(i);
        lower[i] = 1.0 + 0.5 * i;
        upper[i] = 2.0 - 0.1 * i;
    }

    ax.bar(xs.data(), lower.data(), n, 0.8);
    ax.bar(xs.data(), upper.data(), n, 0.8, lower.data());
    return fig;
}

vpl::Figure make_invert_axes()
{
    vpl::Figure fig;

    constexpr int n = 61;
    std::vector<double> xs(n), ys(n);
    for (int i = 0; i < n; ++i) {
        xs[i] = i * 6.0 / (n - 1);
        ys[i] = std::sin(xs[i]);
    }

    vpl::Axes &a = fig.add_subplot(2, 2, 1); /* inverted, autoscaled */
    a.plot(xs.data(), ys.data(), n);
    a.y_inverted = true;

    vpl::Axes &b = fig.add_subplot(2, 2, 2); /* inverted by the argument order */
    b.plot(xs.data(), ys.data(), n);
    b.set_ylim(1.5, -1.5);

    vpl::Axes &c = fig.add_subplot(2, 2, 3); /* the x axis this time */
    c.plot(xs.data(), ys.data(), n);
    c.x_inverted = true;

    vpl::Axes &d = fig.add_subplot(2, 2, 4); /* inverted, then set_ylim puts it back */
    d.plot(xs.data(), ys.data(), n);
    d.y_inverted = true;
    d.set_ylim(-1.5, 1.5);

    return fig;
}

vpl::Figure make_hlines_vlines()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    const double hy[3] = {1.0, 2.0, 3.0};
    const double hx0[3] = {0.0, 0.5, 1.0};
    const double hx1[3] = {3.0, 2.5, 2.0};
    ax.hlines(hy, hx0, hx1, 3);

    const double vx[3] = {0.5, 1.5, 2.5};
    const double vy0[3] = {0.5, 0.0, 1.0};
    const double vy1[3] = {3.5, 3.0, 4.0};
    ax.vlines(vx, vy0, vy1, 3);

    return fig;
}

vpl::Figure make_eventplot()
{
    vpl::Figure fig;

    const double a[7] = {0.2, 0.5, 0.9, 1.4, 2.2, 2.25, 3.1};
    const double b[6] = {0.1, 0.7, 1.1, 1.15, 2.6, 3.4};

    vpl::Axes &top = fig.add_subplot(2, 1, 1);
    top.eventplot(a, 7, 1.0, 1.0, /*horizontal=*/true);

    vpl::Axes &bottom = fig.add_subplot(2, 1, 2);
    bottom.eventplot(b, 6, 1.0, 1.0, /*horizontal=*/false);

    return fig;
}

vpl::Figure make_stem()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    constexpr int n = 13;
    std::vector<double> xs(n), ys(n);
    for (int i = 0; i < n; ++i) {
        xs[i] = i * 0.5;
        ys[i] = std::sin(xs[i]) * std::exp(-xs[i] / 4.0);
    }
    ax.stem(xs.data(), ys.data(), n);
    return fig;
}

vpl::Figure make_stairs()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    const double values[6] = {1.0, 2.5, 2.0, 3.5, 3.0, 1.5};
    const double edges[7] = {0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    ax.stairs(values, edges, 6);
    return fig;
}

vpl::Figure make_fill_poly()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    constexpr int n = 24;
    std::vector<double> xs(n), ys(n);
    for (int i = 0; i < n; ++i) {
        const double t = 2.0 * 3.14159265358979323846 * i / n;
        xs[i] = 10.0 + std::cos(t);
        ys[i] = 20.0 + 0.5 * std::sin(t);
    }
    ax.fill(xs.data(), ys.data(), n);
    return fig;
}

vpl::Figure make_broken_barh()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    const double s1[3] = {0.0, 2.0, 3.5};
    const double w1[3] = {1.5, 0.75, 2.0};
    ax.broken_barh(s1, w1, 3, 10.0, 4.0);

    const double s2[2] = {0.5, 3.0};
    const double w2[2] = {1.0, 2.5};
    ax.broken_barh(s2, w2, 2, 16.0, 4.0);

    return fig;
}

static void stackplot_series(std::vector<double> &xs, std::vector<double> &ys, int n)
{
    xs.resize(n);
    ys.resize(3 * n);
    for (int i = 0; i < n; ++i) {
        const double x = i * 6.0 / (n - 1);
        xs[i] = x;
        ys[i] = 1.0 + 0.5 * std::sin(x);
        ys[n + i] = 1.5 + 0.5 * std::cos(x);
        ys[2 * n + i] = 0.8 + 0.3 * std::sin(2.0 * x);
    }
}

vpl::Figure make_stackplot()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();
    std::vector<double> xs, ys;
    stackplot_series(xs, ys, 41);
    ax.stackplot(xs.data(), ys.data(), 3, 41);
    return fig;
}

vpl::Figure make_stackplot_sym()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();
    std::vector<double> xs, ys;
    stackplot_series(xs, ys, 41);
    ax.stackplot(xs.data(), ys.data(), 3, 41, vpl::StackBaseline::Sym);
    return fig;
}

vpl::Figure make_stackplot_stream()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();
    std::vector<double> xs, ys;
    stackplot_series(xs, ys, 41);
    ax.stackplot(xs.data(), ys.data(), 3, 41, vpl::StackBaseline::WeightedWiggle);
    return fig;
}

vpl::Figure make_boxplot()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    const double values[] = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 20.0,
                             2.0, 3.0, 4.0, 5.0, 6.0,
                             0.0, 1.0, 2.0, 3.0, 10.0};
    const std::size_t counts[3] = {9, 5, 5};
    ax.boxplot(values, counts, 3);
    return fig;
}

vpl::Figure make_bxp()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    /* Not the summary of any sample set: bxp recomputes nothing, so whiskers
       are drawn exactly where given. */
    std::vector<vpl::BoxStats> stats(3);
    stats[0].q1 = 3.0;
    stats[0].med = 5.0;
    stats[0].q3 = 7.0;
    stats[0].whislo = 1.0;
    stats[0].whishi = 9.0;
    stats[0].fliers = {-1.0, 11.5};

    stats[1].q1 = 3.5;
    stats[1].med = 4.25;
    stats[1].q3 = 6.0;
    stats[1].whislo = 2.75;
    stats[1].whishi = 8.0;

    stats[2].q1 = 1.0;
    stats[2].med = 2.0;
    stats[2].q3 = 3.0;
    stats[2].whislo = 0.5;
    stats[2].whishi = 10.0;
    stats[2].fliers = {12.0};

    ax.bxp(stats.data(), stats.size());
    return fig;
}

vpl::Figure make_quiver()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    constexpr int g = 7;
    std::vector<double> xs, ys, us, vs;
    for (int i = 0; i < g; ++i) {
        for (int j = 0; j < g; ++j) {
            const double x = -2.0 + 4.0 * j / (g - 1);
            const double y = -2.0 + 4.0 * i / (g - 1);
            xs.push_back(x);
            ys.push_back(y);
            us.push_back(-y * 0.5);
            vs.push_back(x * 0.5 + 0.2 * std::sin(x));
        }
    }

    ax.quiver(xs.data(), ys.data(), us.data(), vs.data(), xs.size());
    return fig;
}

vpl::Figure make_axis_off()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    constexpr int rows = 12;
    constexpr int cols = 16;
    std::vector<double> z(rows * cols);
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            z[r * cols + c] = std::sin(0.6 * c) * std::cos(0.5 * r);
        }
    }
    ax.imshow(z.data(), rows, cols);
    ax.xlabel = "stripped";
    ax.title = "but the title stays";
    ax.axis_on = false;
    return fig;
}

vpl::Figure make_suplabels()
{
    vpl::Figure fig;
    constexpr int n = 61;
    std::vector<double> xs(n), ys(n);
    for (int i = 0; i < n; ++i) {
        xs[i] = i * 6.0 / (n - 1);
    }
    for (int k = 0; k < 4; ++k) {
        vpl::Axes &ax = fig.add_subplot(2, 2, k + 1);
        for (int i = 0; i < n; ++i) {
            ys[i] = std::sin((k + 1) * xs[i]);
        }
        ax.plot(xs.data(), ys.data(), n);
        ax.label_outer();
    }
    fig.suptitle("shared grid");
    fig.supxlabel("time");
    fig.supylabel("amplitude");
    return fig;
}

vpl::Figure make_label_outer()
{
    vpl::Figure fig;
    constexpr int n = 61;
    std::vector<double> xs(n), ys(n);
    for (int i = 0; i < n; ++i) {
        xs[i] = i * 6.0 / (n - 1);
    }
    for (int k = 0; k < 4; ++k) {
        vpl::Axes &ax = fig.add_subplot(2, 2, k + 1);
        for (int i = 0; i < n; ++i) {
            ys[i] = std::sin((k + 1) * xs[i]);
        }
        ax.plot(xs.data(), ys.data(), n);
        ax.xlabel = "t";
        ax.ylabel = "value";
        ax.label_outer();
    }
    return fig;
}

vpl::Figure make_margins()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    constexpr int n = 61;
    std::vector<double> xs(n), ys(n);
    for (int i = 0; i < n; ++i) {
        xs[i] = i * 6.0 / (n - 1);
        ys[i] = std::sin(xs[i]);
    }
    ax.plot(xs.data(), ys.data(), n);
    ax.xmargin = 0.0;
    ax.ymargin = 0.3;
    return fig;
}

vpl::Figure make_inset_axes()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    constexpr int n = 201;
    std::vector<double> xs(n), ys(n);
    for (int i = 0; i < n; ++i) {
        xs[i] = i * 6.0 / (n - 1);
        ys[i] = std::sin(xs[i]);
    }
    ax.plot(xs.data(), ys.data(), n);

    vpl::Axes &ins = fig.inset_axes(ax, 0.6, 0.6, 0.35, 0.3);
    ins.plot(xs.data(), ys.data(), n);
    ins.set_xlim(1.2, 2.0);
    ins.set_ylim(0.85, 1.02);
    return fig;
}

vpl::Figure make_indicate_inset_zoom()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    constexpr int n = 201;
    std::vector<double> xs(n), ys(n);
    for (int i = 0; i < n; ++i) {
        xs[i] = i * 6.0 / (n - 1);
        ys[i] = std::sin(xs[i]);
    }
    ax.plot(xs.data(), ys.data(), n);

    vpl::Axes &ins = fig.inset_axes(ax, 0.55, 0.08, 0.4, 0.35);
    ins.plot(xs.data(), ys.data(), n);
    ins.set_xlim(1.2, 2.0);
    ins.set_ylim(0.85, 1.02);

    /* indicate_inset_zoom: the rectangle IS the inset's own limits. */
    ax.indicate_inset(1.2, 0.85, 0.8, 0.17, ins, ax.position);
    return fig;
}

vpl::Figure make_secondary_xaxis()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    constexpr int n = 101;
    std::vector<double> xs(n), ys(n);
    for (int i = 0; i < n; ++i) {
        xs[i] = i * 100.0 / (n - 1);
        ys[i] = std::sin(xs[i] * 0.1);
    }
    ax.plot(xs.data(), ys.data(), n);
    ax.xlabel = "samples";
    fig.secondary_xaxis(ax, /*top=*/true, 1.0 / 20.0, 0.0);
    return fig;
}

vpl::Figure make_secondary_yaxis()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    constexpr int n = 101;
    std::vector<double> xs(n), ys(n);
    for (int i = 0; i < n; ++i) {
        xs[i] = i * 10.0 / (n - 1);
        ys[i] = 10.0 + 15.0 * std::sin(xs[i]);
    }
    ax.plot(xs.data(), ys.data(), n);
    ax.ylabel = "degrees C";
    fig.secondary_yaxis(ax, /*right=*/true, 1.8, 32.0);
    return fig;
}

vpl::Figure make_grid_over_image()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    constexpr int rows = 6;
    constexpr int cols = 8;
    std::vector<double> z(rows * cols);
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            z[r * cols + c] = static_cast<double>(r * cols + c);
        }
    }
    ax.imshow(z.data(), rows, cols);
    ax.set_grid(true);
    return fig;
}

vpl::Figure make_axisbelow()
{
    vpl::Figure fig;

    constexpr int n = 7;
    std::vector<double> xs(n), hs(n), line_ys(n);
    for (int i = 0; i < n; ++i) {
        xs[i] = static_cast<double>(i);
        hs[i] = 1.0 + 0.5 * i;
        line_ys[i] = 0.5 + 0.4 * i;
    }

    vpl::Axes &ax1 = fig.add_subplot(1, 2, 1);
    ax1.bar(xs.data(), hs.data(), n, 0.8);
    vpl::Line2D &l1 = ax1.plot(xs.data(), line_ys.data(), n);
    l1.color = vpl::Color::rgb(1.0, 0.498039, 0.054902); /* C1 */
    l1.linewidth = 2.0;
    ax1.set_grid(true);
    ax1.axisbelow = vpl::AxisBelow::Line;
    ax1.title = "line (default)";

    vpl::Axes &ax2 = fig.add_subplot(1, 2, 2);
    ax2.bar(xs.data(), hs.data(), n, 0.8);
    vpl::Line2D &l2 = ax2.plot(xs.data(), line_ys.data(), n);
    l2.color = vpl::Color::rgb(1.0, 0.498039, 0.054902); /* C1 */
    l2.linewidth = 2.0;
    ax2.set_grid(true);
    ax2.axisbelow = vpl::AxisBelow::On;
    ax2.title = "True";

    return fig;
}

vpl::Figure make_subplots_shared()
{
    /* subplots(sharex='col', sharey='row') on a 2x2 grid. vplot's sharing is a
       one-directional follow (unlike matplotlib's symmetric union), so each
       leader is given the largest data in its group to make the two coincide. */
    vpl::Figure fig;

    constexpr int n_wide = 11;
    std::vector<double> x_wide(n_wide), y00(n_wide), y10(n_wide);
    for (int i = 0; i < n_wide; ++i) {
        x_wide[i] = static_cast<double>(i);
        y00[i] = std::sin(x_wide[i] * 0.5);
        y10[i] = 5.0 * x_wide[i];
    }

    constexpr int n_narrow = 7;
    std::vector<double> x01(n_narrow), y01(n_narrow), x11(n_narrow), y11(n_narrow);
    for (int i = 0; i < n_narrow; ++i) {
        x01[i] = static_cast<double>(i) * 0.5; /* 0..3, col 1's leader */
        y01[i] = 0.05 * x01[i] * x01[i]; /* small: dominated by a00's y */
        x11[i] = static_cast<double>(i) * 0.3; /* 0..1.8, smaller than x01 */
        y11[i] = x11[i] * x11[i] * x11[i]; /* small: dominated by a10's y */
    }

    vpl::Axes &a00 = fig.add_subplot(2, 2, 1);
    a00.plot(x_wide.data(), y00.data(), n_wide);
    vpl::Axes &a01 = fig.add_subplot(2, 2, 2);
    a01.plot(x01.data(), y01.data(), n_narrow);
    vpl::Axes &a10 = fig.add_subplot(2, 2, 3);
    a10.plot(x_wide.data(), y10.data(), n_wide);
    vpl::Axes &a11 = fig.add_subplot(2, 2, 4);
    a11.plot(x11.data(), y11.data(), n_narrow);

    /* sharex='col': each column follows its own row-0 member. */
    a10.shared_x = &a00;
    a11.shared_x = &a01;
    /* sharey='row': each row follows its own column-0 member. */
    a01.shared_y = &a00;
    a11.shared_y = &a10;

    /* subplots() strips interior tick labels when either axis is shared, the
       same edge rule as label_outer. */
    a00.label_outer();
    a01.label_outer();
    a10.label_outer();
    a11.label_outer();

    return fig;
}

vpl::Figure make_sharex()
{
    vpl::Figure fig;

    /* Top panel spans x 0..20, bottom only 0..5. With sharex the bottom
       follows the top, so its range is 0..20 and its curve is squeezed into
       the left quarter. */
    constexpr int n_top = 21;
    std::vector<double> x_top(n_top), y_top(n_top);
    for (int i = 0; i < n_top; ++i) {
        x_top[i] = static_cast<double>(i);
        y_top[i] = std::sin(x_top[i] * 0.5);
    }

    constexpr int n_bottom = 11;
    std::vector<double> x_bottom(n_bottom), y_bottom(n_bottom);
    for (int i = 0; i < n_bottom; ++i) {
        x_bottom[i] = static_cast<double>(i) * 0.5;
        y_bottom[i] = x_bottom[i] * x_bottom[i];
    }

    vpl::Axes &ax1 = fig.add_subplot(2, 1, 1);
    ax1.plot(x_top.data(), y_top.data(), n_top);
    ax1.label_outer();

    vpl::Axes &ax2 = fig.add_subplot(2, 1, 2);
    ax2.plot(x_bottom.data(), y_bottom.data(), n_bottom);
    ax2.shared_x = &ax1;
    ax2.label_outer();

    return fig;
}


vpl::Figure make_share_both()
{
    vpl::Figure fig;

    /* The follower's data is smaller than the leader's in both directions,
       required by vplot's one-directional sharing (see tools/gen_references.py). */
    constexpr int n = 21;
    std::vector<double> x1(n), y1(n), x2(n), y2(n);
    for (int i = 0; i < n; ++i) {
        x1[i] = static_cast<double>(i) * 0.5;
        y1[i] = 4.0 * std::sin(x1[i]);
        x2[i] = static_cast<double>(i) * 0.125;
        y2[i] = std::cos(x2[i] * 4.0);
    }

    vpl::Axes &ax1 = fig.add_subplot(2, 1, 1);
    ax1.plot(x1.data(), y1.data(), n);

    vpl::Axes &ax2 = fig.add_subplot(2, 1, 2);
    ax2.plot(x2.data(), y2.data(), n);
    ax2.shared_x = &ax1;
    ax2.shared_y = &ax1;

    return fig;
}


vpl::Figure make_sci_offset()
{
    vpl::Figure fig;

    const double x[2] = {0.0, 1.0};
    const double big[2] = {1.0e6, 2.0e6};
    const double small[2] = {1.0e-6, 2.0e-6};
    const double shifted[2] = {1000000.0, 1000005.0};

    fig.add_subplot(3, 1, 1).plot(x, big, 2);
    fig.add_subplot(3, 1, 2).plot(x, small, 2);
    fig.add_subplot(3, 1, 3).plot(x, shifted, 2);

    return fig;
}


vpl::Figure make_box_aspect_cycle()
{
    vpl::Figure fig;

    constexpr int n = 61;
    std::vector<double> xs(n), ys(n), half(n), third(n);
    for (int i = 0; i < n; ++i) {
        xs[i] = i * 6.0 / (n - 1);
        ys[i] = std::sin(xs[i]);
        half[i] = ys[i] * 0.6;
        third[i] = ys[i] * 0.3;
    }

    vpl::Axes &top = fig.add_subplot(2, 1, 1);
    top.plot(xs.data(), ys.data(), n);
    top.box_aspect = 0.35;
    top.anchor_x = 0.0; /* "SW" */
    top.anchor_y = 0.0;
    top.x_nbins = 4;
    top.y_nbins = 4;

    vpl::Axes &bottom = fig.add_subplot(2, 1, 2);
    for (const char *spec : {"#d62728", "#2ca02c", "#9467bd"}) {
        vpl::Color c;
        vpl::parse_color(spec, c);
        bottom.prop_cycle.colors.push_back(c);
    }
    bottom.plot(xs.data(), ys.data(), n);
    bottom.plot(xs.data(), half.data(), n);
    bottom.plot(xs.data(), third.data(), n);

    return fig;
}

vpl::Figure make_prop_cycle_full()
{
    /* Five lines through a three-entry cycle: the fourth and fifth wrap back to
       the first and second (colour, linestyle) pair. Straight lines are used so
       dash phase along a curve does not add noise to the test of cycle order. */
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    for (const char *spec : {"tab:blue", "tab:orange", "tab:green"}) {
        vpl::Color c;
        vpl::parse_color(spec, c);
        ax.prop_cycle.colors.push_back(c);
    }
    ax.prop_cycle.linestyles = {vpl::LineStyle::Solid, vpl::LineStyle::Dashed,
                                vpl::LineStyle::Dotted};

    constexpr int n = 21;
    std::vector<double> xs(n);
    for (int i = 0; i < n; ++i) {
        xs[i] = i * 6.0 / (n - 1);
    }
    for (int k = 0; k < 5; ++k) {
        std::vector<double> ys(n);
        for (int i = 0; i < n; ++i) {
            ys[i] = 0.15 * k * xs[i];
        }
        ax.plot(xs.data(), ys.data(), n);
    }

    return fig;
}

vpl::Figure make_locator_params_full()
{
    /* steps={1, 5, 10} forbids the 2 and 2.5 multiples AutoLocator would
       otherwise pick, giving coarser spacing. tight=True has no visible effect
       on a MaxNLocator axis, so the margin here is the plain default. */
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    constexpr int n = 61;
    std::vector<double> xs(n), ys(n);
    for (int i = 0; i < n; ++i) {
        xs[i] = i * 6.0 / (n - 1);
        ys[i] = std::sin(xs[i]);
    }
    ax.plot(xs.data(), ys.data(), n);
    ax.x_tick_steps = {1.0, 5.0, 10.0};
    ax.y_tick_steps = ax.x_tick_steps;

    return fig;
}

vpl::Figure make_figure_patch()
{
    vpl::Figure fig;

    constexpr int n = 41;
    std::vector<double> xs(n), ys(n);
    for (int i = 0; i < n; ++i) {
        xs[i] = i * 6.0 / (n - 1);
        ys[i] = std::sin(xs[i]);
    }

    /* Most of this case's RMS is one pixel of the border: matplotlib snaps the
       figure-patch stroke to a whole number of pixels (3 inward from the edge)
       where vplot covers 2.78 and antialiases the third. */
    vpl::parse_color("#f4f0e6", fig.facecolor);
    vpl::parse_color("C3", fig.edgecolor);
    fig.linewidth = 4.0;

    fig.add_subplot().plot(xs.data(), ys.data(), n);
    return fig;
}


/* The spectral cases share one signal, computed from the same closed form as
   the Python side. */
std::vector<double> spectral_signal(std::size_t n = 1024, double fs = 100.0)
{
    constexpr double kPi = 3.14159265358979323846;
    std::vector<double> s(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / fs;
        s[i] = std::sin(2.0 * kPi * 12.0 * t) + 0.5 * std::sin(2.0 * kPi * 31.0 * t) +
               0.2 * std::sin(2.0 * kPi * 7.3 * t + 1.1);
    }
    return s;
}

std::vector<double> spectral_signal_b(std::size_t n = 1024, double fs = 100.0)
{
    constexpr double kPi = 3.14159265358979323846;
    std::vector<double> s(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / fs;
        s[i] = std::sin(2.0 * kPi * 12.0 * t + 0.7) + 0.4 * std::sin(2.0 * kPi * 22.0 * t);
    }
    return s;
}

vpl::Figure make_psd()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();
    const std::vector<double> s = spectral_signal();
    ax.psd(s.data(), s.size(), 256, 100.0, 0);
    return fig;
}

vpl::Figure make_psd_detrend_window()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();
    const std::vector<double> s = spectral_signal();
    vpl::SpectralOptions opts;
    opts.detrend = vpl::SpectralDetrend::Linear;
    opts.window = vpl::SpectralWindow::Hamming;
    ax.psd(s.data(), s.size(), 256, 100.0, 0, opts);
    return fig;
}

vpl::Figure make_psd_twosided()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();
    const std::vector<double> s = spectral_signal();
    vpl::SpectralOptions opts;
    opts.sides = vpl::SpectralSides::TwoSided;
    ax.psd(s.data(), s.size(), 256, 100.0, 0, opts);
    return fig;
}

vpl::Figure make_csd()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();
    const std::vector<double> a = spectral_signal();
    const std::vector<double> b = spectral_signal_b();
    ax.csd(a.data(), b.data(), a.size(), 256, 100.0, 0);
    return fig;
}

vpl::Figure make_cohere()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();
    const std::vector<double> a = spectral_signal();
    const std::vector<double> b = spectral_signal_b();
    ax.cohere(a.data(), b.data(), a.size(), 256, 100.0, 0);
    return fig;
}

vpl::Figure make_magnitude_spectrum()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();
    const std::vector<double> s = spectral_signal(256);
    ax.magnitude_spectrum(s.data(), s.size(), 100.0);
    return fig;
}

vpl::Figure make_magnitude_spectrum_db()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();
    const std::vector<double> s = spectral_signal(256);
    ax.magnitude_spectrum(s.data(), s.size(), 100.0, /*db=*/true);
    return fig;
}

vpl::Figure make_angle_spectrum()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();
    const std::vector<double> s = spectral_signal(256);
    ax.angle_spectrum(s.data(), s.size(), 100.0);
    return fig;
}

vpl::Figure make_phase_spectrum()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();
    const std::vector<double> s = spectral_signal(256);
    ax.phase_spectrum(s.data(), s.size(), 100.0);
    return fig;
}

vpl::Figure make_specgram()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();
    const std::vector<double> s = spectral_signal();
    ax.specgram(s.data(), s.size(), 256, 100.0, 128);
    return fig;
}

vpl::Figure make_specgram_magnitude()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();
    const std::vector<double> s = spectral_signal();
    ax.specgram(s.data(), s.size(), 256, 100.0, 128, {}, vpl::SpectralMode::Magnitude,
                vpl::SpecgramScale::Db);
    return fig;
}

vpl::Figure make_streamplot()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    constexpr int g = 25;
    std::vector<double> xs(g), ys(g), us(g * g), vs(g * g);
    for (int i = 0; i < g; ++i) {
        xs[i] = -3.0 + 6.0 * i / (g - 1);
        ys[i] = -3.0 + 6.0 * i / (g - 1);
    }
    for (int r = 0; r < g; ++r) {
        for (int c = 0; c < g; ++c) {
            us[r * g + c] = -ys[r];
            vs[r * g + c] = xs[c];
        }
    }

    ax.streamplot(xs.data(), ys.data(), us.data(), vs.data(), g, g);
    return fig;
}

vpl::Figure make_pie_label()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    const double values[5] = {35.0, 25.0, 20.0, 12.0, 8.0};
    ax.pie(values, 5);

    const std::string labels[5] = {"alpha", "beta", "gamma", "delta", "eps"};
    ax.pie_label(labels, 5);
    return fig;
}

vpl::Figure make_barbs()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    const double mags[9] = {0.0, 4.0, 7.0, 12.0, 23.0, 27.0, 45.0, 55.0, 65.0};
    std::vector<double> xs, ys, us, vs;
    for (int i = 0; i < 9; ++i) {
        const double ang = 20.0 * i * 3.14159265358979323846 / 180.0;
        xs.push_back(static_cast<double>(i % 3));
        ys.push_back(static_cast<double>(i / 3));
        us.push_back(mags[i] * std::cos(ang));
        vs.push_back(mags[i] * std::sin(ang));
    }

    ax.barbs(xs.data(), ys.data(), us.data(), vs.data(), xs.size());
    return fig;
}

vpl::Figure make_grouped_bar()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    const double heights[12] = {3.0, 5.0, 2.0, 4.0,  /* dataset 0 */
                                4.5, 2.5, 3.5, 1.5,  /* dataset 1 */
                                1.0, 3.0, 4.0, 2.5}; /* dataset 2 */
    const std::string ticks[4] = {"north", "south", "east", "west"};
    ax.grouped_bar(heights, 3, 4, ticks);
    return fig;
}

vpl::Figure make_acorr()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    constexpr int n = 60;
    std::vector<double> xs(n);
    for (int i = 0; i < n; ++i) {
        xs[i] = std::sin(i * 0.3) + 0.4 * std::cos(i * 0.11);
    }
    ax.acorr(xs.data(), n, 15);
    return fig;
}

vpl::Figure make_xcorr()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    constexpr int n = 60;
    std::vector<double> a(n), b(n);
    for (int i = 0; i < n; ++i) {
        a[i] = std::sin(i * 0.3);
        b[i] = std::sin((i - 4) * 0.3);
    }
    ax.xcorr(a.data(), b.data(), n, 15);
    return fig;
}

vpl::Figure make_quiverkey()
{
    vpl::Figure fig = make_quiver();
    vpl::Axes &ax = *fig.axes.front();
    ax.quiverkey(*ax.quivers.front(), 0.8, 0.9, 1.0, "1 unit");
    return fig;
}

vpl::Figure make_table()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    const double xs[4] = {0.0, 1.0, 2.0, 3.0};
    const double ys[4] = {1.0, 3.0, 2.0, 4.0};
    ax.plot(xs, ys, 4);

    const std::string cells[9] = {"1.0", "20", "300", "4.5",  "60",
                                  "700", "8.25", "90", "1000"};
    vpl::Table &t = ax.table(cells, 3, 3);
    t.col_labels = {"alpha", "beta", "gamma"};
    t.row_labels = {"first", "second", "third"};
    return fig;
}

vpl::Figure make_contourf()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    constexpr int n = 21;
    std::vector<double> z(n * n);
    for (int r = 0; r < n; ++r) {
        const double y = -2.0 + 4.0 * r / (n - 1);
        for (int c = 0; c < n; ++c) {
            const double x = -2.0 + 4.0 * c / (n - 1);
            z[r * n + c] = std::exp(-((x - 0.8) * (x - 0.8) + y * y)) +
                           std::exp(-((x + 0.8) * (x + 0.8) + y * y));
        }
    }

    const double levels[5] = {0.1, 0.3, 0.5, 0.7, 0.9};
    ax.contourf(z.data(), n, n, levels, 5);
    return fig;
}

vpl::Figure make_contourf_saddle()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    const double z[9] = {1.0, 0.0, 1.0,
                         0.0, 0.9, 0.2,
                         1.0, 0.1, 1.0};
    const double levels[2] = {0.5, 2.0};
    ax.contourf(z, 3, 3, levels, 2);
    return fig;
}

vpl::Figure make_violinplot()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    const double values[] = {1.0, 2.0, 2.5, 3.0, 3.2, 4.0, 5.0, 5.5, 6.0, 9.0,
                             2.0, 3.0, 3.5, 4.0, 4.1, 4.2, 5.0, 6.0,
                             0.0, 1.0, 1.5, 5.0, 5.2, 5.4, 8.0, 9.0, 9.5};
    const std::size_t counts[3] = {10, 8, 9};
    ax.violinplot(values, counts, 3);
    return fig;
}

vpl::Figure make_color_cycles()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    const double x2[2] = {0.0, 1.0};

    const double y0[2] = {1.0, 1.2};
    ax.plot(x2, y0, 2); /* line cycle 0 */

    const double sx[2] = {0.2, 0.4};
    const double sy[2] = {2.0, 2.2};
    ax.stem(sx, sy, 2); /* neither */

    const double p0[2] = {3.0, 3.2};
    ax.scatter(x2, p0, 2); /* patch cycle 0 */

    const double bx[1] = {0.3};
    const double bh[1] = {0.4};
    const double bb[1] = {3.6};
    ax.bar(bx, bh, 1, 0.1, bb); /* patch cycle 1 */

    const double hy[1] = {4.4};
    const double hx0[1] = {0.0};
    const double hx1[1] = {1.0};
    ax.hlines(hy, hx0, hx1, 1); /* neither */

    const double y1[2] = {5.0, 5.2};
    ax.plot(x2, y1, 2); /* line cycle 1 */

    const double lo[2] = {5.8, 5.8};
    const double hi[2] = {6.2, 6.4};
    ax.fill_between(x2, lo, hi, 2); /* patch cycle 2 */

    const double y2[2] = {6.8, 7.0};
    ax.plot(x2, y2, 2); /* line cycle 2 */

    const double p1[2] = {7.4, 7.6};
    ax.scatter(x2, p1, 2); /* patch cycle 3 */

    return fig;
}

vpl::Figure make_pcolormesh()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    const double xedges[6] = {0.0, 0.5, 1.5, 3.0, 3.5, 5.0};
    const double yedges[5] = {0.0, 1.0, 1.2, 2.5, 4.0};
    double z[20];
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 5; ++c) {
            z[r * 5 + c] = static_cast<double>((r * 5 + c) % 7);
        }
    }
    ax.pcolormesh(z, 4, 5, xedges, yedges);
    return fig;
}

vpl::Figure make_matshow()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    const double z[12] = {1.0, 2.0,  3.0,  4.0,
                          5.0, 6.0,  7.0,  8.0,
                          9.0, 10.0, 11.0, 12.0};
    ax.matshow(z, 3, 4);
    return fig;
}

vpl::Figure make_pie()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    const double values[5] = {3.0, 1.0, 2.0, 4.0, 1.5};
    ax.pie(values, 5);
    return fig;
}

vpl::Figure make_hist2d()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    const double x[14] = {0.1, 0.5, 0.9, 1.2, 1.8, 2.1, 2.4, 0.3, 1.1, 2.9, 1.4, 2.0, 0.7, 2.6};
    const double y[14] = {0.2, 0.4, 1.1, 1.5, 0.8, 2.2, 0.5, 1.9, 1.0, 2.5, 1.3, 0.3, 2.4, 1.7};
    ax.hist2d(x, y, 14, 4, 4);
    return fig;
}

/* Mirrors case_hexbin in tools/gen_references.py, so both sides bin the same
   points. */
vpl::Figure make_hexbin()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    std::vector<double> xs, ys;
    for (int i = 0; i < 120; ++i) {
        const double t = i * 0.37;
        xs.push_back(2.0 + std::cos(t) * (0.4 + 0.02 * i));
        ys.push_back(2.0 + std::sin(t * 1.3) * (0.4 + 0.02 * i));
    }
    ax.hexbin(xs.data(), ys.data(), xs.size(), 8);
    return fig;
}

/* Mirrors case_triplot in tools/gen_references.py. The lattice is perturbed so
   no four points are co-circular, keeping the Delaunay triangulation unique. */
vpl::Figure make_triplot()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    std::vector<double> xs, ys;
    for (int i = 0; i < 5; ++i) {
        for (int j = 0; j < 5; ++j) {
            xs.push_back(i + 0.13 * std::sin(2.7 * (i + j)));
            ys.push_back(j + 0.11 * std::cos(1.9 * (i - j)));
        }
    }
    ax.triplot(xs.data(), ys.data(), xs.size());
    return fig;
}

/* Mirrors case_tricontour in tools/gen_references.py, using the same perturbed
   lattice as make_triplot. */
vpl::Figure make_tricontour()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    std::vector<double> xs, ys, zs;
    for (int i = 0; i < 6; ++i) {
        for (int j = 0; j < 6; ++j) {
            const double px = i + 0.13 * std::sin(2.7 * (i + j));
            const double py = j + 0.11 * std::cos(1.9 * (i - j));
            xs.push_back(px);
            ys.push_back(py);
            zs.push_back(std::sin(0.8 * px) * std::cos(0.6 * py));
        }
    }
    const double levels[5] = {-0.5, -0.2, 0.1, 0.4, 0.7};
    ax.tricontour(xs.data(), ys.data(), zs.data(), xs.size(), levels, 5);
    return fig;
}

/* Mirrors case_tricontourf, using the same lattice, field and levels as
   make_tricontour so the two differ only in boundary versus interior. */
vpl::Figure make_tricontourf()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    std::vector<double> xs, ys, zs;
    for (int i = 0; i < 6; ++i) {
        for (int j = 0; j < 6; ++j) {
            const double px = i + 0.13 * std::sin(2.7 * (i + j));
            const double py = j + 0.11 * std::cos(1.9 * (i - j));
            xs.push_back(px);
            ys.push_back(py);
            zs.push_back(std::sin(0.8 * px) * std::cos(0.6 * py));
        }
    }
    const double levels[5] = {-0.5, -0.2, 0.1, 0.4, 0.7};
    ax.tricontourf(xs.data(), ys.data(), zs.data(), xs.size(), levels, 5);
    return fig;
}

/* Mirrors case_tripcolor, using the same lattice as make_triplot and
   make_tricontour. */
vpl::Figure make_tripcolor()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    std::vector<double> xs, ys, zs;
    for (int i = 0; i < 6; ++i) {
        for (int j = 0; j < 6; ++j) {
            const double px = i + 0.13 * std::sin(2.7 * (i + j));
            const double py = j + 0.11 * std::cos(1.9 * (i - j));
            xs.push_back(px);
            ys.push_back(py);
            zs.push_back(std::sin(0.8 * px) * std::cos(0.6 * py));
        }
    }
    ax.tripcolor(xs.data(), ys.data(), zs.data(), xs.size());
    return fig;
}

vpl::Figure make_spy()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    const double z[20] = {1.0, 0.0, 0.0, 2.0, 0.0,
                          0.0, 3.0, 0.0, 0.0, 0.0,
                          0.0, 0.0, 0.0, 4.0, 5.0,
                          6.0, 0.0, 7.0, 0.0, 0.0};
    ax.spy(z, 4, 5);
    return fig;
}

vpl::Figure make_axline_barlabel()
{
    vpl::Figure fig;

    vpl::Axes &top = fig.add_subplot(2, 1, 1);
    const double lx[2] = {0.0, 4.0};
    const double ly[2] = {1.0, 3.0};
    top.plot(lx, ly, 2);
    top.axline(0.0, 0.0, 1.0);
    top.axline(1.0, 4.0, -0.75);
    top.axline(3.0, 0.0, 0.0, /*vertical=*/true);

    vpl::Axes &bottom = fig.add_subplot(2, 1, 2);
    const double bx[4] = {0.0, 1.0, 2.0, 3.0};
    const double bh[4] = {2.0, 3.5, 1.0, 2.75};
    vpl::PolyPatch &bars = bottom.bar(bx, bh, 4, 0.7);
    bottom.bar_label(bars);

    return fig;
}

vpl::Figure make_bar_label_full()
{
    /* Top: custom format fmt='%.1f' and padding=6 on edge labels. Bottom: a
       stacked bar labelled at each segment's centre, showing the segment's own
       height rather than its cumulative position. */
    vpl::Figure fig;

    vpl::Axes &top = fig.add_subplot(2, 1, 1);
    const double bx[4] = {0.0, 1.0, 2.0, 3.0};
    const double bh[4] = {2.0, 3.5, 1.0, 2.75};
    vpl::PolyPatch &bars = top.bar(bx, bh, 4, 0.7);
    top.bar_label(bars, "%.1f", vpl::BarLabelType::Edge, 6.0);

    vpl::Axes &bottom = fig.add_subplot(2, 1, 2);
    const double xs[4] = {0.0, 1.0, 2.0, 3.0};
    const double lower[4] = {1.0, 1.5, 0.8, 1.2};
    const double upper[4] = {1.6, 1.1, 1.4, 0.9};
    vpl::PolyPatch &b1 = bottom.bar(xs, lower, 4, 0.7);
    vpl::PolyPatch &b2 = bottom.bar(xs, upper, 4, 0.7, lower);
    bottom.bar_label(b1, "%g", vpl::BarLabelType::Center);
    bottom.bar_label(b2, "%g", vpl::BarLabelType::Center);

    return fig;
}

vpl::Figure make_figure_api()
{
    vpl::Figure fig;

    constexpr int n = 41;
    std::vector<double> xs(n), ys(n), sq(n), neg(n);
    for (int i = 0; i < n; ++i) {
        xs[i] = i * 6.0 / (n - 1);
        ys[i] = std::sin(xs[i]);
        sq[i] = ys[i] * ys[i];
        neg[i] = -ys[i];
    }

    vpl::Axes &a1 = fig.add_subplot(2, 1, 1);
    a1.plot(xs.data(), ys.data(), n);
    vpl::Axes &a2 = fig.add_subplot(2, 1, 2);
    a2.plot(xs.data(), sq.data(), n);

    fig.subplot_params.left = 0.2;
    fig.subplot_params.right = 0.85;
    fig.subplot_params.bottom = 0.15;
    fig.subplot_params.top = 0.82;
    fig.subplot_params.hspace = 0.4;
    fig.subplots_adjust();
    fig.suptitle("figure title");

    vpl::Axes &inset = fig.add_axes(0.55, 0.55, 0.2, 0.15);
    inset.plot(xs.data(), neg.data(), n);

    return fig;
}

vpl::Figure make_ecdf_arrow_xerr()
{
    vpl::Figure fig;

    vpl::Axes &top = fig.add_subplot(2, 1, 1);
    const double sample[10] = {3.0, 1.0, 4.0, 1.0, 5.0, 9.0, 2.0, 6.0, 5.0, 3.0};
    top.ecdf(sample, 10);

    vpl::Axes &bottom = fig.add_subplot(2, 1, 2);
    const double x[3] = {1.0, 2.0, 3.0};
    const double y[3] = {1.0, 2.0, 1.5};
    const double xe[3] = {0.3, 0.5, 0.2};
    const double ye[3] = {0.2, 0.3, 0.25};
    vpl::Line2D &bars = bottom.errorbar(x, y, ye, 3);
    bars.xerr.assign(xe, xe + 3);
    bars.capsize = 3.0;
    bottom.arrow(1.0, 2.5, 1.5, 0.6, 0.06);

    return fig;
}

vpl::Figure make_styling_knobs()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    constexpr int n = 9;
    std::vector<double> xs(n), ys(n), lower(n), es(n, 0.15);
    for (int i = 0; i < n; ++i) {
        xs[i] = i * 4.0 / (n - 1);
        ys[i] = std::sin(xs[i]);
        lower[i] = ys[i] - 1.5;
    }

    vpl::Line2D &line = ax.plot(xs.data(), ys.data(), n);
    line.linewidth = 3.5;
    line.marker = vpl::MarkerStyle::Circle;
    line.markersize = 12.0;
    line.markeredgewidth = 2.5;
    line.has_markerfacecolor = true;
    vpl::parse_color("white", line.markerfacecolor);
    line.has_markeredgecolor = true;
    vpl::parse_color("C3", line.markeredgecolor);

    vpl::Line2D &bars = ax.errorbar(xs.data(), lower.data(), es.data(), n);
    bars.elinewidth = 0.5;
    bars.capsize = 5.0;

    ax.set_grid(true);
    ax.grid_linestyle = vpl::LineStyle::Dotted;
    ax.grid_linewidth = 1.2;

    ax.xlabel = "x label";
    ax.label_pad = 15.0;
    ax.title = "title";
    ax.title_pad = 20.0;

    return fig;
}

vpl::Figure make_imshow()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    constexpr int rows = 6;
    constexpr int cols = 8;
    std::vector<double> zz(rows * cols);
    for (int i = 0; i < rows; ++i) {
        for (int j = 0; j < cols; ++j) {
            zz[i * cols + j] = std::sin(0.7 * j) * std::cos(0.5 * i);
        }
    }
    ax.imshow(zz.data(), rows, cols);
    return fig;
}

vpl::Figure make_tight_layout()
{
    vpl::Figure fig;

    constexpr int n = 61;
    std::vector<double> xs(n), ys(n);
    for (int i = 0; i < n; ++i) {
        xs[i] = i * 6.0 / (n - 1);
    }

    for (int k = 0; k < 4; ++k) {
        vpl::Axes &ax = fig.add_subplot(2, 2, static_cast<unsigned>(k + 1));
        for (int i = 0; i < n; ++i) {
            ys[i] = std::sin((k + 1) * xs[i]);
        }
        ax.plot(xs.data(), ys.data(), n);
        ax.title = "panel " + std::to_string(k + 1);
        ax.xlabel = "t";
        ax.ylabel = "value";
    }
    return fig;
}

vpl::Figure make_align_ylabels()
{
    vpl::Figure fig;

    constexpr int n = 11;
    std::vector<double> xs(n), top(n), bottom(n);
    for (int i = 0; i < n; ++i) {
        xs[i] = i / 10.0;
        top[i] = xs[i] * 80000.0;
        bottom[i] = xs[i];
    }

    vpl::Axes &ax1 = fig.add_subplot(2, 1, 1);
    ax1.plot(xs.data(), top.data(), n);
    ax1.ylabel = "amplitude";

    vpl::Axes &ax2 = fig.add_subplot(2, 1, 2);
    ax2.plot(xs.data(), bottom.data(), n);
    ax2.ylabel = "ratio";

    fig.align_ylabels({&ax1, &ax2});
    return fig;
}

vpl::Figure make_ecdf_complementary()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    constexpr int n = 60;
    std::vector<double> vals(n);
    for (int i = 0; i < n; ++i) {
        vals[i] = std::sin(i * 1.3) + 0.5 * std::cos(i * 0.7);
    }
    ax.ecdf(vals.data(), n, /*complementary=*/true);
    return fig;
}

vpl::Figure make_ecdf_weighted()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    const std::vector<double> vals = {1.0, 2.0, 3.0, 4.0, 5.0, 2.5, 3.5};
    const std::vector<double> w = {1.0, 3.0, 1.0, 2.0, 1.0, 4.0, 2.0};
    ax.ecdf(vals.data(), vals.size(), /*complementary=*/false, w.data());
    return fig;
}

vpl::Figure make_ecdf_horizontal()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    constexpr int n = 40;
    std::vector<double> vals(n);
    for (int i = 0; i < n; ++i) {
        vals[i] = std::sin(i * 1.3);
    }
    ax.ecdf(vals.data(), n, /*complementary=*/false, /*weights=*/nullptr, /*horizontal=*/true);
    return fig;
}

vpl::Figure make_fill_where()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    constexpr int n = 51;
    const double pi = 3.14159265358979323846;
    std::vector<double> xs(n), y1(n), y2(n);
    std::vector<std::uint8_t> where(n);
    for (int i = 0; i < n; ++i) {
        xs[i] = i * 2.0 * pi / (n - 1);
        y1[i] = std::sin(xs[i]);
        y2[i] = std::cos(xs[i]);
        where[i] = y1[i] > y2[i] ? 1 : 0;
    }
    ax.plot(xs.data(), y1.data(), n);
    ax.plot(xs.data(), y2.data(), n);
    ax.fill_between(xs.data(), y1.data(), y2.data(), n, where.data(), /*interpolate=*/true);
    return fig;
}

vpl::Figure make_stairs_fill()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();
    const std::vector<double> values = {1.0, 3.0, 2.0, 4.0, 2.5};
    const std::vector<double> edges = {0, 1, 2, 3, 4, 5};
    const double baseline = 0.5;
    ax.stairs(values.data(), edges.data(), values.size(), /*fill=*/true, &baseline, 1);
    return fig;
}

vpl::Figure make_stairs_horizontal()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();
    const std::vector<double> values = {1.0, 3.0, 2.0, 4.0, 2.5};
    const std::vector<double> edges = {0, 1, 2, 3, 4, 5};
    ax.stairs(values.data(), edges.data(), values.size(), /*fill=*/true, nullptr, 0,
              /*horizontal=*/true);
    return fig;
}

vpl::Figure make_label_outer_ticks()
{
    vpl::Figure fig;
    constexpr int n = 41;
    std::vector<double> xs(n), ys(n);
    for (int i = 0; i < n; ++i) {
        xs[i] = i * 6.0 / (n - 1);
    }
    for (int k = 0; k < 4; ++k) {
        vpl::Axes &ax = fig.add_subplot(2, 2, static_cast<unsigned>(k + 1));
        for (int i = 0; i < n; ++i) {
            ys[i] = std::sin((k + 1) * xs[i]);
        }
        ax.plot(xs.data(), ys.data(), n);
        ax.label_outer(/*remove_inner_ticks=*/true);
    }
    return fig;
}

vpl::Figure make_stem_styled()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    constexpr int n = 12;
    std::vector<double> xs(n), ys(n);
    for (int i = 0; i < n; ++i) {
        xs[i] = i * 0.5;
        ys[i] = 1.0 + std::sin(xs[i]);
    }
    vpl::StemStyle style;
    style.bottom = 1.0;
    style.line_color = vpl::Color::rgb(0.0, 0.5, 0.0); /* g */
    style.line_style = vpl::LineStyle::Dashed;
    style.marker = vpl::MarkerStyle::Square;
    style.marker_color = vpl::Color::rgb(1.0, 0.0, 0.0); /* r */
    style.base_color = vpl::Color::rgb(0.0, 0.0, 0.0);   /* k */
    ax.stem(xs.data(), ys.data(), n, style);
    return fig;
}

vpl::Figure make_no_sticky_edges()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();
    const double x[4] = {0, 1, 2, 3};
    const double h[4] = {1.0, 3.0, 2.0, 4.0};
    ax.bar(x, h, 4, 0.8);
    ax.use_sticky_edges = false;
    return fig;
}

vpl::Figure make_autoscale_tight()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    constexpr int n = 61;
    std::vector<double> xs(n), ys(n);
    for (int i = 0; i < n; ++i) {
        xs[i] = i * 6.0 / (n - 1);
        ys[i] = std::sin(xs[i]);
    }
    ax.plot(xs.data(), ys.data(), n);
    ax.autoscale(/*enable=*/1, /*axis_x=*/true, /*axis_y=*/true, /*tight=*/1);
    return fig;
}

vpl::Figure make_grid_minor()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    constexpr int n = 61;
    std::vector<double> xs(n), ys(n);
    for (int i = 0; i < n; ++i) {
        xs[i] = i * 6.0 / (n - 1);
        ys[i] = std::sin(xs[i]);
    }
    ax.plot(xs.data(), ys.data(), n);
    ax.minor_x_visible = true;
    ax.minor_y_visible = true;
    ax.set_grid(true, vpl::GridWhich::Both, vpl::GridAxis::Both);
    return fig;
}

vpl::Figure make_sup_positioned()
{
    vpl::Figure fig;
    for (int k = 0; k < 2; ++k) {
        vpl::Axes &ax = fig.add_subplot(1, 2, static_cast<unsigned>(k + 1));
        const double xs[2] = {0.0, 1.0};
        const double ys[2] = {0.0, 1.0};
        ax.plot(xs, ys, 2);
    }
    fig.suptitle("moved title", 0.3, 0.92, 16.0);
    fig.supxlabel("low x label", 0.5, 0.04, 14.0);
    fig.supylabel("left y label", 0.06, 0.5, 14.0);
    return fig;
}

vpl::Figure make_fill_step()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    const std::vector<double> xs = {0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0};
    const std::vector<double> lo = {0.2, 0.5, 0.4, 0.9, 0.7, 0.3, 0.6, 0.5};
    const std::vector<double> hi = {1.0, 1.4, 1.1, 1.8, 1.5, 1.2, 1.6, 1.3};
    ax.fill_between(xs.data(), lo.data(), hi.data(), xs.size(), nullptr,
                    /*interpolate=*/false, vpl::FillStep::Mid);
    return fig;
}

vpl::Figure make_figimage()
{
    vpl::Figure fig;
    fig.width_inch = 2.0;
    fig.height_inch = 2.0;

    constexpr int rows = 40;
    constexpr int cols = 60;
    std::vector<double> X(static_cast<std::size_t>(rows) * cols);
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            X[static_cast<std::size_t>(r) * cols + c] =
                (r + c) / static_cast<double>(rows + cols);
        }
    }
    fig.figimage(X.data(), rows, cols, 40.0, 60.0);
    return fig;
}

vpl::Figure make_contour()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    constexpr int rows = 21;
    constexpr int cols = 25;
    std::vector<double> zz(rows * cols);
    for (int i = 0; i < rows; ++i) {
        const double y = -2.0 + 4.0 * i / (rows - 1);
        for (int j = 0; j < cols; ++j) {
            const double x = -3.0 + 6.0 * j / (cols - 1);
            zz[i * cols + j] = std::sin(x) * std::cos(y);
        }
    }

    const double levels[4] = {-0.6, -0.2, 0.2, 0.6};
    ax.contour(zz.data(), rows, cols, levels, 4, -3.0, 3.0, -2.0, 2.0);
    return fig;
}

vpl::Figure make_clabel()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    constexpr int rows = 21;
    constexpr int cols = 25;
    std::vector<double> zz(rows * cols);
    for (int i = 0; i < rows; ++i) {
        const double y = -2.0 + 4.0 * i / (rows - 1);
        for (int j = 0; j < cols; ++j) {
            const double x = -3.0 + 6.0 * j / (cols - 1);
            zz[i * cols + j] = std::sin(x) * std::cos(y);
        }
    }

    const double levels[4] = {-0.6, -0.2, 0.2, 0.6};
    vpl::ContourSet &cs =
        ax.contour(zz.data(), rows, cols, levels, 4, -3.0, 3.0, -2.0, 2.0);
    ax.clabel(cs);
    return fig;
}

} // namespace

/*
 * Reproduces matplotlib's tests/test_axes.py::test_single_point: two single-
 * point plots with markers and grid. The single point exercises the degenerate
 * autoscale path, where Locator.nonsingular expands to limits of 0.95..1.05.
 */
vpl::Figure make_mpl_single_point()
{
    vpl::Figure fig;

    const double xs0[1] = {0.0};
    const double ys0[1] = {0.0};
    const double xs1[1] = {1.0};
    const double ys1[1] = {1.0};

    vpl::Axes &ax1 = fig.add_subplot(2, 1, 1);
    vpl::Line2D &l1 = ax1.plot(xs0, ys0, 1);
    l1.marker = vpl::MarkerStyle::Circle;
    l1.linestyle = vpl::LineStyle::None;
    ax1.set_grid(true);

    vpl::Axes &ax2 = fig.add_subplot(2, 1, 2);
    vpl::Line2D &l2 = ax2.plot(xs1, ys1, 1);
    l2.marker = vpl::MarkerStyle::Circle;
    l2.linestyle = vpl::LineStyle::None;
    ax2.set_grid(true);

    return fig;
}

void compare_against_mpl(const std::string &relative, vpl::Figure &fig,
                         vpl::FontEngine &fonts, double tolerance)
{
    Reference ref;
    std::string why;
    if (!load_mpl_baseline(relative, ref, why)) {
        std::printf("  %-22s SKIP (%s)\n", relative.c_str(), why.c_str());
        ++skipped;
        return;
    }

    const unsigned w = fig.pixel_width();
    const unsigned h = fig.pixel_height();
    if (ref.width != w || ref.height != h) {
        std::printf("  %-22s FAIL size: matplotlib %ux%u, vplot %ux%u\n", relative.c_str(),
                    ref.width, ref.height, w, h);
        ++failed;
        return;
    }

    vpl::AggRenderer renderer(w, h, fig.dpi);
    renderer.set_font_engine(&fonts);
    renderer.clear(fig.facecolor);
    fig.draw(renderer);

    const double rms = rms_difference(renderer.pixels(), ref.rgba.data(), ref.rgba.size());
    ++checked;

    report_diff("baseline", ref, renderer.pixels(), w, h, fig.dpi);

    /* The figure agrees to about a unit of RMS, so the comparison asserts. */
    const bool ok = rms <= tolerance;
    if (!ok) {
        ++failed;
    }
    std::printf("  %-22s %s rms %.2f (tolerance %.2f)  (%ux%u; wrote baseline_vplot.png / "
                "baseline_mpl.png)\n",
                relative.c_str(), ok ? "OK  " : "FAIL", rms, tolerance, w, h);
}

/*
 * Compares the two text paths: hinted glyph bitmaps (raster) versus filled
 * glyph outlines (vector). The comparison is of ink bounding boxes rather than
 * pixels, since hinting makes the edges differ by design and only placement is
 * worth asserting; a two-pixel tolerance passes that and fails anything
 * structural.
 */
void check_outline_matches_raster(vpl::FontEngine &fonts)
{
    struct Case
    {
        const char *text;
        const char *what;
    };
    const Case cases[] = {
        {"Apg 1.00", "plain"},
        {"$\\mathdefault{10^{-1}}$", "mathtext superscript"},
        {"two lines\nof text", "two lines"},
    };

    for (const Case &c : cases) {
        vpl::FontProps font;
        font.size = 10.0;

        /* Raster: the union of the glyph bitmaps, relative to the origin. */
        const std::vector<vpl::GlyphImage> glyphs =
            fonts.render_glyphs(c.text, font, 100.0, 0.0, 0.0, 0.0);
        if (glyphs.empty()) {
            std::printf("  %-22s FAIL no glyphs rasterized\n", c.what);
            ++failed;
            continue;
        }

        double raster_x0 = 1e9, raster_x1 = -1e9, raster_y0 = 1e9, raster_y1 = -1e9;
        for (const vpl::GlyphImage &g : glyphs) {
            raster_x0 = std::min(raster_x0, static_cast<double>(g.left));
            raster_x1 = std::max(raster_x1,
                                 static_cast<double>(g.left + g.image.shape(1)));
            raster_y1 = std::max(raster_y1, static_cast<double>(g.top));
            raster_y0 = std::min(raster_y0,
                                 static_cast<double>(g.top) - g.image.shape(0));
        }

        /* Vector: the outline path, in the same origin-relative space. */
        vpl::Path path;
        vpl::TextExtent extent;
        if (!fonts.outline(c.text, font, 100.0, path, extent) || path.size() == 0) {
            std::printf("  %-22s FAIL no outline produced\n", c.what);
            ++failed;
            continue;
        }

        double vec_x0 = 1e9, vec_x1 = -1e9, vec_y0 = 1e9, vec_y1 = -1e9;
        for (std::size_t i = 0; i < path.size(); ++i) {
            /* CLOSEPOLY carries a placeholder vertex of (0, 0) that is not part
               of the shape, so it must not enter the box. */
            if (!path.codes.empty() && path.codes[i] == CLOSEPOLY) {
                continue;
            }
            vec_x0 = std::min(vec_x0, path.vertices[2 * i]);
            vec_x1 = std::max(vec_x1, path.vertices[2 * i]);
            vec_y0 = std::min(vec_y0, path.vertices[2 * i + 1]);
            vec_y1 = std::max(vec_y1, path.vertices[2 * i + 1]);
        }

        const double dx0 = std::fabs(vec_x0 - raster_x0);
        const double dx1 = std::fabs(vec_x1 - raster_x1);
        const double dy0 = std::fabs(vec_y0 - raster_y0);
        const double dy1 = std::fabs(vec_y1 - raster_y1);
        const double worst = std::max(std::max(dx0, dx1), std::max(dy0, dy1));

        ++checked;
        if (worst <= 2.0) {
            std::printf("  outline %-14s OK   within %.2f px of the raster box\n",
                        c.what, worst);
        } else {
            std::printf("  outline %-14s FAIL %.2f px apart: raster [%.2f %.2f %.2f %.2f] "
                        "vector [%.2f %.2f %.2f %.2f]\n",
                        c.what, worst, raster_x0, raster_y0, raster_x1, raster_y1,
                        vec_x0, vec_y0, vec_x1, vec_y1);
            ++failed;
        }
    }
}

int main()
{
    vpl::FontEngine fonts;
    if (!fonts.load_default_face()) {
        std::fprintf(stderr, "FAIL: could not load assets/fonts/DejaVuSans.ttf\n");
        return 1;
    }

    /* Against matplotlib's own shipped output -- no Python required. */
    std::printf("against matplotlib's baseline corpus:\n");
    vpl::Figure single = make_mpl_single_point();
    /* Residual is the antialiasing of the marker rings; held at 2.0 against a
       measured 1.13. */
    compare_against_mpl("test_axes/single_point.png", single, fonts, 2.0);

    std::printf("\nglyph outlines against glyph bitmaps:\n");
    check_outline_matches_raster(fonts);

    std::printf("\ndifferential comparison against matplotlib:\n");

    compare_text_probe(fonts);

    vpl::Figure simple = make_simple_line();
    compare("simple_line", simple, fonts);

    vpl::Figure labels = make_labels();
    compare("labels", labels, fonts);

    vpl::Figure styles = make_styles_markers();
    compare("styles_markers", styles, fonts);

    vpl::Figure loglog = make_loglog();
    compare("loglog", loglog, fonts);

    vpl::Figure band = make_fill_between();
    compare("fill_between", band, fonts);

    vpl::Figure errors = make_errorbar();
    compare("errorbar", errors, fonts);

    vpl::Figure points = make_scatter();
    compare("scatter", points, fonts);

    vpl::Figure bars = make_bar();
    compare("bar", bars, fonts);

    vpl::Figure twin = make_twinx();
    compare("twinx", twin, fonts);

    vpl::Figure cbar = make_colorbar();
    compare("colorbar", cbar, fonts);

    vpl::Figure cbarh = make_colorbar_horizontal();
    compare("colorbar_horizontal", cbarh, fonts);

    vpl::Figure texts = make_text();
    compare("text", texts, fonts);

    vpl::Figure lines = make_contour();
    compare("contour", lines, fonts);

    vpl::Figure labelled = make_clabel();
    compare("clabel", labelled, fonts);

    vpl::Figure field = make_imshow();
    compare("imshow", field, fonts);

    vpl::Figure refs = make_reference_lines();
    compare("reference_lines", refs, fonts);

    vpl::Figure minors = make_minor_ticks();
    compare("minor_ticks", minors, fonts);

    vpl::Figure varied = make_scatter_varied();
    compare("scatter_varied", varied, fonts);

    vpl::Figure note = make_annotate();
    compare("annotate", note, fonts);

    vpl::Figure bins = make_hist();
    compare("hist", bins, fonts);

    vpl::Figure density = make_hist_density();
    compare("hist_density", density, fonts);

    vpl::Figure hopt = make_hist_options();
    compare("hist_options", hopt, fonts);

    vpl::Figure hhor = make_hist_horizontal();
    compare("hist_horizontal", hhor, fonts);

    vpl::Figure hlog = make_hist_log();
    compare("hist_log", hlog, fonts);

    vpl::Figure ebf = make_errorbar_full();
    compare("errorbar_full", ebf, fonts);

    vpl::Figure pief = make_pie_full();
    compare("pie_full", pief, fonts);

    vpl::Figure cauto = make_contour_auto();
    compare("contour_auto", cauto, fonts);

    vpl::Figure hexf = make_hexbin_full();
    compare("hexbin_full", hexf, fonts);

    vpl::Figure violf = make_violin_full();
    compare("violin_full", violf, fonts);

    vpl::Figure xcf = make_xcorr_full();
    compare("xcorr_full", xcf, fonts);

    vpl::Figure boxf = make_boxplot_full();
    compare("boxplot_full", boxf, fonts);

    vpl::Figure qcol = make_quiver_colors();
    compare("quiver_colors", qcol, fonts);

    vpl::Figure trix = make_tri_explicit();
    compare("tri_explicit", trix, fonts);

    vpl::Figure com = make_contour_over_mesh();
    compare("contour_over_mesh", com, fonts);

    vpl::Figure ebl = make_errorbar_limits();
    compare("errorbar_limits", ebl, fonts);

    vpl::Figure hlm = make_hexbin_log_marginals();
    compare("hexbin_log_marginals", hlm, fonts);

    vpl::Figure psd = make_pie_shadow_donut();
    compare("pie_shadow_donut", psd, fonts);

    vpl::Figure qun = make_quiver_units();
    compare("quiver_units", qun, fonts);

    vpl::Figure bop = make_barbs_options();
    compare("barbs_options", bop, fonts);

    vpl::Figure bpp = make_boxplot_props();
    compare("boxplot_props", bpp, fonts);

    vpl::Figure fixed = make_fixed_ticks();
    compare("fixed_ticks", fixed, fonts);

    vpl::Figure frame = make_spines();
    compare("spines", frame, fonts);

    vpl::Figure lines_of_text = make_multiline();
    compare("multiline", lines_of_text, fonts);

    vpl::Figure hbars = make_barh();
    compare("barh", hbars, fonts);

    vpl::Figure bands = make_spans();
    compare("spans", bands, fonts);

    vpl::Figure stair = make_step();
    compare("step", stair, fonts);

    vpl::Figure bandx = make_fill_betweenx();
    compare("fill_betweenx", bandx, fonts);

    vpl::Figure twin_x = make_twiny();
    compare("twiny", twin_x, fonts);

    vpl::Figure locs = make_legend_locs();
    compare("legend_locs", locs, fonts);

    vpl::Figure sb = make_set_bound();
    compare("set_bound", sb, fonts);

    vpl::Figure lnc = make_legend_ncols();
    compare("legend_ncols", lnc, fonts);

    vpl::Figure lt = make_legend_title();
    compare("legend_title", lt, fonts, /*tight_layout=*/true);

    vpl::Figure tp = make_tick_params();
    compare("tick_params", tp, fonts);

    vpl::Figure tc = make_tick_colors();
    compare("tick_colors", tc, fonts);

    vpl::Figure stacked = make_bar_stacked();
    compare("bar_stacked", stacked, fonts);

    vpl::Figure inverted = make_invert_axes();
    compare("invert_axes", inverted, fonts);

    vpl::Figure rules = make_hlines_vlines();
    compare("hlines_vlines", rules, fonts);

    vpl::Figure events = make_eventplot();
    compare("eventplot", events, fonts);

    vpl::Figure stems = make_stem();
    compare("stem", stems, fonts);

    vpl::Figure steps = make_stairs();
    compare("stairs", steps, fonts);

    vpl::Figure poly = make_fill_poly();
    compare("fill_poly", poly, fonts);

    vpl::Figure gantt = make_broken_barh();
    compare("broken_barh", gantt, fonts);

    vpl::Figure stack_sym = make_stackplot_sym();
    compare("stackplot_sym", stack_sym, fonts);

    vpl::Figure stack_stream = make_stackplot_stream();
    compare("stackplot_stream", stack_stream, fonts);

    vpl::Figure stack = make_stackplot();
    compare("stackplot", stack, fonts);

    vpl::Figure boxes = make_boxplot();
    compare("boxplot", boxes, fonts);

    vpl::Figure precomputed = make_bxp();
    compare("bxp", precomputed, fonts);

    vpl::Figure grid = make_table();
    compare("table", grid, fonts);

    vpl::Figure arrows = make_quiver();
    compare("quiver", arrows, fonts);

    vpl::Figure keyed = make_quiverkey();
    compare("quiverkey", keyed, fonts);

    vpl::Figure ao = make_axis_off();
    compare("axis_off", ao, fonts);

    vpl::Figure sl = make_suplabels();
    compare("suplabels", sl, fonts);

    vpl::Figure lo = make_label_outer();
    compare("label_outer", lo, fonts);

    vpl::Figure mg = make_margins();
    compare("margins", mg, fonts);

    vpl::Figure ia = make_inset_axes();
    compare("inset_axes", ia, fonts);

    vpl::Figure iz = make_indicate_inset_zoom();
    compare("indicate_inset_zoom", iz, fonts);

    vpl::Figure sx = make_secondary_xaxis();
    compare("secondary_xaxis", sx, fonts);

    vpl::Figure sy = make_secondary_yaxis();
    compare("secondary_yaxis", sy, fonts);

    vpl::Figure gi = make_grid_over_image();
    compare("grid_over_image", gi, fonts);

    vpl::Figure ab = make_axisbelow();
    compare("axisbelow", ab, fonts, /*tight_layout=*/true);

    vpl::Figure sgs = make_subplots_shared();
    compare("subplots_shared", sgs, fonts, /*tight_layout=*/true);

    vpl::Figure shx = make_sharex();
    compare("sharex", shx, fonts, /*tight_layout=*/true);

    vpl::Figure shb = make_share_both();
    compare("share_both", shb, fonts);

    vpl::Figure sci = make_sci_offset();
    compare("sci_offset", sci, fonts);

    vpl::Figure boxa = make_box_aspect_cycle();
    compare("box_aspect_cycle", boxa, fonts);

    vpl::Figure pcf = make_prop_cycle_full();
    compare("prop_cycle_full", pcf, fonts);

    vpl::Figure lpf = make_locator_params_full();
    compare("locator_params_full", lpf, fonts);

    vpl::Figure figpatch = make_figure_patch();
    compare("figure_patch", figpatch, fonts);

    vpl::Figure sp_psd_dw = make_psd_detrend_window();
    compare("psd_detrend_window", sp_psd_dw, fonts);

    vpl::Figure sp_psd_2s = make_psd_twosided();
    compare("psd_twosided", sp_psd_2s, fonts);

    vpl::Figure sp_psd = make_psd();
    compare("psd", sp_psd, fonts);

    vpl::Figure sp_csd = make_csd();
    compare("csd", sp_csd, fonts);

    vpl::Figure sp_coh = make_cohere();
    compare("cohere", sp_coh, fonts);

    vpl::Figure sp_mag = make_magnitude_spectrum();
    compare("magnitude_spectrum", sp_mag, fonts);

    vpl::Figure sp_mag_db = make_magnitude_spectrum_db();
    compare("magnitude_spectrum_db", sp_mag_db, fonts);

    vpl::Figure sp_ang = make_angle_spectrum();
    compare("angle_spectrum", sp_ang, fonts);

    vpl::Figure sp_pha = make_phase_spectrum();
    compare("phase_spectrum", sp_pha, fonts);

    vpl::Figure sp_spec_mag = make_specgram_magnitude();
    compare("specgram_magnitude", sp_spec_mag, fonts);

    vpl::Figure sp_spec = make_specgram();
    compare("specgram", sp_spec, fonts);

    vpl::Figure stream = make_streamplot();
    compare("streamplot", stream, fonts);

    vpl::Figure pl = make_pie_label();
    compare("pie_label", pl, fonts);

    vpl::Figure wind = make_barbs();
    compare("barbs", wind, fonts);

    vpl::Figure gb = make_grouped_bar();
    compare("grouped_bar", gb, fonts);

    vpl::Figure ac = make_acorr();
    compare("acorr", ac, fonts);

    vpl::Figure xc = make_xcorr();
    compare("xcorr", xc, fonts);

    vpl::Figure filled = make_contourf();
    compare("contourf", filled, fonts);

    vpl::Figure saddle = make_contourf_saddle();
    compare("contourf_saddle", saddle, fonts);

    vpl::Figure violins = make_violinplot();
    compare("violinplot", violins, fonts);

    vpl::Figure cycles = make_color_cycles();
    compare("color_cycles", cycles, fonts);

    vpl::Figure mesh = make_pcolormesh();
    compare("pcolormesh", mesh, fonts);

    vpl::Figure matrix = make_matshow();
    compare("matshow", matrix, fonts);

    vpl::Figure wedges = make_pie();
    compare("pie", wedges, fonts);

    vpl::Figure h2 = make_hist2d();
    compare("hist2d", h2, fonts);

    vpl::Figure honeycomb = make_hexbin();
    compare("hexbin", honeycomb, fonts);

    vpl::Figure trigrid = make_triplot();
    compare("triplot", trigrid, fonts);

    vpl::Figure triiso = make_tricontour();
    compare("tricontour", triiso, fonts);

    vpl::Figure tribands = make_tricontourf();
    compare("tricontourf", tribands, fonts);

    vpl::Figure trimesh = make_tripcolor();
    compare("tripcolor", trimesh, fonts);

    vpl::Figure sparsity = make_spy();
    compare("spy", sparsity, fonts);

    vpl::Figure infinite = make_axline_barlabel();
    compare("axline_barlabel", infinite, fonts);

    vpl::Figure blf = make_bar_label_full();
    compare("bar_label_full", blf, fonts);

    vpl::Figure figapi = make_figure_api();
    compare("figure_api", figapi, fonts);

    vpl::Figure small = make_ecdf_arrow_xerr();
    compare("ecdf_arrow_xerr", small, fonts);

    vpl::Figure knobs = make_styling_knobs();
    compare("styling_knobs", knobs, fonts);

    vpl::Figure packed = make_tight_layout();
    compare("tight_layout", packed, fonts, /*tight_layout=*/true);

    vpl::Figure aligned = make_align_ylabels();
    compare("align_ylabels", aligned, fonts);

    vpl::Figure ecdfc = make_ecdf_complementary();
    compare("ecdf_complementary", ecdfc, fonts);

    vpl::Figure ecdfw = make_ecdf_weighted();
    compare("ecdf_weighted", ecdfw, fonts);

    vpl::Figure ecdfh = make_ecdf_horizontal();
    compare("ecdf_horizontal", ecdfh, fonts);

    vpl::Figure fillwhere = make_fill_where();
    compare("fill_where", fillwhere, fonts);

    vpl::Figure fillstep = make_fill_step();
    compare("fill_step", fillstep, fonts);

    vpl::Figure sfill = make_stairs_fill();
    compare("stairs_fill", sfill, fonts);

    vpl::Figure shorz = make_stairs_horizontal();
    compare("stairs_horizontal", shorz, fonts);

    vpl::Figure louter = make_label_outer_ticks();
    compare("label_outer_ticks", louter, fonts);

    vpl::Figure stemS = make_stem_styled();
    compare("stem_styled", stemS, fonts);

    vpl::Figure nosticky = make_no_sticky_edges();
    compare("no_sticky_edges", nosticky, fonts);

    vpl::Figure atight = make_autoscale_tight();
    compare("autoscale_tight", atight, fonts);

    vpl::Figure gridm = make_grid_minor();
    compare("grid_minor", gridm, fonts);

    vpl::Figure suppos = make_sup_positioned();
    compare("sup_positioned", suppos, fonts);

    vpl::Figure figimg = make_figimage();
    compare("figimage", figimg, fonts);

    std::printf("\n%d compared, %d skipped, %d failed\n", checked, skipped, failed);

    if (failed != 0) {
        return 1;
    }
    if (checked == 0) {
        std::printf("\nNo references present, so nothing was actually verified against\n"
                    "matplotlib. Generate them with:  python tools/gen_references.py\n");
    }
    return 0;
}
