#include "figure.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <map>
#include <utility>

#include "delaunay.h"
#include "path.h"
#include "ticker.h"

namespace vpl
{
namespace
{

/* matplotlib's 'tab10' property cycle. */
const Color kTab10[] = {
    {0.121569, 0.466667, 0.705882, 1.0}, /* #1f77b4 */
    {1.000000, 0.498039, 0.054902, 1.0}, /* #ff7f0e */
    {0.172549, 0.627451, 0.172549, 1.0}, /* #2ca02c */
    {0.839216, 0.152941, 0.156863, 1.0}, /* #d62728 */
    {0.580392, 0.403922, 0.741176, 1.0}, /* #9467bd */
    {0.549020, 0.337255, 0.294118, 1.0}, /* #8c564b */
    {0.890196, 0.466667, 0.760784, 1.0}, /* #e377c2 */
    {0.498039, 0.498039, 0.498039, 1.0}, /* #7f7f7f */
    {0.737255, 0.741176, 0.133333, 1.0}, /* #bcbd22 */
    {0.090196, 0.745098, 0.811765, 1.0}, /* #17becf */
};

/* A log axis is plotted on log10 of the data, so anything non-positive has no
   place on it. */
constexpr double kLogFloor = 1e-300;

double apply_scale(double v, ScaleType scale)
{
    if (scale == ScaleType::Log) {
        return std::log10(v > kLogFloor ? v : kLogFloor);
    }
    return v;
}

} // namespace

const Color *tab10_palette()
{
    return kTab10;
}

std::size_t tab10_size()
{
    return sizeof(kTab10) / sizeof(kTab10[0]);
}

namespace
{

Color cycle_color(const std::vector<Color> &custom, std::size_t index)
{
    if (custom.empty()) {
        return kTab10[index % tab10_size()];
    }
    return custom[index % custom.size()];
}

} // namespace

Color Axes::next_line_color()
{
    return cycle_color(prop_cycle.colors, line_cycle++);
}

Color Axes::next_patch_color()
{
    return cycle_color(prop_cycle.colors, patch_cycle++);
}

namespace
{

/* A line that does NOT touch the cycle, for the artists that build Line2Ds
   internally and colour them themselves -- stem and boxplot. */
Line2D &push_line(std::vector<std::unique_ptr<Line2D>> &lines,
                  const double *x,
                  const double *y,
                  std::size_t n)
{
    auto line = std::make_unique<Line2D>();
    line->xdata.assign(x, x + n);
    line->ydata.assign(y, y + n);
    lines.push_back(std::move(line));
    return *lines.back();
}

} // namespace

Line2D &Axes::plot(const double *x, const double *y, std::size_t n)
{
    Line2D &line = push_line(lines, x, y, n);
    /* Captured before line_cycle advances so linestyle/marker/linewidth read at
       the same cycle position: matplotlib's cycler zips properties together. */
    const std::size_t idx = line_cycle;
    line.color = next_line_color();
    if (!prop_cycle.linestyles.empty()) {
        line.linestyle = prop_cycle.linestyles[idx % prop_cycle.linestyles.size()];
    }
    if (!prop_cycle.markers.empty()) {
        line.marker = prop_cycle.markers[idx % prop_cycle.markers.size()];
    }
    if (!prop_cycle.linewidths.empty()) {
        line.linewidth = prop_cycle.linewidths[idx % prop_cycle.linewidths.size()];
    }
    return line;
}

ScatterCollection &Axes::scatter(const double *x, const double *y, std::size_t n)
{
    auto sc = std::make_unique<ScatterCollection>();
    sc->xdata.assign(x, x + n);
    sc->ydata.assign(y, y + n);
    /* Patch cycle, not line cycle: matplotlib's scatter draws its default color
       from _get_patches_for_fill. */
    sc->color = next_patch_color();
    collections.push_back(std::move(sc));
    return *collections.back();
}

Line2D &Axes::errorbar(const double *x, const double *y, const double *yerr, std::size_t n)
{
    Line2D &line = plot(x, y, n);
    if (yerr != nullptr) {
        line.yerr.assign(yerr, yerr + n);
    }
    return line;
}

namespace
{

/* np.interp across two nodes: linear between them, clamped flat past each end,
   with the nodes taken in ascending-x order as np.interp requires. */
double interp_clamped(double x, double xp0, double fp0, double xp1, double fp1)
{
    if (xp1 < xp0) {
        std::swap(xp0, xp1);
        std::swap(fp0, fp1);
    }
    if (x <= xp0) {
        return fp0;
    }
    if (x >= xp1) {
        return fp1;
    }
    return fp0 + (x - xp0) / (xp1 - xp0) * (fp1 - fp0);
}

/* Crossing point at a filled region's edge when interpolate=True: the t where
   f1 - f2 = 0, and f1 at that t. `idx` is the boundary index (region start or
   stop, may be n); a boundary on the data's end has no crossing, so the endpoint
   stands. Mirrors FillBetweenPolyCollection._get_interpolating_points. */
std::pair<double, double> fill_interp_point(const double *t, const double *f1,
                                            const double *f2, std::size_t n, std::size_t idx)
{
    const std::size_t im1 = idx > 0 ? idx - 1 : 0;
    const std::size_t hi = std::min(idx + 1, n);
    if (hi - im1 < 2) {
        return {t[im1], f1[im1]};
    }
    const double d0 = f1[im1] - f2[im1];
    const double d1 = f1[idx] - f2[idx];
    const double root_t = interp_clamped(0.0, d0, t[im1], d1, t[idx]);
    const double root_f = interp_clamped(root_t, t[im1], f1[im1], t[idx], f1[idx]);
    return {root_t, root_f};
}

/* Build fill_between's polygons into `path`. `t` is the varying coordinate,
   f1/f2 the two curves; `where` (or null for all) masks which to fill. A band
   spans t[i]..t[i+1] only when both ends are in the mask, so each contiguous run
   is one polygon. `interpolate` puts region edges on the curves' crossing;
   `swap_xy` emits (f, t) instead of (t, f) for fill_betweenx. */
/* Turn a region's slice [a, b) into a staircase, mirroring cbook's
   pts_to_{pre,post,mid}step. FillStep::None (or a single point) copies through. */
void step_expand(FillStep step, const double *t, const double *f1, const double *f2,
                 std::size_t a, std::size_t b, std::vector<double> &wt,
                 std::vector<double> &wf1, std::vector<double> &wf2)
{
    wt.clear();
    wf1.clear();
    wf2.clear();
    const std::size_t k = b - a;
    if (k == 0) {
        return;
    }
    if (step == FillStep::None || k == 1) {
        for (std::size_t i = a; i < b; ++i) {
            wt.push_back(t[i]);
            wf1.push_back(f1[i]);
            wf2.push_back(f2[i]);
        }
        return;
    }
    if (step == FillStep::Mid) {
        const std::size_t m = 2 * k;
        wt.resize(m);
        wf1.resize(m);
        wf2.resize(m);
        wt[0] = t[a];
        wt[m - 1] = t[b - 1];
        for (std::size_t i = 0; i + 1 < k; ++i) {
            const double mid = (t[a + i] + t[a + i + 1]) / 2.0;
            wt[1 + 2 * i] = mid;
            wt[2 + 2 * i] = mid;
        }
        for (std::size_t i = 0; i < k; ++i) {
            wf1[2 * i] = wf1[2 * i + 1] = f1[a + i];
            wf2[2 * i] = wf2[2 * i + 1] = f2[a + i];
        }
        return;
    }
    const bool pre = (step == FillStep::Pre);
    const std::size_t m = 2 * k - 1;
    wt.resize(m);
    wf1.resize(m);
    wf2.resize(m);
    for (std::size_t i = 0; i < k; ++i) {
        wt[2 * i] = t[a + i];
        wf1[2 * i] = f1[a + i];
        wf2[2 * i] = f2[a + i];
    }
    for (std::size_t i = 0; i + 1 < k; ++i) {
        wt[2 * i + 1] = pre ? t[a + i] : t[a + i + 1];
        wf1[2 * i + 1] = pre ? f1[a + i + 1] : f1[a + i];
        wf2[2 * i + 1] = pre ? f2[a + i + 1] : f2[a + i];
    }
}

void build_fill_regions(Path &path, const double *t, const double *f1, const double *f2,
                        std::size_t n, const std::uint8_t *where, bool interpolate,
                        FillStep step, bool swap_xy)
{
    auto vert = [&](double tc, double fc, bool first) {
        const double vx = swap_xy ? fc : tc;
        const double vy = swap_xy ? tc : fc;
        if (first) {
            path.move_to(vx, vy);
        } else {
            path.line_to(vx, vy);
        }
    };

    std::vector<double> wt;
    std::vector<double> wf1;
    std::vector<double> wf2;

    std::size_t i = 0;
    while (i < n) {
        while (i < n && where != nullptr && where[i] == 0) {
            ++i;
        }
        if (i >= n) {
            break;
        }
        const std::size_t idx0 = i;
        while (i < n && (where == nullptr || where[i] != 0)) {
            ++i;
        }
        const std::size_t idx1 = i; /* exclusive */

        /* Step the region's slice, then close with the same start/end:
           interpolated on the original curves, or the stepped slice's own ends. */
        step_expand(step, t, f1, f2, idx0, idx1, wt, wf1, wf2);

        std::pair<double, double> start;
        std::pair<double, double> end;
        if (interpolate) {
            start = fill_interp_point(t, f1, f2, n, idx0);
            end = fill_interp_point(t, f1, f2, n, idx1);
        } else {
            start = {wt.front(), wf2.front()};
            end = {wt.back(), wf2.back()};
        }

        vert(start.first, start.second, true);
        for (std::size_t k = 0; k < wt.size(); ++k) {
            vert(wt[k], wf1[k], false);
        }
        vert(end.first, end.second, false);
        for (std::size_t k = wt.size(); k-- > 0;) {
            vert(wt[k], wf2[k], false);
        }
        path.close_poly();

        if (where == nullptr) {
            break; /* one region already covered everything */
        }
    }
}

} // namespace

PolyPatch &Axes::fill_between(const double *x, const double *y1, const double *y2,
                              std::size_t n, const std::uint8_t *where, bool interpolate,
                              FillStep step)
{
    auto patch = std::make_unique<PolyPatch>();

    /* One polygon per contiguous filled region -- matplotlib's
       FillBetweenPolyCollection. */
    patch->path.vertices.reserve(4 * n + 2);
    build_fill_regions(patch->path, x, y1, y2, n, where, interpolate, step, /*swap_xy=*/false);

    /* Opaque, matching fill_between; callers wanting a translucent band set it. */
    patch->facecolor = next_patch_color();

    patches.push_back(std::move(patch));
    return *patches.back();
}

PolyPatch &Axes::bar(const double *x,
                     const double *height,
                     std::size_t n,
                     double width,
                     const double *bottom)
{
    auto patch = std::make_unique<PolyPatch>();

    const double half = width / 2.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double b = bottom != nullptr ? bottom[i] : 0.0;
        /* Each bar is its own subpath; CLOSEPOLY separates them so a single
           patch can hold the whole series. */
        patch->path.move_to(x[i] - half, b);
        patch->path.line_to(x[i] + half, b);
        patch->path.line_to(x[i] + half, b + height[i]);
        patch->path.line_to(x[i] - half, b + height[i]);
        patch->path.close_poly();
    }

    patch->facecolor = next_patch_color();

    /* Bars sit on their baseline, not 5% above it: matplotlib gives each bar a
       sticky edge at its own bottom, not at zero (matters for stacks). */
    for (std::size_t i = 0; i < n; ++i) {
        const double b = bottom != nullptr ? bottom[i] : 0.0;
        if (std::find(sticky_y.begin(), sticky_y.end(), b) == sticky_y.end()) {
            sticky_y.push_back(b);
        }
    }

    patches.push_back(std::move(patch));
    return *patches.back();
}

PolyPatch &Axes::hist(const double *values, std::size_t n, std::size_t bins, bool density)
{
    HistOptions options;
    options.bins = bins;
    options.density = density;
    const double *const datasets[1] = {values};
    const std::size_t counts[1] = {n};
    return *hist(datasets, counts, nullptr, 1, options).front();
}

namespace
{

/* numpy's linspace: k * step + start, with the last edge pinned to `stop`. */
std::vector<double> linspace_edges(double start, double stop, std::size_t bins)
{
    std::vector<double> edges(bins + 1);
    const double step = (stop - start) / static_cast<double>(bins);
    for (std::size_t k = 0; k <= bins; ++k) {
        edges[k] = static_cast<double>(k) * step + start;
    }
    edges[bins] = stop;
    return edges;
}

/*
 * numpy.histogram over the given edges, unweighted or weighted. `uniform`
 * selects numpy's equal-bin fast path (what an integer `bins` takes), which
 * computes the index arithmetically and then nudges it by the edges; otherwise
 * it is a search, as for explicit edges. Both treat the bins as half-open
 * except the last, which includes its right edge, and drop anything outside.
 */
std::vector<double> numpy_histogram(const double *x, std::size_t n, const double *w,
                                    const std::vector<double> &edges, bool uniform)
{
    const std::size_t nbins = edges.size() - 1;
    std::vector<double> counts(nbins, 0.0);
    const double first = edges.front();
    const double last = edges.back();

    for (std::size_t i = 0; i < n; ++i) {
        const double v = x[i];
        if (!(v >= first && v <= last)) {
            continue; /* out of range, or NaN */
        }
        std::size_t k = 0;
        if (uniform) {
            const double f = (v - first) / (last - first) * static_cast<double>(nbins);
            std::ptrdiff_t idx = static_cast<std::ptrdiff_t>(f);
            if (idx == static_cast<std::ptrdiff_t>(nbins)) {
                --idx;
            }
            if (v < edges[static_cast<std::size_t>(idx)]) {
                --idx;
            }
            if (v >= edges[static_cast<std::size_t>(idx) + 1] &&
                idx != static_cast<std::ptrdiff_t>(nbins) - 1) {
                ++idx;
            }
            k = static_cast<std::size_t>(idx);
        } else if (v == last) {
            /* The last edge is inclusive. */
            k = nbins - 1;
        } else {
            /* Elsewhere the bin starts at the last edge at or below v. */
            k = static_cast<std::size_t>(std::upper_bound(edges.begin(), edges.end(), v) -
                                         edges.begin()) -
                1;
        }
        counts[k] += w != nullptr ? w[i] : 1.0;
    }
    return counts;
}

} // namespace

std::vector<PolyPatch *> Axes::hist(const double *const *datasets,
                                    const std::size_t *counts,
                                    const double *const *weights,
                                    std::size_t nx,
                                    const HistOptions &opt,
                                    std::vector<double> *out_tops,
                                    std::vector<double> *out_edges)
{
    std::vector<PolyPatch *> result;
    if (nx == 0) {
        return result;
    }

    const bool stacked = opt.stacked || opt.type == HistType::BarStacked;

    /* hist takes its colours from the LINE cycle, one per dataset, before any
       drawing -- not the patch cycle bar() would use. */
    std::vector<Color> colors;
    for (std::size_t i = 0; i < nx; ++i) {
        colors.push_back(next_line_color());
    }

    /* The bin edges every dataset shares. */
    std::vector<double> edges;
    bool uniform = false;
    if (opt.edges != nullptr && opt.edge_count >= 2) {
        edges.assign(opt.edges, opt.edges + opt.edge_count);
    } else {
        const std::size_t bins = opt.bins == 0 ? 10 : opt.bins;
        double lo = 0.0;
        double hi = 1.0;
        if (opt.has_range) {
            lo = opt.range_lo;
            hi = opt.range_hi;
        } else {
            /* matplotlib's own nan-ignoring range over every dataset. */
            double mn = std::numeric_limits<double>::infinity();
            double mx = -mn;
            for (std::size_t d = 0; d < nx; ++d) {
                for (std::size_t i = 0; i < counts[d]; ++i) {
                    const double v = datasets[d][i];
                    if (std::isnan(v)) {
                        continue;
                    }
                    mn = std::min(mn, v);
                    mx = std::max(mx, v);
                }
            }
            if (mn <= mx) {
                lo = mn;
                hi = mx;
            }
        }
        if (lo == hi) {
            /* numpy widens a degenerate range rather than dividing by zero. */
            lo -= 0.5;
            hi += 0.5;
        }
        edges = linspace_edges(lo, hi, bins);
        /* With one dataset numpy bins on the integer count (its fast path);
           with several, matplotlib computes the edges first and hands the
           array over, which takes the search path. */
        uniform = nx == 1;
    }
    const std::size_t nbins = edges.size() - 1;
    std::vector<double> db(nbins);
    for (std::size_t k = 0; k < nbins; ++k) {
        db[k] = edges[k + 1] - edges[k];
    }

    /* tops[d][k], as matplotlib's (n_datasets, n_bins) array. */
    std::vector<std::vector<double>> tops(nx);
    for (std::size_t d = 0; d < nx; ++d) {
        const double *w = weights != nullptr ? weights[d] : nullptr;
        tops[d] = numpy_histogram(datasets[d], counts[d], w, edges, uniform);
        if (opt.density && !stacked) {
            double total = 0.0;
            for (double v : tops[d]) {
                total += v;
            }
            for (std::size_t k = 0; k < nbins; ++k) {
                tops[d][k] = tops[d][k] / db[k] / total;
            }
        }
    }

    if (stacked) {
        for (std::size_t d = 1; d < nx; ++d) {
            for (std::size_t k = 0; k < nbins; ++k) {
                tops[d][k] += tops[d - 1][k];
            }
        }
        if (opt.density) {
            double total = 0.0;
            for (double v : tops[nx - 1]) {
                total += v;
            }
            for (std::size_t d = 0; d < nx; ++d) {
                for (std::size_t k = 0; k < nbins; ++k) {
                    tops[d][k] = (tops[d][k] / db[k]) / total;
                }
            }
        }
    }

    if (opt.cumulative != 0) {
        for (std::size_t d = 0; d < nx; ++d) {
            std::vector<double> &t = tops[d];
            if (opt.density) {
                for (std::size_t k = 0; k < nbins; ++k) {
                    t[k] *= db[k];
                }
            }
            if (opt.cumulative > 0) {
                for (std::size_t k = 1; k < nbins; ++k) {
                    t[k] += t[k - 1];
                }
            } else {
                for (std::size_t k = nbins - 1; k-- > 0;) {
                    t[k] += t[k + 1];
                }
            }
        }
    }

    if (opt.log) {
        (opt.horizontal ? xscale : yscale) = ScaleType::Log;
    }

    std::vector<double> &sticky = opt.horizontal ? sticky_x : sticky_y;
    auto add_sticky = [&sticky](double v) {
        if (std::find(sticky.begin(), sticky.end(), v) == sticky.end()) {
            sticky.push_back(v);
        }
    };

    if (opt.type == HistType::Bar || opt.type == HistType::BarStacked) {
        double dr = 1.0;
        if (!std::isnan(opt.rwidth)) {
            dr = std::clamp(opt.rwidth, 0.0, 1.0);
        } else if (nx > 1 && !stacked) {
            dr = 0.8;
        }

        std::vector<double> width(nbins);
        std::vector<double> boffset(nbins);
        std::vector<double> dw(nbins, 0.0);
        for (std::size_t k = 0; k < nbins; ++k) {
            const double totwidth = db[k];
            if (!stacked) {
                width[k] = dr * totwidth / static_cast<double>(nx);
                dw[k] = width[k];
                boffset[k] = -0.5 * dr * totwidth * (1.0 - 1.0 / static_cast<double>(nx));
            } else {
                width[k] = dr * totwidth;
                boffset[k] = 0.0;
            }
            if (opt.align == HistAlign::Mid) {
                boffset[k] += 0.5 * totwidth;
            } else if (opt.align == HistAlign::Right) {
                boffset[k] += totwidth;
            }
        }

        std::vector<double> bottom(nbins, opt.bottom);
        for (std::size_t d = 0; d < nx; ++d) {
            auto patch = std::make_unique<PolyPatch>();
            for (std::size_t k = 0; k < nbins; ++k) {
                const double height = stacked ? tops[d][k] - bottom[k] : tops[d][k];
                /* bar(align='center'): the bar is centred on its position. */
                const double c = edges[k] + boffset[k];
                const double a0 = c - 0.5 * width[k];
                const double a1 = a0 + width[k];
                const double v0 = bottom[k];
                const double v1 = bottom[k] + height;
                if (opt.horizontal) {
                    patch->path.move_to(v0, a0);
                    patch->path.line_to(v1, a0);
                    patch->path.line_to(v1, a1);
                    patch->path.line_to(v0, a1);
                } else {
                    patch->path.move_to(a0, v0);
                    patch->path.line_to(a1, v0);
                    patch->path.line_to(a1, v1);
                    patch->path.line_to(a0, v1);
                }
                patch->path.close_poly();
                boffset[k] += dw[k];
            }
            patch->facecolor = colors[d];
            /* Only the lowest bars keep their sticky edge, so margins can still
               expand past the bars stacked on top. */
            if (d == 0) {
                for (double b : bottom) {
                    add_sticky(b);
                }
            }
            if (stacked) {
                bottom = tops[d];
            }
            patches.push_back(std::move(patch));
            result.push_back(patches.back().get());
        }
    } else {
        /* The outline, as matplotlib builds it: the bin edges doubled up into
           a staircase over the top, then the same x values back along the
           bottom, 4 * len(bins) - 3 points in all. */
        const std::size_t nb = edges.size();
        const std::size_t total = 4 * nb - 3;
        std::vector<double> sx(total, 0.0);
        std::vector<double> sy(total, 0.0);
        for (std::size_t k = 0; k < nb; ++k) {
            sx[2 * k] = edges[k];
        }
        for (std::size_t k = 0; k + 1 < nb; ++k) {
            sx[2 * k + 1] = edges[k];
        }
        for (std::size_t j = 0; j < 2 * nb - 2; ++j) {
            sx[2 * nb - 1 + j] = sx[2 * nb - 2 - j];
        }
        for (std::size_t j = 1; j < 2 * nb - 1; j += 2) {
            sy[j] = opt.bottom;
            sy[j + 1] = opt.bottom;
        }
        for (std::size_t j = 0; j < 2 * nb - 2; ++j) {
            sy[2 * nb - 1 + j] = sy[2 * nb - 2 - j];
        }
        const double shift = 0.5 * (edges[1] - edges[0]);
        if (opt.align == HistAlign::Left) {
            for (double &v : sx) {
                v -= shift;
            }
        } else if (opt.align == HistAlign::Right) {
            for (double &v : sx) {
                v += shift;
            }
        }

        const bool fill = opt.type == HistType::StepFilled;
        std::vector<std::vector<double>> ys;
        for (std::size_t d = 0; d < nx; ++d) {
            if (stacked) {
                /* The previous polygon's top becomes this one's bottom. */
                for (std::size_t j = 0; j < 2 * nb - 2; ++j) {
                    sy[2 * nb - 1 + j] = sy[2 * nb - 2 - j];
                }
            }
            for (std::size_t k = 0; k + 1 < nb; ++k) {
                const double t = tops[d][k] + opt.bottom;
                sy[2 * k + 1] = t;
                sy[2 * k + 2] = t;
            }
            sy[0] = sy[total - 1];
            ys.push_back(sy);
        }

        /* step is open (it stops after the top run); stepfilled drops the
           redundant last point and closes. */
        const std::size_t count = fill ? total - 1 : 2 * nb;
        std::vector<PolyPatch *> made(nx, nullptr);
        /* Added in reverse so that, stacked, the lower datasets draw on top. */
        for (std::size_t d = nx; d-- > 0;) {
            auto patch = std::make_unique<PolyPatch>();
            std::vector<double> px(sx.begin(), sx.begin() + static_cast<std::ptrdiff_t>(count));
            std::vector<double> py(ys[d].begin(),
                                   ys[d].begin() + static_cast<std::ptrdiff_t>(count));
            if (opt.horizontal) {
                std::swap(px, py);
            }
            std::size_t m = count;
            /* Polygon: an open outline that ends where it began drops the
               repeat; a closed one treats a repeated end as the close. */
            if (m > 2 && px.front() == px[m - 1] && py.front() == py[m - 1]) {
                --m;
            }
            patch->path.move_to(px[0], py[0]);
            for (std::size_t j = 1; j < m; ++j) {
                patch->path.line_to(px[j], py[j]);
            }
            if (fill) {
                patch->path.close_poly();
                patch->facecolor = colors[d];
            } else {
                patch->facecolor = Color::none();
                patch->edgecolor = colors[d];
                patch->linewidth = 1.0; /* patch.linewidth */
                patch->line_tier = true;
            }
            /* fill() draws a colour from the patch cycle even though hist
               overrides it, so the cycle still advances. */
            (void)next_patch_color();
            patches.push_back(std::move(patch));
            made[d] = patches.back().get();
        }
        add_sticky(0.0);
        result = made;
    }

    if (out_edges != nullptr) {
        *out_edges = edges;
    }
    if (out_tops != nullptr) {
        out_tops->clear();
        for (const std::vector<double> &t : tops) {
            out_tops->insert(out_tops->end(), t.begin(), t.end());
        }
    }
    return result;
}

ImageGrid &Axes::imshow(const double *values, std::size_t rows, std::size_t cols)
{
    auto img = std::make_unique<ImageGrid>();
    img->values.assign(values, values + rows * cols);
    img->rows = rows;
    img->cols = cols;
    /* imshow's default extent puts cell centres on integer indices, so the
       drawn area runs from -0.5 to n-0.5. */
    img->x0 = -0.5;
    img->x1 = static_cast<double>(cols) - 0.5;
    img->y0 = static_cast<double>(rows) - 0.5;
    img->y1 = -0.5;

    /* Pin the view to the image extent with square pixels. y limits are inverted
       on purpose: origin='upper' puts row 0 at the top by running the axis
       backwards, so y tick labels count downwards. */
    set_xlim(img->x0, img->x1);
    set_ylim(img->y0, img->y1);
    aspect = 1.0; /* image.aspect defaults to 'equal' */

    images.push_back(std::move(img));
    return *images.back();
}

ImageGrid &Axes::pcolormesh(const double *values, std::size_t rows, std::size_t cols,
                            const double *xedges, const double *yedges)
{
    auto img = std::make_unique<ImageGrid>();
    img->values.assign(values, values + rows * cols);
    img->rows = rows;
    img->cols = cols;
    img->xedges.assign(xedges, xedges + cols + 1);
    img->yedges.assign(yedges, yedges + rows + 1);

    /* Row 0 at the bottom, opposite imshow: a mesh is indexed like a plot, not a
       matrix. Extent still runs y0 -> y1 for the drawing code. */
    img->x0 = img->xedges.front();
    img->x1 = img->xedges.back();
    img->y0 = img->yedges.front();
    img->y1 = img->yedges.back();

    /* Ends exactly at the outermost edges: no margin, no forced aspect (unlike
       imshow). */
    tight_view = true;

    images.push_back(std::move(img));
    return *images.back();
}

ImageGrid &Axes::matshow(const double *values, std::size_t rows, std::size_t cols)
{
    ImageGrid &img = imshow(values, rows, cols);

    /* Column indices along the top (marks at the bottom too), both axes integer. */
    x_ticks_top = true;
    x_ticks_both = true;
    integer_x_ticks = true;
    integer_y_ticks = true;
    x_tick_steps = {1.0, 2.0, 5.0, 10.0};
    y_tick_steps = x_tick_steps;

    return img;
}

ImageGrid &Axes::spy(const double *values, std::size_t rows, std::size_t cols)
{
    /* The pattern, not the values: non-zero -> 1, else 0, on a fixed 0..1 scale. */
    std::vector<double> mask(rows * cols);
    for (std::size_t i = 0; i < rows * cols; ++i) {
        mask[i] = (values[i] != 0.0 && std::isfinite(values[i])) ? 1.0 : 0.0;
    }

    ImageGrid &img = matshow(mask.data(), rows, cols);
    img.cmap = Colormap::Binary;
    img.has_vlimits = true;
    img.vmin = 0.0;
    img.vmax = 1.0;
    return img;
}

void Axes::hexbin(const double *x, const double *y, std::size_t n, std::size_t gridsize)
{
    HexbinOptions options;
    options.nx = gridsize;
    hexbin(x, y, n, options);
}

namespace
{

double reduce_values(std::vector<double> v, HexReduce how)
{
    if (v.empty()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    double sum = 0.0;
    for (double d : v) {
        sum += d;
    }
    switch (how) {
        case HexReduce::Sum: return sum;
        case HexReduce::Max: return *std::max_element(v.begin(), v.end());
        case HexReduce::Min: return *std::min_element(v.begin(), v.end());
        case HexReduce::Median: {
            std::sort(v.begin(), v.end());
            const std::size_t m = v.size() / 2;
            return v.size() % 2 == 1 ? v[m] : 0.5 * (v[m - 1] + v[m]);
        }
        case HexReduce::Std: {
            const double mean = sum / static_cast<double>(v.size());
            double ss = 0.0;
            for (double d : v) {
                ss += (d - mean) * (d - mean);
            }
            return std::sqrt(ss / static_cast<double>(v.size())); /* numpy's ddof=0 */
        }
        case HexReduce::Mean:
        default: return sum / static_cast<double>(v.size());
    }
}

} // namespace

void Axes::hexbin(const double *x_data, const double *y_data, std::size_t n,
                  const HexbinOptions &opt)
{
    if (n == 0 || opt.nx == 0) {
        return;
    }

    /* Binning runs on tx, ty: the data, or its log10 on a log axis. */
    std::vector<double> lx, ly;
    if (opt.xlog) {
        lx.resize(n);
        for (std::size_t i = 0; i < n; ++i) {
            lx[i] = std::log10(x_data[i]);
        }
    }
    if (opt.ylog) {
        ly.resize(n);
        for (std::size_t i = 0; i < n; ++i) {
            ly[i] = std::log10(y_data[i]);
        }
    }
    const double *x = opt.xlog ? lx.data() : x_data;
    const double *y = opt.ylog ? ly.data() : y_data;

    double xmin = std::numeric_limits<double>::infinity();
    double xmax = -xmin;
    double ymin = xmin;
    double ymax = xmax;
    if (opt.has_extent) {
        xmin = opt.extent[0];
        xmax = opt.extent[1];
        ymin = opt.extent[2];
        ymax = opt.extent[3];
    } else {
        for (std::size_t i = 0; i < n; ++i) {
            if (!std::isfinite(x[i]) || !std::isfinite(y[i])) {
                continue;
            }
            xmin = std::min(xmin, x[i]);
            xmax = std::max(xmax, x[i]);
            ymin = std::min(ymin, y[i]);
            ymax = std::max(ymax, y[i]);
        }
    }
    if (!std::isfinite(xmin) || !std::isfinite(ymin)) {
        return;
    }

    const long nx = static_cast<long>(opt.nx);
    /* sqrt(3) keeps the hexagons regular rather than squashed. */
    long ny = opt.ny != 0 ? static_cast<long>(opt.ny) : static_cast<long>(nx / std::sqrt(3.0));
    if (ny < 1) {
        ny = 1;
    }

    /* matplotlib pads the extent by a part in a billion so a point sitting
       exactly on xmax does not round into a bin that does not exist. */
    const double padding = 1e-9 * (xmax - xmin);
    xmin -= padding;
    xmax += padding;

    const double sx = (xmax - xmin) / static_cast<double>(nx);
    const double sy = (ymax - ymin) / static_cast<double>(ny);
    if (!(sx > 0.0) || !(sy > 0.0)) {
        return;
    }

    /* Two lattices: grid 1 on the cell corners, grid 2 offset by half a cell. */
    const long nx1 = nx + 1, ny1 = ny + 1;
    const long nx2 = nx, ny2 = ny;

    std::vector<double> counts1(static_cast<std::size_t>(nx1 * ny1), 0.0);
    std::vector<double> counts2(static_cast<std::size_t>(nx2 * ny2), 0.0);
    /* With C, the values that land in each hexagon, to reduce afterwards. */
    std::vector<std::vector<double>> cs1;
    std::vector<std::vector<double>> cs2;
    if (opt.C != nullptr) {
        cs1.resize(counts1.size());
        cs2.resize(counts2.size());
    }

    for (std::size_t i = 0; i < n; ++i) {
        if (!std::isfinite(x[i]) || !std::isfinite(y[i])) {
            continue;
        }
        const double ix = (x[i] - xmin) / sx;
        const double iy = (y[i] - ymin) / sy;

        const long ix1 = static_cast<long>(std::lround(ix));
        const long iy1 = static_cast<long>(std::lround(iy));
        const long ix2 = static_cast<long>(std::floor(ix));
        const long iy2 = static_cast<long>(std::floor(iy));

        /* The 3x weight on dy makes the winning cell a hexagon, not a rectangle. */
        const double d1 = (ix - ix1) * (ix - ix1) + 3.0 * (iy - iy1) * (iy - iy1);
        const double d2 = (ix - ix2 - 0.5) * (ix - ix2 - 0.5) +
                          3.0 * (iy - iy2 - 0.5) * (iy - iy2 - 0.5);

        if (d1 < d2) {
            if (ix1 >= 0 && ix1 < nx1 && iy1 >= 0 && iy1 < ny1) {
                const auto k = static_cast<std::size_t>(ix1 * ny1 + iy1);
                counts1[k] += 1.0;
                if (opt.C != nullptr) {
                    cs1[k].push_back(opt.C[i]);
                }
            }
        } else {
            if (ix2 >= 0 && ix2 < nx2 && iy2 >= 0 && iy2 < ny2) {
                const auto k = static_cast<std::size_t>(ix2 * ny2 + iy2);
                counts2[k] += 1.0;
                if (opt.C != nullptr) {
                    cs2[k].push_back(opt.C[i]);
                }
            }
        }
    }

    /* accum, as matplotlib builds it: counts (dropping those under mincnt),
       or with C each hexagon's reduction, needing at least mincnt (default 1)
       points. NaN marks a hexagon that is not drawn. */
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const auto finish = [&](std::vector<double> &counts, std::vector<std::vector<double>> &cs) {
        for (std::size_t k = 0; k < counts.size(); ++k) {
            if (opt.C != nullptr) {
                const std::size_t need = opt.mincnt == 0 ? 1 : opt.mincnt;
                counts[k] = cs[k].size() >= need ? reduce_values(std::move(cs[k]), opt.reduce) : nan;
            } else if (opt.mincnt != 0 && counts[k] < static_cast<double>(opt.mincnt)) {
                counts[k] = nan;
            }
        }
    };
    finish(counts1, cs1);
    finish(counts2, cs2);

    double amin = std::numeric_limits<double>::infinity();
    double amax = -amin;
    for (const std::vector<double> *acc : {&counts1, &counts2}) {
        for (double c : *acc) {
            if (!std::isnan(c)) {
                amin = std::min(amin, c);
                amax = std::max(amax, c);
            }
        }
    }
    if (!(amin <= amax)) {
        return;
    }

    /* bins=N: replace each value by the index of the value bin it falls in --
       bins.searchsorted(accum) over N - 1 interior edges. */
    if (opt.bins > 0 && !opt.log_bins) {
        const std::size_t b = opt.bins - 1;
        std::vector<double> edges;
        for (std::size_t k = 0; k < b; ++k) {
            edges.push_back(amin + (amax - amin) * static_cast<double>(k) / static_cast<double>(b));
        }
        for (std::vector<double> *acc : {&counts1, &counts2}) {
            for (double &c : *acc) {
                if (!std::isnan(c)) {
                    c = static_cast<double>(std::lower_bound(edges.begin(), edges.end(), c) -
                                            edges.begin());
                }
            }
        }
        amin = std::numeric_limits<double>::infinity();
        amax = -amin;
        for (const std::vector<double> *acc : {&counts1, &counts2}) {
            for (double c : *acc) {
                if (!std::isnan(c)) {
                    amin = std::min(amin, c);
                    amax = std::max(amax, c);
                }
            }
        }
    }

    /* The norm autoscales over the drawn values unless vmin/vmax are given.
       Under bins='log' it is a LogNorm, which ignores non-positive values when
       autoscaling and paints them in the colormap's (transparent) bad colour. */
    double vmin = amin;
    double vmax = amax;
    if (opt.log_bins) {
        vmin = std::numeric_limits<double>::infinity();
        vmax = -vmin;
        for (const std::vector<double> *acc : {&counts1, &counts2}) {
            for (double c : *acc) {
                if (c > 0.0) {
                    vmin = std::min(vmin, c);
                    vmax = std::max(vmax, c);
                }
            }
        }
    }
    if (opt.has_vlimits) {
        vmin = opt.vmin;
        vmax = opt.vmax;
    }
    const auto color_of = [&](double v) {
        if (opt.log_bins) {
            if (!(v > 0.0) || !(vmin > 0.0)) {
                return Color::none();
            }
            return colormap_lookup(opt.cmap,
                                   normalize(std::log10(v), std::log10(vmin), std::log10(vmax)));
        }
        return colormap_lookup(opt.cmap, normalize(v, vmin, vmax));
    };

    /* Unit hexagon, scaled by (sx, sy/3) -- matplotlib's polygon table. */
    static const double kHex[6][2] = {
        {0.5, -0.5}, {0.5, 0.5}, {0.0, 1.0}, {-0.5, 0.5}, {-0.5, -0.5}, {0.0, -1.0}};

    auto emit = [&](double cx, double cy, double count) {
        if (std::isnan(count)) {
            return; /* mincnt (or an empty hexagon under C) drops it */
        }
        auto patch = std::make_unique<PolyPatch>();
        for (int k = 0; k < 6; ++k) {
            /* On a log axis the hexagon is regular in log space: 10 ** each
               vertex, which the axis maps back to straight edges. */
            double px = cx + kHex[k][0] * sx;
            double py = cy + kHex[k][1] * (sy / 3.0);
            if (opt.xlog) {
                px = std::pow(10.0, px);
            }
            if (opt.ylog) {
                py = std::pow(10.0, py);
            }
            if (k == 0) {
                patch->path.move_to(px, py);
            } else {
                patch->path.line_to(px, py);
            }
        }
        patch->path.close_poly();
        patch->facecolor = color_of(count);
        /*
         * Stroked in its own fill colour, which is hexbin's edgecolors="face".
         * Without it the antialiased boundary between two abutting hexagons is
         * covered by neither, and the honeycomb comes out veined with pale
         * seams -- the entire remaining difference against matplotlib was on
         * those edges and nowhere else.
         */
        patch->edgecolor = patch->facecolor;
        patch->linewidth = 1.0; /* patch.linewidth */
        patch->join = JoinStyle::Round; /* Collection default */
        patches.push_back(std::move(patch));
    };

    for (long i = 0; i < nx1; ++i) {
        for (long j = 0; j < ny1; ++j) {
            emit(xmin + i * sx, ymin + j * sy,
                 counts1[static_cast<std::size_t>(i * ny1 + j)]);
        }
    }
    for (long i = 0; i < nx2; ++i) {
        for (long j = 0; j < ny2; ++j) {
            emit(xmin + (i + 0.5) * sx, ymin + (j + 0.5) * sy,
                 counts2[static_cast<std::size_t>(i * ny2 + j)]);
        }
    }

    /* View is the bin extent plus the usual 5% margin (matplotlib's tight
       autoscale loses to the following add_collection). Limits are pinned rather
       than autoscaled so the hexagons' half-cell overhang does not widen them. */
    const double mx = 0.05 * (xmax - xmin);
    const double my = 0.05 * (ymax - ymin);
    /* On a log axis the margin is taken in log space, as autoscale does. */
    const auto out = [](bool log, double v) { return log ? std::pow(10.0, v) : v; };
    if (opt.xlog) {
        xscale = ScaleType::Log;
    }
    if (opt.ylog) {
        yscale = ScaleType::Log;
    }
    set_xlim(out(opt.xlog, xmin - mx), out(opt.xlog, xmax + mx));
    set_ylim(out(opt.ylog, ymin - my), out(opt.ylog, ymax + my));

    if (!opt.marginals) {
        return;
    }

    /* marginals: nx bars along x and 2 * ny along y, over the bin extent in
       data units (geomspace on a log axis), each the reduction of C -- or of
       ones, so a plain count gives every bar the value 1. A point goes to bin
       searchsorted(edges, z) - 1, which drops one sitting on the first edge.
       Without bins='log' each strip has its own Normalize, autoscaled over
       its own values; with it they share the hexagons' LogNorm. */
    for (const bool along_x : {true, false}) {
        const bool log = along_x ? opt.xlog : opt.ylog;
        const double zmin = out(log, along_x ? xmin : ymin);
        const double zmax = out(log, along_x ? xmax : ymax);
        const std::size_t nb = along_x ? static_cast<std::size_t>(nx)
                                       : 2 * static_cast<std::size_t>(ny);
        const double *z = along_x ? x_data : y_data;

        std::vector<double> edges(nb + 1);
        if (log) {
            const double l0 = std::log10(zmin), l1 = std::log10(zmax);
            const double step = (l1 - l0) / static_cast<double>(nb);
            for (std::size_t k = 0; k <= nb; ++k) {
                edges[k] = std::pow(10.0, l0 + static_cast<double>(k) * step);
            }
        } else {
            const double step = (zmax - zmin) / static_cast<double>(nb);
            for (std::size_t k = 0; k <= nb; ++k) {
                edges[k] = zmin + static_cast<double>(k) * step;
            }
        }
        edges.front() = zmin;
        edges.back() = zmax;

        std::vector<std::vector<double>> members(nb);
        for (std::size_t i = 0; i < n; ++i) {
            const auto pos = std::lower_bound(edges.begin(), edges.end(), z[i]) - edges.begin();
            if (pos >= 1 && static_cast<std::size_t>(pos) <= nb) {
                members[static_cast<std::size_t>(pos) - 1].push_back(opt.C != nullptr ? opt.C[i]
                                                                                    : 1.0);
            }
        }
        std::vector<double> values(nb, nan);
        double bmin = std::numeric_limits<double>::infinity();
        double bmax = -bmin;
        for (std::size_t k = 0; k < nb; ++k) {
            if (!members[k].empty()) {
                values[k] = reduce_values(std::move(members[k]), opt.reduce);
                if (!std::isnan(values[k])) {
                    bmin = std::min(bmin, values[k]);
                    bmax = std::max(bmax, values[k]);
                }
            }
        }
        for (std::size_t k = 0; k < nb; ++k) {
            if (std::isnan(values[k])) {
                continue;
            }
            auto bar = std::make_unique<PolyPatch>();
            const double a = edges[k], b = edges[k + 1];
            if (along_x) {
                bar->path.move_to(a, 0.0);
                bar->path.line_to(a, 0.05);
                bar->path.line_to(b, 0.05);
                bar->path.line_to(b, 0.0);
                bar->y_in_axes = true;
            } else {
                bar->path.move_to(0.0, a);
                bar->path.line_to(0.05, a);
                bar->path.line_to(0.05, b);
                bar->path.line_to(0.0, b);
                bar->x_in_axes = true;
            }
            bar->path.close_poly();
            bar->facecolor = opt.log_bins
                                 ? color_of(values[k])
                                 : colormap_lookup(opt.cmap, normalize(values[k], bmin, bmax));
            bar->edgecolor = bar->facecolor; /* edgecolors="face" */
            bar->linewidth = 1.0;
            bar->join = JoinStyle::Round;
            bar->counts_towards_limits = false;
            patches.push_back(std::move(bar));
        }
    }
}

void Axes::hist2d(const double *x, const double *y, std::size_t n,
                  std::size_t xbins, std::size_t ybins)
{
    if (n == 0 || xbins == 0 || ybins == 0) {
        return;
    }

    double xlo = std::numeric_limits<double>::infinity();
    double xhi = -xlo;
    double ylo = xlo;
    double yhi = xhi;
    for (std::size_t i = 0; i < n; ++i) {
        if (!std::isfinite(x[i]) || !std::isfinite(y[i])) {
            continue;
        }
        xlo = std::min(xlo, x[i]);
        xhi = std::max(xhi, x[i]);
        ylo = std::min(ylo, y[i]);
        yhi = std::max(yhi, y[i]);
    }
    if (!std::isfinite(xlo) || !std::isfinite(ylo)) {
        return;
    }
    /* numpy widens a degenerate range rather than dividing by zero. */
    if (xlo == xhi) {
        xlo -= 0.5;
        xhi += 0.5;
    }
    if (ylo == yhi) {
        ylo -= 0.5;
        yhi += 0.5;
    }

    std::vector<double> xedges(xbins + 1);
    std::vector<double> yedges(ybins + 1);
    for (std::size_t i = 0; i <= xbins; ++i) {
        xedges[i] = xlo + (xhi - xlo) * static_cast<double>(i) / static_cast<double>(xbins);
    }
    for (std::size_t i = 0; i <= ybins; ++i) {
        yedges[i] = ylo + (yhi - ylo) * static_cast<double>(i) / static_cast<double>(ybins);
    }

    /* Row-major with y as the row, which is the transpose of what histogram2d
       returns -- hist2d passes H.T to the mesh for exactly this reason. */
    std::vector<double> counts(ybins * xbins, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        if (!std::isfinite(x[i]) || !std::isfinite(y[i])) {
            continue;
        }
        std::size_t cx = static_cast<std::size_t>((x[i] - xlo) / (xhi - xlo) *
                                                  static_cast<double>(xbins));
        std::size_t cy = static_cast<std::size_t>((y[i] - ylo) / (yhi - ylo) *
                                                  static_cast<double>(ybins));
        /* The topmost edge belongs to the last bin, not to one past it. */
        cx = std::min(cx, xbins - 1);
        cy = std::min(cy, ybins - 1);
        counts[cy * xbins + cx] += 1.0;
    }

    pcolormesh(counts.data(), ybins, xbins, xedges.data(), yedges.data());
}

void Axes::pie(const double *values, std::size_t n, const PieOptions &opt)
{
    double sum = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        sum += values[i];
    }
    if (!(sum > 0.0)) {
        return;
    }

    pie_wedges.clear();
    std::size_t color_index = 0;
    double theta1 = opt.startangle / 360.0; /* in turns, as matplotlib keeps it */
    for (std::size_t i = 0; i < n; ++i) {
        const double frac = opt.normalize ? values[i] / sum : values[i];
        const double theta2 = opt.counterclock ? theta1 + frac : theta1 - frac;
        const double thetam = 2.0 * 3.14159265358979323846 * 0.5 * (theta1 + theta2);
        const double expl = opt.explode != nullptr ? opt.explode[i] : 0.0;
        const double cx = opt.center_x + expl * std::cos(thetam);
        const double cy = opt.center_y + expl * std::sin(thetam);
        const double t1 = 360.0 * std::min(theta1, theta2);
        const double t2 = 360.0 * std::max(theta1, theta2);
        pie_wedges.push_back(PieWedge{t1, t2, cx, cy, opt.radius, frac, values[i]});

        /* matplotlib's Wedge._recompute_path: a full turn is drawn from 0 to
           360 with the centre (or inner ring) reached by a MOVETO; otherwise
           the arc runs to the centre -- or, with a width, back along the
           inner ring reversed -- and closes. Then scaled and moved. */
        const bool full = std::fabs((t2 - t1) - 360.0) <= 1e-12;
        const Path arc = full ? Path::arc(0.0, 360.0) : Path::arc(t1, t2);
        Path unit = arc;
        const std::uint8_t connector = full ? MOVETO : LINETO;
        if (opt.width > 0.0) {
            const double k = (opt.radius - opt.width) / opt.radius;
            const std::size_t m = arc.size();
            for (std::size_t j = 0; j < m; ++j) {
                const std::size_t src = m - 1 - j;
                unit.vertices.push_back(arc.vertices[2 * src] * k);
                unit.vertices.push_back(arc.vertices[2 * src + 1] * k);
                unit.codes.push_back(j == 0 ? connector : arc.codes[j]);
            }
        } else {
            unit.vertices.push_back(0.0);
            unit.vertices.push_back(0.0);
            unit.codes.push_back(connector);
        }
        unit.close_poly();
        auto patch = std::make_unique<PolyPatch>();
        patch->path = unit;
        for (std::size_t k = 0; k < unit.size(); ++k) {
            if (!unit.codes.empty() && unit.codes[k] == CLOSEPOLY) {
                continue;
            }
            patch->path.vertices[2 * k] = unit.vertices[2 * k] * opt.radius + cx;
            patch->path.vertices[2 * k + 1] = unit.vertices[2 * k + 1] * opt.radius + cy;
        }
        if (!opt.colors.empty()) {
            patch->facecolor = opt.colors[color_index++ % opt.colors.size()];
        } else {
            patch->facecolor = next_patch_color();
        }
        patch->clip = false;
        patch->alpha = opt.alpha;
        if (opt.has_edgecolor) {
            patch->edgecolor = opt.edgecolor;
            patch->linewidth = opt.linewidth;
        }
        patches.push_back(std::move(patch));
        theta1 = theta2;
    }

    /* Shadows go in front of this pie's wedges in the list: their zorder is
       just under the wedges', so they draw first however they were added. */
    if (opt.shadow) {
        const std::size_t first = patches.size() - n;
        std::vector<std::unique_ptr<PolyPatch>> shadows;
        for (std::size_t i = first; i < patches.size(); ++i) {
            const PolyPatch &w = *patches[i];
            auto s = std::make_unique<PolyPatch>(w);
            const double k = 1.0 - opt.shadow_shade;
            const Color c = Color::rgb(k * w.facecolor.r, k * w.facecolor.g, k * w.facecolor.b);
            s->facecolor = c;
            s->edgecolor = c;
            /* update_from copies the wedge's line width; the edge is now
               drawn, since the shadow sets an edge colour. */
            s->linewidth = opt.linewidth;
            s->alpha = 0.5;
            s->shift_pt_x = opt.shadow_ox;
            s->shift_pt_y = opt.shadow_oy;
            s->label.clear();
            shadows.push_back(std::move(s));
        }
        patches.insert(patches.begin() + static_cast<std::ptrdiff_t>(first),
                       std::make_move_iterator(shadows.begin()),
                       std::make_move_iterator(shadows.end()));
    }

    aspect = 1.0;
    if (!opt.frame) {
        spine_left = spine_right = spine_top = spine_bottom = false;
        xticks_fixed.clear();
        yticks_fixed.clear();
        xticks_fixed_set = true;
        yticks_fixed_set = true;
        patch_visible = false;
        set_xlim(-1.25 + opt.center_x, 1.25 + opt.center_x);
        set_ylim(-1.25 + opt.center_y, 1.25 + opt.center_y);
    }
}

void Axes::pie(const double *values, std::size_t n)
{
    double total = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        if (std::isfinite(values[i]) && values[i] > 0.0) {
            total += values[i];
        }
    }
    if (!(total > 0.0)) {
        return;
    }

    /* Remembered so pie_label can put a label at each wedge's midpoint
       without being handed the values a second time. */
    pie_wedges.clear();

    double theta = 0.0; /* 3 o'clock, running counter-clockwise */
    for (std::size_t i = 0; i < n; ++i) {
        const double v = (std::isfinite(values[i]) && values[i] > 0.0) ? values[i] : 0.0;
        const double next = theta + 360.0 * v / total;
        pie_wedges.push_back(PieWedge{theta, next, 0.0, 0.0, 1.0, v / total, values[i]});

        auto patch = std::make_unique<PolyPatch>();
        /* Wedge = the arc, a line to the centre, and close. */
        patch->path = Path::arc(theta, next);
        patch->path.line_to(0.0, 0.0);
        patch->path.close_poly();
        patch->facecolor = next_patch_color();
        patches.push_back(std::move(patch));

        theta = next;
    }

    /* A pie replaces the axes: square, no frame, no ticks, limits pinned wide
       enough for the labels outside the circle. */
    aspect = 1.0;
    spine_left = spine_right = spine_top = spine_bottom = false;
    xticks_fixed.clear();
    yticks_fixed.clear();
    xticks_fixed_set = true;
    yticks_fixed_set = true;
    patch_visible = false;
    set_xlim(-1.25, 1.25);
    set_ylim(-1.25, 1.25);
}

namespace
{

/*
 * Join the per-cell marching-squares segments into whole polylines, matching
 * matplotlib's contour generator. This matters because a dashed contour needs
 * one dash phase along the whole line, a self-meeting polyline uses a round join
 * (not two overlapping caps), and clabel needs the whole component to place a
 * label. Endpoints match exactly (both cells compute the shared-edge crossing
 * from the same doubles), so keying on coordinates instead of a tolerance is
 * safe. Chains are grown from loose ends first so an open line is not entered in
 * the middle and split into two.
 */
void chain_segments(const std::vector<std::array<double, 4>> &segs,
                    const std::vector<std::pair<double, double>> &ring_starts, Path &out)
{
    if (segs.empty()) {
        return;
    }

    using Key = std::pair<double, double>;
    /* Which segment ends touch each point (normally one or two; the saddle rule
       already split ambiguous cells, so a higher degree is coincident crossings
       where any consistent choice works). */
    std::map<Key, std::vector<std::size_t>> incident;
    for (std::size_t i = 0; i < segs.size(); ++i) {
        incident[Key(segs[i][0], segs[i][1])].push_back(i);
        incident[Key(segs[i][2], segs[i][3])].push_back(i);
    }

    std::vector<bool> used(segs.size(), false);

    /* Walk from `start`, consuming segments, appending each new point. */
    auto walk = [&](Key start, std::vector<Key> &pts) {
        pts.push_back(start);
        Key at = start;
        for (;;) {
            auto it = incident.find(at);
            if (it == incident.end()) {
                break;
            }
            /* Prefer a segment that starts here, so the walk keeps the cells'
               orientation instead of doubling back. */
            std::size_t next = segs.size();
            for (std::size_t s : it->second) {
                if (!used[s] && Key(segs[s][0], segs[s][1]) == at) {
                    next = s;
                    break;
                }
            }
            if (next == segs.size()) {
                for (std::size_t s : it->second) {
                    if (!used[s]) {
                        next = s;
                        break;
                    }
                }
            }
            if (next == segs.size()) {
                break;
            }
            used[next] = true;
            const Key a(segs[next][0], segs[next][1]);
            const Key b(segs[next][2], segs[next][3]);
            at = (a == at) ? b : a;
            pts.push_back(at);
        }
    };

    auto emit = [&](const std::vector<Key> &pts, bool closed) {
        if (pts.size() < 2) {
            return;
        }
        out.move_to(pts[0].first, pts[0].second);
        /* A closed ring repeats its first point at the end; CLOSEPOLY carries
           that, so the repeat is dropped. */
        const std::size_t n = closed ? pts.size() - 1 : pts.size();
        for (std::size_t i = 1; i < n; ++i) {
            out.line_to(pts[i].first, pts[i].second);
        }
        if (closed) {
            out.close_poly();
        }
    };

    /* Open lines first, entered at the loose end the segments point away from,
       so the chain keeps the cells' orientation (the reverse would flip every
       vertex index clabel counts from). */
    for (const auto &entry : incident) {
        if (entry.second.size() != 1 || used[entry.second[0]]) {
            continue;
        }
        const std::array<double, 4> &s = segs[entry.second[0]];
        if (Key(s[0], s[1]) != entry.first) {
            continue; /* this is where the line ends, not where it starts */
        }
        std::vector<Key> pts;
        walk(entry.first, pts);
        emit(pts, /*closed=*/false);
    }

    /* Any open line whose orientation did not resolve -- which would mean two
       cells disagreed about which side is above -- still gets drawn, from
       whichever end is left. */
    for (const auto &entry : incident) {
        if (entry.second.size() != 1 || used[entry.second[0]]) {
            continue;
        }
        std::vector<Key> pts;
        walk(entry.first, pts);
        emit(pts, /*closed=*/false);
    }

    /* Whatever is left is a closed ring, with no loose end to start at. The start
       vertex matters because clabel counts label-width blocks from the first
       vertex. matplotlib enters a ring at the crossing on a vertical cell edge
       that its cell scan reaches first, on the side the oriented contour leaves.
       `ring_starts` is that scan order; the tail test picks the leaving side. */
    for (const Key &start : ring_starts) {
        const auto it = incident.find(start);
        if (it == incident.end()) {
            continue;
        }
        bool leaves_here = false;
        for (std::size_t s : it->second) {
            if (!used[s] && Key(segs[s][0], segs[s][1]) == start) {
                leaves_here = true;
                break;
            }
        }
        if (!leaves_here) {
            continue;
        }
        std::vector<Key> pts;
        walk(start, pts);
        emit(pts, /*closed=*/pts.size() > 2 && pts.front() == pts.back());
    }

    /* A ring with no vertical crossing (small enough to fit in one cell row)
       still has to be drawn. */
    for (std::size_t i = 0; i < segs.size(); ++i) {
        if (used[i]) {
            continue;
        }
        std::vector<Key> pts;
        walk(Key(segs[i][0], segs[i][1]), pts);
        emit(pts, /*closed=*/pts.size() > 2 && pts.front() == pts.back());
    }
}

/* Marching squares for one level. Each cell is classified by which corners
   exceed the level; crossings are interpolated along the separating edges. Cases
   5 and 10 are saddles, where the pairing is ambiguous and the cell's mean
   decides (as in matplotlib's _contour). The loose segments are joined into
   polylines by chain_segments. */
void marching_squares(const ContourSet &cs, double level, Path &out)
{
    if (cs.rows < 2 || cs.cols < 2) {
        return;
    }

    const double dx = (cs.x1 - cs.x0) / static_cast<double>(cs.cols - 1);
    const double dy = (cs.y1 - cs.y0) / static_cast<double>(cs.rows - 1);

    auto value = [&](std::size_t r, std::size_t c) { return cs.values[r * cs.cols + c]; };
    auto px = [&](std::size_t c) { return cs.x0 + static_cast<double>(c) * dx; };
    auto py = [&](std::size_t r) { return cs.y0 + static_cast<double>(r) * dy; };

    /* Where along an edge the level is crossed. */
    auto lerp = [&](double va, double vb, double a, double b) {
        const double denom = vb - va;
        if (std::fabs(denom) < 1e-300) {
            return a;
        }
        return a + (level - va) / denom * (b - a);
    };

    std::vector<std::array<double, 4>> segs;
    /* Crossings on vertical cell edges, in cell-scan order (left edge then right
       per cell), for the closed-ring pass in chain_segments. */
    std::vector<std::pair<double, double>> ring_starts;

    for (std::size_t r = 0; r + 1 < cs.rows; ++r) {
        for (std::size_t c = 0; c + 1 < cs.cols; ++c) {
            const double va = value(r, c);          /* top-left */
            const double vb = value(r, c + 1);      /* top-right */
            const double vc = value(r + 1, c + 1);  /* bottom-right */
            const double vd = value(r + 1, c);      /* bottom-left */

            if (!std::isfinite(va) || !std::isfinite(vb) || !std::isfinite(vc) ||
                !std::isfinite(vd)) {
                continue;
            }

            const int idx = (va > level ? 8 : 0) | (vb > level ? 4 : 0) |
                            (vc > level ? 2 : 0) | (vd > level ? 1 : 0);
            if (idx == 0 || idx == 15) {
                continue;
            }

            const double xl = px(c);
            const double xr = px(c + 1);
            const double yt = py(r);
            const double yb = py(r + 1);

            /* Crossing point on each of the four edges. */
            const Point top{lerp(va, vb, xl, xr), yt};
            const Point right{xr, lerp(vb, vc, yt, yb)};
            const Point bottom{lerp(vd, vc, xl, xr), yb};
            const Point left{xl, lerp(va, vd, yt, yb)};

            auto segment = [&](const Point &p, const Point &q) {
                segs.push_back({p.x, p.y, q.x, q.y});
            };

            /* A vertical edge is crossed when its two corners fall on opposite
               sides of the level: bits 3 and 0 for the left edge, 2 and 1 for
               the right. */
            if (((idx >> 3) & 1) != (idx & 1)) {
                ring_starts.emplace_back(left.x, left.y);
            }
            if (((idx >> 2) & 1) != ((idx >> 1) & 1)) {
                ring_starts.emplace_back(right.x, right.y);
            }

            switch (idx) {
                /* Each segment is emitted with the above-level side on its left,
                   so a case and its complement run opposite ways (1 and 14 cut
                   the same corner, inside in one and outside in the other). The
                   consistent orientation makes chaining reproducible: every line
                   runs the same way round its field and a closed ring starts at
                   matplotlib's start vertex, which matters because clabel counts
                   label-width blocks from the first vertex. */
                case 1:  segment(left, bottom); break;
                case 14: segment(bottom, left); break;
                case 2:  segment(bottom, right); break;
                case 13: segment(right, bottom); break;
                case 3:  segment(left, right); break;
                case 12: segment(right, left); break;
                case 4:  segment(right, top); break;
                case 11: segment(top, right); break;
                case 6:  segment(bottom, top); break;
                case 9:  segment(top, bottom); break;
                case 7:  segment(left, top); break;
                case 8:  segment(top, left); break;

                case 5:
                case 10: {
                    /* Saddle: the mean decides which pair of corners is
                       connected; each chord is oriented as the equivalent
                       single-corner case above. */
                    const double centre = 0.25 * (va + vb + vc + vd);
                    const bool centre_above = centre > level;
                    if ((idx == 5) == centre_above) {
                        if (idx == 5) {
                            segment(left, top);     /* isolates va, below */
                            segment(right, bottom); /* isolates vc, below */
                        } else {
                            segment(top, left);     /* isolates va, above */
                            segment(bottom, right); /* isolates vc, above */
                        }
                    } else {
                        if (idx == 5) {
                            segment(left, bottom); /* isolates vd, above */
                            segment(right, top);   /* isolates vb, above */
                        } else {
                            segment(bottom, left); /* isolates vd, below */
                            segment(top, right);   /* isolates vb, below */
                        }
                    }
                    break;
                }
                default: break;
            }
        }
    }

    chain_segments(segs, ring_starts, out);
}

} // namespace

/* One cell's worth of a filled band, by walking the cell boundary. matplotlib's
   model: z is known only on the boundary (linear along each edge), and the
   region's boundary inside the cell is a straight chord between two crossings.
   Walk the four edges inserting the crossings of `lo` and `hi`, take the maximal
   runs within the band, and close each with a chord. The saddle is ambiguous
   (two runs, connected or disjoint); the mean of the four corners decides, as in
   matplotlib. */
void band_cell(double x0, double y0, double x1, double y1,
               double v00, double v10, double v11, double v01,
               double lo, double hi, Path &out)
{
    struct Pt
    {
        double x, y, z;
    };

    /* Four corners counter-clockwise from bottom left, so every polygon is wound
       the same way and neighbouring cells' shared edges cancel when rasterized. */
    const Pt corner[4] = {{x0, y0, v00}, {x1, y0, v10}, {x1, y1, v11}, {x0, y1, v01}};

    std::vector<Pt> ring;
    ring.reserve(16);

    auto crossing = [](const Pt &a, const Pt &b, double level) {
        const double d = b.z - a.z;
        const double t = d == 0.0 ? 0.0 : (level - a.z) / d;
        return Pt{a.x + t * (b.x - a.x), a.y + t * (b.y - a.y), level};
    };

    for (int e = 0; e < 4; ++e) {
        const Pt &a = corner[e];
        const Pt &b = corner[(e + 1) % 4];
        ring.push_back(a);

        /* Both levels can cross the same edge; they must go in in the order
           they occur ALONG the edge, or the ring stops being a ring. */
        const bool up = b.z > a.z;
        const double first = up ? lo : hi;
        const double second = up ? hi : lo;
        for (double level : {first, second}) {
            const double zmin = std::min(a.z, b.z);
            const double zmax = std::max(a.z, b.z);
            if (level > zmin && level < zmax) {
                ring.push_back(crossing(a, b, level));
            }
        }
    }

    const std::size_t n = ring.size();
    if (n == 0) {
        return;
    }

    auto inside = [&](const Pt &p) { return p.z >= lo && p.z <= hi; };

    /* Everything inside: the cell is the polygon. */
    bool all_in = true;
    for (const Pt &p : ring) {
        all_in = all_in && inside(p);
    }
    if (all_in) {
        out.move_to(ring[0].x, ring[0].y);
        for (std::size_t i = 1; i < n; ++i) {
            out.line_to(ring[i].x, ring[i].y);
        }
        out.close_poly();
        return;
    }

    /* Maximal cyclic runs of inside points. Start the walk at a point that is
       outside, so no run is split across the seam. */
    std::size_t start = n;
    for (std::size_t i = 0; i < n; ++i) {
        if (!inside(ring[i])) {
            start = i;
            break;
        }
    }
    if (start == n) {
        return; /* unreachable: all_in covered it */
    }

    std::vector<std::vector<Pt>> arcs;
    std::vector<Pt> current;
    for (std::size_t k = 0; k < n; ++k) {
        const Pt &p = ring[(start + k) % n];
        if (inside(p)) {
            current.push_back(p);
        } else if (!current.empty()) {
            arcs.push_back(current);
            current.clear();
        }
    }
    if (!current.empty()) {
        arcs.push_back(current);
    }
    if (arcs.empty()) {
        return;
    }

    if (arcs.size() == 1) {
        const std::vector<Pt> &a = arcs[0];
        out.move_to(a[0].x, a[0].y);
        for (std::size_t i = 1; i < a.size(); ++i) {
            out.line_to(a[i].x, a[i].y);
        }
        out.close_poly();
        return;
    }

    /* Two or more runs: connected or not, decided by the cell's mean. Strict
       against `lo`, non-strict against `hi` (matplotlib's calc_z_level_mid), so a
       mean on the lower level counts as outside the band. */
    const double mean = (v00 + v10 + v11 + v01) / 4.0;
    const bool connected = mean > lo && mean <= hi;

    if (connected) {
        bool first = true;
        for (const std::vector<Pt> &a : arcs) {
            for (const Pt &p : a) {
                if (first) {
                    out.move_to(p.x, p.y);
                    first = false;
                } else {
                    out.line_to(p.x, p.y);
                }
            }
        }
        out.close_poly();
        return;
    }

    for (const std::vector<Pt> &a : arcs) {
        out.move_to(a[0].x, a[0].y);
        for (std::size_t i = 1; i < a.size(); ++i) {
            out.line_to(a[i].x, a[i].y);
        }
        out.close_poly();
    }
}

std::vector<double> Axes::auto_contour_levels(const double *values, std::size_t count,
                                               std::size_t n, ContourExtend extend)
{
    double zmin = std::numeric_limits<double>::infinity();
    double zmax = -zmin;
    for (std::size_t i = 0; i < count; ++i) {
        if (std::isfinite(values[i])) {
            zmin = std::min(zmin, values[i]);
            zmax = std::max(zmax, values[i]);
        }
    }
    if (!(zmin <= zmax)) {
        return {};
    }

    MaxNLocator locator;
    locator.nbins = static_cast<int>((n == 0 ? 7 : n) + 1);
    locator.min_n_ticks = 1;
    /* A bare MaxNLocator's own steps, not AutoLocator's [1, 2, 2.5, 5, 10]. */
    locator.steps = {1.0, 1.5, 2.0, 2.5, 3.0, 4.0, 5.0, 6.0, 8.0, 10.0};
    const std::vector<double> lev = locator.tick_values(zmin, zmax);

    /* Keep one level below zmin and one above zmax, no more. */
    std::ptrdiff_t i0 = 0;
    for (std::size_t k = 0; k < lev.size(); ++k) {
        if (lev[k] < zmin) {
            i0 = static_cast<std::ptrdiff_t>(k);
        }
    }
    std::ptrdiff_t i1 = static_cast<std::ptrdiff_t>(lev.size());
    for (std::size_t k = 0; k < lev.size(); ++k) {
        if (lev[k] > zmax) {
            i1 = static_cast<std::ptrdiff_t>(k) + 1;
            break;
        }
    }
    if (extend == ContourExtend::Min || extend == ContourExtend::Both) {
        ++i0;
    }
    if (extend == ContourExtend::Max || extend == ContourExtend::Both) {
        --i1;
    }
    if (i1 - i0 < 3) {
        i0 = 0;
        i1 = static_cast<std::ptrdiff_t>(lev.size());
    }
    return std::vector<double>(lev.begin() + i0, lev.begin() + i1);
}

ContourSet &Axes::contourf(const double *values, std::size_t rows, std::size_t cols,
                           const double *levels, std::size_t nlevels,
                           double x0, double x1, double y0, double y1, ContourExtend extend)
{
    ContourSet &cs = contour(values, rows, cols, levels, nlevels, x0, x1, y0, y1);
    cs.filled = true;
    cs.extend = extend;
    cs.level_paths.clear(); /* filled contours draw no lines of their own */

    if (nlevels < 2 || rows < 2 || cols < 2) {
        return cs;
    }

    /* matplotlib's _levels: an extended end gets a band reaching to
       +/-1e250, past anything the data can hold. */
    cs.band_levels = cs.levels;
    if (extend == ContourExtend::Min || extend == ContourExtend::Both) {
        cs.band_levels.insert(cs.band_levels.begin(), -1e250);
    }
    if (extend == ContourExtend::Max || extend == ContourExtend::Both) {
        cs.band_levels.push_back(1e250);
    }
    const std::size_t nbands = cs.band_levels.size() - 1;

    const double dx = (cs.x1 - cs.x0) / static_cast<double>(cols - 1);
    const double dy = (cs.y1 - cs.y0) / static_cast<double>(rows - 1);
    auto value = [&](std::size_t r, std::size_t c) { return cs.values[r * cs.cols + c]; };

    cs.band_paths.resize(nbands);
    for (std::size_t b = 0; b < nbands; ++b) {
        const double lo = cs.band_levels[b];
        const double hi = cs.band_levels[b + 1];
        Path &path = cs.band_paths[b];

        for (std::size_t r = 0; r + 1 < rows; ++r) {
            for (std::size_t c = 0; c + 1 < cols; ++c) {
                band_cell(cs.x0 + static_cast<double>(c) * dx,
                          cs.y0 + static_cast<double>(r) * dy,
                          cs.x0 + static_cast<double>(c + 1) * dx,
                          cs.y0 + static_cast<double>(r + 1) * dy, value(r, c),
                          value(r, c + 1), value(r + 1, c + 1), value(r + 1, c), lo, hi,
                          path);
            }
        }
    }

    return cs;
}

ContourSet &Axes::contour(const double *values, std::size_t rows, std::size_t cols,
                          const double *levels, std::size_t nlevels,
                          double x0, double x1, double y0, double y1)
{
    auto cs = std::make_unique<ContourSet>();
    cs->values.assign(values, values + rows * cols);
    cs->rows = rows;
    cs->cols = cols;

    /* A degenerate extent means "use index coordinates", which is what
       contour(Z) does with no X and Y. */
    const bool have_extent = (x1 != x0) && (y1 != y0);
    cs->x0 = have_extent ? x0 : 0.0;
    cs->x1 = have_extent ? x1 : static_cast<double>(cols) - 1.0;
    cs->y0 = have_extent ? y0 : 0.0;
    cs->y1 = have_extent ? y1 : static_cast<double>(rows) - 1.0;

    cs->levels.assign(levels, levels + nlevels);

    cs->level_paths.resize(nlevels);
    for (std::size_t i = 0; i < nlevels; ++i) {
        marching_squares(*cs, cs->levels[i], cs->level_paths[i]);
    }

    /* matplotlib finishes contour() with autoscale_view(tight=True), so the
       axes ends at the grid rather than 5% past it. */
    tight_view = true;

    contours.push_back(std::move(cs));
    return *contours.back();
}

void Axes::label_outer(bool remove_inner_ticks)
{
    /* Keep tick and axis labels only on the grid's outer edges. Per edge: a panel
       not in the last row loses its x tick labels and xlabel, one not in the
       first column loses its y tick labels and ylabel. Tick marks stay unless
       `remove_inner_ticks` is set. */
    if (grid_rows == 0 || grid_cols == 0 || grid_index == 0) {
        return; /* placed by hand: it is in no row and no column */
    }

    const unsigned cell = grid_index - 1;
    const unsigned row = cell / grid_cols;
    const unsigned col = cell % grid_cols;

    if (row + 1 != grid_rows) {
        xtick.labels_visible = false;
        xlabel.clear();
        if (remove_inner_ticks) {
            xtick.ticks_visible = false;
        }
    }
    if (col != 0) {
        ytick.labels_visible = false;
        ylabel.clear();
        if (remove_inner_ticks) {
            ytick.ticks_visible = false;
        }
    }
}

void Axes::indicate_inset(double x, double y, double width, double height,
                          const Axes &inset, const Bbox &self_position)
{
    /* The rectangle marking what an inset magnifies, plus the two lines joining
       it to the inset's box. Rectangle is in data coordinates, unfilled, with a
       50%-alpha grey edge. */
    auto rect = std::make_unique<PolyPatch>();
    rect->path.move_to(x, y);
    rect->path.line_to(x + width, y);
    rect->path.line_to(x + width, y + height);
    rect->path.line_to(x, y + height);
    rect->path.close_poly();
    rect->facecolor = Color::none();
    rect->edgecolor = Color::rgb(0.5, 0.5, 0.5); /* edgecolor='0.5' */
    rect->linewidth = 1.0;
    rect->alpha = 0.5;
    /* An indicator marks a region, not data, so it must not stretch the view. */
    rect->counts_towards_limits = false;
    patches.push_back(std::move(rect));

    /* Which two of the four corner-to-corner lines to draw (all four would look
       like a cat's cradle). Chosen by comparing the rectangle's box with the
       inset's edge by edge, in figure coordinates, per matplotlib's rule:
           visible[0] = x0 XOR y0      visible[1] = (x0 == y1)
           visible[2] = (x1 == y0)     visible[3] = x1 XOR y1
       where xN/yN say whether that rectangle edge is left of / below the inset's. */
    const Bbox view = view_limits();
    auto to_fig_x = [&](double v) {
        const double t = (v - view.x0) / (view.x1 - view.x0);
        return self_position.x0 + t * self_position.width();
    };
    auto to_fig_y = [&](double v) {
        const double t = (v - view.y0) / (view.y1 - view.y0);
        return self_position.y0 + t * self_position.height();
    };

    const double rx0 = to_fig_x(x);
    const double rx1 = to_fig_x(x + width);
    const double ry0 = to_fig_y(y);
    const double ry1 = to_fig_y(y + height);

    const bool bx0 = rx0 < inset.position.x0;
    const bool bx1 = rx1 < inset.position.x1;
    const bool by0 = ry0 < inset.position.y0;
    const bool by1 = ry1 < inset.position.y1;

    const bool visible[4] = {bx0 != by0, bx0 == by1, bx1 == by0, bx1 != by1};

    /* Corner order matches matplotlib's: lower-left, upper-left, lower-right,
       upper-right of the INSET, each joined to the matching corner of the
       rectangle. */
    const double cx[4] = {0.0, 0.0, 1.0, 1.0};
    const double cy[4] = {0.0, 1.0, 0.0, 1.0};

    for (int k = 0; k < 4; ++k) {
        if (!visible[k]) {
            continue;
        }
        InsetConnector c;
        c.data_x = x + cx[k] * width;
        c.data_y = y + cy[k] * height;
        c.inset_x = inset.position.x0 + cx[k] * inset.position.width();
        c.inset_y = inset.position.y0 + cy[k] * inset.position.height();
        inset_connectors.push_back(c);
    }
}

void Axes::pie_label(const std::string *labels, std::size_t n, double distance,
                     double fontsize, PieLabelAlign align, bool rotate)
{
    /* A label per wedge, at `distance` of the radius along the wedge's middle
       angle from its own (possibly exploded) centre. The default 0.6 puts them
       inside the pie, so they are centred both ways; auto switches to outer
       alignment only past distance 1. */
    if (align == PieLabelAlign::Auto) {
        align = distance > 1.0 ? PieLabelAlign::Outer : PieLabelAlign::Center;
    }
    const std::size_t count = std::min(n, pie_wedges.size());
    for (std::size_t i = 0; i < count; ++i) {
        const PieWedge &w = pie_wedges[i];
        const double mid = 2.0 * 3.14159265358979323846 * 0.5 * (w.theta1 + w.theta2) / 360.0;
        const double xt = w.cx + distance * w.r * std::cos(mid);
        const double yt = w.cy + distance * w.r * std::sin(mid);
        TextArtist &t = text(xt, yt, labels[i]);
        t.halign = align == PieLabelAlign::Outer ? (xt > 0.0 ? HAlign::Left : HAlign::Right)
                                                 : HAlign::Center;
        t.valign = VAlign::Center;
        if (rotate) {
            if (align == PieLabelAlign::Outer) {
                t.valign = yt > 0.0 ? VAlign::Bottom : VAlign::Top;
            }
            t.rotation = mid * 180.0 / 3.14159265358979323846 + (xt > 0.0 ? 0.0 : 180.0);
        }
        t.size = fontsize;
    }
}

namespace
{

/* Average the segment periodograms (Welch's method) for psd/csd estimates. */
std::vector<double> mean_over_segments(const SpectralResult &r)
{
    std::vector<double> out(r.numFreqs, 0.0);
    if (r.nsegs == 0) {
        return out;
    }
    for (std::size_t s = 0; s < r.nsegs; ++s) {
        for (std::size_t f = 0; f < r.numFreqs; ++f) {
            out[f] += r.values[s * r.numFreqs + f];
        }
    }
    for (double &v : out) {
        v /= static_cast<double>(r.nsegs);
    }
    return out;
}

/* Modulus of the segment-averaged complex cross-spectrum. Averaging first, then
   taking the modulus, lets phase-disagreeing segments cancel (required for
   coherence); modulus-first would add every segment as if in phase. */
std::vector<double> abs_mean_over_segments(const SpectralResult &r)
{
    std::vector<double> out(r.numFreqs, 0.0);
    if (r.nsegs == 0) {
        return out;
    }
    for (std::size_t f = 0; f < r.numFreqs; ++f) {
        double re = 0.0;
        double im = 0.0;
        for (std::size_t s = 0; s < r.nsegs; ++s) {
            re += r.values[s * r.numFreqs + f];
            if (!r.values_imag.empty()) {
                im += r.values_imag[s * r.numFreqs + f];
            }
        }
        out[f] = std::hypot(re, im) / static_cast<double>(r.nsegs);
    }
    return out;
}

} // namespace

void Axes::spectral_yticks()
{
    /* psd and csd pin decibel ticks at whole numbers, stepping by
       max(10 * int(log10(range)), 1). int() truncates, so a 60 dB range gives a
       step of 10 and a 6 dB range gives 1. */
    const Bbox view = view_limits();
    const double vmin = view.y0;
    const double vmax = view.y1;
    if (!(vmax > vmin)) {
        return;
    }
    const int decade = static_cast<int>(std::log10(vmax - vmin));
    const double step = std::max(10.0 * decade, 1.0);

    yticks_fixed.clear();
    for (double t = std::floor(vmin); t <= std::ceil(vmax) + 1e-9; t += step) {
        yticks_fixed.push_back(t);
    }
    yticks_fixed_set = true;
    if (yticks_fixed.empty()) {
        return;
    }

    /* Pinning the ticks expands the view to reach the outermost one, matching
       matplotlib's set_yticks dragging the limit out to floor()/ceil(). */
    set_ylim(std::min(vmin, yticks_fixed.front()), std::max(vmax, yticks_fixed.back()));
}

Line2D &Axes::psd(const double *x, std::size_t n, std::size_t NFFT, double Fs,
                  std::size_t noverlap, const SpectralOptions &opts)
{
    const SpectralResult r = spectral_helper(x, nullptr, n, NFFT, Fs, noverlap, opts.pad_to,
                                             opts.sides, SpectralMode::Psd,
                                             /*scale_by_freq=*/true, opts.window, opts.detrend);
    const std::vector<double> pxx = mean_over_segments(r);

    std::vector<double> db(pxx.size());
    for (std::size_t i = 0; i < pxx.size(); ++i) {
        db[i] = 10.0 * std::log10(pxx[i]);
    }

    Line2D &line = plot(r.freqs.data(), db.data(), db.size());
    xlabel = "Frequency";
    ylabel = "Power Spectral Density (dB/Hz)";
    xgrid_major = ygrid_major = true;
    spectral_yticks();
    return line;
}

Line2D &Axes::csd(const double *x, const double *y, std::size_t n, std::size_t NFFT,
                  double Fs, std::size_t noverlap, const SpectralOptions &opts)
{
    const SpectralResult r = spectral_helper(x, y, n, NFFT, Fs, noverlap, opts.pad_to,
                                             opts.sides, SpectralMode::Psd,
                                             /*scale_by_freq=*/true, opts.window, opts.detrend);
    const std::vector<double> pxy = abs_mean_over_segments(r);

    std::vector<double> db(pxy.size());
    for (std::size_t i = 0; i < pxy.size(); ++i) {
        db[i] = 10.0 * std::log10(pxy[i]);
    }

    Line2D &line = plot(r.freqs.data(), db.data(), db.size());
    xlabel = "Frequency";
    ylabel = "Cross Spectrum Magnitude (dB)";
    xgrid_major = ygrid_major = true;
    spectral_yticks();
    return line;
}

Line2D &Axes::cohere(const double *x, const double *y, std::size_t n, std::size_t NFFT,
                     double Fs, std::size_t noverlap, const SpectralOptions &opts)
{
    /* |Pxy|^2 / (Pxx * Pyy) -- three passes of the same helper, as matplotlib. */
    const SpectralResult rxx =
        spectral_helper(x, nullptr, n, NFFT, Fs, noverlap, opts.pad_to, opts.sides,
                        SpectralMode::Psd, true, opts.window, opts.detrend);
    const SpectralResult ryy =
        spectral_helper(y, nullptr, n, NFFT, Fs, noverlap, opts.pad_to, opts.sides,
                        SpectralMode::Psd, true, opts.window, opts.detrend);
    const SpectralResult rxy =
        spectral_helper(x, y, n, NFFT, Fs, noverlap, opts.pad_to, opts.sides,
                        SpectralMode::Psd, true, opts.window, opts.detrend);
    const std::vector<double> pxx = mean_over_segments(rxx);
    const std::vector<double> pyy = mean_over_segments(ryy);
    /* |Pxy| of the complex mean, not the mean of moduli: coherence measures the
       phase cancellation between segments. */
    const std::vector<double> pxy = abs_mean_over_segments(rxy);

    std::vector<double> cxy(pxy.size(), 0.0);
    for (std::size_t i = 0; i < pxy.size(); ++i) {
        const double denom = pxx[i] * pyy[i];
        cxy[i] = denom != 0.0 ? (pxy[i] * pxy[i]) / denom : 0.0;
    }

    Line2D &line = plot(rxy.freqs.data(), cxy.data(), cxy.size());
    xlabel = "Frequency";
    ylabel = "Coherence";
    xgrid_major = ygrid_major = true;
    return line;
}

Line2D &Axes::magnitude_spectrum(const double *x, std::size_t n, double Fs, bool db,
                                 const SpectralOptions &opts)
{
    /* Single spectra take the whole signal as one segment and do not scale by
       frequency, so their units are magnitude rather than density. */
    SpectralResult r =
        spectral_helper(x, nullptr, n, n, Fs, 0, opts.pad_to, opts.sides,
                        SpectralMode::Magnitude, /*scale_by_freq=*/false, opts.window,
                        opts.detrend);

    /* scale='dB' turns magnitude into 20*log10; 'linear'/'default' leaves energy. */
    if (db) {
        for (double &v : r.values) {
            v = 20.0 * std::log10(v);
        }
    }

    Line2D &line = plot(r.freqs.data(), r.values.data(), r.numFreqs);
    xlabel = "Frequency";
    ylabel = db ? "Magnitude (dB)" : "Magnitude (energy)";
    return line;
}

Line2D &Axes::angle_spectrum(const double *x, std::size_t n, double Fs,
                             const SpectralOptions &opts)
{
    const SpectralResult r = spectral_helper(x, nullptr, n, n, Fs, 0, opts.pad_to, opts.sides,
                                             SpectralMode::Angle, false, opts.window,
                                             opts.detrend);
    Line2D &line = plot(r.freqs.data(), r.values.data(), r.numFreqs);
    xlabel = "Frequency";
    ylabel = "Angle (radians)";
    return line;
}

Line2D &Axes::phase_spectrum(const double *x, std::size_t n, double Fs,
                             const SpectralOptions &opts)
{
    /* Like the angle spectrum but unwrapped, so the curve is continuous instead
       of sawtoothing between -pi and pi. */
    const SpectralResult r = spectral_helper(x, nullptr, n, n, Fs, 0, opts.pad_to, opts.sides,
                                             SpectralMode::Phase, false, opts.window,
                                             opts.detrend);
    Line2D &line = plot(r.freqs.data(), r.values.data(), r.numFreqs);
    xlabel = "Frequency";
    ylabel = "Phase (radians)";
    return line;
}

ImageGrid &Axes::specgram(const double *x, std::size_t n, std::size_t NFFT, double Fs,
                          std::size_t noverlap, const SpectralOptions &opts, SpectralMode mode,
                          SpecgramScale scale, const double *xextent, Colormap cmap)
{
    /* psd scales by frequency; the magnitude/angle/phase modes do not, matching
       the single spectra. */
    const bool is_psd = (mode == SpectralMode::Psd);
    const SpectralResult r = spectral_helper(x, nullptr, n, NFFT, Fs, noverlap, opts.pad_to,
                                             opts.sides, mode, /*scale_by_freq=*/is_psd,
                                             opts.window, opts.detrend);

    /* Resolve the scale: default dB for psd/magnitude, linear for angle/phase
       (no dB form). dB is 10*log10 for power (psd), 20*log10 for amplitude. */
    const bool is_amp = (mode == SpectralMode::Magnitude);
    bool to_db = false;
    if (scale == SpecgramScale::Db) {
        to_db = true; /* ABI rejects dB with angle/phase before we get here */
    } else if (scale == SpecgramScale::Default) {
        to_db = is_psd || is_amp;
    }
    const double db_factor = is_amp ? 20.0 : 10.0;

    /* Rows are frequencies, columns times; the array is flipped vertically before
       imshow with origin='upper', so the lowest frequency ends up at the bottom. */
    std::vector<double> z(r.numFreqs * r.nsegs, 0.0);
    for (std::size_t f = 0; f < r.numFreqs; ++f) {
        for (std::size_t s = 0; s < r.nsegs; ++s) {
            const double v = r.values[s * r.numFreqs + f];
            z[(r.numFreqs - 1 - f) * r.nsegs + s] = to_db ? db_factor * std::log10(v) : v;
        }
    }

    /* Half a step of padding at each end, since a segment's value spans its whole
       width, not its centre. `xextent` overrides it. */
    const double pad = static_cast<double>(NFFT - noverlap) / Fs / 2.0;
    const double xmin = xextent != nullptr ? xextent[0] : r.times.front() - pad;
    const double xmax = xextent != nullptr ? xextent[1] : r.times.back() + pad;

    ImageGrid &img = imshow(z.data(), r.numFreqs, r.nsegs);
    img.cmap = cmap;
    img.x0 = xmin;
    img.x1 = xmax;
    /* y1 is the top edge, where row 0 runs from (the flip above put the highest
       frequency there). origin='upper' over an explicit extent leaves the axis
       running the normal way up, unlike imshow's default extent. */
    img.y0 = r.freqs.front();
    img.y1 = r.freqs.back();

    /* imshow pinned the view to its default extent with square pixels; a
       spectrogram wants neither (matplotlib's specgram ends with axis('auto')). */
    set_xlim(xmin, xmax);
    set_ylim(r.freqs.front(), r.freqs.back());
    aspect = 0.0;
    return img;
}

void Axes::grouped_bar(const double *heights, std::size_t datasets, std::size_t groups,
                       const std::string *tick_labels, const std::string *labels,
                       double group_spacing, double bar_spacing)
{
    if (datasets == 0 || groups == 0) {
        return;
    }

    /* Bars side by side within each group (matplotlib's grouped_bar). Width falls
       out of the spacing: one group spans group_distance (1 for default integer
       positions) and fits `datasets` bars plus the gaps, so bar_width = distance
       / (D + (D-1)*bar_spacing + group_spacing). group_spacing is in bar widths. */
    const double d = static_cast<double>(datasets);
    const double group_distance = 1.0;
    const double bar_width =
        group_distance / (d + (d - 1.0) * bar_spacing + group_spacing);
    const double bar_spacing_abs = bar_spacing * bar_width;
    const double margin_abs = 0.5 * group_spacing * bar_width;

    std::vector<double> centres(groups);
    std::vector<double> hs(groups);
    for (std::size_t g = 0; g < groups; ++g) {
        centres[g] = static_cast<double>(g);
    }

    for (std::size_t i = 0; i < datasets; ++i) {
        std::vector<double> xs(groups);
        for (std::size_t g = 0; g < groups; ++g) {
            const double left = centres[g] - 0.5 * group_distance + margin_abs +
                                static_cast<double>(i) * (bar_width + bar_spacing_abs);
            /* matplotlib passes align="edge"; bar() centres, so shift half a bar. */
            xs[g] = left + 0.5 * bar_width;
            hs[g] = heights[i * groups + g];
        }
        PolyPatch &p = bar(xs.data(), hs.data(), groups, bar_width);
        if (labels != nullptr) {
            p.label = labels[i];
        }
    }

    if (tick_labels != nullptr) {
        xticks_fixed.assign(centres.begin(), centres.end());
        xticklabels_fixed.assign(tick_labels, tick_labels + groups);
        xticks_fixed_set = true;
    }
}

namespace
{

/* mlab.detrend_mean / detrend_linear on one series (a copy). */
std::vector<double> detrended(const double *v, std::size_t n, SpectralDetrend how)
{
    std::vector<double> out(v, v + n);
    if (how == SpectralDetrend::None || n == 0) {
        return out;
    }
    double my = 0.0;
    for (double d : out) {
        my += d;
    }
    my /= static_cast<double>(n);
    if (how == SpectralDetrend::Mean) {
        for (double &d : out) {
            d -= my;
        }
        return out;
    }
    /* Linear: the least-squares line over x = 0..n-1, from the biased
       covariance as matplotlib computes it. */
    const double mx = 0.5 * static_cast<double>(n - 1);
    double sxx = 0.0;
    double sxy = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double dx = static_cast<double>(i) - mx;
        sxx += dx * dx;
        sxy += dx * (out[i] - my);
    }
    const double b = sxx > 0.0 ? sxy / sxx : 0.0;
    const double a = my - b * mx;
    for (std::size_t i = 0; i < n; ++i) {
        out[i] -= a + b * static_cast<double>(i);
    }
    return out;
}

} // namespace

Line2D *Axes::xcorr(const double *xin, const double *yin, std::size_t n, int maxlags,
                    bool normed, SpectralDetrend detrend, bool usevlines)
{
    if (n == 0 || maxlags < 1 || static_cast<std::size_t>(maxlags) >= n) {
        return nullptr;
    }
    const std::vector<double> xd = detrended(xin, n, detrend);
    const std::vector<double> yd = detrended(yin, n, detrend);
    const double *x = xd.data();
    const double *y = yd.data();

    /* Plain cross-correlation (numpy's `correlate`, a direct sum, not a spectrum;
       no FFT). c[N - 1 + m] = sum_k x[k + m] * y[k], so index N - 1 is zero lag.
       detrend defaults to detrend_none, so nothing is removed first. */
    const std::size_t full = 2 * n - 1;
    std::vector<double> correls(full, 0.0);
    for (std::size_t k = 0; k < full; ++k) {
        const long m = static_cast<long>(k) - static_cast<long>(n) + 1;
        double sum = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            const long j = static_cast<long>(i) + m;
            if (j >= 0 && j < static_cast<long>(n)) {
                sum += x[static_cast<std::size_t>(j)] * y[i];
            }
        }
        correls[k] = sum;
    }

    if (normed) {
        double xx = 0.0;
        double yy = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            xx += x[i] * x[i];
            yy += y[i] * y[i];
        }
        const double norm = std::sqrt(xx * yy);
        if (norm > 0.0) {
            for (double &c : correls) {
                c /= norm;
            }
        }
    }

    const std::size_t lo = n - 1 - static_cast<std::size_t>(maxlags);
    const std::size_t count = 2 * static_cast<std::size_t>(maxlags) + 1;

    std::vector<double> lags(count);
    std::vector<double> zeros(count, 0.0);
    std::vector<double> tops(count);
    for (std::size_t i = 0; i < count; ++i) {
        lags[i] = static_cast<double>(static_cast<long>(i) - maxlags);
        tops[i] = correls[lo + i];
    }

    if (!usevlines) {
        /* plot(lags, correls, marker='o', linestyle='None'). */
        Line2D &line = plot(lags.data(), tops.data(), count);
        line.marker = MarkerStyle::Circle;
        line.linestyle = LineStyle::None;
        return &line;
    }
    /* usevlines=True: a stalk per lag from zero, and a rule along zero. */
    vlines(lags.data(), zeros.data(), tops.data(), count);
    axhline(0.0);
    return nullptr;
}

Line2D *Axes::acorr(const double *x, std::size_t n, int maxlags, bool normed,
                    SpectralDetrend detrend, bool usevlines)
{
    /* Literally xcorr against itself -- which is how matplotlib defines it,
       and why the zero lag is always the maximum. */
    return xcorr(x, x, n, maxlags, normed, detrend, usevlines);
}

namespace
{

/* ---- streamplot ----
   A port of matplotlib's streamplot.py. Everything below works in grid
   coordinates (x 0..nx-1, y 0..ny-1), which lets the integrator use one step size
   for the whole field. */
struct StreamGrid
{
    std::size_t nx = 0, ny = 0;
    double dx = 1.0, dy = 1.0;
    double x_origin = 0.0, y_origin = 0.0;

    bool within(double xi, double yi) const
    {
        return xi >= 0.0 && xi <= static_cast<double>(nx) - 1.0 && yi >= 0.0 &&
               yi <= static_cast<double>(ny) - 1.0;
    }
};

/* Bilinear sample of a row-major grid at a fractional index. */
double interpgrid(const std::vector<double> &a, std::size_t nx, std::size_t ny, double xi,
                  double yi)
{
    const int x = static_cast<int>(xi);
    const int y = static_cast<int>(yi);
    const int xn = (x == static_cast<int>(nx) - 1) ? x : x + 1;
    const int yn = (y == static_cast<int>(ny) - 1) ? y : y + 1;

    const double a00 = a[static_cast<std::size_t>(y) * nx + static_cast<std::size_t>(x)];
    const double a01 = a[static_cast<std::size_t>(y) * nx + static_cast<std::size_t>(xn)];
    const double a10 = a[static_cast<std::size_t>(yn) * nx + static_cast<std::size_t>(x)];
    const double a11 = a[static_cast<std::size_t>(yn) * nx + static_cast<std::size_t>(xn)];

    const double xt = xi - x;
    const double yt = yi - y;
    const double a0 = a00 * (1.0 - xt) + a01 * xt;
    const double a1 = a10 * (1.0 - xt) + a11 * xt;
    return a0 * (1.0 - yt) + a1 * yt;
}

/* Mask cells tried from the outside in, spiralling (_gen_starting_points).
   Seeding the boundary first lets edge lines run the whole way across and the
   interior fill the gaps, rather than building from short interior fragments. */
std::vector<std::pair<int, int>> spiral_seeds(int nx, int ny)
{
    std::vector<std::pair<int, int>> out;
    out.reserve(static_cast<std::size_t>(nx) * ny);

    int xfirst = 0, yfirst = 1, xlast = nx - 1, ylast = ny - 1;
    int x = 0, y = 0;
    enum Dir { Right, Up, Left, Down } dir = Right;

    for (int i = 0; i < nx * ny; ++i) {
        out.emplace_back(x, y);
        switch (dir) {
            case Right:
                if (++x >= xlast) { --xlast; dir = Up; }
                break;
            case Up:
                if (++y >= ylast) { --ylast; dir = Left; }
                break;
            case Left:
                if (--x <= xfirst) { ++xfirst; dir = Down; }
                break;
            case Down:
                if (--y <= yfirst) { ++yfirst; dir = Right; }
                break;
        }
    }
    return out;
}

} // namespace

StreamPlot &Axes::streamplot(const double *x, const double *y, const double *u,
                             const double *v, std::size_t rows, std::size_t cols,
                             double density)
{
    auto sp = std::make_unique<StreamPlot>();
    sp->color = next_line_color();

    if (rows < 2 || cols < 2) {
        streams.push_back(std::move(sp));
        return *streams.back();
    }

    StreamGrid grid;
    grid.nx = cols;
    grid.ny = rows;
    grid.dx = x[1] - x[0];
    grid.dy = y[1] - y[0];
    grid.x_origin = x[0];
    grid.y_origin = y[0];

    sp->x0 = x[0];
    sp->x1 = x[cols - 1];
    sp->y0 = y[0];
    sp->y1 = y[rows - 1];

    /* Mask is 30 cells per unit of density; its resolution sets the streamline
       spacing, since a line stops on entering an already-claimed cell. */
    const int mnx = std::max(1, static_cast<int>(30.0 * density));
    const int mny = mnx;
    std::vector<uint8_t> mask(static_cast<std::size_t>(mnx) * mny, 0);

    const double x_grid2mask = (mnx - 1.0) / (static_cast<double>(cols) - 1.0);
    const double y_grid2mask = (mny - 1.0) / (static_cast<double>(rows) - 1.0);
    /* Reciprocals are precomputed and multiplied by, never divided by, to match
       matplotlib's floating-point exactly: dividing instead lets far-edge seeds
       that multiplication rejects (e.g. 29 -> 24.000000000000004, outside the
       grid) survive and add spurious streamlines. */
    const double x_mask2grid = 1.0 / x_grid2mask;
    const double y_mask2grid = 1.0 / y_grid2mask;

    const std::size_t n = rows * cols;
    /* Velocities converted to grid units per unit time. */
    std::vector<double> ug(n), vg(n), speed(n);
    for (std::size_t i = 0; i < n; ++i) {
        ug[i] = u[i] / grid.dx;
        vg[i] = v[i] / grid.dy;
        const double uax = ug[i] / (static_cast<double>(cols) - 1.0);
        const double vax = vg[i] / (static_cast<double>(rows) - 1.0);
        speed[i] = std::sqrt(uax * uax + vax * vax);
    }

    /* The field, normalised so one unit of the integration parameter is one unit
       of arc length in axes coordinates; otherwise the step size and error
       control would mean different things in fast and slow parts of the field. */
    enum class Step { Ok, OutOfBounds, Terminate };
    auto field = [&](double xi, double yi, bool backward, double &dx, double &dy) {
        if (!grid.within(xi, yi)) {
            return Step::OutOfBounds;
        }
        const double ds_dt = interpgrid(speed, cols, rows, xi, yi);
        if (ds_dt == 0.0) {
            return Step::Terminate;
        }
        const double dt_ds = 1.0 / ds_dt;
        dx = interpgrid(ug, cols, rows, xi, yi) * dt_ds;
        dy = interpgrid(vg, cols, rows, xi, yi) * dt_ds;
        if (backward) {
            dx = -dx;
            dy = -dy;
        }
        return Step::Ok;
    };

    /* maxlength is halved because each seed is integrated BOTH ways. */
    const double maxlength = 4.0 / 2.0;
    const double minlength = 0.1;
    const double maxerror = 0.003;
    const double maxds = std::min(std::min(1.0 / mnx, 1.0 / mny), 0.1);

    std::vector<std::pair<int, int>> claimed; /* this trajectory's mask cells */
    int cur_xm = -1, cur_ym = -1;

    auto mask_at = [&](int xm, int ym) -> uint8_t & {
        return mask[static_cast<std::size_t>(ym) * mnx + static_cast<std::size_t>(xm)];
    };
    /* Returns false when the cell is already taken, which ends the line. */
    auto update_mask = [&](double xg, double yg) {
        const int xm = static_cast<int>(std::nearbyint(xg * x_grid2mask));
        const int ym = static_cast<int>(std::nearbyint(yg * y_grid2mask));
        if (xm == cur_xm && ym == cur_ym) {
            return true;
        }
        if (xm < 0 || ym < 0 || xm >= mnx || ym >= mny) {
            return false;
        }
        if (mask_at(xm, ym) != 0) {
            return false;
        }
        mask_at(xm, ym) = 1;
        claimed.emplace_back(xm, ym);
        cur_xm = xm;
        cur_ym = ym;
        return true;
    };

    /* Second-order Runge-Kutta with an embedded Euler error estimate
       (_integrate_rk12). Step accepted when the two disagree by less than
       maxerror in axes coordinates, resized by 0.85 * (tol/err)^(1/2). */
    auto integrate_one = [&](double x0, double y0, bool backward, double &stotal,
                             std::vector<Point> &traj) {
        double ds = maxds;
        stotal = 0.0;
        double xi = x0, yi = y0;

        for (;;) {
            double k1x = 0.0, k1y = 0.0, k2x = 0.0, k2y = 0.0;
            bool out = false;
            bool terminate = false;

            if (grid.within(xi, yi)) {
                traj.push_back(Point{xi, yi});
                const Step s1 = field(xi, yi, backward, k1x, k1y);
                if (s1 == Step::OutOfBounds) {
                    out = true;
                } else if (s1 == Step::Terminate) {
                    terminate = true;
                } else {
                    const Step s2 =
                        field(xi + ds * k1x, yi + ds * k1y, backward, k2x, k2y);
                    if (s2 == Step::OutOfBounds) {
                        out = true;
                    } else if (s2 == Step::Terminate) {
                        terminate = true;
                    }
                }
            } else {
                out = true;
            }

            if (terminate) {
                break;
            }
            if (out) {
                /* An Euler step straight to the boundary, so the line ends on
                   the frame rather than a step short of it. */
                if (!traj.empty()) {
                    const Point last = traj.back();
                    double cx = 0.0, cy = 0.0;
                    if (field(last.x, last.y, backward, cx, cy) == Step::Ok) {
                        const double inf = std::numeric_limits<double>::infinity();
                        const double dsx = cx == 0.0 ? inf
                                           : cx < 0.0
                                               ? last.x / -cx
                                               : (static_cast<double>(cols) - 1.0 - last.x) / cx;
                        const double dsy = cy == 0.0 ? inf
                                           : cy < 0.0
                                               ? last.y / -cy
                                               : (static_cast<double>(rows) - 1.0 - last.y) / cy;
                        const double step = std::min(dsx, dsy);
                        if (std::isfinite(step)) {
                            traj.push_back(Point{last.x + cx * step, last.y + cy * step});
                            stotal += step;
                        }
                    }
                }
                break;
            }

            const double dx1 = ds * k1x;
            const double dy1 = ds * k1y;
            const double dx2 = ds * 0.5 * (k1x + k2x);
            const double dy2 = ds * 0.5 * (k1y + k2y);
            const double error = std::hypot((dx2 - dx1) / (static_cast<double>(cols) - 1.0),
                                            (dy2 - dy1) / (static_cast<double>(rows) - 1.0));

            if (error < maxerror) {
                xi += dx2;
                yi += dy2;
                /* A step landing outside the grid ends the line where it stands,
                   with no Euler step to the boundary: matplotlib's
                   DomainMap.update_trajectory checks within_grid before touching
                   the mask, and the integrator breaks on the resulting error. */
                if (!grid.within(xi, yi)) {
                    break;
                }
                if (!update_mask(xi, yi)) {
                    break;
                }
                if (stotal + ds > maxlength) {
                    break;
                }
                stotal += ds;
            }

            ds = error == 0.0 ? maxds
                              : std::min(maxds, 0.85 * ds * std::sqrt(maxerror / error));
        }
    };

    const bool debug = std::getenv("VPL_STREAM_DEBUG") != nullptr;
    int seed_index = -1;

    for (const std::pair<int, int> &seed : spiral_seeds(mnx, mny)) {
        ++seed_index;
        if (mask_at(seed.first, seed.second) != 0) {
            continue;
        }
        /* Mask cell back to grid coordinates: the seed sits at the cell's corner,
           not its centre. Multiply by the reciprocal; see above. */
        const double xg = seed.first * x_mask2grid;
        const double yg = seed.second * y_mask2grid;

        /* cur_xm/cur_ym carry over from the previous trajectory, matching
           matplotlib's StreamMask._start_trajectory, which clears its cell list
           but not its _current_xy: a new seed on a just-released cell is not
           re-claimed, leaving the cell for the line to claim on its own terms. */
        claimed.clear();
        if (!update_mask(xg, yg)) {
            continue;
        }

        std::vector<Point> back_traj, fwd_traj;
        double s_back = 0.0, s_fwd = 0.0;
        integrate_one(xg, yg, /*backward=*/true, s_back, back_traj);
        /* The forward leg resumes from the seed's own mask cell rather than
           wherever the backward leg finished. */
        cur_xm = static_cast<int>(std::nearbyint(xg * x_grid2mask));
        cur_ym = static_cast<int>(std::nearbyint(yg * y_grid2mask));
        integrate_one(xg, yg, /*backward=*/false, s_fwd, fwd_traj);

        if (s_back + s_fwd <= minlength) {
            /* Too short to draw; its cells return to the pool for other lines. */
            for (const std::pair<int, int> &c : claimed) {
                mask_at(c.first, c.second) = 0;
            }
            if (debug) {
                std::fprintf(stderr, "seed %4d (%2d,%2d) -> reject\n", seed_index,
                             seed.first, seed.second);
            }
            continue;
        }
        if (debug) {
            std::fprintf(stderr, "seed %4d (%2d,%2d) -> KEEP n=%3zu claimed=%3zu\n",
                         seed_index, seed.first, seed.second,
                         back_traj.size() + (fwd_traj.empty() ? 0 : fwd_traj.size() - 1),
                         claimed.size());
        }

        std::vector<Point> full;
        full.reserve(back_traj.size() + fwd_traj.size());
        for (auto it = back_traj.rbegin(); it != back_traj.rend(); ++it) {
            full.push_back(*it);
        }
        for (std::size_t i = 1; i < fwd_traj.size(); ++i) {
            full.push_back(fwd_traj[i]);
        }
        if (full.size() < 2) {
            continue;
        }

        Path line;
        std::vector<double> arclen(full.size(), 0.0);
        for (std::size_t i = 0; i < full.size(); ++i) {
            const double dxv = grid.x_origin + full[i].x * grid.dx;
            const double dyv = grid.y_origin + full[i].y * grid.dy;
            if (i == 0) {
                line.move_to(dxv, dyv);
            } else {
                line.line_to(dxv, dyv);
                arclen[i] = arclen[i - 1] + std::hypot(dxv - line.vertices[2 * (i - 1)],
                                                       dyv - line.vertices[2 * i - 1]);
            }
        }
        sp->lines.push_back(std::move(line));

        /* One arrow halfway along by arc length, not step count (steps bunch up
           where the field turns). matplotlib's cumulative array is per segment
           starting at the first length; arclen here is per vertex starting at
           zero, so the equivalent search is over arclen[i + 1]. */
        const Path &added = sp->lines.back();
        const double half = arclen.back() / 2.0;
        std::size_t idx = 0;
        while (idx + 2 < arclen.size() && arclen[idx + 1] < half) {
            ++idx;
        }
        if (idx + 1 < full.size()) {
            StreamPlot::Arrow a;
            a.tail_x = added.vertices[2 * idx];
            a.tail_y = added.vertices[2 * idx + 1];
            a.head_x = 0.5 * (added.vertices[2 * idx] + added.vertices[2 * idx + 2]);
            a.head_y = 0.5 * (added.vertices[2 * idx + 1] + added.vertices[2 * idx + 3]);
            sp->arrows.push_back(a);
        }
    }

    /* streamplot pins the view to the grid via sticky edges on the
       LineCollection, so the usual 5% margins do not apply. */
    set_xlim(sp->x0, sp->x1);
    set_ylim(sp->y0, sp->y1);

    /* VPL_STREAM_DEBUG=1 logs one line per seed: spiral index, mask cell, and
       whether the trajectory was kept or rejected as too short. */
    streams.push_back(std::move(sp));
    return *streams.back();
}

BarbField &Axes::barbs(const double *x, const double *y, const double *u, const double *v,
                       std::size_t n)
{
    auto b = std::make_unique<BarbField>();
    b->x.assign(x, x + n);
    b->y.assign(y, y + n);
    b->u.assign(u, u + n);
    b->v.assign(v, v + n);
    barb_fields.push_back(std::move(b));
    return *barb_fields.back();
}

QuiverField &Axes::quiver(const double *x, const double *y, const double *u,
                          const double *v, std::size_t n)
{
    auto q = std::make_unique<QuiverField>();
    q->x.assign(x, x + n);
    q->y.assign(y, y + n);
    q->u.assign(u, u + n);
    q->v.assign(v, v + n);
    quivers.push_back(std::move(q));
    return *quivers.back();
}

QuiverKey &Axes::quiverkey(const QuiverField &field, double x, double y, double magnitude,
                           const std::string &label)
{
    QuiverKey key;
    key.field = quivers.size();
    for (std::size_t i = 0; i < quivers.size(); ++i) {
        if (quivers[i].get() == &field) {
            key.field = i;
            break;
        }
    }
    key.x = x;
    key.y = y;
    key.magnitude = magnitude;
    key.label = label;
    quiver_keys.push_back(key);
    return quiver_keys.back();
}

Table &Axes::table(const std::string *cells, std::size_t rows, std::size_t cols)
{
    auto t = std::make_unique<Table>();
    t->rows = rows;
    t->cols = cols;
    t->cells.assign(cells, cells + rows * cols);
    tables.push_back(std::move(t));
    return *tables.back();
}

TextArtist &Axes::text(double x, double y, const std::string &s)
{
    auto t = std::make_unique<TextArtist>();
    t->x = x;
    t->y = y;
    t->text = s;
    texts.push_back(std::move(t));
    return *texts.back();
}

PolyPatch &Axes::barh(const double *y,
                      const double *width,
                      std::size_t n,
                      double height,
                      const double *left)
{
    auto patch = std::make_unique<PolyPatch>();

    const double half = height / 2.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double l = left != nullptr ? left[i] : 0.0;
        patch->path.move_to(l, y[i] - half);
        patch->path.line_to(l + width[i], y[i] - half);
        patch->path.line_to(l + width[i], y[i] + half);
        patch->path.line_to(l, y[i] + half);
        patch->path.close_poly();
    }

    patch->facecolor = next_patch_color();

    /* Horizontal bars stick to the X axis, each at its own left edge (barh's
       autoscale difference from bar's). */
    for (std::size_t i = 0; i < n; ++i) {
        const double l = left != nullptr ? left[i] : 0.0;
        if (std::find(sticky_x.begin(), sticky_x.end(), l) == sticky_x.end()) {
            sticky_x.push_back(l);
        }
    }

    patches.push_back(std::move(patch));
    return *patches.back();
}

PolyPatch &Axes::fill_betweenx(const double *y, const double *x1, const double *x2,
                               std::size_t n, const std::uint8_t *where, bool interpolate,
                               FillStep step)
{
    auto patch = std::make_unique<PolyPatch>();

    /* fill_between with the axes swapped: the varying coordinate is y, the two
       curves are x1 and x2, and each vertex comes out (x, y). */
    patch->path.vertices.reserve(4 * n + 2);
    build_fill_regions(patch->path, y, x1, x2, n, where, interpolate, step, /*swap_xy=*/true);

    patch->facecolor = next_patch_color();

    patches.push_back(std::move(patch));
    return *patches.back();
}

Line2D &Axes::step(const double *x, const double *y, std::size_t n, StepWhere where)
{
    /* matplotlib staircases at draw time via drawstyle; vplot expands here,
       drawing the same line without changing the data limits. The transforms
       (cbook.pts_to_{pre,post,mid}step) for x = [a, b, c]:
         pre    x -> [a, a, b, b, c]           y -> [ya, yb, yb, yc, yc]
         post   x -> [a, b, b, c, c]           y -> [ya, ya, yb, yb, yc]
         mid    x -> [a, m0, m0, m1, m1, c]    y -> [ya, ya, yb, yb, yc, yc]
       mid produces 2n vertices (vs 2n - 1) since both ends are half-treads. */
    std::vector<double> sx;
    std::vector<double> sy;
    if (n > 0 && where == StepWhere::Mid) {
        const std::size_t m = 2 * n;
        sx.resize(m);
        sy.resize(m);
        sx[0] = x[0];
        sx[m - 1] = x[n - 1];
        for (std::size_t i = 0; i + 1 < n; ++i) {
            const double mid = 0.5 * (x[i] + x[i + 1]);
            sx[2 * i + 1] = mid;
            sx[2 * i + 2] = mid;
        }
        for (std::size_t i = 0; i < n; ++i) {
            sy[2 * i] = y[i];
            sy[2 * i + 1] = y[i];
        }
    } else if (n > 0) {
        const std::size_t m = 2 * n - 1;
        sx.resize(m);
        sy.resize(m);
        for (std::size_t i = 0; i < n; ++i) {
            sx[2 * i] = x[i];
            sy[2 * i] = y[i];
        }
        for (std::size_t i = 0; i + 1 < n; ++i) {
            /* The only difference between pre and post: which end of the tread
               the riser stands on. */
            sx[2 * i + 1] = (where == StepWhere::Pre) ? x[i] : x[i + 1];
            sy[2 * i + 1] = (where == StepWhere::Pre) ? y[i + 1] : y[i];
        }
    }

    return plot(sx.data(), sy.data(), sx.size());
}

AxesSpan &Axes::axhspan(double y0, double y1)
{
    AxesSpan span;
    span.horizontal = true;
    span.lo = std::min(y0, y1);
    span.hi = std::max(y0, y1);
    /* patch.facecolor, not the property cycle: a span is a plain Patch, so every
       span is the same colour. */
    spans.push_back(span);
    return spans.back();
}

AxesSpan &Axes::axvspan(double x0, double x1)
{
    AxesSpan span;
    span.horizontal = false;
    span.lo = std::min(x0, x1);
    span.hi = std::max(x0, x1);
    /* patch.facecolor, not the property cycle: a span is a plain Patch, so every
       span is the same colour. */
    spans.push_back(span);
    return spans.back();
}

SegmentCollection &Axes::hlines(const double *y,
                               const double *xmin,
                               const double *xmax,
                               std::size_t n)
{
    auto sc = std::make_unique<SegmentCollection>();
    for (std::size_t i = 0; i < n; ++i) {
        sc->path.move_to(xmin[i], y[i]);
        sc->path.line_to(xmax[i], y[i]);
    }
    segment_collections.push_back(std::move(sc));
    return *segment_collections.back();
}

SegmentCollection &Axes::triplot(const double *x, const double *y, std::size_t n)
{
    auto sc = std::make_unique<SegmentCollection>();

    const std::vector<Triangle> tris = delaunay(x, y, n);
    for (const auto &[u, v] : triangulation_edges(tris)) {
        sc->path.move_to(x[u], y[u]);
        sc->path.line_to(x[v], y[v]);
    }

    segment_collections.push_back(std::move(sc));
    return *segment_collections.back();
}

void Axes::triplot(const double *x, const double *y, std::size_t n,
                   const std::vector<Triangle> &tris, const TriplotStyle &style)
{
    /* The edges: ax.plot of the NaN-separated edge list, marker 'None'. Its
       colour is taken from the cycle even when there is no line to draw, as
       ax.plot([], []) takes one. */
    const Color edge_color = style.has_color ? style.color : next_line_color();
    if (style.linestyle != LineStyle::None) {
        auto sc = std::make_unique<SegmentCollection>();
        sc->color = edge_color;
        sc->linestyle = style.linestyle;
        for (const auto &[u, v] : triangulation_edges(tris)) {
            sc->path.move_to(x[u], y[u]);
            sc->path.line_to(x[v], y[v]);
        }
        segment_collections.push_back(std::move(sc));
    }

    /* The nodes: every point, used by a triangle or not, linestyle 'None'. */
    const Color node_color = style.has_color ? style.color : next_line_color();
    if (style.marker != MarkerStyle::None) {
        Line2D &line = push_line(lines, x, y, n);
        line.color = node_color;
        line.linestyle = LineStyle::None;
        line.marker = style.marker;
    }
}

ContourSet &Axes::tricontour(const double *x, const double *y, const double *z,
                             std::size_t n, const double *levels, std::size_t nlevels)
{
    return tricontour(x, y, z, n, delaunay(x, y, n), levels, nlevels);
}

ContourSet &Axes::tricontour(const double *x, const double *y, const double *z,
                             std::size_t n, const std::vector<Triangle> &tris,
                             const double *levels, std::size_t nlevels)
{
    auto cs = std::make_unique<ContourSet>();
    cs->levels.assign(levels, levels + nlevels);
    cs->level_paths.resize(nlevels);

    double xmin = x[0], xmax = x[0], ymin = y[0], ymax = y[0];
    for (std::size_t i = 1; i < n; ++i) {
        xmin = std::min(xmin, x[i]);
        xmax = std::max(xmax, x[i]);
        ymin = std::min(ymin, y[i]);
        ymax = std::max(ymax, y[i]);
    }
    cs->x0 = xmin;
    cs->x1 = xmax;
    cs->y0 = ymin;
    cs->y1 = ymax;

    for (std::size_t k = 0; k < nlevels; ++k) {
        const double level = cs->levels[k];
        std::vector<std::array<double, 4>> segs;

        for (const Triangle &t : tris) {
            const int v[3] = {t.a, t.b, t.c};

            /* Where the level crosses each edge. The field is linear over the
               triangle, so there are either no crossings or exactly two. */
            double cx[3], cy[3];
            int found = 0;
            int above = -1;
            for (int e = 0; e < 3; ++e) {
                const int p = v[e];
                const int q = v[(e + 1) % 3];
                if (!std::isfinite(z[p]) || !std::isfinite(z[q])) {
                    found = 0;
                    break;
                }
                if (z[p] > level) {
                    above = p;
                }
                if ((z[p] > level) == (z[q] > level)) {
                    continue;
                }
                /* Interpolated from the lower index to the higher, so the two
                   triangles sharing this edge get the very same point and
                   chain_segments can join them on exact coordinates. */
                const int lo = std::min(p, q);
                const int hi = std::max(p, q);
                const double denom = z[hi] - z[lo];
                const double s = std::fabs(denom) < 1e-300 ? 0.0 : (level - z[lo]) / denom;
                if (found < 3) {
                    cx[found] = x[lo] + s * (x[hi] - x[lo]);
                    cy[found] = y[lo] + s * (y[hi] - y[lo]);
                    ++found;
                }
            }

            if (found == 2 && above >= 0) {
                /* matplotlib's TriContourGenerator runs each line with the
                   higher values on its left. */
                const double cross = (cx[1] - cx[0]) * (y[above] - cy[0]) -
                                     (cy[1] - cy[0]) * (x[above] - cx[0]);
                if (cross >= 0.0) {
                    segs.push_back({cx[0], cy[0], cx[1], cy[1]});
                } else {
                    segs.push_back({cx[1], cy[1], cx[0], cy[0]});
                }
            }
        }
        /* Joined into whole lines, so a dash pattern runs along the line
           rather than restarting at every triangle. */
        chain_segments(segs, {}, cs->level_paths[k]);
    }

    /* tricontour finishes with autoscale_view(tight=True), like contour. */
    tight_view = true;

    contours.push_back(std::move(cs));
    return *contours.back();
}

/* One triangle clipped to the band [lo, hi] as a convex polygon:
   Sutherland-Hodgman against two half-planes in z. The field is linear over the
   triangle, so each level is a straight cut and clipping is exact (a triangle
   holds no saddle). Result is convex with at most five vertices. */
namespace {

struct BandPt
{
    double x, y, z;
};

void clip_half(std::vector<BandPt> &poly, double level, bool keep_above)
{
    if (poly.empty()) {
        return;
    }
    auto inside = [&](const BandPt &p) { return keep_above ? p.z >= level : p.z <= level; };

    std::vector<BandPt> out;
    out.reserve(poly.size() + 1);
    const std::size_t n = poly.size();
    for (std::size_t i = 0; i < n; ++i) {
        const BandPt &a = poly[i];
        const BandPt &b = poly[(i + 1) % n];
        const bool ain = inside(a);
        if (ain) {
            out.push_back(a);
        }
        if (ain != inside(b)) {
            const double d = b.z - a.z;
            const double t = d == 0.0 ? 0.0 : (level - a.z) / d;
            out.push_back(BandPt{a.x + t * (b.x - a.x), a.y + t * (b.y - a.y), level});
        }
    }
    poly.swap(out);
}

} /* namespace */

ContourSet &Axes::tricontourf(const double *x, const double *y, const double *z,
                              std::size_t n, const double *levels, std::size_t nlevels)
{
    return tricontourf(x, y, z, n, delaunay(x, y, n), levels, nlevels,
                       ContourExtend::Neither);
}

ContourSet &Axes::tricontourf(const double *x, const double *y, const double *z,
                              std::size_t n, const std::vector<Triangle> &tris,
                              const double *levels, std::size_t nlevels,
                              ContourExtend extend)
{
    ContourSet &cs = tricontour(x, y, z, n, tris, levels, nlevels);
    cs.filled = true;
    cs.extend = extend;
    cs.level_paths.clear(); /* filled contours draw no lines of their own */

    if (nlevels < 2) {
        return cs;
    }

    /* As contourf: an extended end's band reaches to +/-1e250. */
    cs.band_levels = cs.levels;
    if (extend == ContourExtend::Min || extend == ContourExtend::Both) {
        cs.band_levels.insert(cs.band_levels.begin(), -1e250);
    }
    if (extend == ContourExtend::Max || extend == ContourExtend::Both) {
        cs.band_levels.push_back(1e250);
    }
    const std::size_t nbands = cs.band_levels.size() - 1;

    cs.band_paths.resize(nbands);
    for (std::size_t b = 0; b < nbands; ++b) {
        const double lo = cs.band_levels[b];
        const double hi = cs.band_levels[b + 1];
        Path &path = cs.band_paths[b];

        for (const Triangle &t : tris) {
            const int v[3] = {t.a, t.b, t.c};
            if (!std::isfinite(z[v[0]]) || !std::isfinite(z[v[1]]) ||
                !std::isfinite(z[v[2]])) {
                continue;
            }

            std::vector<BandPt> poly;
            poly.reserve(5);
            for (int e = 0; e < 3; ++e) {
                poly.push_back(BandPt{x[v[e]], y[v[e]], z[v[e]]});
            }
            clip_half(poly, lo, /*keep_above=*/true);
            clip_half(poly, hi, /*keep_above=*/false);

            if (poly.size() < 3) {
                continue;
            }
            /* Every fragment of a band goes into one path, as in contourf, so
               shared edges of abutting polygons cancel instead of leaving a seam. */
            path.move_to(poly[0].x, poly[0].y);
            for (std::size_t i = 1; i < poly.size(); ++i) {
                path.line_to(poly[i].x, poly[i].y);
            }
            path.close_poly();
        }
    }

    return cs;
}

void Axes::tripcolor(const double *x, const double *y, const double *z, std::size_t n)
{
    const std::vector<Triangle> tris = delaunay(x, y, n);
    if (tris.empty()) {
        return;
    }

    /* One value per triangle: the mean of its three vertices, which is what
       shading='flat' means. */
    std::vector<double> facevals;
    facevals.reserve(tris.size());
    for (const Triangle &t : tris) {
        facevals.push_back((z[t.a] + z[t.b] + z[t.c]) / 3.0);
    }
    tripcolor(x, y, n, tris, facevals, TripcolorOptions{});
}

void Axes::tripcolor(const double *x, const double *y, std::size_t n,
                     const std::vector<Triangle> &tris, const std::vector<double> &facevals,
                     const TripcolorOptions &options)
{
    double vmin = std::numeric_limits<double>::infinity();
    double vmax = -vmin;
    for (double v : facevals) {
        if (!std::isfinite(v)) {
            continue;
        }
        vmin = std::min(vmin, v);
        vmax = std::max(vmax, v);
    }
    if (options.has_vlimits) {
        vmin = options.vmin;
        vmax = options.vmax;
    }

    /* update_datalim over every point, then the grid off, even when no
       triangle survives the mask. */
    if (n > 0) {
        double x0 = x[0], x1 = x[0], y0 = y[0], y1 = y[0];
        for (std::size_t i = 1; i < n; ++i) {
            x0 = std::min(x0, x[i]);
            x1 = std::max(x1, x[i]);
            y0 = std::min(y0, y[i]);
            y1 = std::max(y1, y[i]);
        }
        extra_datalim.push_back(Bbox(x0, y0, x1, y1));
    }
    xgrid_major = ygrid_major = xgrid_minor = ygrid_minor = false;
    if (!std::isfinite(vmin) || !std::isfinite(vmax)) {
        return;
    }

    for (std::size_t i = 0; i < tris.size(); ++i) {
        const Triangle &t = tris[i];
        auto patch = std::make_unique<PolyPatch>();
        patch->path.move_to(x[t.a], y[t.a]);
        patch->path.line_to(x[t.b], y[t.b]);
        patch->path.line_to(x[t.c], y[t.c]);
        patch->path.close_poly();
        patch->facecolor =
            colormap_lookup(options.cmap, normalize(facevals[i], vmin, vmax));
        patch->alpha = options.alpha;
        /* No edge and no antialiasing: tripcolor's PolyCollection has empty
           edgecolor (linewidth 0, no stroke) and antialiased=False. The
           antialiasing, not a missing edge, is what veins an untreated mesh. */
        patch->antialiased = false;
        patch->join = JoinStyle::Round; /* Collection default */
        patches.push_back(std::move(patch));
    }

    /* tripcolor calls ax.grid(False): grid lines fight a mesh of filled cells. */
    xgrid_major = ygrid_major = xgrid_minor = ygrid_minor = false;
}

SegmentCollection &Axes::vlines(const double *x,
                               const double *ymin,
                               const double *ymax,
                               std::size_t n)
{
    auto sc = std::make_unique<SegmentCollection>();
    for (std::size_t i = 0; i < n; ++i) {
        sc->path.move_to(x[i], ymin[i]);
        sc->path.line_to(x[i], ymax[i]);
    }
    segment_collections.push_back(std::move(sc));
    return *segment_collections.back();
}

SegmentCollection &Axes::eventplot(const double *positions,
                                   std::size_t n,
                                   double lineoffset,
                                   double linelength,
                                   bool horizontal)
{
    auto sc = std::make_unique<SegmentCollection>();

    /* Each mark is centred on the offset, running half a length either side.
       Emitted from the far end back to keep matplotlib's vertex order. */
    const double half = linelength / 2.0;
    for (std::size_t i = 0; i < n; ++i) {
        if (horizontal) {
            sc->path.move_to(positions[i], lineoffset + half);
            sc->path.line_to(positions[i], lineoffset - half);
        } else {
            sc->path.move_to(lineoffset + half, positions[i]);
            sc->path.line_to(lineoffset - half, positions[i]);
        }
    }

    /* The cross-axis limits run a whole linelength either side of the offset, not
       the half a mark is drawn over, which keeps a rug clear of the frame. */
    if (n != 0) {
        double lo = positions[0];
        double hi = positions[0];
        for (std::size_t i = 1; i < n; ++i) {
            lo = std::min(lo, positions[i]);
            hi = std::max(hi, positions[i]);
        }
        const double a = lineoffset - linelength;
        const double b = lineoffset + linelength;
        extra_datalim.push_back(horizontal ? Bbox(lo, a, hi, b) : Bbox(a, lo, b, hi));
    }

    segment_collections.push_back(std::move(sc));
    return *segment_collections.back();
}

void Axes::stem(const double *x, const double *y, std::size_t n, const StemStyle &style)
{
    if (n == 0) {
        return;
    }

    /* Three artists, added in matplotlib's order -- stalks, then heads, then
       the baseline on top. All three are zorder 2, so the order they go in is
       the order they are drawn. */
    auto stalks = std::make_unique<SegmentCollection>();
    stalks->color = style.line_color;
    stalks->linestyle = style.line_style;
    for (std::size_t i = 0; i < n; ++i) {
        stalks->path.move_to(x[i], style.bottom);
        stalks->path.line_to(x[i], y[i]);
    }
    segment_collections.push_back(std::move(stalks));

    /* Heads are a Line2D with markers and no line (linefmt drives the stalks,
       markerfmt the heads). Built without touching the cycle. */
    Line2D &heads = push_line(lines, x, y, n);
    heads.linestyle = LineStyle::None;
    heads.marker = style.marker;
    heads.color = style.marker_color;

    /* The baseline spans the data's x range at `bottom` (C3 by default). */
    double lo = x[0];
    double hi = x[0];
    for (std::size_t i = 1; i < n; ++i) {
        lo = std::min(lo, x[i]);
        hi = std::max(hi, x[i]);
    }
    const double bx[2] = {lo, hi};
    const double by[2] = {style.bottom, style.bottom};
    Line2D &baseline = push_line(lines, bx, by, 2);
    baseline.color = style.base_color;
    baseline.linestyle = style.base_style;
}

PolyPatch &Axes::stairs(const double *values, const double *edges, std::size_t n, bool fill,
                        const double *baseline, std::size_t baseline_n, bool horizontal)
{
    auto patch = std::make_unique<PolyPatch>();
    if (n == 0) {
        patches.push_back(std::move(patch));
        return *patches.back();
    }

    /* baseline: default scalar 0 (count 0), an explicit scalar (count 1), or None
       (an open top with no descent), which the caller signals with a scalar NaN. */
    const bool none = baseline != nullptr && baseline_n == 1 && std::isnan(baseline[0]);
    const bool has_base = !none;
    const double base = (baseline == nullptr || baseline_n == 0) ? 0.0 : baseline[0];
    auto bl = [&](std::size_t) -> double { return base; };

    /* A vertex in (edge, value) space, swapped to (value, edge) when horizontal. */
    auto vert = [&](double e, double v, bool first) {
        const double vx = horizontal ? v : e;
        const double vy = horizontal ? e : v;
        if (first) {
            patch->path.move_to(vx, vy);
        } else {
            patch->path.line_to(vx, vy);
        }
    };

    /* Top staircase: up from the baseline at the first edge (or straight in at
       the value with no baseline), through every step, back down at the last. */
    vert(edges[0], has_base ? bl(0) : values[0], true);
    vert(edges[0], values[0], false);
    for (std::size_t i = 0; i < n; ++i) {
        vert(edges[i + 1], values[i], false);
        if (i + 1 < n) {
            vert(edges[i + 1], values[i + 1], false);
        }
    }
    vert(edges[n], has_base ? bl(n - 1) : values[n - 1], false);

    if (fill) {
        /* fill=True: filled in C0, no edge -- matplotlib swaps the two. */
        patch->facecolor = Color::rgb(0.121569, 0.466667, 0.705882);
        patch->edgecolor = Color::none();
        patch->linewidth = 0.0;
        patch->path.close_poly();
    } else {
        patch->facecolor = Color::none();
        patch->edgecolor = Color::rgb(0.121569, 0.466667, 0.705882);
        patch->linewidth = 1.0;
    }

    /* matplotlib pins the sticky edge to a scalar baseline (the default 0
       included), but not to None. */
    if (has_base) {
        std::vector<double> &sticky = horizontal ? sticky_x : sticky_y;
        if (std::find(sticky.begin(), sticky.end(), base) == sticky.end()) {
            sticky.push_back(base);
        }
    }

    patches.push_back(std::move(patch));
    return *patches.back();
}

PolyPatch &Axes::fill(const double *x, const double *y, std::size_t n)
{
    auto patch = std::make_unique<PolyPatch>();
    if (n != 0) {
        patch->path.move_to(x[0], y[0]);
        for (std::size_t i = 1; i < n; ++i) {
            patch->path.line_to(x[i], y[i]);
        }
        patch->path.close_poly();
    }
    patch->facecolor = next_patch_color();
    patches.push_back(std::move(patch));
    return *patches.back();
}

PolyPatch &Axes::broken_barh(const double *xstart,
                             const double *xwidth,
                             std::size_t n,
                             double y0,
                             double height)
{
    auto patch = std::make_unique<PolyPatch>();
    for (std::size_t i = 0; i < n; ++i) {
        const double a = xstart[i];
        const double b = xstart[i] + xwidth[i];
        patch->path.move_to(a, y0);
        patch->path.line_to(b, y0);
        patch->path.line_to(b, y0 + height);
        patch->path.line_to(a, y0 + height);
        patch->path.close_poly();
    }
    /* Fixed at C0: broken_barh does not take the next colour of the cycle. */
    patch->facecolor = Color::rgb(0.121569, 0.466667, 0.705882);
    patches.push_back(std::move(patch));
    return *patches.back();
}

void Axes::stackplot(const double *x, const double *ys, std::size_t nseries, std::size_t n,
                     StackBaseline baseline)
{
    if (nseries == 0 || n == 0) {
        return;
    }

    /* The baseline is the bottom curve the whole stack sits on. 'zero' is a flat
       floor; the other three shift every band by a `first_line` (the streamgraph
       layouts), per matplotlib's stackplot. `y[i][j]` is `ys[i * n + j]`. */
    auto y = [&](std::size_t i, std::size_t j) { return ys[i * n + j]; };
    std::vector<double> first_line(n, 0.0);
    if (baseline == StackBaseline::Sym) {
        for (std::size_t j = 0; j < n; ++j) {
            double s = 0.0;
            for (std::size_t i = 0; i < nseries; ++i) {
                s += y(i, j);
            }
            first_line[j] = -0.5 * s;
        }
    } else if (baseline == StackBaseline::Wiggle) {
        const double m = static_cast<double>(nseries);
        for (std::size_t j = 0; j < n; ++j) {
            double s = 0.0;
            for (std::size_t i = 0; i < nseries; ++i) {
                s += y(i, j) * (m - 0.5 - static_cast<double>(i));
            }
            first_line[j] = -s / m;
        }
    } else if (baseline == StackBaseline::WeightedWiggle) {
        double running_center = 0.0;
        for (std::size_t j = 0; j < n; ++j) {
            double total = 0.0;
            for (std::size_t i = 0; i < nseries; ++i) {
                total += y(i, j);
            }
            const double inv_total = total > 0.0 ? 1.0 / total : 0.0;
            double stack_ij = 0.0;
            double contrib = 0.0;
            for (std::size_t i = 0; i < nseries; ++i) {
                stack_ij += y(i, j); /* cumsum over series 0..i */
                const double increase = (j == 0) ? y(i, 0) : y(i, j) - y(i, j - 1);
                const double below_size = total - stack_ij + 0.5 * y(i, j);
                const double move_up = (j == 0) ? 0.5 : below_size * inv_total;
                contrib += (move_up - 0.5) * increase;
            }
            running_center += contrib;
            first_line[j] = running_center - 0.5 * total;
        }
    }

    /* Each band is filled between the running total below it and its own top,
       making a stack rather than overlapping fills. */
    std::vector<double> lower = first_line;
    std::vector<double> upper(n, 0.0);

    for (std::size_t si = 0; si < nseries; ++si) {
        for (std::size_t i = 0; i < n; ++i) {
            upper[i] = lower[i] + ys[si * n + i];
        }

        auto patch = std::make_unique<PolyPatch>();
        for (std::size_t i = 0; i < n; ++i) {
            (i == 0 ? patch->path.move_to(x[i], lower[i])
                    : patch->path.line_to(x[i], lower[i]));
        }
        for (std::size_t i = n; i-- > 0;) {
            patch->path.line_to(x[i], upper[i]);
        }
        patch->path.close_poly();

        /* A stack draws from the line cycle, one colour per series. */
        patch->facecolor = next_line_color();
        patches.push_back(std::move(patch));

        /* The stack sits on the baseline: matplotlib sets the sticky edge by hand
           on the first band only, so a stackplot starts at zero. */
        if (si == 0 && std::find(sticky_y.begin(), sticky_y.end(), 0.0) == sticky_y.end()) {
            sticky_y.push_back(0.0);
        }

        lower = upper;
    }
}

namespace
{

/* numpy.percentile's default: the value at fractional index (n - 1) * q of the
   sorted data, interpolated linearly between neighbours. */
double percentile(const std::vector<double> &sorted, double q)
{
    if (sorted.empty()) {
        return 0.0;
    }
    if (sorted.size() == 1) {
        return sorted[0];
    }
    const double pos = (static_cast<double>(sorted.size()) - 1.0) * q;
    const std::size_t lo = static_cast<std::size_t>(std::floor(pos));
    const std::size_t hi = static_cast<std::size_t>(std::ceil(pos));
    if (lo == hi) {
        return sorted[lo];
    }
    const double frac = pos - static_cast<double>(lo);
    return sorted[lo] + frac * (sorted[hi] - sorted[lo]);
}

} // namespace

void Axes::bxp(const BoxStats *stats, std::size_t ngroups)
{
    bxp(stats, ngroups, BoxOptions{});
}

namespace
{

/* A position as matplotlib's FixedFormatter prints it: positions arrive as a
   numpy array, so whole numbers from the default range print as integers and
   anything else as Python's float repr ("0.5", "2.0"). */
std::string position_label(double v, bool all_integral)
{
    char buf[64];
    if (all_integral) {
        std::snprintf(buf, sizeof(buf), "%.0f", v);
        return buf;
    }
    for (int prec = 1; prec <= 17; ++prec) {
        std::snprintf(buf, sizeof(buf), "%.*g", prec, v);
        if (std::strtod(buf, nullptr) == v) {
            break;
        }
    }
    std::string s = buf;
    if (s.find_first_of(".en") == std::string::npos) {
        s += ".0";
    }
    return s;
}

} // namespace

void Axes::bxp(const BoxStats *stats, std::size_t ngroups, const BoxOptions &opt)
{
    if (ngroups == 0) {
        return;
    }

    const Color black = Color::rgb(0.0, 0.0, 0.0);
    const Color c1 = Color::rgb(1.0, 0.498039, 0.054902); /* the median, and only it */
    const Color c2 = Color::rgb(0.172549, 0.627451, 0.172549); /* the mean */

    std::vector<double> positions(ngroups);
    for (std::size_t g = 0; g < ngroups; ++g) {
        positions[g] = opt.positions != nullptr ? opt.positions[g] : static_cast<double>(g) + 1.0;
    }
    const auto [pmin, pmax] = std::minmax_element(positions.begin(), positions.end());

    /* Width follows the span (np.ptp) of the positions, clipped to [0.15, 0.5],
       so boxes do not shrink to threads as groups are added. */
    const double auto_width = std::min(std::max(0.15 * (*pmax - *pmin), 0.15), 0.5);

    for (std::size_t g = 0; g < ngroups; ++g) {
        const BoxStats &s = stats[g];
        const double pos = positions[g];
        const double width = opt.widths != nullptr ? opt.widths[g] : auto_width;
        const double half = width / 2.0;
        /* capwidths defaults to half the box. */
        const double cap_half = (opt.capwidths != nullptr ? opt.capwidths[g] : width / 2.0) / 2.0;
        const std::string *legend_label =
            opt.legend_labels != nullptr && !opt.legend_labels[g].empty() ? &opt.legend_labels[g]
                                                                         : nullptr;

        /* A group with no summary draws nothing but still holds its position,
           as matplotlib does (an empty group keeps its slot). */
        if (!std::isfinite(s.med) || !std::isfinite(s.q1) || !std::isfinite(s.q3)) {
            continue;
        }

        /* Every piece is coloured explicitly, so none of them takes a turn of
           the cycle -- a boxplot leaves the next plot() at C0. `xs` run across
           the box and `ys` along the data; horizontal swaps them, as
           matplotlib's do_plot does. */
        auto add = [&](const double *xs, const double *ys, std::size_t m, Color c) -> Line2D & {
            Line2D &l = opt.horizontal ? push_line(lines, ys, xs, m) : push_line(lines, xs, ys, m);
            l.color = c;
            l.linewidth = 1.0;
            return l;
        };
        /* A *props dictionary over the rcParams defaults already on the line. */
        auto style = [](Line2D &l, const BoxElementProps &p) -> Line2D & {
            if (p.color) {
                l.color = *p.color;
            }
            if (p.linewidth) {
                l.linewidth = *p.linewidth;
            }
            if (p.linestyle) {
                l.linestyle = *p.linestyle;
            }
            if (p.marker) {
                l.marker = *p.marker;
            }
            if (p.markersize) {
                l.markersize = *p.markersize;
            }
            if (p.markeredgewidth) {
                l.markeredgewidth = *p.markeredgewidth;
            }
            if (p.markerfacecolor) {
                l.has_markerfacecolor = true;
                l.markerfacecolor = *p.markerfacecolor;
            }
            if (p.markeredgecolor) {
                l.has_markeredgecolor = true;
                l.markeredgecolor = *p.markeredgecolor;
            }
            return l;
        };

        const double box_l = pos - half;
        const double box_r = pos + half;
        std::vector<double> bx = {box_l, box_r, box_r, box_l, box_l};
        std::vector<double> by = {s.q1, s.q1, s.q3, s.q3, s.q1};
        double med_l = box_l;
        double med_r = box_r;
        if (opt.shownotches) {
            const double nl = pos - width * 0.25;
            const double nr = pos + width * 0.25;
            bx = {box_l, box_r, box_r, nr, box_r, box_r, box_l, box_l, nl, box_l, box_l};
            by = {s.q1, s.q1, s.cilo, s.med, s.cihi, s.q3, s.q3, s.cihi, s.med, s.cilo, s.q1};
            med_l = nl;
            med_r = nr;
        }

        /* matplotlib's order, because these overlap: box, whiskers, caps,
           median, mean, fliers. */
        if (opt.showbox) {
            if (opt.patch_artist) {
                /* A PathPatch at zorder 2: C0 face, black edge, among the lines. */
                auto box = std::make_unique<PolyPatch>();
                for (std::size_t k = 0; k < bx.size(); ++k) {
                    const double px = opt.horizontal ? by[k] : bx[k];
                    const double py = opt.horizontal ? bx[k] : by[k];
                    if (k == 0) {
                        box->path.move_to(px, py);
                    } else if (k + 1 < bx.size()) {
                        box->path.line_to(px, py);
                    }
                }
                box->path.close_poly();
                const BoxElementProps &bp = opt.boxprops;
                box->facecolor = bp.facecolor.value_or(Color::rgb(0.121569, 0.466667, 0.705882));
                box->edgecolor = bp.edgecolor.value_or(black);
                if (bp.color) {
                    /* Patch's color wins over facecolor and edgecolor. */
                    box->facecolor = *bp.color;
                    box->edgecolor = *bp.color;
                }
                box->linewidth = bp.linewidth.value_or(1.0);
                box->alpha = bp.alpha.value_or(1.0);
                box->line_tier = true;
                if (legend_label != nullptr) {
                    box->label = *legend_label;
                }
                patches.push_back(std::move(box));
            } else {
                style(add(bx.data(), by.data(), bx.size(), black), opt.boxprops);
            }
        }

        const double wx[2] = {pos, pos};
        const double wlo[2] = {s.q1, s.whislo};
        const double whi[2] = {s.q3, s.whishi};
        style(add(wx, wlo, 2, black), opt.whiskerprops);
        style(add(wx, whi, 2, black), opt.whiskerprops);

        if (opt.showcaps) {
            const double cx[2] = {pos - cap_half, pos + cap_half};
            const double clo[2] = {s.whislo, s.whislo};
            const double chi[2] = {s.whishi, s.whishi};
            style(add(cx, clo, 2, black), opt.capprops);
            style(add(cx, chi, 2, black), opt.capprops);
        }

        const double mx[2] = {med_l, med_r};
        const double my[2] = {s.med, s.med};
        Line2D &median = style(add(mx, my, 2, c1), opt.medianprops);
        median.butt_cap = true;
        if (legend_label != nullptr && !(opt.showbox && opt.patch_artist)) {
            median.label = *legend_label;
        }

        if (opt.showmeans && std::isfinite(s.mean)) {
            /* meanprops' rcParams carry both a marker and a dashed style;
               meanline drops whichever does not apply unless meanprops sets
               it explicitly. */
            const BoxElementProps &mp = opt.meanprops;
            Line2D *ml = nullptr;
            if (opt.meanline) {
                const double lx[2] = {box_l, box_r};
                const double ly[2] = {s.mean, s.mean};
                ml = &add(lx, ly, 2, c2);
            } else {
                const double px[1] = {pos};
                const double py[1] = {s.mean};
                ml = &add(px, py, 1, c2);
            }
            ml->linestyle = opt.meanline ? LineStyle::Dashed : LineStyle::None;
            ml->marker = opt.meanline ? MarkerStyle::None : MarkerStyle::TriangleUp;
            ml->has_markerfacecolor = true;
            ml->markerfacecolor = c2;
            ml->has_markeredgecolor = true;
            ml->markeredgecolor = c2;
            ml->butt_cap = true;
            style(*ml, mp);
        }

        if (opt.showfliers && !s.fliers.empty()) {
            std::vector<double> fx(s.fliers.size(), pos);
            Line2D &fl = add(fx.data(), s.fliers.data(), s.fliers.size(), black);
            fl.linestyle = LineStyle::None;
            fl.marker = MarkerStyle::Circle;
            /* Hollow: the flier marker is an outline, so a cluster of them
               stays countable. */
            fl.has_markerfacecolor = true;
            fl.markerfacecolor = Color::none();
            fl.has_markeredgecolor = true;
            fl.markeredgecolor = black;
            style(fl, opt.flierprops);
        }
    }

    if (!opt.manage_ticks) {
        return;
    }
    /* Half a unit either side of the outermost box, pinned rather than
       autoscaled, with a tick per box labelled by its position. */
    bool all_integral = true;
    for (double p : positions) {
        all_integral = all_integral && p == std::floor(p);
    }
    std::vector<double> &ticks = opt.horizontal ? yticks_fixed : xticks_fixed;
    std::vector<std::string> &tick_labels = opt.horizontal ? yticklabels_fixed : xticklabels_fixed;
    ticks = positions;
    tick_labels.clear();
    for (std::size_t g = 0; g < ngroups; ++g) {
        tick_labels.push_back(opt.labels != nullptr ? opt.labels[g]
                                                    : position_label(positions[g], all_integral));
    }
    if (opt.horizontal) {
        set_ylim(*pmin - 0.5, *pmax + 0.5);
        yticks_fixed_set = true;
    } else {
        set_xlim(*pmin - 0.5, *pmax + 0.5);
        xticks_fixed_set = true;
    }
}

void Axes::boxplot(const double *values, const std::size_t *counts, std::size_t ngroups)
{
    boxplot(values, counts, ngroups, BoxOptions{});
}

void Axes::boxplot(const double *values, const std::size_t *counts, std::size_t ngroups,
                   const BoxOptions &opt, double whis, const double *whis_percentiles)
{
    BoxStatsOptions so;
    so.whis = whis;
    so.whis_percentiles = whis_percentiles;
    boxplot(values, counts, ngroups, opt, so);
}

namespace
{

/* np.median of a scratch buffer, which it reorders. */
double median_inplace(std::vector<double> &v)
{
    const std::size_t n = v.size();
    const std::size_t mid = n / 2;
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(mid), v.end());
    const double hi = v[mid];
    if (n % 2 == 1) {
        return hi;
    }
    const double lo = *std::max_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(mid));
    return lo + (hi - lo) * 0.5;
}

/* cbook._bootstrap_median: the 2.5th and 97.5th percentiles of the medians of
   `resamples` resamples (with replacement) of the data. The resamples come
   from a fixed-seed splitmix64, so a plot is reproducible, though not
   numpy's draw for draw. */
void bootstrap_median(const std::vector<double> &data, int resamples, double &lo, double &hi)
{
    std::uint64_t state = 0x9E3779B97F4A7C15ull;
    auto next = [&state]() {
        std::uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    };
    const std::size_t m = data.size();
    std::vector<double> sample(m);
    std::vector<double> estimates(static_cast<std::size_t>(resamples));
    for (double &e : estimates) {
        for (double &x : sample) {
            x = data[static_cast<std::size_t>(next() % m)];
        }
        e = median_inplace(sample);
    }
    std::sort(estimates.begin(), estimates.end());
    lo = percentile(estimates, 0.025);
    hi = percentile(estimates, 0.975);
}

} // namespace

void Axes::boxplot(const double *values, const std::size_t *counts, std::size_t ngroups,
                   const BoxOptions &opt, const BoxStatsOptions &so)
{
    if (ngroups == 0) {
        return;
    }
    const double whis = so.whis;

    /* boxplot is bxp with a summarizer in front, as in matplotlib, so the two
       cannot draw a box differently. */
    std::vector<BoxStats> stats;
    stats.reserve(ngroups);

    std::size_t offset = 0;
    for (std::size_t g = 0; g < ngroups; ++g) {
        const std::size_t n = counts[g];
        std::vector<double> sorted;
        sorted.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            const double v = values[offset + i];
            if (std::isfinite(v)) {
                sorted.push_back(v);
            }
        }
        offset += n;

        BoxStats s;
        if (sorted.empty()) {
            /* NaN, not zero: zero is a real box flat against the axis, while
               non-finite marks an empty group that bxp skips but keeps a slot for. */
            const double nan = std::numeric_limits<double>::quiet_NaN();
            s.q1 = s.med = s.q3 = s.whislo = s.whishi = nan;
            stats.push_back(s);
            continue;
        }
        std::sort(sorted.begin(), sorted.end());

        s.q1 = percentile(sorted, 0.25);
        s.med = percentile(sorted, 0.50);
        s.q3 = percentile(sorted, 0.75);
        const double iqr = s.q3 - s.q1;

        double sum = 0.0;
        for (double v : sorted) {
            sum += v;
        }
        s.mean = sum / static_cast<double>(sorted.size());
        /* _compute_conf_interval: a bootstrapped interval, or the median
           +/- 1.57 IQR / sqrt(N). */
        if (so.bootstrap > 0) {
            bootstrap_median(sorted, so.bootstrap, s.cilo, s.cihi);
        } else {
            const double notch = 1.57 * iqr / std::sqrt(static_cast<double>(sorted.size()));
            s.cilo = s.med - notch;
            s.cihi = s.med + notch;
        }

        /* The whisker reaches the furthest datum within whis IQR (or inside
           the given percentiles), not the bound itself, so it always ends on a
           real observation. autorange turns a zero-IQR box's rule into the
           full range. */
        const double full_range[2] = {0.0, 100.0};
        const double *whis_percentiles =
            iqr == 0.0 && so.autorange ? full_range : so.whis_percentiles;
        const double lo_bound = whis_percentiles != nullptr
                                    ? percentile(sorted, whis_percentiles[0] / 100.0)
                                    : s.q1 - whis * iqr;
        const double hi_bound = whis_percentiles != nullptr
                                    ? percentile(sorted, whis_percentiles[1] / 100.0)
                                    : s.q3 + whis * iqr;
        s.whislo = s.q1;
        s.whishi = s.q3;
        for (double v : sorted) {
            if (v >= lo_bound && v < s.whislo) {
                s.whislo = v;
            }
            if (v <= hi_bound && v > s.whishi) {
                s.whishi = v;
            }
        }

        for (double v : sorted) {
            if (v < s.whislo || v > s.whishi) {
                s.fliers.push_back(v);
            }
        }
        stats.push_back(std::move(s));
    }

    /* boxplot's overrides, after the summary: the notch interval was already
       computed around the data's own median. */
    for (std::size_t g = 0; g < ngroups; ++g) {
        if (so.usermedians != nullptr && !std::isnan(so.usermedians[g])) {
            stats[g].med = so.usermedians[g];
        }
        if (so.conf_intervals != nullptr) {
            if (!std::isnan(so.conf_intervals[2 * g])) {
                stats[g].cilo = so.conf_intervals[2 * g];
            }
            if (!std::isnan(so.conf_intervals[2 * g + 1])) {
                stats[g].cihi = so.conf_intervals[2 * g + 1];
            }
        }
    }

    bxp(stats.data(), stats.size(), opt);
}

namespace
{

/* numpy's percentile with the default linear interpolation, on sorted data. */
double percentile_sorted(const std::vector<double> &s, double q)
{
    const double pos = q * static_cast<double>(s.size() - 1);
    const std::size_t lo = static_cast<std::size_t>(std::floor(pos));
    const std::size_t hi = std::min(lo + 1, s.size() - 1);
    const double frac = pos - static_cast<double>(lo);
    return s[lo] + (s[hi] - s[lo]) * frac;
}

} // namespace

void Axes::violinplot(const double *values, const std::size_t *counts, std::size_t ngroups,
                      const ViolinOptions &opt)
{
    if (ngroups == 0) {
        return;
    }

    /* violin_stats: per group, the KDE over linspace(min, max, points) plus
       the summary values. */
    struct Stats
    {
        std::vector<double> coords, vals;
        double mean, median, min, max;
        std::vector<double> quantiles;
        bool present = false;
    };
    std::vector<Stats> stats(ngroups);
    std::size_t offset = 0;
    std::size_t qoffset = 0;
    const std::size_t points = opt.points < 2 ? 2 : opt.points;
    for (std::size_t g = 0; g < ngroups; ++g) {
        std::vector<double> d;
        for (std::size_t i = 0; i < counts[g]; ++i) {
            if (std::isfinite(values[offset + i])) {
                d.push_back(values[offset + i]);
            }
        }
        offset += counts[g];
        const std::size_t nq = opt.quantile_counts != nullptr ? opt.quantile_counts[g] : 0;
        const double *q = opt.quantiles != nullptr ? opt.quantiles + qoffset : nullptr;
        qoffset += nq;
        if (d.empty()) {
            continue;
        }
        Stats &st = stats[g];
        st.present = true;
        std::vector<double> sorted = d;
        std::sort(sorted.begin(), sorted.end());
        st.min = sorted.front();
        st.max = sorted.back();
        double sum = 0.0;
        for (double v : d) {
            sum += v;
        }
        st.mean = sum / static_cast<double>(d.size());
        st.median = percentile_sorted(sorted, 0.5);
        for (std::size_t k = 0; k < nq; ++k) {
            st.quantiles.push_back(percentile_sorted(sorted, q[k]));
        }

        st.coords.resize(points);
        st.vals.assign(points, 0.0);
        const double step = (st.max - st.min) / static_cast<double>(points - 1);
        for (std::size_t k = 0; k < points; ++k) {
            st.coords[k] = static_cast<double>(k) * step + st.min;
        }
        st.coords[points - 1] = st.max;

        if (st.min == st.max) {
            /* A constant dataset: 1 exactly at the value, as violin_stats has it. */
            for (std::size_t k = 0; k < points; ++k) {
                st.vals[k] = st.coords[k] == d[0] ? 1.0 : 0.0;
            }
            continue;
        }
        /* GaussianKDE: the sample variance (ddof=1) times factor^2. */
        double ss = 0.0;
        for (double v : d) {
            ss += (v - st.mean) * (v - st.mean);
        }
        const double nn = static_cast<double>(d.size());
        const double var = ss / (nn - 1.0);
        double factor = std::pow(nn, -0.2);
        if (opt.bandwidth == ViolinOptions::Bandwidth::Silverman) {
            factor = std::pow(nn * 3.0 / 4.0, -0.2);
        } else if (opt.bandwidth == ViolinOptions::Bandwidth::Scalar) {
            factor = opt.bw_factor;
        }
        const double covariance = var * factor * factor;
        if (!(covariance > 0.0)) {
            continue;
        }
        const double norm = std::sqrt(2.0 * 3.14159265358979323846 * covariance) * nn;
        for (std::size_t k = 0; k < points; ++k) {
            double acc = 0.0;
            for (double v : d) {
                const double diff = st.coords[k] - v;
                acc += std::exp(-0.5 * diff * diff / covariance);
            }
            st.vals[k] = acc / norm;
        }
    }

    std::vector<double> positions(ngroups), widths(ngroups);
    for (std::size_t g = 0; g < ngroups; ++g) {
        positions[g] = opt.positions != nullptr ? opt.positions[g] : static_cast<double>(g) + 1.0;
        widths[g] = opt.widths != nullptr ? opt.widths[g] : opt.width;
    }

    /* One cycle colour for the whole call, drawn only if a colour is unset. */
    Color next = Color::none();
    if (opt.facecolors.empty() || opt.linecolors.empty()) {
        next = next_line_color();
    }
    const auto pick = [](const std::vector<Color> &list, std::size_t g, Color fallback) {
        return list.empty() ? fallback : list[g % list.size()];
    };

    const bool low = opt.side != ViolinOptions::Side::High;
    const bool high = opt.side != ViolinOptions::Side::Low;
    const bool one_sided = opt.side != ViolinOptions::Side::Both;

    /* A point in (along, across) violin coordinates, laid out for the
       orientation: vertical violins run along y. */
    const auto at = [&](Path &p, bool move, double along, double across) {
        const double px = opt.horizontal ? along : across;
        const double py = opt.horizontal ? across : along;
        if (move) {
            p.move_to(px, py);
        } else {
            p.line_to(px, py);
        }
    };

    for (std::size_t g = 0; g < ngroups; ++g) {
        const Stats &st = stats[g];
        if (!st.present) {
            continue;
        }
        double peak = 0.0;
        for (double v : st.vals) {
            peak = std::max(peak, v);
        }
        std::vector<double> half(points, 0.0);
        if (peak > 0.0) {
            for (std::size_t k = 0; k < points; ++k) {
                half[k] = 0.5 * widths[g] * st.vals[k] / peak;
            }
        }
        const double pos = positions[g];
        auto body = std::make_unique<PolyPatch>();
        at(body->path, true, st.coords[0], low ? pos - half[0] : pos);
        for (std::size_t k = 1; k < points; ++k) {
            at(body->path, false, st.coords[k], low ? pos - half[k] : pos);
        }
        for (std::size_t k = points; k-- > 0;) {
            at(body->path, false, st.coords[k], high ? pos + half[k] : pos);
        }
        body->path.close_poly();
        body->facecolor = pick(opt.facecolors, g, next);
        body->alpha = opt.facecolors.empty() ? 0.3 : 1.0;
        patches.push_back(std::move(body));
    }

    /* The line collections in matplotlib's order: means, maxes, mins, bars,
       medians, quantiles -- each across every violin. */
    const auto perp = [&](const std::function<bool(std::size_t, std::vector<double> &)> &values_of) {
        auto lines = std::make_unique<SegmentCollection>();
        bool any = false;
        for (std::size_t g = 0; g < ngroups; ++g) {
            std::vector<double> vs;
            if (!stats[g].present || !values_of(g, vs)) {
                continue;
            }
            const double a = positions[g] + (low ? -0.25 : 0.0) * widths[g];
            const double b = positions[g] + (high ? 0.25 : 0.0) * widths[g];
            for (double v : vs) {
                at(lines->path, true, v, a);
                at(lines->path, false, v, b);
                any = true;
            }
        }
        if (!any) {
            return;
        }
        /* A LineCollection takes one colour per segment; with one colour per
           violin they agree unless linecolors cycles, which the first wins. */
        lines->color = pick(opt.linecolors, 0, next);
        lines->cap = one_sided ? CapStyle::Projecting : CapStyle::Butt;
        segment_collections.push_back(std::move(lines));
    };
    if (opt.showmeans) {
        perp([&](std::size_t g, std::vector<double> &v) { v = {stats[g].mean}; return true; });
    }
    if (opt.showextrema) {
        perp([&](std::size_t g, std::vector<double> &v) { v = {stats[g].max}; return true; });
        perp([&](std::size_t g, std::vector<double> &v) { v = {stats[g].min}; return true; });
        auto bars = std::make_unique<SegmentCollection>();
        for (std::size_t g = 0; g < ngroups; ++g) {
            if (!stats[g].present) {
                continue;
            }
            at(bars->path, true, stats[g].min, positions[g]);
            at(bars->path, false, stats[g].max, positions[g]);
        }
        bars->color = pick(opt.linecolors, 0, next);
        bars->cap = one_sided ? CapStyle::Projecting : CapStyle::Butt;
        segment_collections.push_back(std::move(bars));
    }
    if (opt.showmedians) {
        perp([&](std::size_t g, std::vector<double> &v) { v = {stats[g].median}; return true; });
    }
    perp([&](std::size_t g, std::vector<double> &v) {
        v = stats[g].quantiles;
        return !v.empty();
    });
}

void Axes::violinplot(const double *values, const std::size_t *counts, std::size_t ngroups)
{
    violinplot(values, counts, ngroups, ViolinOptions{});
}

ReferenceLine &Axes::axhline(double y)
{
    ReferenceLine line;
    line.horizontal = true;
    line.value = y;
    reference_lines.push_back(line);
    return reference_lines.back();
}

Line2D &Axes::ecdf(const double *values, std::size_t n, bool complementary,
                   const double *weights, bool horizontal)
{
    /* Sort the observations, carrying each one's weight along. */
    std::vector<std::pair<double, double>> obs;
    obs.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        if (std::isfinite(values[i])) {
            obs.emplace_back(values[i], weights != nullptr ? weights[i] : 1.0);
        }
    }
    std::sort(obs.begin(), obs.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });

    /* Cumulative weight, normalised so the last is exactly 1. Unit weights make
       cum[i] = (i + 1) / N, the plain proportion. */
    std::vector<double> value(obs.size());
    std::vector<double> cum(obs.size());
    double total = 0.0;
    for (const auto &o : obs) {
        total += o.second;
    }
    double running = 0.0;
    for (std::size_t i = 0; i < obs.size(); ++i) {
        value[i] = obs[i].first;
        running += obs[i].second;
        cum[i] = total > 0.0 ? running / total : 0.0;
    }

    /*
     * The corner points, in (value, cumulative) space, before the step
     * expansion below. matplotlib's ecdf:
     *   ascending:     value=[v[0], *v], cumulative=[0, *cum]
     *   complementary: value=[*v, v[-1]], cumulative=[1, *(1 - cum)]
     */
    std::vector<double> vpts;
    std::vector<double> cpts;
    vpts.reserve(value.size() + 1);
    cpts.reserve(value.size() + 1);
    if (!value.empty()) {
        if (!complementary) {
            vpts.push_back(value.front());
            cpts.push_back(0.0);
            for (std::size_t i = 0; i < value.size(); ++i) {
                vpts.push_back(value[i]);
                cpts.push_back(cum[i]);
            }
        } else {
            for (std::size_t i = 0; i < value.size(); ++i) {
                vpts.push_back(value[i]);
                cpts.push_back(1.0 - (i == 0 ? 0.0 : cum[i - 1]));
            }
            vpts.push_back(value.back());
            cpts.push_back(0.0);
        }
    }

    /* Value vs cumulative for a vertical ecdf, swapped for horizontal, then the
       corners become a staircase. matplotlib steps post for ascending-vertical
       and complementary-horizontal, pre otherwise (pre exactly when
       `complementary` and `horizontal` disagree). */
    const std::vector<double> &xs = horizontal ? cpts : vpts;
    const std::vector<double> &ys = horizontal ? vpts : cpts;
    const bool pre = complementary != horizontal;

    std::vector<double> sx;
    std::vector<double> sy;
    if (!xs.empty()) {
        const std::size_t m = 2 * xs.size() - 1;
        sx.resize(m);
        sy.resize(m);
        for (std::size_t i = 0; i < xs.size(); ++i) {
            sx[2 * i] = xs[i];
            sy[2 * i] = ys[i];
        }
        for (std::size_t i = 0; i + 1 < xs.size(); ++i) {
            sx[2 * i + 1] = pre ? xs[i] : xs[i + 1];
            sy[2 * i + 1] = pre ? ys[i + 1] : ys[i];
        }
    }

    Line2D &line = plot(sx.data(), sy.data(), sx.size());

    /* A proportion cannot leave [0, 1], and matplotlib pins the cumulative axis
       to it -- the y axis for a vertical ecdf, the x axis for a horizontal one. */
    std::vector<double> &sticky = horizontal ? sticky_x : sticky_y;
    for (double v : {0.0, 1.0}) {
        if (std::find(sticky.begin(), sticky.end(), v) == sticky.end()) {
            sticky.push_back(v);
        }
    }

    return line;
}

PolyPatch &Axes::arrow(double x, double y, double dx, double dy, double width)
{
    auto patch = std::make_unique<PolyPatch>();

    const double head_width = 3.0 * width;
    const double head_length = 1.5 * head_width;
    const double distance = std::hypot(dx, dy);
    if (!(distance > 0.0)) {
        patches.push_back(std::move(patch));
        return *patches.back();
    }

    /* length_includes_head is false by default, so the head is added BEYOND the
       endpoint and the drawn arrow is head_length longer than (dx, dy). */
    const double length = distance + head_length;

    /* Built pointing along +x with its tip at the origin, then rotated onto the
       vector and translated to its end (matplotlib's construction). */
    const double hw = head_width;
    const double hl = head_length;
    const double lw = width;
    const double left[5][2] = {
        {0.0, 0.0},          /* tip */
        {-hl, -hw / 2.0},    /* the barb */
        {-hl, -lw / 2.0},    /* where the head meets the stem */
        {-length, -lw / 2.0},/* the tail */
        {-length, 0.0},
    };

    /* Shifted forward by the head, since the head was not included above. */
    const double shift = head_length;

    double coords[8][2];
    for (int i = 0; i < 4; ++i) {
        coords[i][0] = left[i][0] + shift;
        coords[i][1] = left[i][1];
    }
    for (int i = 0; i < 4; ++i) {
        coords[4 + i][0] = left[3 - i][0] + shift;
        coords[4 + i][1] = -left[3 - i][1];
    }

    const double cx = dx / distance;
    const double sx = dy / distance;
    for (std::size_t i = 0; i < 8; ++i) {
        const double px = coords[i][0] * cx + coords[i][1] * -sx;
        const double py = coords[i][0] * sx + coords[i][1] * cx;
        const double vx = px + x + dx;
        const double vy = py + y + dy;
        (i == 0 ? patch->path.move_to(vx, vy) : patch->path.line_to(vx, vy));
    }
    patch->path.close_poly();

    /* A patch, so it takes the patch cycle, and unlike most patches it draws its
       edge: patch.linewidth in black over the face colour. */
    patch->facecolor = next_patch_color();
    patch->edgecolor = Color::rgb(0.0, 0.0, 0.0);
    patch->linewidth = 1.0;

    patches.push_back(std::move(patch));
    return *patches.back();
}

ReferenceLine &Axes::axline(double x, double y, double slope, bool vertical)
{
    ReferenceLine line;
    line.through = true;
    line.px = x;
    line.py = y;
    line.slope = slope;
    line.vertical_slope = vertical;
    reference_lines.push_back(line);
    return reference_lines.back();
}

void Axes::bar_label(const PolyPatch &bars, const std::string &fmt, BarLabelType label_type,
                     double padding)
{
    /* Each bar is a five-code subpath: MOVETO, three LINETOs, CLOSEPOLY, at
       (left, base), (right, base), (right, top), (left, top). The label reads the
       corners back to find its bar. */
    const Path &p = bars.path;
    const std::size_t n = p.size();
    for (std::size_t i = 0; i + 4 < n + 1; i += 5) {
        if (i + 3 >= n) {
            break;
        }
        const double x_left = p.vertices[2 * i];
        const double y_base = p.vertices[2 * i + 1];
        const double x_right = p.vertices[2 * (i + 1)];
        const double y_top = p.vertices[2 * (i + 2) + 1];

        const double height = y_top - y_base;

        char buf[64];
        std::snprintf(buf, sizeof(buf), fmt.c_str(), height);

        auto text = std::make_unique<TextArtist>();
        text->text = buf;
        text->halign = HAlign::Center;
        if (label_type == BarLabelType::Center) {
            text->x = (x_left + x_right) / 2.0;
            text->y = (y_base + y_top) / 2.0;
            text->valign = VAlign::Center;
            /* padding is meaningful only relative to an edge; center ignores it. */
        } else {
            text->x = (x_left + x_right) / 2.0;
            text->y = y_top;
            /* A downward bar wants its label under it, and padding pushes outward,
               so a negative bar's offset is negative too. */
            text->valign = height < 0.0 ? VAlign::Top : VAlign::Bottom;
            text->offset_y_pt = (height < 0.0 ? -1.0 : 1.0) * padding;
        }
        texts.push_back(std::move(text));
    }
}

ReferenceLine &Axes::axvline(double x)
{
    ReferenceLine line;
    line.horizontal = false;
    line.value = x;
    reference_lines.push_back(line);
    return reference_lines.back();
}

double Axes::scale_x(double v) const
{
    return apply_scale(v, xscale);
}

double Axes::scale_y(double v) const
{
    return apply_scale(v, yscale);
}

Affine2D Axes::scale_to_display(const Bbox &box) const
{
    const Bbox view = view_limits();
    const Bbox scaled(scale_x(view.x0), scale_y(view.y0), scale_x(view.x1), scale_y(view.y1));
    return Affine2D::from_bbox_to_bbox(scaled, box);
}

Bbox Axes::data_limits() const
{
    double xmin = std::numeric_limits<double>::infinity();
    double xmax = -std::numeric_limits<double>::infinity();
    double ymin = xmin;
    double ymax = xmax;

    for (const std::unique_ptr<Line2D> &line_ptr : lines) {
        const Line2D &line = *line_ptr;
        /* errorbar(fmt='none') never adds its data line, so only the bars
           (below) reach the limits. */
        if (line.errorbars_only) {
            continue;
        }
        const std::size_t n = std::min(line.xdata.size(), line.ydata.size());
        for (std::size_t i = 0; i < n; ++i) {
            const double x = line.xdata[i];
            const double y = line.ydata[i];
            /* Non-finite points are skipped rather than poisoning the limits;
               matplotlib's paths carry them through and drop them at render.
               On a log axis, non-positive values are excluded the same way. */
            if (!std::isfinite(x) || !std::isfinite(y)) {
                continue;
            }
            if (xscale == ScaleType::Log && !(x > 0.0)) {
                continue;
            }
            if (yscale == ScaleType::Log && !(y > 0.0)) {
                continue;
            }
            xmin = std::min(xmin, x);
            xmax = std::max(xmax, x);
            ymin = std::min(ymin, y);
            ymax = std::max(ymax, y);
        }
    }

    /* A span counts towards the limits only in the direction it is measured
       in; the other one is in axes coordinates and always spans the frame. */
    for (const AxesSpan &span : spans) {
        if (span.horizontal) {
            ymin = std::min(ymin, span.lo);
            ymax = std::max(ymax, span.hi);
        } else {
            xmin = std::min(xmin, span.lo);
            xmax = std::max(xmax, span.hi);
        }
    }

    /* The bars are hlines/vlines collections in matplotlib, so each one
       counts with both ends and its position across -- but only where
       errorevery drew one. */
    for (const std::unique_ptr<Line2D> &line_ptr : lines) {
        const Line2D &line = *line_ptr;
        const std::size_t n =
            std::min(std::min(line.xdata.size(), line.ydata.size()), line.xerr.size());
        for (std::size_t i = 0; i < n; ++i) {
            if (!line.error_drawn(i)) {
                continue;
            }
            const double x = line.xdata[i];
            const double lo = line.xerr_lo(i);
            const double hi = line.xerr_hi(i);
            if (!std::isfinite(x) || !std::isfinite(lo) || !std::isfinite(hi)) {
                continue;
            }
            if (line.errorbars_only && std::isfinite(line.ydata[i])) {
                ymin = std::min(ymin, line.ydata[i]);
                ymax = std::max(ymax, line.ydata[i]);
            }
            if (xscale == ScaleType::Log && !(x - lo > 0.0)) {
                xmax = std::max(xmax, x + hi);
                continue;
            }
            xmin = std::min(xmin, x - lo);
            xmax = std::max(xmax, x + hi);
        }
    }

    /* Error bars extend the limits: matplotlib autoscales to y +/- yerr, not to
       the markers alone, so the caps are never clipped by the frame. */
    for (const std::unique_ptr<Line2D> &line_ptr : lines) {
        const Line2D &line = *line_ptr;
        const std::size_t n =
            std::min(std::min(line.xdata.size(), line.ydata.size()), line.yerr.size());
        for (std::size_t i = 0; i < n; ++i) {
            if (!line.error_drawn(i)) {
                continue;
            }
            const double y = line.ydata[i];
            const double lo = line.yerr_lo(i);
            const double hi = line.yerr_hi(i);
            if (!std::isfinite(y) || !std::isfinite(lo) || !std::isfinite(hi)) {
                continue;
            }
            if (line.errorbars_only && std::isfinite(line.xdata[i])) {
                xmin = std::min(xmin, line.xdata[i]);
                xmax = std::max(xmax, line.xdata[i]);
            }
            if (yscale == ScaleType::Log && !(y - lo > 0.0)) {
                ymax = std::max(ymax, y + hi);
                continue;
            }
            ymin = std::min(ymin, y - lo);
            ymax = std::max(ymax, y + hi);
        }
    }

    for (const std::unique_ptr<ScatterCollection> &sc : collections) {
        const std::size_t n = std::min(sc->xdata.size(), sc->ydata.size());
        for (std::size_t i = 0; i < n; ++i) {
            const double x = sc->xdata[i];
            const double y = sc->ydata[i];
            if (!std::isfinite(x) || !std::isfinite(y)) {
                continue;
            }
            if (xscale == ScaleType::Log && !(x > 0.0)) {
                continue;
            }
            if (yscale == ScaleType::Log && !(y > 0.0)) {
                continue;
            }
            xmin = std::min(xmin, x);
            xmax = std::max(xmax, x);
            ymin = std::min(ymin, y);
            ymax = std::max(ymax, y);
        }
    }

    for (const std::unique_ptr<SegmentCollection> &sc : segment_collections) {
        for (std::size_t i = 0; i < sc->path.size(); ++i) {
            const double x = sc->path.vertices[2 * i];
            const double y = sc->path.vertices[2 * i + 1];
            if (!std::isfinite(x) || !std::isfinite(y)) {
                continue;
            }
            if (xscale == ScaleType::Log && !(x > 0.0)) {
                continue;
            }
            if (yscale == ScaleType::Log && !(y > 0.0)) {
                continue;
            }
            xmin = std::min(xmin, x);
            xmax = std::max(xmax, x);
            ymin = std::min(ymin, y);
            ymax = std::max(ymax, y);
        }
    }

    /* A reference line's value counts towards the limits, as in matplotlib
       (axhline(9) pulls the axis up to 9). An axline contributes only the point
       it was defined through, since the infinite line would break autoscaling. */
    for (const ReferenceLine &rl : reference_lines) {
        if (rl.through) {
            xmin = std::min(xmin, rl.px);
            xmax = std::max(xmax, rl.px);
            ymin = std::min(ymin, rl.py);
            ymax = std::max(ymax, rl.py);
        } else if (rl.horizontal) {
            ymin = std::min(ymin, rl.value);
            ymax = std::max(ymax, rl.value);
        } else {
            xmin = std::min(xmin, rl.value);
            xmax = std::max(xmax, rl.value);
        }
    }

    for (const Bbox &b : extra_datalim) {
        xmin = std::min(xmin, std::min(b.x0, b.x1));
        xmax = std::max(xmax, std::max(b.x0, b.x1));
        ymin = std::min(ymin, std::min(b.y0, b.y1));
        ymax = std::max(ymax, std::max(b.y0, b.y1));
    }

    for (const std::unique_ptr<ImageGrid> &img : images) {
        if (img->rows == 0 || img->cols == 0) {
            continue;
        }
        xmin = std::min(xmin, std::min(img->x0, img->x1));
        xmax = std::max(xmax, std::max(img->x0, img->x1));
        ymin = std::min(ymin, std::min(img->y0, img->y1));
        ymax = std::max(ymax, std::max(img->y0, img->y1));
    }

    for (const std::unique_ptr<ContourSet> &cs : contours) {
        xmin = std::min(xmin, std::min(cs->x0, cs->x1));
        xmax = std::max(xmax, std::max(cs->x0, cs->x1));
        ymin = std::min(ymin, std::min(cs->y0, cs->y1));
        ymax = std::max(ymax, std::max(cs->y0, cs->y1));
    }

    /* A quiver counts by arrow positions only: arrows are sized in axes fractions,
       so including their extents would be circular. matplotlib updates datalim
       from XY alone too. */
    for (const std::unique_ptr<QuiverField> &q : quivers) {
        for (std::size_t i = 0; i < q->x.size(); ++i) {
            if (!std::isfinite(q->x[i]) || !std::isfinite(q->y[i])) {
                continue;
            }
            xmin = std::min(xmin, q->x[i]);
            xmax = std::max(xmax, q->x[i]);
            ymin = std::min(ymin, q->y[i]);
            ymax = std::max(ymax, q->y[i]);
        }
    }

    /* Barbs, for the same reason: positions only, since a barb's size is in
       points and does not depend on the view. */
    for (const std::unique_ptr<BarbField> &b : barb_fields) {
        for (std::size_t i = 0; i < b->x.size(); ++i) {
            if (!std::isfinite(b->x[i]) || !std::isfinite(b->y[i])) {
                continue;
            }
            xmin = std::min(xmin, b->x[i]);
            xmax = std::max(xmax, b->x[i]);
            ymin = std::min(ymin, b->y[i]);
            ymax = std::max(ymax, b->y[i]);
        }
    }

    for (const std::unique_ptr<PolyPatch> &patch : patches) {
        if (!patch->counts_towards_limits) {
            continue;
        }
        const std::size_t n = patch->path.size();
        for (std::size_t i = 0; i < n; ++i) {
            /* CLOSEPOLY's vertex is a (0, 0) placeholder, not part of the shape;
               counting it would pull the origin into every closed patch's limits. */
            if (!patch->path.codes.empty() && patch->path.codes[i] == CLOSEPOLY) {
                continue;
            }
            const double x = patch->path.vertices[2 * i];
            const double y = patch->path.vertices[2 * i + 1];
            if (!std::isfinite(x) || !std::isfinite(y)) {
                continue;
            }
            if (xscale == ScaleType::Log && !(x > 0.0)) {
                continue;
            }
            if (yscale == ScaleType::Log && !(y > 0.0)) {
                continue;
            }
            xmin = std::min(xmin, x);
            xmax = std::max(xmax, x);
            ymin = std::min(ymin, y);
            ymax = std::max(ymax, y);
        }
    }

    if (!std::isfinite(xmin) || !std::isfinite(ymin)) {
        return xscale == ScaleType::Log || yscale == ScaleType::Log ? Bbox(1.0, 1.0, 10.0, 10.0)
                                                                   : Bbox(0.0, 0.0, 1.0, 1.0);
    }

    /* Autoscaling widens a degenerate range through Locator.nonsingular, expander
       0.05 (not transforms.nonsingular's 0.001): a lone point at 1.0 gets
       0.95..1.05. */
    Bbox::nonsingular(xmin, xmax, 0.05);
    Bbox::nonsingular(ymin, ymax, 0.05);
    return Bbox(xmin, ymin, xmax, ymax);
}

Bbox Axes::view_limits() const
{
    const Bbox mine = oriented_view_limits();
    if (shared_x == nullptr && shared_y == nullptr) {
        return mine;
    }
    /* A shared axes takes each direction from the axes it follows. The two
       directions are applied independently, since sharex and sharey can both be
       set on the same axes. */
    Bbox out = mine;
    if (shared_x != nullptr) {
        const Bbox theirs = shared_x->view_limits();
        out.x0 = theirs.x0;
        out.x1 = theirs.x1;
    }
    if (shared_y != nullptr) {
        const Bbox theirs = shared_y->view_limits();
        out.y0 = theirs.y0;
        out.y1 = theirs.y1;
    }
    return out;
}

Bbox Axes::oriented_view_limits() const
{
    Bbox v = unshared_view_limits();
    /* invert_xaxis / invert_yaxis, applied after the limits are derived so
       autoscaling into an inverted axes keeps it inverted. */
    if (x_inverted) {
        std::swap(v.x0, v.x1);
    }
    if (y_inverted) {
        std::swap(v.y0, v.y1);
    }
    return v;
}

Bbox Axes::unshared_view_limits() const
{
    if (m_has_xview && m_has_yview) {
        return Bbox(m_xview[0], m_yview[0], m_xview[1], m_yview[1]);
    }

    const Bbox lim = data_limits();

    /* Margins are applied in scale space, so a log axis gets 5% of a decade
       rather than 5% of a raw value range. */
    auto expand = [](double lo, double hi, double margin, ScaleType scale) {
        const double slo = apply_scale(lo, scale);
        const double shi = apply_scale(hi, scale);
        const double d = (shi - slo) * margin;
        const double out_lo = slo - d;
        const double out_hi = shi + d;
        if (scale == ScaleType::Log) {
            return std::pair<double, double>{std::pow(10.0, out_lo), std::pow(10.0, out_hi)};
        }
        return std::pair<double, double>{out_lo, out_hi};
    };

    auto [ax0, ax1] = expand(lim.x0, lim.x1, tight_view ? 0.0 : xmargin, xscale);
    auto [ay0, ay1] = expand(lim.y0, lim.y1, tight_view ? 0.0 : ymargin, yscale);

    /* A margin may not carry a limit past a sticky value the data does not already
       cross (Axes._update_autoscale, via Artist.sticky_edges). The tolerance is
       relative, as matplotlib's is. */
    auto clamp = [](double &lo, double &hi, double raw_lo, double raw_hi,
                    const std::vector<double> &stickies) {
        if (stickies.empty()) {
            return;
        }
        const double tol = 1e-5 * std::max(std::max(std::fabs(raw_lo), std::fabs(raw_hi)),
                                           std::fabs(raw_hi - raw_lo));
        for (double s : stickies) {
            if (s <= raw_lo + tol) {
                lo = std::max(lo, s);
            }
            if (s >= raw_hi - tol) {
                hi = std::min(hi, s);
            }
        }
    };
    if (use_sticky_edges) {
        clamp(ax0, ax1, lim.x0, lim.x1, sticky_x);
        clamp(ay0, ay1, lim.y0, lim.y1, sticky_y);
    }

    /* A pinned axis wins; an unpinned one still follows the data. */
    return Bbox(m_has_xview ? m_xview[0] : ax0,
                m_has_yview ? m_yview[0] : ay0,
                m_has_xview ? m_xview[1] : ax1,
                m_has_yview ? m_yview[1] : ay1);
}

void Axes::set_view_limits(const Bbox &view)
{
    set_xlim(view.x0, view.x1);
    set_ylim(view.y0, view.y1);
}

/* matplotlib reads argument order as orientation: set_xlim(1, 0) inverts the
   axis, set_xlim(0, 1) un-inverts. The pair is stored sorted with direction in a
   flag, so an explicit and an autoscaled limit follow the same rule and a pan
   (which feeds view_limits() back in) does not invert twice. */
void Axes::set_xlim(double lo, double hi)
{
    x_inverted = lo > hi;
    m_xview[0] = std::min(lo, hi);
    m_xview[1] = std::max(lo, hi);
    m_has_xview = true;
}

void Axes::set_ylim(double lo, double hi)
{
    y_inverted = lo > hi;
    m_yview[0] = std::min(lo, hi);
    m_yview[1] = std::max(lo, hi);
    m_has_yview = true;
}

void Axes::set_xbound(double lower, double upper)
{
    /* unshared_view_limits() -- ascending regardless of x_inverted, whether
       the current bound came from an explicit set_xlim or from autoscale --
       is where "leave this end alone" reads its current value from. */
    const Bbox current = unshared_view_limits();
    const double lo = std::isnan(lower) ? current.x0 : lower;
    const double hi = std::isnan(upper) ? current.x1 : upper;
    if (x_inverted) {
        set_xlim(hi, lo);
    } else {
        set_xlim(lo, hi);
    }
}

void Axes::set_ybound(double lower, double upper)
{
    const Bbox current = unshared_view_limits();
    const double lo = std::isnan(lower) ? current.y0 : lower;
    const double hi = std::isnan(upper) ? current.y1 : upper;
    if (y_inverted) {
        set_ylim(hi, lo);
    } else {
        set_ylim(lo, hi);
    }
}

Bbox Axes::display_box(double fig_width, double fig_height) const
{
    Bbox box(position.x0 * fig_width,
             position.y0 * fig_height,
             position.x1 * fig_width,
             position.y1 * fig_height);

    /* set_box_aspect asks for a box of a given shape outright; checked first
       since it does not depend on the view. */
    if (box_aspect > 0.0) {
        const double w = box.width();
        const double h = box.height();
        double new_w = w;
        double new_h = w * box_aspect;
        if (new_h > h) {
            new_h = h;
            new_w = h / box_aspect;
        }
        const double x0 = box.x0 + (w - new_w) * anchor_x;
        const double y0 = box.y0 + (h - new_h) * anchor_y;
        return Bbox(x0, y0, x0 + new_w, y0 + new_h);
    }

    if (!(aspect > 0.0)) {
        return box;
    }

    /* apply_aspect with adjustable='box': shrink the box until its shape matches
       the data's, then anchor it. In pixels the figure's shape cancels, leaving
       height / width == aspect * (y range / x range). */
    const Bbox view = view_limits();
    const double dx = std::fabs(view.x1 - view.x0);
    const double dy = std::fabs(view.y1 - view.y0);
    if (!(dx > 0.0) || !(dy > 0.0)) {
        return box;
    }

    const double want = aspect * (dy / dx);
    const double w = box.width();
    const double h = box.height();
    const double want_h = w * want;

    double new_w = w;
    double new_h = h;
    if (want_h <= h) {
        new_h = want_h;
    } else {
        new_w = h / want;
    }

    /* Anchored, not always centred ('C' is only the default). */
    const double dw = (w - new_w) * anchor_x;
    const double dh = (h - new_h) * anchor_y;
    return Bbox(box.x0 + dw, box.y0 + dh, box.x1 - dw, box.y1 - dh);
}

void Axes::draw_spans(Renderer &renderer, const Bbox &box)
{
    if (spans.empty()) {
        return;
    }

    const Bbox view = view_limits();
    const Affine2D to_display = scale_to_display(box);

    for (const AxesSpan &span : spans) {
        GraphicsContext gc;
        gc.color = Color::none(); /* patches draw no edge by default */
        gc.linewidth = 0.0;
        gc.alpha = span.alpha;

        Bbox rect = box;
        if (span.horizontal) {
            const double a = to_display(Point{scale_x(view.x0), scale_y(span.lo)}).y;
            const double b = to_display(Point{scale_x(view.x0), scale_y(span.hi)}).y;
            rect = Bbox(box.x0, std::min(a, b), box.x1, std::max(a, b));
        } else {
            const double a = to_display(Point{scale_x(span.lo), scale_y(view.y0)}).x;
            const double b = to_display(Point{scale_x(span.hi), scale_y(view.y0)}).x;
            rect = Bbox(std::min(a, b), box.y0, std::max(a, b), box.y1);
        }

        renderer.draw_path(gc, Path::rectangle(rect), Affine2D(), &span.facecolor);
    }
}

void Axes::draw_patches(Renderer &renderer, const Bbox &box, double px_per_pt,
                        bool line_tier)
{
    const Affine2D to_display = scale_to_display(box);
    const Bbox view = view_limits();
    const double vx0 = scale_x(view.x0), vx1 = scale_x(view.x1);
    const double vy0 = scale_y(view.y0), vy1 = scale_y(view.y1);

    for (const std::unique_ptr<PolyPatch> &patch : patches) {
        if (patch->path.size() == 0 || patch->line_tier != line_tier) {
            continue;
        }

        /* Vertices are pre-transformed into scale space, same as lines; an
           axes-fraction coordinate is placed across the view instead. */
        Path scaled;
        scaled.codes = patch->path.codes;
        scaled.vertices.reserve(patch->path.vertices.size());
        const double shx = patch->shift_pt_x * px_per_pt;
        const double shy = patch->shift_pt_y * px_per_pt;
        for (std::size_t i = 0; i < patch->path.size(); ++i) {
            const double px = patch->path.vertices[2 * i] + shx;
            const double py = patch->path.vertices[2 * i + 1] + shy;
            scaled.vertices.push_back(patch->x_in_axes ? vx0 + px * (vx1 - vx0) : scale_x(px));
            scaled.vertices.push_back(patch->y_in_axes ? vy0 + py * (vy1 - vy0) : scale_y(py));
        }

        GraphicsContext gc;
        gc.alpha = patch->alpha;
        gc.color = patch->linewidth > 0.0 ? patch->edgecolor : Color::none();
        gc.linewidth = patch->linewidth;
        gc.join = patch->join;
        gc.antialiased = patch->antialiased;
        gc.has_cliprect = patch->clip;
        gc.cliprect = box;

        renderer.draw_path(gc, scaled, to_display, &patch->facecolor);
    }
}

void Axes::draw_collections(Renderer &renderer, const Bbox &box, double px_per_pt)
{
    const Affine2D to_display = scale_to_display(box);

    for (const std::unique_ptr<ScatterCollection> &sc : collections) {
        const std::size_t n = std::min(sc->xdata.size(), sc->ydata.size());
        if (n == 0) {
            continue;
        }

        const Path unit = marker_path(sc->marker);
        const bool filled = marker_is_filled(sc->marker);

        /* matplotlib's Collection.draw takes a "single path optimization" when
           every point shares one size and colour, routing the whole collection
           through draw_markers. This also affects position: draw_markers stamps a
           rasterized marker at whole pixels, moving each point by up to half a
           pixel, so a uniform scatter must take the same path to match. */
        if (sc->sizes.empty() && sc->colors.empty()) {
            const double half = 0.5 * std::sqrt(std::max(0.0, sc->size)) * px_per_pt;
            if (half <= 0.0) {
                continue;
            }

            GraphicsContext gc;
            gc.alpha = sc->alpha;
            gc.color = sc->has_edgecolor ? sc->edgecolor : sc->color;
            gc.linewidth = sc->linewidth;
            gc.cap = CapStyle::Round;
            gc.join = JoinStyle::Round;
            gc.has_cliprect = true;
            gc.cliprect = box;

            Path points;
            points.vertices.reserve(2 * n);
            for (std::size_t i = 0; i < n; ++i) {
                const Point p =
                    to_display(Point{scale_x(sc->xdata[i]), scale_y(sc->ydata[i])});
                if (!std::isfinite(p.x) || !std::isfinite(p.y)) {
                    continue;
                }
                points.move_to(p.x, p.y);
            }

            renderer.draw_markers(gc, unit, Affine2D::scale(half, half), points, Affine2D(),
                                  filled ? &sc->color : nullptr);
            continue;
        }

        for (std::size_t i = 0; i < n; ++i) {
            const Point p =
                to_display(Point{scale_x(sc->xdata[i]), scale_y(sc->ydata[i])});
            if (!std::isfinite(p.x) || !std::isfinite(p.y)) {
                continue;
            }

            /* scatter's `s` is an area in points squared, so the diameter is its
               square root (default 36 is a 6-point marker, as plot()'s default). */
            const double diameter = std::sqrt(std::max(0.0, sc->size_at(i)));
            const double half = 0.5 * diameter * px_per_pt;
            if (half <= 0.0) {
                continue;
            }

            const Color face = sc->color_at(i);

            GraphicsContext gc;
            gc.alpha = sc->alpha;
            gc.color = sc->has_edgecolor ? sc->edgecolor : face;
            gc.linewidth = sc->linewidth;
            gc.cap = CapStyle::Round;
            gc.join = JoinStyle::Round;
            gc.has_cliprect = true;
            gc.cliprect = box;

            const Affine2D t =
                Affine2D::scale(half, half).then(Affine2D::translate(p.x, p.y));
            renderer.draw_path(gc, unit, t, filled ? &face : nullptr);
        }
    }
}

void Axes::draw_images(Renderer &renderer, const Bbox &box)
{
    const Affine2D to_display = scale_to_display(box);

    for (const std::unique_ptr<ImageGrid> &img : images) {
        if (img->rows == 0 || img->cols == 0) {
            continue;
        }

        double vmin = img->vmin;
        double vmax = img->vmax;
        if (!img->has_vlimits) {
            /* Normalize's autoscale: the data's own finite range. */
            vmin = std::numeric_limits<double>::infinity();
            vmax = -vmin;
            for (double v : img->values) {
                if (!std::isfinite(v)) {
                    continue;
                }
                vmin = std::min(vmin, v);
                vmax = std::max(vmax, v);
            }
            if (!std::isfinite(vmin)) {
                continue;
            }
        }

        const double dx = (img->x1 - img->x0) / static_cast<double>(img->cols);
        /* Rows run from the extent's y1 (top) edge towards y0; imshow's default
           extent is (x0, x1, rows - 0.5, -0.5), so row 0 covers y in [-0.5, 0.5]
           and appears at the top because the axis is inverted (origin='upper'). */
        const double dy = (img->y0 - img->y1) / static_cast<double>(img->rows);

        /* An explicit grid overrides both: a mesh's cells need not be even, and
           its row 0 is at the bottom rather than the top. */
        const bool meshed = !img->xedges.empty() && !img->yedges.empty();
        auto cell_x = [&](std::size_t c) {
            return meshed ? std::pair<double, double>{img->xedges[c], img->xedges[c + 1]}
                          : std::pair<double, double>{img->x0 + static_cast<double>(c) * dx,
                                                      img->x0 + static_cast<double>(c + 1) * dx};
        };
        auto cell_y = [&](std::size_t r) {
            return meshed ? std::pair<double, double>{img->yedges[r], img->yedges[r + 1]}
                          : std::pair<double, double>{img->y1 + static_cast<double>(r) * dy,
                                                      img->y1 + static_cast<double>(r + 1) * dy};
        };

        for (std::size_t r = 0; r < img->rows; ++r) {
            for (std::size_t c = 0; c < img->cols; ++c) {
                const Color fill =
                    colormap_lookup(img->cmap, normalize(img->value_at(r, c), vmin, vmax));
                if (!fill.visible()) {
                    continue; /* NaN: leave a hole rather than inventing a value */
                }

                const auto [cxa, cxb] = cell_x(c);
                const auto [cya, cyb] = cell_y(r);

                /* Cells are drawn exactly, edge to edge. */
                Path quad = Path::rectangle(Bbox(std::min(cxa, cxb), std::min(cya, cyb),
                                                 std::max(cxa, cxb), std::max(cyb, cya)));

                /* Vertices go through the scale transform like everything else,
                   so a heatmap on a log axis stretches correctly. */
                for (std::size_t i = 0; i < quad.size(); ++i) {
                    quad.vertices[2 * i] = scale_x(quad.vertices[2 * i]);
                    quad.vertices[2 * i + 1] = scale_y(quad.vertices[2 * i + 1]);
                }

                GraphicsContext gc;
                /* An image is not antialiased, a mesh is, matching matplotlib:
                   imshow resamples at interpolation='nearest' so cell boundaries
                   are hard, while pcolormesh is drawn as antialiased quads. */
                gc.antialiased = meshed;
                gc.alpha = img->alpha;
                gc.color = Color::none();
                gc.linewidth = 0.0;
                gc.antialiased = true;
                gc.has_cliprect = true;
                gc.cliprect = box;

                renderer.draw_path(gc, quad, to_display, &fill);
            }
        }
    }
}

namespace
{

/* ---- clabel: choosing where a contour label goes ----
   All of this works in display coordinates (every question is about pixels). The
   lines arrive as connected components of the level path, already transformed. */

/* Whether a line is long enough to label (print_label): either many more
   vertices than the label is wide, or an x/y extent exceeding 1.2 label widths. */
bool line_fits_label(const std::vector<Point> &line, double lw)
{
    if (line.empty()) {
        return false;
    }
    if (static_cast<double>(line.size()) > 10.0 * lw) {
        return true;
    }
    double xmin = line[0].x, xmax = line[0].x;
    double ymin = line[0].y, ymax = line[0].y;
    for (const Point &p : line) {
        xmin = std::min(xmin, p.x);
        xmax = std::max(xmax, p.x);
        ymin = std::min(ymin, p.y);
        ymax = std::max(ymax, p.y);
    }
    return (xmax - xmin) > 1.2 * lw || (ymax - ymin) > 1.2 * lw;
}

/* Whether a label already sits near here -- too_close. */
bool label_crowded(double x, double y, double lw, const std::vector<Point> &placed)
{
    const double thresh = (1.2 * lw) * (1.2 * lw);
    for (const Point &p : placed) {
        const double dx = x - p.x;
        const double dy = y - p.y;
        if (dx * dx + dy * dy < thresh) {
            return true;
        }
    }
    return false;
}

/* The straightest stretch of a line, where a label reads best (locate_label).
   The line is cut into blocks one label wide, cycling back to the start to fill
   the last block (numpy resize semantics; the returned index is taken modulo the
   line length). Each block scores the total perpendicular distance of its points
   from the chord joining its ends, so a straight block scores zero. Blocks are
   tried straightest first; the first uncrowded midpoint wins, else the
   straightest. */
struct LabelSpot
{
    double x, y;
    std::size_t idx;
};

LabelSpot locate_label(const std::vector<Point> &line, double lw,
                       const std::vector<Point> &placed)
{
    const std::size_t n = line.size();
    const std::size_t n_blocks =
        lw > 1.0 ? static_cast<std::size_t>(
                       std::ceil(static_cast<double>(n) / lw))
                 : 1;
    std::size_t block = n_blocks == 1 ? n : static_cast<std::size_t>(lw);
    if (block == 0) {
        block = 1;
    }
    const std::size_t half = block / 2;

    auto at = [&](std::size_t b, std::size_t j) -> const Point & {
        return line[(b * block + j) % n];
    };

    std::vector<double> distance(n_blocks, 0.0);
    for (std::size_t b = 0; b < n_blocks; ++b) {
        const Point &first = at(b, 0);
        const Point &last = at(b, block - 1);
        const double cx = last.x - first.x;
        const double cy = last.y - first.y;
        const double len = std::hypot(cx, cy);
        if (len == 0.0) {
            /* Zero-length chord: score infinity so argsort puts it last. */
            distance[b] = std::numeric_limits<double>::infinity();
            continue;
        }
        double sum = 0.0;
        for (std::size_t j = 0; j < block; ++j) {
            const Point &p = at(b, j);
            const double s = (first.y - p.y) * cx - (first.x - p.x) * cy;
            sum += std::fabs(s) / len;
        }
        distance[b] = sum;
    }

    std::vector<std::size_t> order(n_blocks);
    for (std::size_t i = 0; i < n_blocks; ++i) {
        order[i] = i;
    }
    /* Stable sort (numpy's argsort is not); only matters on exact ties. */
    std::stable_sort(order.begin(), order.end(),
                     [&](std::size_t a, std::size_t b) { return distance[a] < distance[b]; });

    std::size_t chosen = order.empty() ? 0 : order[0];
    for (std::size_t k = 0; k < order.size(); ++k) {
        const Point &mid = at(order[k], half);
        if (!label_crowded(mid.x, mid.y, lw, placed)) {
            chosen = order[k];
            break;
        }
    }

    const Point &mid = at(chosen, half);
    return LabelSpot{mid.x, mid.y, (chosen * block + half) % n};
}

/* The fractional position of t along a monotone increasing sequence, or -1
   when t falls outside it -- np.interp's left=-1, right=-1. */
double frac_index(double t, const std::vector<double> &xp)
{
    const std::size_t n = xp.size();
    if (n == 0 || t < xp[0] || t > xp[n - 1]) {
        return -1.0;
    }
    std::size_t lo = 0;
    std::size_t hi = n - 1;
    while (hi - lo > 1) {
        const std::size_t mid = (lo + hi) / 2;
        if (xp[mid] <= t) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    const double d = xp[hi] - xp[lo];
    return static_cast<double>(lo) + (d == 0.0 ? 0.0 : (t - xp[lo]) / d);
}

/* The point at arc length t, clamped at both ends -- np.interp's default. */
Point point_at(double t, const std::vector<double> &cpl, const std::vector<Point> &pts)
{
    const std::size_t n = pts.size();
    if (n == 0) {
        return Point{0.0, 0.0};
    }
    if (t <= cpl.front()) {
        return pts.front();
    }
    if (t >= cpl.back()) {
        return pts.back();
    }
    const double f = frac_index(t, cpl);
    const std::size_t i = static_cast<std::size_t>(std::floor(f));
    const std::size_t j = std::min(i + 1, n - 1);
    const double u = f - static_cast<double>(i);
    return Point{pts[i].x + u * (pts[j].x - pts[i].x),
                 pts[i].y + u * (pts[j].y - pts[i].y)};
}

/* Break a line open so a label can sit in the gap, and compute the label
   rotation (_split_path_and_get_label_rotation). Done together since both need
   the cumulative arc length. The angle is taken between the points half a label
   width either side of the anchor, so it follows the line's overall run; the gap
   is the same span widened by `spacing` at each end (that clearance is left out
   of the angle). A closed component repeats its first point at the end; the
   returned pieces are always open. */
struct LabelBreak
{
    double angle_deg = 0.0;
    std::vector<std::vector<Point>> pieces;
};

LabelBreak split_for_label(const std::vector<Point> &comp, bool closed, std::size_t idx,
                           double lw, double spacing, bool rightside_up)
{
    LabelBreak out;

    /* A closed ring is rotated to start at the label, which turns the wrap
       into an ordinary interior break. */
    std::vector<Point> pts;
    if (closed) {
        const std::size_t m = comp.size() - 1; /* the repeat is not a point */
        pts.reserve(m + 1);
        for (std::size_t k = 0; k <= m; ++k) {
            pts.push_back(comp[(idx + k) % m]);
        }
        idx = 0;
    } else {
        pts = comp;
    }

    const std::size_t n = pts.size();
    if (n < 2 || idx >= n) {
        out.pieces.push_back(comp);
        return out;
    }

    std::vector<double> cpl(n, 0.0);
    for (std::size_t i = 1; i < n; ++i) {
        cpl[i] = cpl[i - 1] + std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
    }
    const double origin = cpl[idx];
    for (double &v : cpl) {
        v -= origin;
    }

    double t0 = -lw / 2.0;
    double t1 = lw / 2.0;
    if (closed) {
        /* Going back from the start means going round to the far end. */
        t0 += cpl.back() - cpl.front();
    }

    const Point a = point_at(t0, cpl, pts);
    const Point b = point_at(t1, cpl, pts);
    double angle = std::atan2(b.y - a.y, b.x - a.x) * 180.0 / 3.14159265358979323846;
    if (rightside_up) {
        /* Fold onto (-90, 90] so the label is never upside down. */
        angle = std::fmod(angle + 90.0, 180.0);
        if (angle < 0.0) {
            angle += 180.0;
        }
        angle -= 90.0;
    }
    out.angle_deg = angle;

    t0 -= spacing;
    t1 += spacing;

    const double f0 = frac_index(t0, cpl);
    const double f1 = frac_index(t1, cpl);
    const long i0 = f0 < 0.0 ? -1 : static_cast<long>(std::floor(f0));
    const long i1 = f1 < 0.0 ? -1 : static_cast<long>(std::ceil(f1));
    const Point c0 = point_at(t0, cpl, pts);
    const Point c1 = point_at(t1, cpl, pts);

    if (closed) {
        if (i0 != -1 && i1 != -1) {
            std::vector<Point> piece;
            piece.push_back(c1);
            for (long k = i1; k <= i0; ++k) {
                piece.push_back(pts[static_cast<std::size_t>(k)]);
            }
            piece.push_back(c0);
            out.pieces.push_back(std::move(piece));
        }
    } else {
        if (i0 != -1) {
            std::vector<Point> piece(pts.begin(), pts.begin() + (i0 + 1));
            piece.push_back(c0);
            out.pieces.push_back(std::move(piece));
        }
        if (i1 != -1) {
            std::vector<Point> piece;
            piece.push_back(c1);
            piece.insert(piece.end(), pts.begin() + i1, pts.end());
            out.pieces.push_back(std::move(piece));
        }
    }

    return out;
}

/* Split a path into its connected components, in display coordinates. */
std::vector<std::pair<std::vector<Point>, bool>> components_in_display(
    const Path &src, const std::function<Point(double, double)> &to_px)
{
    std::vector<std::pair<std::vector<Point>, bool>> out;
    const std::size_t n = src.size();
    for (std::size_t k = 0; k < n; ++k) {
        const uint8_t code = src.codes.empty() ? (k == 0 ? MOVETO : LINETO)
                                               : src.codes[k];
        if (code == MOVETO || out.empty()) {
            out.emplace_back();
        }
        if (code == CLOSEPOLY) {
            /* The ring's first point, repeated -- which is what the rest of
               this expects a closed component to carry. */
            out.back().first.push_back(out.back().first.front());
            out.back().second = true;
            continue;
        }
        out.back().first.push_back(to_px(src.vertices[2 * k], src.vertices[2 * k + 1]));
    }
    return out;
}

} // namespace

void Axes::clabel(ContourSet &cs)
{
    cs.labeled = true;
}

void Axes::draw_contours(Renderer &renderer, const Bbox &box, double px_per_pt,
                         bool line_tier)
{
    const Affine2D to_display = scale_to_display(box);

    for (const std::unique_ptr<ContourSet> &cs : contours) {
        if (cs->filled == line_tier) {
            continue;
        }
        for (std::size_t b = 0; b < cs->band_paths.size(); ++b) {
            const Path &src = cs->band_paths[b];
            if (src.size() == 0) {
                continue;
            }

            Path scaled;
            scaled.codes = src.codes;
            scaled.vertices.reserve(src.vertices.size());
            for (std::size_t k = 0; k < src.size(); ++k) {
                scaled.vertices.push_back(scale_x(src.vertices[2 * k]));
                scaled.vertices.push_back(scale_y(src.vertices[2 * k + 1]));
            }

            /* Band colour is the colormap at its midpoint, normalised over the
               whole level range. */
            Color face = cs->color;
            if (!cs->has_color) {
                /* An extended band's midpoint is near +/-5e249, which clamps
                   to the colormap's end -- the under/over colour. */
                const std::vector<double> &edges =
                    cs->band_levels.empty() ? cs->levels : cs->band_levels;
                const double span = cs->levels.back() - cs->levels.front();
                const double mid = (edges[b] + edges[b + 1]) / 2.0;
                const double t = span == 0.0 ? 0.5 : (mid - cs->levels.front()) / span;
                face = colormap_lookup(cs->cmap, t);
            }

            GraphicsContext gc;
            gc.color = Color::none(); /* no edge: a filled band is face only */
            gc.linewidth = 0.0;
            /* Not antialiased, matplotlib's default for filled contours: shared
               band boundaries would otherwise composite to a pale seam along
               every level line. Hard edges give the boundary pixel to one band. */
            gc.antialiased = false;
            gc.has_cliprect = true;
            gc.cliprect = box;

            renderer.draw_path(gc, scaled, to_display, &face);
        }

        /* Label text comes from the same ScalarFormatter as the tick labels
           (clabel's fallback with no fmt), formatted for the whole level list at
           once so neighbouring labels agree on decimal places. */
        std::vector<std::string> label_text;
        std::vector<Point> placed_labels;
        if (cs->labeled) {
            label_text = format_ticks(cs->levels);
        }

        for (std::size_t i = 0; i < cs->level_paths.size(); ++i) {
            const Path &src = cs->level_paths[i];
            if (src.size() == 0) {
                continue;
            }

            Path scaled;
            scaled.codes = src.codes;
            scaled.vertices.reserve(src.vertices.size());
            for (std::size_t k = 0; k < src.size(); ++k) {
                scaled.vertices.push_back(scale_x(src.vertices[2 * k]));
                scaled.vertices.push_back(scale_y(src.vertices[2 * k + 1]));
            }

            Color color = cs->color;
            if (!cs->has_color) {
                /* Colour each level by where it sits in the level range, as
                   contour() does with a colormap and no explicit colour. */
                const double t = cs->levels.size() < 2
                                     ? 0.5
                                     : static_cast<double>(i) /
                                           static_cast<double>(cs->levels.size() - 1);
                color = colormap_lookup(cs->cmap, t);
            }

            GraphicsContext gc;
            gc.color = color;
            gc.linewidth = cs->linewidth;
            /* A Collection with no capstyle of its own: the GraphicsContext
               default, butt. It shows at line ends and on every dash. */
            gc.cap = CapStyle::Butt;
            gc.join = JoinStyle::Round;
            gc.has_cliprect = true;
            gc.cliprect = box;
            /* A monochrome set draws its negative levels in
               contour.negative_linestyle, 'dashed'. */
            if (cs->has_color && cs->levels[i] < 0.0) {
                for (const auto &[on, off] : dash_pattern(LineStyle::Dashed, cs->linewidth)) {
                    gc.dashes.emplace_back(on, off);
                }
            }

            if (!cs->labeled) {
                renderer.draw_path(gc, scaled, to_display, nullptr);
                continue;
            }

            /* Labelled: each connected component is considered on its own, and one
               that gets a label is drawn broken so the text sits in clear space. */
            FontProps font;
            font.size = cs->label_size;
            const std::string &text = i < label_text.size() ? label_text[i] : std::string();
            const double lw = renderer.measure_text(text, font).width;

            const std::vector<std::pair<std::vector<Point>, bool>> comps =
                components_in_display(src, [&](double x, double y) {
                    return to_display(Point{scale_x(x), scale_y(y)});
                });

            Path drawn; /* already in display coordinates */
            auto add_piece = [&](const std::vector<Point> &piece, bool closed) {
                if (piece.size() < 2) {
                    return;
                }
                const std::size_t stop = closed ? piece.size() - 1 : piece.size();
                drawn.move_to(piece[0].x, piece[0].y);
                for (std::size_t k = 1; k < stop; ++k) {
                    drawn.line_to(piece[k].x, piece[k].y);
                }
                if (closed) {
                    drawn.close_poly();
                }
            };

            for (const std::pair<std::vector<Point>, bool> &comp : comps) {
                if (!line_fits_label(comp.first, lw)) {
                    add_piece(comp.first, comp.second);
                    continue;
                }

                const LabelSpot spot = locate_label(comp.first, lw, placed_labels);
                const LabelBreak brk =
                    split_for_label(comp.first, comp.second, spot.idx, lw,
                                    cs->label_inline_spacing, cs->rightside_up);

                if (cs->label_inline) {
                    for (const std::vector<Point> &piece : brk.pieces) {
                        add_piece(piece, /*closed=*/false);
                    }
                } else {
                    add_piece(comp.first, comp.second);
                }

                GraphicsContext tgc;
                tgc.color = cs->label_has_color ? cs->label_color : color;
                renderer.draw_text(tgc, spot.x, spot.y, text, font, brk.angle_deg,
                                   HAlign::Center, VAlign::Center);
                placed_labels.push_back(Point{spot.x, spot.y});
            }

            if (drawn.size() > 0) {
                renderer.draw_path(gc, drawn, Affine2D(), nullptr);
            }
        }
    }
}

namespace
{

/* One arrow's outline in arrow-width units (matplotlib's _h_arrows). The arrow
   points along +x, symmetric about it: eight vertices up the top edge to the tip
   and back. Two degeneracies from matplotlib: below minshaft * headlength the
   whole shape is scaled down so the head does not eat past the tail; below
   minlength it becomes a small heptagonal dot with no direction. */
void arrow_outline(double length, const QuiverField &q, double out_x[8], double out_y[8])
{
    const double minsh = q.minshaft * q.headlength;
    /* The index pattern that turns four x values into the eight-vertex ring. */
    const int ii[8] = {0, 1, 2, 3, 2, 1, 0, 0};

    if (length < q.minlength) {
        for (int k = 0; k < 8; ++k) {
            const double th = static_cast<double>(k) * (3.14159265358979323846 / 3.0);
            out_x[k] = std::cos(th) * q.minlength * 0.5;
            out_y[k] = std::sin(th) * q.minlength * 0.5;
        }
        return;
    }

    double x[4];
    const double y[4] = {0.5, 0.5, 0.5 * q.headwidth, 0.0};
    double shrink = 1.0;
    if (length < minsh) {
        /* The short form: a fixed shape, scaled by how short it is. */
        x[0] = 0.0;
        x[1] = minsh - q.headaxislength;
        x[2] = minsh - q.headlength;
        x[3] = minsh;
        shrink = minsh != 0.0 ? length / minsh : 0.0;
    } else {
        x[0] = 0.0;
        x[1] = length - q.headaxislength;
        x[2] = length - q.headlength;
        x[3] = length;
    }

    for (int k = 0; k < 8; ++k) {
        out_x[k] = x[ii[k]] * shrink;
        out_y[k] = y[ii[k]] * shrink;
    }
    /* The return leg is the mirror image, indices 3..6 (the tip at 3 is zero, so
       negating it is harmless). */
    for (int k = 3; k <= 6; ++k) {
        out_y[k] = -out_y[k];
    }
}

} // namespace

double Axes::quiver_dots_per_unit(QuiverUnits units, const Bbox &box, double px_per_pt) const
{
    /* bbox over viewLim, both in data -- not the scaled space. */
    const Bbox vl = view_limits();
    switch (units) {
    case QuiverUnits::Width: return box.width();
    case QuiverUnits::Height: return box.height();
    case QuiverUnits::Dots: return 1.0;
    case QuiverUnits::Inches: return 72.0 * px_per_pt;
    case QuiverUnits::X: return box.width() / std::fabs(vl.x1 - vl.x0);
    case QuiverUnits::Y: return box.height() / std::fabs(vl.y1 - vl.y0);
    case QuiverUnits::XY:
    default:
        return std::hypot(box.width(), box.height()) /
               std::hypot(vl.x1 - vl.x0, vl.y1 - vl.y0);
    }
}

void Axes::draw_quivers(Renderer &renderer, const Bbox &box, double px_per_pt)
{

    if (quivers.empty() && quiver_keys.empty() && barb_fields.empty() && streams.empty()) {
        return;
    }

    const Affine2D to_display = scale_to_display(box);

    /* Build one arrow's path in display coordinates from its start, length, and
       direction. */
    auto arrow_path = [&](const QuiverField &q, double px, double py, double length,
                          double theta, double width_px, Path &out) {
        double ax[8];
        double ay[8];
        arrow_outline(length, q, ax, ay);

        if (q.pivot == QuiverPivot::Middle) {
            const double half = 0.5 * ax[3];
            for (double &v : ax) {
                v -= half;
            }
        } else if (q.pivot == QuiverPivot::Tip) {
            const double tip = ax[3];
            for (double &v : ax) {
                v -= tip;
            }
        }

        const double ct = std::cos(theta);
        const double st = std::sin(theta);
        for (int k = 0; k < 8; ++k) {
            const double rx = (ax[k] * ct - ay[k] * st) * width_px;
            const double ry = (ax[k] * st + ay[k] * ct) * width_px;
            if (k == 0) {
                out.move_to(px + rx, py + ry);
            } else {
                out.line_to(px + rx, py + ry);
            }
        }
        out.close_poly();
    };

    for (const std::unique_ptr<QuiverField> &qp : quivers) {
        const QuiverField &q = *qp;
        const std::size_t n = q.x.size();
        if (n == 0 || box.width() <= 0.0) {
            continue;
        }

        /*
         * matplotlib's _set_transform and _make_verts. An arrow unit is
         * `units` (trans_scale pixels); `span` is the axes width in those
         * units, which is 1 under the default units='width'.
         */
        const double trans_scale = quiver_dots_per_unit(q.units, box, px_per_pt);
        if (!(trans_scale > 0.0) || !std::isfinite(trans_scale)) {
            continue;
        }
        const double span = box.width() / trans_scale;
        double width = q.width;
        if (!q.has_width) {
            const double sn = std::min(std::max(std::sqrt(static_cast<double>(n)), 8.0), 25.0);
            width = 0.06 * span / sn;
        }

        /* angles='xy' or scale_units='xy' measure each arrow on screen: from
           (x, y) to (x, y) + eps * (u, v), eps 1 when both are 'xy' (so arrows
           built by differencing X and Y join up) and otherwise a thousandth of
           the data's largest extent, to keep clear of roundoff. */
        const bool xy_scale = q.has_scale_units && q.scale_units == QuiverUnits::XY;
        const bool xy_angles = q.angles == QuiverAngles::XY;
        std::vector<double> scr_angle, scr_length;
        if (xy_angles || xy_scale) {
            double eps = 1.0;
            if (!(xy_angles && xy_scale)) {
                const Bbox dl = data_limits();
                eps = 0.001 * std::max({std::fabs(dl.x0), std::fabs(dl.y0), std::fabs(dl.x1),
                                        std::fabs(dl.y1)});
            }
            scr_angle.resize(n);
            scr_length.resize(n);
            for (std::size_t i = 0; i < n; ++i) {
                const Point a = to_display(Point{scale_x(q.x[i]), scale_y(q.y[i])});
                const Point b = to_display(
                    Point{scale_x(q.x[i] + eps * q.u[i]), scale_y(q.y[i] + eps * q.v[i])});
                scr_angle[i] = std::atan2(b.y - a.y, b.x - a.x);
                scr_length[i] = std::hypot(b.x - a.x, b.y - a.y) / eps;
            }
        }
        /* The magnitude that becomes length: on screen when scale_units='xy'
           (and the angles are not fixed), else |(u, v)|. */
        const bool screen_mag = xy_scale && q.angles != QuiverAngles::Fixed;
        const auto magnitude = [&](std::size_t i) {
            return screen_mag ? scr_length[i] : std::hypot(q.u[i], q.v[i]);
        };

        double scale_auto = 0.0;
        {
            /* NOTE the different clamp from width's: max(10, sqrt(N)) here,
               clip(sqrt(N), 8, 25) there. matplotlib really does use two, and
               they disagree for N below 64 and above 625. Masked (non-finite)
               arrows stay out of the mean. */
            const double sn = std::max(10.0, std::sqrt(static_cast<double>(n)));
            double amean = 0.0;
            std::size_t good = 0;
            for (std::size_t i = 0; i < n; ++i) {
                if (std::isfinite(q.u[i]) && std::isfinite(q.v[i])) {
                    amean += magnitude(i);
                    ++good;
                }
            }
            amean = good != 0 ? amean / static_cast<double>(good) : 0.0;
            scale_auto = 1.8 * amean * sn / span;
        }
        double widthu_per_lenu = 1.0;
        if (q.has_scale_units) {
            const double dx =
                xy_scale ? 1.0 : quiver_dots_per_unit(q.scale_units, box, px_per_pt);
            widthu_per_lenu = dx / trans_scale;
        }
        const double scale = q.has_scale ? q.scale : scale_auto * widthu_per_lenu;
        q.resolved_scale = scale;
        q.resolved_width = width;
        q.resolved_trans_scale = trans_scale;
        q.resolved_widthu_per_lenu = widthu_per_lenu;

        const double width_px = width * trans_scale;
        const double denom = scale * width / widthu_per_lenu;

        GraphicsContext gc;
        gc.color = Color::none();
        gc.linewidth = 0.0; /* quiver sets linewidths=(0,) */
        gc.join = JoinStyle::Round;
        gc.has_cliprect = true;
        gc.cliprect = box;

        /* With C, the norm autoscales over the finite values of C. */
        const bool mapped = !q.c.empty();
        double cmin = q.vmin;
        double cmax = q.vmax;
        if (mapped && !q.has_vlimits) {
            cmin = std::numeric_limits<double>::infinity();
            cmax = -cmin;
            for (double cv : q.c) {
                if (std::isfinite(cv)) {
                    cmin = std::min(cmin, cv);
                    cmax = std::max(cmax, cv);
                }
            }
        }
        const bool per_arrow = mapped || !q.colors.empty();

        Path path;
        for (std::size_t i = 0; i < n; ++i) {
            const Point p = to_display(Point{scale_x(q.x[i]), scale_y(q.y[i])});
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(q.u[i]) ||
                !std::isfinite(q.v[i])) {
                continue; /* matplotlib masks an invalid U or V */
            }
            const double a = magnitude(i);
            const double length = denom != 0.0 ? a / denom : 0.0;
            /* angles='uv': direction in (u, v) as given, so an arrow keeps its
               angle when the axes aspect changes; 'xy' follows the screen;
               fixed angles are degrees (NaN reads as 0). */
            double theta = std::atan2(q.v[i], q.u[i]);
            if (xy_angles) {
                theta = scr_angle[i];
            } else if (q.angles == QuiverAngles::Fixed) {
                const double deg = i < q.angle_degrees.size() ? q.angle_degrees[i] : 0.0;
                theta = std::isfinite(deg) ? deg * 3.14159265358979323846 / 180.0 : 0.0;
            }
            if (!per_arrow) {
                arrow_path(q, p.x, p.y, length, theta, width_px, path);
                continue;
            }
            /* A PolyCollection with one facecolour per arrow: each arrow is its
               own path, in the order given. */
            Path one;
            arrow_path(q, p.x, p.y, length, theta, width_px, one);
            if (one.size() == 0) {
                continue;
            }
            Color face = q.color;
            if (mapped) {
                face = i < q.c.size() ? colormap_lookup(q.cmap, normalize(q.c[i], cmin, cmax))
                                      : Color::none();
            } else {
                face = q.colors[i % q.colors.size()];
            }
            renderer.draw_path(gc, one, Affine2D(), &face);
        }

        if (path.size() != 0) {
            renderer.draw_path(gc, path, Affine2D(), &q.color);
        }
    }

    for (const std::unique_ptr<StreamPlot> &sp : streams) {
        const StreamPlot &s = *sp;

        for (const Path &src : s.lines) {
            if (src.size() < 2) {
                continue;
            }
            Path scaled;
            scaled.codes = src.codes;
            scaled.vertices.reserve(src.vertices.size());
            for (std::size_t k = 0; k < src.size(); ++k) {
                scaled.vertices.push_back(scale_x(src.vertices[2 * k]));
                scaled.vertices.push_back(scale_y(src.vertices[2 * k + 1]));
            }

            GraphicsContext gc;
            gc.color = s.color;
            gc.linewidth = s.linewidth;
            /* A LineCollection: butt caps and round joins, not a Line2D's round
               caps. */
            gc.cap = CapStyle::Butt;
            gc.join = JoinStyle::Round;
            gc.has_cliprect = true;
            gc.cliprect = box;
            renderer.draw_path(gc, scaled, to_display, nullptr);
        }

        /* The arrowheads. arrowstyle='-|>' is a filled triangle (not annotate's
           open '->'); mutation_scale is 10 * arrowsize, and head_length/width are
           0.4 and 0.2 of it, as for every ArrowStyle._Curve. */
        const double mutation = 10.0 * s.arrowsize * px_per_pt;
        const double head_length = 0.4 * mutation;
        const double head_width = 0.2 * mutation;

        for (const StreamPlot::Arrow &a : s.arrows) {
            const Point tail = to_display(Point{scale_x(a.tail_x), scale_y(a.tail_y)});
            const Point head = to_display(Point{scale_x(a.head_x), scale_y(a.head_y)});
            const double dx = head.x - tail.x;
            const double dy = head.y - tail.y;
            const double len = std::hypot(dx, dy);
            if (!(len > 0.0)) {
                continue;
            }
            const double ux = dx / len;
            const double uy = dy / len;

            /* Base of the triangle, one head-length back along the line, with
               head_width to each side (head is 2 * head_width wide). The tip sits
               back from the aimed point by two amounts: shrinkB (FancyArrowPatch
               shrinks its path 2 points at each end) and pad_projected
               (0.5 * linewidth / sin(t), the projecting stroke's overshoot). */
            const double head_dist = std::hypot(head_length, head_width);
            const double sin_t = head_dist > 0.0 ? head_width / head_dist : 1.0;
            const double pad = sin_t > 0.0 ? 0.5 * s.linewidth * px_per_pt / sin_t : 0.0;
            const double shrink = 2.0 * px_per_pt;

            const double tipx = head.x - ux * (pad + shrink);
            const double tipy = head.y - uy * (pad + shrink);
            const double bx = tipx - ux * head_length;
            const double by = tipy - uy * head_length;
            const double px = -uy * head_width;
            const double py = ux * head_width;

            GraphicsContext gc;
            gc.color = s.color;
            gc.linewidth = s.linewidth;
            gc.join = JoinStyle::Round;
            gc.has_cliprect = true;
            gc.cliprect = box;

            /* The shaft, from shrunk tail to pulled-back tip (paths[0] of the
               transmuted arrow). */
            renderer.draw_path(gc,
                               Path::segment(tail.x + ux * shrink, tail.y + uy * shrink,
                                             tipx, tipy),
                               Affine2D(), nullptr);

            /* The head: filled and stroked, since '-|>' is fillable and the patch
               carries both colours. */
            Path tri;
            tri.move_to(tipx, tipy);
            tri.line_to(bx + px, by + py);
            tri.line_to(bx - px, by - py);
            tri.close_poly();
            renderer.draw_path(gc, tri, Affine2D(), &s.color);
        }
    }

    for (const std::unique_ptr<BarbField> &bp : barb_fields) {
        const BarbField &b = *bp;
        const std::size_t n = b.x.size();
        if (n == 0) {
            continue;
        }

        /* Barbs are a PolyCollection with sizes=(length^2 / 4,), and a Collection
           scales vertices by sqrt(size) * dpi / 72. The shape is built at `length`
           units then multiplied by length/2 * dpi/72, so length enters twice (the
           square is matplotlib's "empirically determined"). */
        const double px_scale = (b.length / 2.0) * renderer.dpi() / 72.0;

        const double spacing = b.length * b.spacing;
        const double full_height = b.length * b.height;
        const double full_width = b.length * b.width;
        const double empty_rad = b.length * b.emptybarb;
        const double endy = b.has_pivot_value ? b.pivot_value
                            : b.pivot_middle  ? -b.length / 2.0
                                              : 0.0;

        /* Colours: barbcolor/flagcolor as matplotlib resolves them, and with
           C a face colour per barb normalised over the finite C values. */
        const bool mapped = !b.c.empty();
        const Color face0 = b.has_flagcolor ? b.flagcolor
                            : b.has_barbcolor ? b.barbcolor
                                              : b.color;
        const bool fixed_edge = b.has_barbcolor && b.has_flagcolor;
        double cmin = b.vmin, cmax = b.vmax;
        if (mapped && !b.has_vlimits) {
            cmin = std::numeric_limits<double>::infinity();
            cmax = -cmin;
            for (double cv : b.c) {
                if (std::isfinite(cv)) {
                    cmin = std::min(cmin, cv);
                    cmax = std::max(cmax, cv);
                }
            }
        }

        GraphicsContext gc;
        /* edgecolors='face' and linewidth 1: most of the polygon is degenerate,
           so without a stroke only the flags would show. */
        gc.linewidth = b.linewidth;
        gc.join = JoinStyle::Round;
        gc.has_cliprect = true;
        gc.cliprect = box;

        Path shared;
        for (std::size_t i = 0; i < n; ++i) {
            const Point p = to_display(Point{scale_x(b.x[i]), scale_y(b.y[i])});
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(b.u[i]) ||
                !std::isfinite(b.v[i])) {
                continue;
            }
            if (mapped && (i >= b.c.size() || !std::isfinite(b.c[i]))) {
                continue;
            }
            const bool flip_i = b.flips.empty() ? b.flip
                                                : (i < b.flips.size() && b.flips[i] != 0);
            /* With per-barb colours each barb is its own path; otherwise one
               path holds them all. */
            Path one;
            Path &path = mapped ? one : shared;
            const auto finish = [&]() {
                if (!mapped || one.size() == 0) {
                    return;
                }
                const Color face =
                    colormap_lookup(b.cmap, normalize(b.c[i], cmin, cmax));
                gc.color = fixed_edge ? b.barbcolor : face;
                renderer.draw_path(gc, one, Affine2D(), &face);
            };

            /* How many of each feature (_find_tails). Magnitude is first rounded
               to the nearest half-barb, so 27 and 23 both read as two and a half. */
            double mag = std::hypot(b.u[i], b.v[i]);
            if (b.rounding) {
                mag = b.half * std::nearbyint(mag / b.half);
            }
            const int nflags = static_cast<int>(std::floor(mag / b.flag));
            mag -= nflags * b.flag;
            const int nbarbs = static_cast<int>(std::floor(mag / b.full));
            mag -= nbarbs * b.full;
            const bool half_barb = mag >= b.half;
            const bool empty = !(half_barb || nflags > 0 || nbarbs > 0);

            /* The staff points into the wind: built along +y and rotated by
               atan2(v, u) + 90 degrees, landing opposite the vector. */
            const double phi = std::atan2(b.v[i], b.u[i]) + 3.14159265358979323846 / 2.0;
            const double ct = std::cos(phi);
            const double st = std::sin(phi);

            std::vector<Point> verts;
            if (empty) {
                /* A calm reading is a circle, drawn as a 20-gon from the top
                   (Path.unit_regular_polygon's phase). Unfilled, it is traced then
                   retraced backwards so the fill cancels to an outline. */
                constexpr int kRes = 20;
                std::vector<Point> circ;
                circ.reserve(kRes + 1);
                for (int k = 0; k <= kRes; ++k) {
                    const double th = 2.0 * 3.14159265358979323846 * k / kRes +
                                      3.14159265358979323846 / 2.0;
                    circ.push_back(Point{std::cos(th) * empty_rad, std::sin(th) * empty_rad});
                }
                verts = circ;
                if (!b.fill_empty) {
                    for (auto it = circ.rbegin(); it != circ.rend(); ++it) {
                        verts.push_back(*it);
                    }
                }
                /* No preferred orientation, so this one is not rotated. */
                for (std::size_t k = 0; k < verts.size(); ++k) {
                    const double vx = verts[k].x * px_scale;
                    const double vy = verts[k].y * px_scale;
                    if (k == 0) {
                        path.move_to(p.x + vx, p.y + vy);
                    } else {
                        path.line_to(p.x + vx, p.y + vy);
                    }
                }
                path.close_poly();
                finish();
                continue;
            }

            const double bh = flip_i ? -full_height : full_height;
            double offset = b.length;
            verts.push_back(Point{0.0, endy});

            for (int k = 0; k < nflags; ++k) {
                if (offset != b.length) {
                    offset += spacing / 2.0;
                }
                verts.push_back(Point{0.0, endy + offset});
                verts.push_back(Point{bh, endy - full_width / 2.0 + offset});
                verts.push_back(Point{0.0, endy - full_width + offset});
                offset -= full_width + spacing;
            }
            for (int k = 0; k < nbarbs; ++k) {
                /* Three vertices per barb: out and straight back, so the
                   polygon pulls a line out of itself and the fill cancels. */
                verts.push_back(Point{0.0, endy + offset});
                verts.push_back(Point{bh, endy + offset + full_width / 2.0});
                verts.push_back(Point{0.0, endy + offset});
                offset -= spacing;
            }
            if (half_barb) {
                /* A lone half barb is pushed down the staff so it cannot be
                   mistaken for a full one. */
                if (offset == b.length) {
                    verts.push_back(Point{0.0, endy + offset});
                    offset -= 1.5 * spacing;
                }
                verts.push_back(Point{0.0, endy + offset});
                verts.push_back(Point{bh / 2.0, endy + offset + full_width / 4.0});
                verts.push_back(Point{0.0, endy + offset});
            }

            for (std::size_t k = 0; k < verts.size(); ++k) {
                const double rx = (verts[k].x * ct - verts[k].y * st) * px_scale;
                const double ry = (verts[k].x * st + verts[k].y * ct) * px_scale;
                if (k == 0) {
                    path.move_to(p.x + rx, p.y + ry);
                } else {
                    path.line_to(p.x + rx, p.y + ry);
                }
            }
            path.close_poly();
            finish();
        }

        if (mapped || shared.size() == 0) {
            continue;
        }
        gc.color = fixed_edge ? b.barbcolor : face0;
        renderer.draw_path(gc, shared, Affine2D(), &face0);
    }

    /*
     * The key arrow. Drawn with the field's RESOLVED scale, which is why this
     * runs after the loop above rather than beside it -- with no explicit
     * scale the field does not know its own until it has been drawn once.
     */
    for (const QuiverKey &key : quiver_keys) {
        if (key.field >= quivers.size()) {
            continue;
        }
        const QuiverField &q = *quivers[key.field];
        if (q.resolved_width <= 0.0) {
            continue;
        }

        const double px = box.x0 + key.x * box.width();
        const double py = box.y0 + key.y * box.height();
        const double width_px = q.resolved_width * q.resolved_trans_scale;
        const double denom =
            q.resolved_scale * q.resolved_width / q.resolved_widthu_per_lenu;
        /* Under scale_units='xy' the key's length is measured on screen too:
           its magnitude along x, through the data transform. */
        double mag = key.magnitude;
        if (q.has_scale_units && q.scale_units == QuiverUnits::XY) {
            const Bbox view = view_limits();
            mag *= box.width() / (scale_x(view.x1) - scale_x(view.x0));
        }
        const double length = denom != 0.0 ? mag / denom : 0.0;

        /*
         * The key arrow pivots on its MIDDLE, whatever the field does.
         * labelpos='N' sets it -- matplotlib maps each label position to a
         * pivot, so that (x, y) means the middle of the arrow when the label
         * is above it and the tip or tail when the label is beside it.
         */
        QuiverField keyq = q;
        keyq.pivot = QuiverPivot::Middle;

        Path path;
        arrow_path(keyq, px, py, length, 0.0, width_px, path);

        GraphicsContext gc;
        gc.color = Color::none();
        gc.linewidth = 0.0;
        gc.join = JoinStyle::Round;
        renderer.draw_path(gc, path, Affine2D(), &q.color);

        if (!key.label.empty()) {
            FontProps font;
            font.size = key.fontsize;
            GraphicsContext tgc;
            tgc.color = Color::rgb(0.0, 0.0, 0.0);
            /* labelsep is 0.1 inches (not font-size relative), measured from the
               anchor point. */
            const double sep = 0.1 * renderer.dpi();
            renderer.draw_text(tgc, px, py + sep, key.label, font, 0.0, HAlign::Center,
                               VAlign::Bottom);
        }
    }
}

void Axes::draw_tables(Renderer &renderer, const Bbox &box, double px_per_pt)
{


    for (const std::unique_ptr<Table> &tp : tables) {
        const Table &t = *tp;
        if (t.rows == 0 || t.cols == 0 || box.width() <= 0.0 || box.height() <= 0.0) {
            continue;
        }

        FontProps font;
        font.size = t.fontsize;

        /* Row height is approximated from the font size (matplotlib's
           _approx_text_height), so every row is the same height. */
        const double row_h = t.fontsize / 72.0 * renderer.dpi() / box.height() * 1.2;

        const bool has_col_labels = !t.col_labels.empty();
        const bool has_row_labels = !t.row_labels.empty();
        const std::size_t offset = has_col_labels ? 1 : 0;
        const std::size_t total_rows = t.rows + offset;

        std::vector<double> widths(t.cols, 1.0 / static_cast<double>(t.cols));
        if (t.col_widths.size() == t.cols) {
            widths = t.col_widths;
        }

        /* The row-label column is measured: matplotlib's auto_set_column_width(-1)
           sizes it to the widest label plus a tenth of that width each side. */
        double label_w = 0.0;
        if (has_row_labels) {
            for (const std::string &s : t.row_labels) {
                const double w = renderer.measure_text(s, font).width / box.width();
                label_w = std::max(label_w, w * (1.0 + 2.0 * 0.1));
            }
        }

        /* Columns run left to right from zero, the label column first. */
        std::vector<double> lefts(t.cols, 0.0);
        double x = label_w;
        for (std::size_t c = 0; c < t.cols; ++c) {
            lefts[c] = x;
            x += widths[c];
        }
        const double grid_w = x - label_w;
        const double grid_l = label_w;

        /* Rows are laid out from the BOTTOM by descending index, so row 0 --
           the column labels when there are any -- ends up on top. */
        auto bottom_of = [&](std::size_t r) {
            return static_cast<double>(total_rows - 1 - r) * row_h;
        };
        const double grid_h = static_cast<double>(total_rows) * row_h;
        const double grid_b = 0.0;

        /* Where the whole block goes. The row-label column is not positioned:
           matplotlib's _get_grid_bbox takes only cells with row >= 0 and col >= 0,
           so the labels hang off the left of the positioned grid. */
        constexpr double kAxesPad = 0.02; /* Table.AXESPAD */
        double ox = (0.5 - grid_w / 2.0) - grid_l;
        double oy = (0.5 - grid_h / 2.0) - grid_b;

        using L = TableLoc;
        const L loc = t.loc;
        auto is = [&](std::initializer_list<L> set) {
            for (L v : set) {
                if (loc == v) {
                    return true;
                }
            }
            return false;
        };

        if (is({L::UpperLeft, L::LowerLeft, L::CenterLeft})) {
            ox = kAxesPad - grid_l;
        }
        if (is({L::Best, L::UpperRight, L::LowerRight, L::Right, L::CenterRight})) {
            ox = 1.0 - (grid_l + grid_w + kAxesPad);
        }
        if (is({L::Best, L::UpperRight, L::UpperLeft, L::UpperCenter})) {
            oy = 1.0 - (grid_b + grid_h + kAxesPad);
        }
        if (is({L::LowerLeft, L::LowerRight, L::LowerCenter})) {
            oy = kAxesPad - grid_b;
        }
        if (is({L::LowerCenter, L::UpperCenter, L::Center})) {
            ox = (0.5 - grid_w / 2.0) - grid_l;
        }
        if (is({L::CenterLeft, L::CenterRight, L::Center})) {
            oy = (0.5 - grid_h / 2.0) - grid_b;
        }
        /* Outside the axes entirely. */
        if (is({L::TopLeft, L::BottomLeft, L::Left})) {
            ox = -(grid_l + grid_w);
        }
        if (is({L::TopRight, L::BottomRight, L::Right})) {
            ox = 1.0 - grid_l;
        }
        if (is({L::TopRight, L::TopLeft, L::Top})) {
            oy = 1.0 - grid_b;
        }
        if (is({L::BottomLeft, L::BottomRight, L::Bottom})) {
            oy = -(grid_b + grid_h);
        }

        auto to_px = [&](double ax_x, double ax_y) {
            return Point{box.x0 + ax_x * box.width(), box.y0 + ax_y * box.height()};
        };

        /* One cell: a filled, stroked rectangle with its text inside. */
        auto draw_cell = [&](double cx, double cy, double cw, double ch,
                             const std::string &text, HAlign align) {
            const Point p0 = to_px(cx + ox, cy + oy);
            const Point p1 = to_px(cx + ox + cw, cy + oy + ch);

            Path rect;
            rect.move_to(p0.x, p0.y);
            rect.line_to(p1.x, p0.y);
            rect.line_to(p1.x, p1.y);
            rect.line_to(p0.x, p1.y);
            rect.close_poly();

            GraphicsContext gc;
            gc.color = t.edgecolor;
            gc.linewidth = t.linewidth;
            gc.join = JoinStyle::Miter; /* a Rectangle is a Patch */
            /* Not clipped to the axes: the default position is below it, so
               clipping would erase the whole table. */
            renderer.draw_path(gc, rect, Affine2D(), &t.facecolor);

            if (text.empty()) {
                return;
            }

            /* A tenth of the cell width of padding at the aligned side (Cell.PAD,
               a fraction of the cell, not the text). */
            constexpr double kPad = 0.1;
            double tx = p0.x + (p1.x - p0.x) / 2.0;
            if (align == HAlign::Left) {
                tx = p0.x + (p1.x - p0.x) * kPad;
            } else if (align == HAlign::Right) {
                tx = p0.x + (p1.x - p0.x) * (1.0 - kPad);
            }
            const double ty = p0.y + (p1.y - p0.y) / 2.0;

            GraphicsContext tgc;
            tgc.color = Color::rgb(0.0, 0.0, 0.0);
            renderer.draw_text(tgc, tx, ty, text, font, 0.0, align, VAlign::Center);
        };

        if (has_col_labels) {
            for (std::size_t c = 0; c < t.cols; ++c) {
                draw_cell(lefts[c], bottom_of(0), widths[c], row_h,
                          c < t.col_labels.size() ? t.col_labels[c] : std::string(),
                          t.col_loc);
            }
        }

        for (std::size_t r = 0; r < t.rows; ++r) {
            const std::size_t tr = r + offset;
            if (has_row_labels) {
                draw_cell(0.0, bottom_of(tr), label_w, row_h,
                          r < t.row_labels.size() ? t.row_labels[r] : std::string(),
                          t.row_loc);
            }
            for (std::size_t c = 0; c < t.cols; ++c) {
                const std::size_t k = r * t.cols + c;
                draw_cell(lefts[c], bottom_of(tr), widths[c], row_h,
                          k < t.cells.size() ? t.cells[k] : std::string(), t.cell_loc);
            }
        }
    }
}

void Axes::draw_texts(Renderer &renderer, const Bbox &box, double px_per_pt)
{
    const Affine2D to_display = scale_to_display(box);

    for (const std::unique_ptr<TextArtist> &t : texts) {
        Point anchor = to_display(Point{scale_x(t->x), scale_y(t->y)});
        if (!std::isfinite(anchor.x) || !std::isfinite(anchor.y)) {
            continue;
        }
        /* bar_label's padding: points applied in display space, after the
           data->pixel transform and before everything below, so both the arrow
           box and plain-text alignment read the shifted `anchor`. vplot's display
           y increases upward, so a positive offset_y_pt adds directly. */
        anchor.x += t->offset_x_pt * px_per_pt;
        anchor.y += t->offset_y_pt * px_per_pt;

        if (t->has_arrow) {
            const Point target =
                to_display(Point{scale_x(t->arrow_x), scale_y(t->arrow_y)});
            if (std::isfinite(target.x) && std::isfinite(target.y)) {
                {
                    /* The box is computed unrotated; a rotated annotation label
                       is rare and the error is at most a few pixels. */
                    FontProps probe;
                    probe.size = t->size;
                    const TextExtent e = renderer.measure_text(t->text, probe);
                    /* The clip box is the text's layout extent (ascent + descent),
                       not its ink, so a label without a descender starts its leader
                       line where one with a descender would. */
                    const LineMetrics line = renderer.text_line_metrics(t->text, probe);
                    const double box_h = line.ascent + line.descent;

                    double bx0 = anchor.x;
                    switch (t->halign) {
                        case HAlign::Center: bx0 -= e.width / 2.0; break;
                        case HAlign::Right: bx0 -= e.width; break;
                        case HAlign::Left: break;
                    }
                    /* The box's BOTTOM edge, from where the alignment puts the
                       baseline. */
                    double by0 = anchor.y;
                    switch (t->valign) {
                        case VAlign::Top: by0 -= box_h; break;
                        case VAlign::Center: by0 -= box_h / 2.0; break;
                        case VAlign::Baseline: by0 -= line.descent; break;
                        case VAlign::CenterBaseline:
                            by0 -= line.ascent / 2.0 + line.descent;
                            break;
                        case VAlign::Bottom: break;
                    }

                    /* The leader starts at the box centre, not the text anchor:
                       matplotlib's `relpos` defaults to (0.5, 0.5), and the ray
                       from there sets the arrow's angle. */
                    const double cx = bx0 + e.width / 2.0;
                    const double cy = by0 + box_h / 2.0;

                    const double dx = target.x - cx;
                    const double dy = target.y - cy;
                    const double len = std::sqrt(dx * dx + dy * dy);
                    if (!(len > 1.0)) {
                        continue;
                    }
                    const double ux = dx / len;
                    const double uy = dy / len;

                    /* The path is then clipped where it leaves the box -- the
                       nearest slab crossing, always ahead of us since we start
                       inside -- and shrunk by another two points. */
                    double exit_t = 0.0;
                    if (e.width > 0.0 && box_h > 0.0) {
                        const double tx = ux > 0.0   ? (bx0 + e.width - cx) / ux
                                          : ux < 0.0 ? (bx0 - cx) / ux
                                                     : len;
                        const double ty = uy > 0.0   ? (by0 + box_h - cy) / uy
                                          : uy < 0.0 ? (by0 - cy) / uy
                                                     : len;
                        exit_t = std::min(std::min(tx, ty), len);
                        exit_t = std::max(0.0, exit_t) + 2.0 * px_per_pt; /* shrinkA */
                    }

                    const double start_x = cx + ux * exit_t;
                    const double start_y = cy + uy * exit_t;

                    /* matplotlib's arrowstyle "->" (ArrowStyle.CurveB): the shaft
                       runs to the head's vertex and the head is an open V stroked
                       on top, not a filled triangle. Both ends are pulled back by
                       FancyArrowPatch's default two-point shrink. */
                    const double shrink_b = 2.0 * px_per_pt;
                    const double tip_x = target.x - ux * shrink_b;
                    const double tip_y = target.y - uy * shrink_b;

                    /* head_length and head_width are fractions of the mutation
                       scale, which annotate sets to the text's font size. */
                    const double mutation = t->size * px_per_pt;
                    const double head_length = 0.4 * mutation;
                    const double head_width = 0.2 * mutation;
                    const double head_dist = std::hypot(head_length, head_width);
                    const double cos_t = head_length / head_dist;
                    const double sin_t = head_width / head_dist;

                    /* The V's projecting cap would overshoot the annotated point,
                       so the whole head moves back by that overshoot. */
                    const double lw_px = t->arrow_linewidth * px_per_pt;
                    const double pad = sin_t > 0.0 ? 0.5 * lw_px / sin_t : 0.0;
                    const double vx = tip_x - ux * pad;
                    const double vy = tip_y - uy * pad;

                    GraphicsContext agc;
                    agc.color = t->color;
                    agc.linewidth = t->arrow_linewidth;
                    agc.cap = CapStyle::Projecting;
                    agc.join = JoinStyle::Round;
                    agc.has_cliprect = true;
                    agc.cliprect = box;

                    if (std::hypot(vx - start_x, vy - start_y) > 0.0) {
                        renderer.draw_path(agc, Path::segment(start_x, start_y, vx, vy),
                                           Affine2D(), nullptr);
                    }

                    /* Back along the shaft by the head's length, then rotated out
                       to either side, built in display space since the arrow points
                       at a place on screen. */
                    const double bx = -ux * head_dist;
                    const double by = -uy * head_dist;

                    Path head_path;
                    head_path.move_to(vx + (cos_t * bx + sin_t * by),
                                      vy + (-sin_t * bx + cos_t * by));
                    head_path.line_to(vx, vy);
                    head_path.line_to(vx + (cos_t * bx - sin_t * by),
                                      vy + (sin_t * bx + cos_t * by));
                    renderer.draw_path(agc, head_path, Affine2D(), nullptr);
                }
            }
        }

        GraphicsContext gc;
        gc.color = t->color;
        /* Not clipped to the axes: text carries clip_on=False in matplotlib, so a
           label overhanging the frame is drawn overhanging it. */

        FontProps font;
        font.size = t->size;

        renderer.draw_text(gc, anchor.x, anchor.y, t->text, font, t->rotation, t->halign,
                           t->valign);
    }
}

void Axes::draw_reference_lines(Renderer &renderer, const Bbox &box, double px_per_pt)
{
    const Bbox view = view_limits();
    const Affine2D to_display = scale_to_display(box);

    for (const ReferenceLine &line : reference_lines) {
        GraphicsContext gc;
        gc.color = line.color;
        gc.linewidth = line.linewidth;
        gc.has_cliprect = true;
        gc.cliprect = box;
        for (const auto &[on, off] : dash_pattern(line.linestyle, line.linewidth)) {
            gc.dashes.emplace_back(on, off);
        }

        /* An axline is clipped to the view by evaluating it at the view's left and
           right edges, so it stays corner to corner under pan and zoom. Done in
           scale space: matplotlib's axline is straight in the picture, which on a
           log axis is not straight in the data. */
        if (line.through) {
            if (line.vertical_slope) {
                if (line.px < std::min(view.x0, view.x1) ||
                    line.px > std::max(view.x0, view.x1)) {
                    continue;
                }
                const Point p = to_display(Point{scale_x(line.px), scale_y(view.y0)});
                renderer.draw_path(gc, Path::segment(p.x, box.y0, p.x, box.y1),
                                   Affine2D(), nullptr);
                continue;
            }

            const Point a =
                to_display(Point{scale_x(view.x0),
                                 scale_y(line.py + line.slope * (view.x0 - line.px))});
            const Point b =
                to_display(Point{scale_x(view.x1),
                                 scale_y(line.py + line.slope * (view.x1 - line.px))});
            renderer.draw_path(gc, Path::segment(a.x, a.y, b.x, b.y), Affine2D(), nullptr);
            continue;
        }

        /* Spans the axes rather than the data, so it is re-derived from the
           current view every draw and stays full width under pan and zoom. */
        if (line.horizontal) {
            if (line.value < view.y0 || line.value > view.y1) {
                continue;
            }
            const Point p = to_display(Point{scale_x(view.x0), scale_y(line.value)});
            renderer.draw_path(gc, Path::segment(box.x0, p.y, box.x1, p.y), Affine2D(),
                               nullptr);
        } else {
            if (line.value < view.x0 || line.value > view.x1) {
                continue;
            }
            const Point p = to_display(Point{scale_x(line.value), scale_y(view.y0)});
            renderer.draw_path(gc, Path::segment(p.x, box.y0, p.x, box.y1), Affine2D(),
                               nullptr);
        }
    }
}

void Axes::draw_segments(Renderer &renderer, const Bbox &box)
{
    const Affine2D to_display = scale_to_display(box);

    for (const std::unique_ptr<SegmentCollection> &sc_ptr : segment_collections) {
        const SegmentCollection &sc = *sc_ptr;
        if (sc.path.size() == 0 || sc.linestyle == LineStyle::None) {
            continue;
        }

        Path scaled;
        scaled.codes = sc.path.codes;
        scaled.vertices.reserve(sc.path.vertices.size());
        for (std::size_t i = 0; i < sc.path.size(); ++i) {
            scaled.vertices.push_back(scale_x(sc.path.vertices[2 * i]));
            scaled.vertices.push_back(scale_y(sc.path.vertices[2 * i + 1]));
        }

        GraphicsContext gc;
        gc.color = sc.color;
        gc.linewidth = sc.linewidth;
        /* A Collection takes butt caps and miter joins, not a Line2D's round ones,
           so an hlines rule has square ends. */
        gc.cap = sc.cap;
        gc.join = JoinStyle::Miter;
        gc.has_cliprect = true;
        gc.cliprect = box;
        for (const auto &[on, off] : sc.dashes()) {
            gc.dashes.emplace_back(on, off);
        }

        renderer.draw_path(gc, scaled, to_display, nullptr);
    }
}

void Axes::draw_lines(Renderer &renderer, const Bbox &box)
{
    const Affine2D to_display = scale_to_display(box);
    const double px_per_pt = renderer.dpi() / 72.0;

    for (const std::unique_ptr<Line2D> &line_ptr : lines) {
        const Line2D &line = *line_ptr;
        const std::size_t n = std::min(line.xdata.size(), line.ydata.size());
        if (n == 0) {
            continue;
        }

        /* Vertices are pre-transformed into scale space; the affine handles the
           rest. On a linear axis this is a copy, on a log axis it is where the
           curve actually bends. */
        Path path;
        path.vertices.reserve(2 * n);
        for (std::size_t i = 0; i < n; ++i) {
            path.vertices.push_back(scale_x(line.xdata[i]));
            path.vertices.push_back(scale_y(line.ydata[i]));
        }

        /* The data line and its markers: zorder 2.1 over the bars by
           default, 1.9 under them with barsabove. */
        const auto draw_data = [&]() {
            if (line.linestyle != LineStyle::None && n >= 2) {
                GraphicsContext gc;
                gc.color = line.color;
                gc.linewidth = line.linewidth;
                /* lines.solid_capstyle (projecting) vs lines.dash_capstyle (butt):
                   matplotlib picks per line via is_dashed(). A round cap on a dashed
                   line lengthens each dash by a half-linewidth bump at both ends. Join
                   stays round either way (both dash and solid joinstyle default
                   round). */
                gc.cap = line.linestyle == LineStyle::Solid && !line.butt_cap
                             ? CapStyle::Projecting
                             : CapStyle::Butt;
                gc.join = JoinStyle::Round;
                gc.has_cliprect = true;
                gc.cliprect = box;

                for (const auto &[on, off] : line.dashes()) {
                    gc.dashes.emplace_back(on, off);
                }

                path.should_simplify = n > 128;
                renderer.draw_path(gc, path, to_display, nullptr);
            }

            if (line.marker == MarkerStyle::None) {
                return;
            }

            /* Handed to the renderer as one marker plus the points to stamp it at, so
               the raster backend rasterizes the shape once; vector backends fall back
               to one path per point. */
            const Path unit = marker_path(line.marker);
            const double half = 0.5 * line.markersize * px_per_pt;

            const Color face =
                line.has_markerfacecolor ? line.markerfacecolor : line.color;
            const Color edge =
                line.has_markeredgecolor ? line.markeredgecolor : line.color;
            const bool filled = marker_is_filled(line.marker);

            /* Line2D.draw: the marker's own join, butt caps, and snapping
               decided by its size against the marker's snap threshold. */
            GraphicsContext mgc;
            mgc.color = edge;
            mgc.linewidth = line.markeredgewidth;
            mgc.cap = CapStyle::Butt;
            mgc.join = marker_join(line.marker);
            mgc.snap = line.markersize * px_per_pt >= marker_snap_threshold(line.marker)
                           ? GraphicsContext::Snap::On
                           : GraphicsContext::Snap::Off;
            mgc.has_cliprect = true;
            mgc.cliprect = box;

            Path points;
            points.vertices.reserve(2 * n);
            for (std::size_t i = 0; i < n; ++i) {
                const Point p =
                    to_display(Point{scale_x(line.xdata[i]), scale_y(line.ydata[i])});
                if (!std::isfinite(p.x) || !std::isfinite(p.y)) {
                    continue;
                }
                points.move_to(p.x, p.y);
            }

            renderer.draw_markers(mgc, unit, Affine2D::scale(half, half), points, Affine2D(),
                                  filled ? &face : nullptr);
        };

        const auto draw_errorbars = [&]() {
            if (line.xerr.empty() && line.yerr.empty()) {
                return;
            }
            const double cap = line.capsize * px_per_pt;

            GraphicsContext egc;
            egc.color = line.has_ecolor ? line.ecolor : line.color;
            egc.linewidth = line.elinewidth > 0.0 ? line.elinewidth : line.linewidth;
            egc.cap = CapStyle::Butt;
            egc.has_cliprect = true;
            egc.cliprect = box;
            for (const auto &[on, off] : dash_pattern(line.elinestyle, egc.linewidth)) {
                egc.dashes.emplace_back(on, off);
            }

            /* capthick defaults to markeredgewidth, not to the bar's width. */
            GraphicsContext cgc = egc;
            cgc.linewidth = line.capthick >= 0.0 ? line.capthick : line.markeredgewidth;
            /* The cap markers' snap threshold is 1 px, so any visible cap snaps. */
            cgc.snap = 2.0 * cap >= 1.0 ? GraphicsContext::Snap::On : GraphicsContext::Snap::Off;

            /* matplotlib adds the x bars, then the y bars, then all the caps. */
            struct Bar
            {
                Point lo, hi;
                bool vertical;
                bool limited; /* a lolims/uplims point: no end caps */
            };
            std::vector<Bar> bars;

            /* The caps and carets lose the bars' dashes. */
            cgc.dashes.clear();

            /* Horizontal whiskers, the mirror of the vertical ones. */
            const std::size_t mx = std::min(n, line.xerr.size());
            for (std::size_t i = 0; i < mx; ++i) {
                const double xv = line.xdata[i];
                const double elo = line.xerr_lo(i);
                const double ehi = line.xerr_hi(i);
                if (!line.error_drawn(i) || !std::isfinite(xv) || !std::isfinite(elo) ||
                    !std::isfinite(ehi) || (elo == 0.0 && ehi == 0.0)) {
                    continue;
                }
                const double sy = scale_y(line.ydata[i]);
                const Point lo = to_display(Point{scale_x(xv - elo), sy});
                const Point hi = to_display(Point{scale_x(xv + ehi), sy});
                if (!std::isfinite(lo.x) || !std::isfinite(hi.x)) {
                    continue;
                }
                bars.push_back(Bar{lo, hi, false,
                                   Line2D::flag(line.xlolims, i) ||
                                       Line2D::flag(line.xuplims, i)});
            }

            const std::size_t my = std::min(n, line.yerr.size());
            for (std::size_t i = 0; i < my; ++i) {
                const double y = line.ydata[i];
                const double elo = line.yerr_lo(i);
                const double ehi = line.yerr_hi(i);
                if (!line.error_drawn(i) || !std::isfinite(y) || !std::isfinite(elo) ||
                    !std::isfinite(ehi) || (elo == 0.0 && ehi == 0.0)) {
                    continue;
                }
                const double sx = scale_x(line.xdata[i]);
                const Point lo = to_display(Point{sx, scale_y(y - elo)});
                const Point hi = to_display(Point{sx, scale_y(y + ehi)});
                if (!std::isfinite(lo.y) || !std::isfinite(hi.y)) {
                    continue;
                }
                bars.push_back(Bar{lo, hi, true,
                                   Line2D::flag(line.lolims, i) || Line2D::flag(line.uplims, i)});
            }

            for (const Bar &bar : bars) {
                renderer.draw_path(egc, Path::segment(bar.lo.x, bar.lo.y, bar.hi.x, bar.hi.y),
                                   Affine2D(), nullptr);
            }

            /* errorbar.capsize is 0 by default, so caps are opt-in. A cap is a
               '|' (x bars) or '_' (y bars) marker of markersize 2 * capsize: it
               reaches a whole capsize either side, at capthick. Stamped through
               draw_markers as matplotlib's caplines are, which snaps each one to
               the pixel grid; x caps go down before y caps. */
            /* Limit carets: an unfilled CARET*BASE marker, base on the bar's
               end, at markersize 2 * capsize (or lines.markersize without
               caps), mitred, snapped from 3 px. */
            const double caret_px = (cap > 0.0 ? 2.0 * line.capsize : 6.0) * px_per_pt;
            GraphicsContext kgc = cgc;
            kgc.join = JoinStyle::Miter;
            kgc.snap = caret_px >= 3.0 ? GraphicsContext::Snap::On : GraphicsContext::Snap::Off;
            const auto caret = [](bool vertical, bool positive) {
                Path p;
                if (vertical && positive) { /* CARETUPBASE */
                    p.move_to(0.5, 0.0);
                    p.line_to(0.0, 0.75);
                    p.line_to(-0.5, 0.0);
                } else if (vertical) { /* CARETDOWNBASE */
                    p.move_to(-0.5, 0.0);
                    p.line_to(0.0, -0.75);
                    p.line_to(0.5, 0.0);
                } else if (positive) { /* CARETRIGHTBASE */
                    p.move_to(0.0, -0.5);
                    p.line_to(0.75, 0.0);
                    p.line_to(0.0, 0.5);
                } else { /* CARETLEFTBASE */
                    p.move_to(0.0, 0.5);
                    p.line_to(-0.75, 0.0);
                    p.line_to(0.0, -0.5);
                }
                return p;
            };
            const auto stamp_caps = [&](const Path &ends, bool vertical) {
                if (cap <= 0.0 || ends.size() == 0) {
                    return;
                }
                const Path unit = vertical ? Path::segment(-1.0, 0.0, 1.0, 0.0)
                                           : Path::segment(0.0, -1.0, 0.0, 1.0);
                renderer.draw_markers(cgc, unit, Affine2D::scale(cap, cap), ends, Affine2D(),
                                      nullptr);
            };

            /* Per axis, x first: the plain caps, then each limit kind's carets
               followed by its caps at the data point -- matplotlib's caplines. */
            for (const bool vertical : {false, true}) {
                Path ends;
                for (const Bar &bar : bars) {
                    if (bar.vertical == vertical && !bar.limited) {
                        ends.move_to(bar.lo.x, bar.lo.y);
                        ends.move_to(bar.hi.x, bar.hi.y);
                    }
                }
                stamp_caps(ends, vertical);

                const std::vector<double> &err = vertical ? line.yerr : line.xerr;
                const std::size_t m = std::min(n, err.size());
                for (const bool upper_limit : {false, true}) {
                    const std::vector<std::uint8_t> &flags =
                        vertical ? (upper_limit ? line.uplims : line.lolims)
                                 : (upper_limit ? line.xuplims : line.xlolims);
                    if (flags.empty()) {
                        continue;
                    }
                    Path tips[2]; /* by display direction: [0] negative, [1] positive */
                    Path points;
                    for (std::size_t i = 0; i < m; ++i) {
                        if (!Line2D::flag(flags, i) || !line.error_drawn(i)) {
                            continue;
                        }
                        const double xv = line.xdata[i];
                        const double yv = line.ydata[i];
                        /* lolims sits at high = dep + upper extent; uplims at
                           low = dep - lower extent. */
                        double ext;
                        if (vertical) {
                            ext = upper_limit ? -line.yerr_lo(i) : line.yerr_hi(i);
                        } else {
                            ext = upper_limit ? -line.xerr_lo(i) : line.xerr_hi(i);
                        }
                        if (!std::isfinite(xv) || !std::isfinite(yv) || !std::isfinite(ext)) {
                            continue;
                        }
                        const Point at = to_display(Point{scale_x(xv), scale_y(yv)});
                        const Point tip =
                            vertical ? to_display(Point{scale_x(xv), scale_y(yv + ext)})
                                     : to_display(Point{scale_x(xv + ext), scale_y(yv)});
                        if (!std::isfinite(at.x) || !std::isfinite(at.y) ||
                            !std::isfinite(tip.x) || !std::isfinite(tip.y)) {
                            continue;
                        }
                        /* The caret points on along the bar: towards larger
                           data values for lolims, smaller for uplims, flipped
                           on an inverted axis. */
                        const double d = vertical ? tip.y - at.y : tip.x - at.x;
                        const bool positive = d != 0.0 ? d > 0.0 : !upper_limit;
                        tips[positive ? 1 : 0].move_to(tip.x, tip.y);
                        points.move_to(at.x, at.y);
                    }
                    /* "Unfilled" in name only: the cap line keeps fillstyle
                       'full', so draw_markers fills the open V (closing it) in
                       the data line's colour, then strokes it in ecolor. */
                    for (const bool positive : {false, true}) {
                        if (tips[positive ? 1 : 0].size() != 0) {
                            renderer.draw_markers(kgc, caret(vertical, positive),
                                                  Affine2D::scale(caret_px, caret_px),
                                                  tips[positive ? 1 : 0], Affine2D(),
                                                  &line.color);
                        }
                    }
                    stamp_caps(points, vertical);
                }
            }
        };

        if (!line.barsabove) {
            draw_errorbars();
        }
        if (!line.errorbars_only) {
            draw_data();
        }
        if (line.barsabove) {
            draw_errorbars();
        }
    }
}

/* A view can run backwards (imshow inverts the y axis), so a tick-vs-limits
   comparison must sort the ends rather than assume lo < hi. */
bool within(double t, double a, double b)
{
    return t >= std::min(a, b) && t <= std::max(a, b);
}

void Axes::compute_ticks(const Bbox &box,
                         double px_per_pt,
                         std::vector<double> &xticks,
                         std::vector<double> &yticks,
                         std::vector<std::string> &xlabels,
                         std::vector<std::string> &ylabels) const
{
    const Bbox view = view_limits();
    const double dpi = px_per_pt * 72.0;

    const double x_lo = std::min(view.x0, view.x1);
    const double x_hi = std::max(view.x0, view.x1);
    const double y_lo = std::min(view.y0, view.y1);
    const double y_hi = std::max(view.y0, view.y1);

    MaxNLocator x_locator;
    MaxNLocator y_locator;
    /* get_tick_space measures against the tick label size, so smaller labels fit
       more ticks on the same axis. */
    x_locator.nbins = auto_nbins(box.width(), dpi, xtick.label_size, /*horizontal=*/true);
    y_locator.nbins =
        auto_nbins(box.height(), dpi, ytick.label_size, /*horizontal=*/false);

    /* locator_params(nbins=...) overrides what the axis's length suggested. */
    if (x_nbins > 0) {
        x_locator.nbins = x_nbins;
    }
    if (y_nbins > 0) {
        y_locator.nbins = y_nbins;
    }

    x_locator.integer = integer_x_ticks;
    y_locator.integer = integer_y_ticks;
    if (!x_tick_steps.empty()) {
        x_locator.steps = x_tick_steps;
    }
    if (!y_tick_steps.empty()) {
        y_locator.steps = y_tick_steps;
    }

    const LogLocator log_locator;

    xticks = xscale == ScaleType::Log ? log_locator.tick_values(x_lo, x_hi)
                                      : x_locator.tick_values(x_lo, x_hi);
    yticks = yscale == ScaleType::Log ? log_locator.tick_values(y_lo, y_hi)
                                      : y_locator.tick_values(y_lo, y_hi);

    /* ScalarFormatter's scaling: a common order of magnitude or offset is pulled
       out of the labels and written once at the axis end. A log axis has its own
       formatter and takes none of this. */
    xlabels = xscale == ScaleType::Log
                  ? format_log_ticks(xticks)
                  : format_ticks_scaled(xticks,
                                        tick_scaling(xticks, x_lo, x_hi, x_tick_format));
    ylabels = yscale == ScaleType::Log
                  ? format_log_ticks(yticks)
                  : format_ticks_scaled(yticks,
                                        tick_scaling(yticks, y_lo, y_hi, y_tick_format));

    /* A fixed list replaces the locator, fixed labels the formatter. With no
       labels given, they are formatted from the fixed positions, so set_xticks
       alone still reads sensibly. */
    if (xticks_fixed_set) {
        xticks = xticks_fixed;
        xlabels = xticklabels_fixed.empty() ? format_ticks(xticks) : xticklabels_fixed;
        xlabels.resize(xticks.size());
    }
    if (yticks_fixed_set) {
        yticks = yticks_fixed;
        ylabels = yticklabels_fixed.empty() ? format_ticks(yticks) : yticklabels_fixed;
        ylabels.resize(yticks.size());
    }
}

void Axes::tick_offset_texts(const Bbox &box,
                             double px_per_pt,
                             std::string &x_offset,
                             std::string &y_offset) const
{
    x_offset.clear();
    y_offset.clear();

    std::vector<double> xticks;
    std::vector<double> yticks;
    std::vector<std::string> xlabels;
    std::vector<std::string> ylabels;
    compute_ticks(box, px_per_pt, xticks, yticks, xlabels, ylabels);

    const Bbox view = view_limits();
    const double x_lo = std::min(view.x0, view.x1);
    const double x_hi = std::max(view.x0, view.x1);
    const double y_lo = std::min(view.y0, view.y1);
    const double y_hi = std::max(view.y0, view.y1);

    /* Fixed labels are the caller's own strings, so no scale factor is written. */
    if (xscale != ScaleType::Log && !(xticks_fixed_set && !xticklabels_fixed.empty())) {
        x_offset = tick_offset_text(tick_scaling(xticks, x_lo, x_hi, x_tick_format));
    }
    if (yscale != ScaleType::Log && !(yticks_fixed_set && !yticklabels_fixed.empty())) {
        y_offset = tick_offset_text(tick_scaling(yticks, y_lo, y_hi, y_tick_format));
    }
}

void Axes::set_grid(bool visible, GridWhich which, GridAxis axis)
{
    const bool touch_x = axis != GridAxis::Y;
    const bool touch_y = axis != GridAxis::X;
    const bool touch_major = which != GridWhich::Minor;
    const bool touch_minor = which != GridWhich::Major;
    if (touch_x && touch_major) {
        xgrid_major = visible;
    }
    if (touch_x && touch_minor) {
        xgrid_minor = visible;
    }
    if (touch_y && touch_major) {
        ygrid_major = visible;
    }
    if (touch_y && touch_minor) {
        ygrid_minor = visible;
    }
}

void Axes::draw_grid_and_ticks(Renderer &renderer, const Bbox &box, double px_per_pt)
{
    /* Axis off takes the grid and ticks with it: in matplotlib the grid belongs
       to the axis. */
    if (!axis_on) {
        return;
    }

    const Bbox view = view_limits();
    const Affine2D to_display = scale_to_display(box);

    std::vector<double> xticks;
    std::vector<double> yticks;
    std::vector<std::string> xlabels;
    std::vector<std::string> ylabels;
    compute_ticks(box, px_per_pt, xticks, yticks, xlabels, ylabels);

    /* Minor tick positions, for both the minor gridlines and the minor tick marks.
       A log axis always has them (LogScale installs a LogLocator as the minor
       locator); {x,y}tick.minor.visible governs only AutoMinorLocator on a linear
       axis. */
    const std::vector<double> x_minors =
        xscale == ScaleType::Log ? log_minor_ticks(view.x0, view.x1, 10.0)
        : minor_x_visible        ? auto_minor_ticks(xticks, view.x0, view.x1)
                                 : std::vector<double>{};
    const std::vector<double> y_minors =
        yscale == ScaleType::Log ? log_minor_ticks(view.y0, view.y1, 10.0)
        : minor_y_visible        ? auto_minor_ticks(yticks, view.y0, view.y1)
                                 : std::vector<double>{};

    /* Grid first: it belongs under the data. Each of the four sets (x/y major,
       x/y minor) is drawn only if its own flag is on. */
    if (xgrid_major || ygrid_major || xgrid_minor || ygrid_minor) {
        GraphicsContext ggc;
        ggc.color = grid_color;
        ggc.alpha = grid_alpha;
        ggc.linewidth = grid_linewidth;
        ggc.has_cliprect = true;
        ggc.cliprect = box;
        for (const auto &[on, off] : dash_pattern(grid_linestyle, grid_linewidth)) {
            ggc.dashes.emplace_back(on, off);
        }

        auto vlines = [&](const std::vector<double> &ts) {
            for (double t : ts) {
                if (!within(t, view.x0, view.x1)) {
                    continue;
                }
                const Point p = to_display(Point{scale_x(t), scale_y(view.y0)});
                renderer.draw_path(ggc, Path::segment(p.x, box.y0, p.x, box.y1), Affine2D(),
                                   nullptr);
            }
        };
        auto hlines = [&](const std::vector<double> &ts) {
            for (double t : ts) {
                if (!within(t, view.y0, view.y1)) {
                    continue;
                }
                const Point p = to_display(Point{scale_x(view.x0), scale_y(t)});
                renderer.draw_path(ggc, Path::segment(box.x0, p.y, box.x1, p.y), Affine2D(),
                                   nullptr);
            }
        };
        if (xgrid_major) {
            vlines(xticks);
        }
        if (xgrid_minor) {
            vlines(x_minors);
        }
        if (ygrid_major) {
            hlines(yticks);
        }
        if (ygrid_minor) {
            hlines(y_minors);
        }
    }

    /* Separate contexts per axis, since each carries its own tick width and its
       own label colour (tick_params colors= and labelcolor= are independent
       rcParams, not shared with each other or with the spine's edgecolor). */
    GraphicsContext x_tick_gc;
    x_tick_gc.color = xtick.color;
    x_tick_gc.linewidth = xtick.width;
    x_tick_gc.cap = CapStyle::Butt;
    GraphicsContext x_label_gc = x_tick_gc;
    x_label_gc.color = xtick.label_color;

    GraphicsContext y_tick_gc;
    y_tick_gc.color = ytick.color;
    y_tick_gc.linewidth = ytick.width;
    y_tick_gc.cap = CapStyle::Butt;
    GraphicsContext y_label_gc = y_tick_gc;
    y_label_gc.color = ytick.label_color;

    /* Tick direction sets how much falls on each side of the spine: 'out' all
       outside, 'in' all inside, 'inout' half each way. */
    const double x_sign = x_ticks_top ? 1.0 : -1.0; /* which way is outward */
    const double y_sign = y_ticks_right ? 1.0 : -1.0;
    const double x_edge = x_ticks_top ? box.y1 : box.y0;
    const double y_edge = y_ticks_right ? box.x1 : box.x0;

    if (!x_minors.empty() || !y_minors.empty()) {
        const double minor_px = minor_tick_size * px_per_pt;

        GraphicsContext x_mgc;
        x_mgc.color = xtick.color;
        x_mgc.linewidth = minor_tick_width;
        x_mgc.cap = CapStyle::Butt;
        GraphicsContext y_mgc = x_mgc;
        y_mgc.color = ytick.color;

        if (xtick.ticks_visible) {
            const double out = x_sign * xtick.outward(minor_px);
            const double in = -x_sign * xtick.inward(minor_px);
            for (double t : x_minors) {
                const Point p = to_display(Point{scale_x(t), scale_y(view.y0)});
                renderer.draw_path(x_mgc,
                                   Path::segment(p.x, x_edge + in, p.x, x_edge + out),
                                   Affine2D(), nullptr);
            }
        }

        if (ytick.ticks_visible) {
            const double out = y_sign * ytick.outward(minor_px);
            const double in = -y_sign * ytick.inward(minor_px);
            for (double t : y_minors) {
                const Point p = to_display(Point{scale_x(view.x0), scale_y(t)});
                renderer.draw_path(y_mgc,
                                   Path::segment(y_edge + in, p.y, y_edge + out, p.y),
                                   Affine2D(), nullptr);
            }
        }
    }

    FontProps x_tick_font;
    x_tick_font.size = xtick.label_size;
    FontProps y_tick_font;
    y_tick_font.size = ytick.label_size;

    /* On the top for a twiny axes, where the labels hang above. The label pad is
       measured from the spine plus the tick's outward reach, so a tick turned
       inwards brings its label in with it (Tick.get_tick_padding). */
    if (x_axis_visible) {
        const double len = xtick.size * px_per_pt;
        const double out = x_sign * xtick.outward(len);
        const double in = -x_sign * xtick.inward(len);
        const double label_at =
            x_edge + x_sign * (xtick.outward(len) + xtick.pad * px_per_pt);

        for (std::size_t i = 0; i < xticks.size(); ++i) {
            const double t = xticks[i];
            if (!within(t, view.x0, view.x1)) {
                continue;
            }
            const Point p = to_display(Point{scale_x(t), scale_y(view.y0)});
            if (xtick.ticks_visible) {
                renderer.draw_path(x_tick_gc,
                                   Path::segment(p.x, x_edge + in, p.x, x_edge + out),
                                   Affine2D(), nullptr);
                if (x_ticks_both) {
                    /* The far edge gets the mark but not the label, pointing the
                       other way since "outward" is mirrored there. */
                    const double far_edge = x_ticks_top ? box.y0 : box.y1;
                    renderer.draw_path(
                        x_tick_gc,
                        Path::segment(p.x, far_edge - in, p.x, far_edge - out),
                        Affine2D(), nullptr);
                }
            }
            if (xtick.labels_visible) {
                renderer.draw_text(x_label_gc, p.x, label_at, xlabels[i], x_tick_font, 0.0,
                                   HAlign::Center,
                                   x_ticks_top ? VAlign::Bottom : VAlign::Top);
            }
        }
    }

    /* On the right for a twinned axes or a colorbar: ticks point the other way
       and labels are left-aligned against them. */
    if (y_axis_visible) {
        const double len = ytick.size * px_per_pt;
        const double out = y_sign * ytick.outward(len);
        const double in = -y_sign * ytick.inward(len);
        const double label_at =
            y_edge + y_sign * (ytick.outward(len) + ytick.pad * px_per_pt);

        for (std::size_t i = 0; i < yticks.size(); ++i) {
            const double t = yticks[i];
            if (!within(t, view.y0, view.y1)) {
                continue;
            }
            const Point p = to_display(Point{scale_x(view.x0), scale_y(t)});
            if (ytick.ticks_visible) {
                renderer.draw_path(y_tick_gc,
                                   Path::segment(y_edge + in, p.y, y_edge + out, p.y),
                                   Affine2D(), nullptr);
            }
            if (ytick.labels_visible) {
                /* ytick.alignment is center_baseline, not center. */
                renderer.draw_text(y_label_gc, label_at, p.y, ylabels[i], y_tick_font, 0.0,
                                   y_ticks_right ? HAlign::Left : HAlign::Right,
                                   VAlign::CenterBaseline);
            }
        }
    }

    /* The scale factor, written once at the axis end. Axis.OFFSETTEXTPAD is 3
       points; the y factor sits above the frame at the left corner on its
       baseline, the x factor below the tick labels at the right corner. */
    std::string x_offset;
    std::string y_offset;
    tick_offset_texts(box, px_per_pt, x_offset, y_offset);

    const double offset_pad = 3.0 * px_per_pt;

    if (!y_offset.empty() && ytick.labels_visible) {
        renderer.draw_text(y_label_gc, box.x0, box.y1 + offset_pad, y_offset, y_tick_font,
                           0.0, HAlign::Left, VAlign::Baseline);
    }
    if (!x_offset.empty() && xtick.labels_visible) {
        /* Below where the tick labels reach, not below the frame, to clear them. */
        const LineMetrics line = renderer.text_line_metrics("0", x_tick_font);
        const double labels_bottom =
            box.y0 - (xtick.outward(xtick.size) + xtick.pad) * px_per_pt -
            (line.ascent + line.descent);
        renderer.draw_text(x_label_gc, box.x1, labels_bottom - offset_pad, x_offset,
                           x_tick_font, 0.0, HAlign::Right, VAlign::Top);
    }
}

namespace
{

/* BoxStyle.Round with pad=0, as legend.fancybox draws. Corners are quadratic
   Beziers of radius `dr`, taken from the box's mutation scale (the legend's font
   size in points, used directly as pixels). */
Path rounded_rect(const Bbox &b, double dr)
{
    Path p;
    p.move_to(b.x0 + dr, b.y0);
    p.line_to(b.x1 - dr, b.y0);
    p.curve3(b.x1, b.y0, b.x1, b.y0 + dr);
    p.line_to(b.x1, b.y1 - dr);
    p.curve3(b.x1, b.y1, b.x1 - dr, b.y1);
    p.line_to(b.x0 + dr, b.y1);
    p.curve3(b.x0, b.y1, b.x0, b.y1 - dr);
    p.line_to(b.x0, b.y0 + dr);
    p.curve3(b.x0, b.y0, b.x0 + dr, b.y0);
    p.close_poly();
    return p;
}

} // namespace

void Axes::draw_legend(Renderer &renderer, const Bbox &box, double px_per_pt)
{
    /* A legend row is a line sample, marker, or patch, so entries carry whichever
       artist they came from. */
    struct Entry
    {
        const Line2D *line = nullptr;
        const ScatterCollection *scatter = nullptr;
        const PolyPatch *patch = nullptr;
        const std::string *label = nullptr;
    };

    std::vector<Entry> entries;
    for (const std::unique_ptr<Line2D> &line : lines) {
        if (!line->label.empty()) {
            entries.push_back(Entry{line.get(), nullptr, nullptr, &line->label});
        }
    }
    for (const std::unique_ptr<ScatterCollection> &sc : collections) {
        if (!sc->label.empty()) {
            entries.push_back(Entry{nullptr, sc.get(), nullptr, &sc->label});
        }
    }
    for (const std::unique_ptr<PolyPatch> &patch : patches) {
        if (!patch->label.empty()) {
            entries.push_back(Entry{nullptr, nullptr, patch.get(), &patch->label});
        }
    }
    if (entries.empty()) {
        return;
    }

    /* legend.* metrics are multiples of the font size. */
    const double fs = legend_size * px_per_pt;
    const double borderpad = 0.4 * fs;
    const double labelspacing = 0.5 * fs;
    const double handlelength = 2.0 * fs;
    const double handletextpad = 0.8 * fs;
    const double borderaxespad = 0.5 * fs;

    FontProps font;
    font.size = legend_size;

    /* A row packs like matplotlib's offsetbox: handle and label share a baseline,
       and the row is as tall as the tallest ascent plus deepest descent. The
       handle box is legend.handleheight font sizes tall on that baseline. */
    const double handleheight = 0.7 * fs;

    /* TextArea.get_bbox never lets a label stand shorter than "lp" does:
       its ascent is at least that probe's, so a row of capitals ("A", "B")
       is as tall as a row of "sin" and "cos". */
    const double lp_ascent = renderer.text_line_metrics("lp", font).ascent;
    double text_ascent = lp_ascent;
    double text_descent = 0.0;
    std::vector<double> entry_text_width(entries.size());
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const TextExtent e = renderer.measure_text(*entries[i].label, font);
        const LineMetrics m = renderer.text_line_metrics(*entries[i].label, font);
        entry_text_width[i] = e.width;
        text_ascent = std::max(text_ascent, m.ascent);
        text_descent = std::max(text_descent, m.descent);
    }

    const double row_ascent = std::max(handleheight, text_ascent);
    const double row_height = row_ascent + text_descent;

    /* legend(ncols=): np.array_split's rule -- the first (count % ncols) columns
       get one extra entry, the rest get count / ncols. Each column's width is its
       own widest label; row height stays the single global value above. */
    const int ncols = std::max(1, std::min(legend_ncols, static_cast<int>(entries.size())));
    const std::size_t col_base = entries.size() / static_cast<std::size_t>(ncols);
    const std::size_t col_extra = entries.size() % static_cast<std::size_t>(ncols);
    std::vector<std::size_t> col_start(static_cast<std::size_t>(ncols) + 1, 0);
    for (int c = 0; c < ncols; ++c) {
        const std::size_t count =
            col_base + (static_cast<std::size_t>(c) < col_extra ? 1 : 0);
        col_start[static_cast<std::size_t>(c) + 1] = col_start[static_cast<std::size_t>(c)] + count;
    }
    std::vector<double> col_entries_w(static_cast<std::size_t>(ncols), 0.0);
    std::vector<std::size_t> col_rows(static_cast<std::size_t>(ncols), 0);
    for (int c = 0; c < ncols; ++c) {
        double widest = 0.0;
        for (std::size_t i = col_start[static_cast<std::size_t>(c)];
             i < col_start[static_cast<std::size_t>(c) + 1]; ++i) {
            widest = std::max(widest, entry_text_width[i]);
        }
        col_entries_w[static_cast<std::size_t>(c)] = handlelength + handletextpad + widest;
        col_rows[static_cast<std::size_t>(c)] =
            col_start[static_cast<std::size_t>(c) + 1] - col_start[static_cast<std::size_t>(c)];
    }
    const double columnspacing = 2.0 * fs; /* legend.columnspacing, default 2.0 */

    /* set_title: a heading row above the entries in its own VPacker cell.
       matplotlib's _legend_box is [title, handle_box] stacked with the same
       labelspacing gap, centred so the narrower of the two sits in the middle. */
    const bool has_title = !legend_title.empty();
    double title_width = 0.0;
    double title_ascent = 0.0;
    double title_descent = 0.0;
    if (has_title) {
        const TextExtent e = renderer.measure_text(legend_title, font);
        const LineMetrics m = renderer.text_line_metrics(legend_title, font);
        title_width = e.width;
        title_ascent = std::max(m.ascent, lp_ascent);
        title_descent = m.descent;
    }
    const double title_height = has_title ? title_ascent + title_descent : 0.0;
    const double title_gap = has_title ? labelspacing : 0.0;

    double entries_total_w = 0.0;
    for (int c = 0; c < ncols; ++c) {
        entries_total_w += col_entries_w[static_cast<std::size_t>(c)];
    }
    entries_total_w += static_cast<double>(ncols - 1) * columnspacing;

    std::size_t tallest_col = 0;
    for (int c = 0; c < ncols; ++c) {
        tallest_col = std::max(tallest_col, col_rows[static_cast<std::size_t>(c)]);
    }

    const double content_w = std::max(entries_total_w, title_width);
    const double content_h = title_height + title_gap +
                             static_cast<double>(tallest_col) * row_height +
                             static_cast<double>(tallest_col - 1) * labelspacing;

    const double w = content_w + 2.0 * borderpad;
    const double h = content_h + 2.0 * borderpad;

    /* Candidate corners, in matplotlib's preference order for ties. */
    /*
     * offsetbox._get_anchored_bbox: shrink the axes box inwards by
     * borderaxespad, then anchor the legend to one of its compass points. One
     * rule covers all ten positions, which is why matplotlib has ten and not
     * four.
     */
    auto corner_frame = [&](LegendLoc loc) {
        const Bbox container(box.x0 + borderaxespad, box.y0 + borderaxespad,
                             box.x1 - borderaxespad, box.y1 - borderaxespad);

        /* -1 left/bottom, 0 centred, +1 right/top. */
        int ax = 1;
        int ay = 1;
        switch (loc) {
            case LegendLoc::UpperRight: ax = 1; ay = 1; break;
            case LegendLoc::UpperLeft: ax = -1; ay = 1; break;
            case LegendLoc::LowerLeft: ax = -1; ay = -1; break;
            case LegendLoc::LowerRight: ax = 1; ay = -1; break;
            case LegendLoc::Right: ax = 1; ay = 0; break;
            case LegendLoc::CenterLeft: ax = -1; ay = 0; break;
            case LegendLoc::CenterRight: ax = 1; ay = 0; break;
            case LegendLoc::LowerCenter: ax = 0; ay = -1; break;
            case LegendLoc::UpperCenter: ax = 0; ay = 1; break;
            case LegendLoc::Center: ax = 0; ay = 0; break;
            case LegendLoc::Best:
            default: ax = 1; ay = 1; break;
        }

        const double x0 = ax < 0   ? container.x0
                          : ax > 0 ? container.x1 - w
                                   : container.x0 + (container.width() - w) / 2.0;
        const double y0 = ay < 0   ? container.y0
                          : ay > 0 ? container.y1 - h
                                   : container.y0 + (container.height() - h) / 2.0;
        return Bbox(x0, y0, x0 + w, y0 + h);
    };

    LegendLoc chosen = legend_loc;
    if (chosen == LegendLoc::Best) {
        /* "Best" means least occluded. matplotlib scores by overlapped geometry;
           counting data points inside each candidate is a cheaper stand-in that
           picks the same corner in ordinary cases. */
        const Affine2D to_display = scale_to_display(box);

        auto occlusion = [&](const Bbox &frame) {
            std::size_t hits = 0;
            auto count = [&](double dx, double dy) {
                const Point p = to_display(Point{scale_x(dx), scale_y(dy)});
                if (p.x >= frame.x0 && p.x <= frame.x1 && p.y >= frame.y0 &&
                    p.y <= frame.y1) {
                    ++hits;
                }
            };
            for (const std::unique_ptr<Line2D> &line : lines) {
                const std::size_t n = std::min(line->xdata.size(), line->ydata.size());
                for (std::size_t i = 0; i < n; ++i) {
                    count(line->xdata[i], line->ydata[i]);
                }
            }
            for (const std::unique_ptr<ScatterCollection> &sc : collections) {
                const std::size_t n = std::min(sc->xdata.size(), sc->ydata.size());
                for (std::size_t i = 0; i < n; ++i) {
                    count(sc->xdata[i], sc->ydata[i]);
                }
            }
            /* A patch fills its whole area, so a candidate overlapping one is
               heavily penalised via its vertex count. */
            for (const std::unique_ptr<PolyPatch> &patch : patches) {
                const std::size_t n = patch->path.size();
                for (std::size_t i = 0; i < n; ++i) {
                    count(patch->path.vertices[2 * i], patch->path.vertices[2 * i + 1]);
                }
            }
            return hits;
        };

        /* matplotlib scores every position, not just the corners. */
        const LegendLoc candidates[] = {
            LegendLoc::UpperRight,  LegendLoc::UpperLeft,   LegendLoc::LowerLeft,
            LegendLoc::LowerRight,  LegendLoc::Right,       LegendLoc::CenterLeft,
            LegendLoc::CenterRight, LegendLoc::LowerCenter, LegendLoc::UpperCenter,
            LegendLoc::Center};
        std::size_t best_score = static_cast<std::size_t>(-1);
        chosen = LegendLoc::UpperRight;
        for (LegendLoc candidate : candidates) {
            const std::size_t score = occlusion(corner_frame(candidate));
            if (score < best_score) {
                best_score = score;
                chosen = candidate;
            }
        }
    }

    const Bbox frame = corner_frame(chosen);

    /* legend.framealpha 0.8 over legend.facecolor (inherited from the axes),
       with a light legend.edgecolor of 0.8 grey. */
    GraphicsContext frame_gc;
    frame_gc.color = Color{0.8, 0.8, 0.8, 1.0};
    frame_gc.linewidth = 0.8;
    frame_gc.join = JoinStyle::Miter;
    Color frame_face = facecolor;
    frame_face.a = 0.8;
    /* legend.fancybox is on by default: rounding_size 0.2 of the mutation
       scale, which the legend sets to its font size. */
    renderer.draw_path(frame_gc, rounded_rect(frame, 0.2 * legend_size), Affine2D(),
                       &frame_face);

    if (has_title) {
        GraphicsContext title_gc;
        title_gc.color = Color::rgb(0.0, 0.0, 0.0);
        const double baseline = frame.y1 - borderpad - title_ascent;
        renderer.draw_text(title_gc, (frame.x0 + frame.x1) / 2.0, baseline, legend_title,
                           font, 0.0, HAlign::Center, VAlign::Baseline);
    }

    /* align="center": when the title is wider, the entries block centres in the
       space the title claimed rather than staying flush left. */
    double col_x = frame.x0 + borderpad + (content_w - entries_total_w) / 2.0;
    for (int c = 0; c < ncols; ++c) {
        double row_top = frame.y1 - borderpad - title_height - title_gap;
        for (std::size_t idx = col_start[static_cast<std::size_t>(c)];
             idx < col_start[static_cast<std::size_t>(c) + 1]; ++idx) {
        const Entry &entry = entries[idx];
        const double baseline = row_top - row_ascent;
        /* The handle box sits on the baseline; the sample is drawn across its
           middle (HandlerLine2D puts the line at height/2). */
        const double cy = baseline + handleheight / 2.0;
        const double hx0 = col_x;
        const double hx1 = hx0 + handlelength;

        /* legend.numpoints is 1: one marker, centred on the handle. */
        auto draw_marker = [&](MarkerStyle marker, double diameter_pt, const Color &face,
                               const Color &edge, double edge_width) {
            if (marker == MarkerStyle::None) {
                return;
            }
            const Path unit = marker_path(marker);
            const double half = 0.5 * diameter_pt * px_per_pt;
            GraphicsContext mgc;
            mgc.color = edge;
            mgc.linewidth = edge_width;
            mgc.cap = CapStyle::Butt;
            mgc.join = marker_join(marker);
            mgc.snap = diameter_pt * px_per_pt >= marker_snap_threshold(marker)
                           ? GraphicsContext::Snap::On
                           : GraphicsContext::Snap::Off;
            /* Through draw_markers, like the data: the legend's handle is a Line2D
               too, so its marker is stamped at a whole pixel the same way. */
            Path at;
            at.move_to((hx0 + hx1) / 2.0, cy);
            renderer.draw_markers(mgc, unit, Affine2D::scale(half, half), at, Affine2D(),
                                  marker_is_filled(marker) ? &face : nullptr);
        };

        if (entry.line != nullptr) {
            const Line2D *line = entry.line;
            if (line->linestyle != LineStyle::None) {
                GraphicsContext gc;
                gc.color = line->color;
                gc.linewidth = line->linewidth;
                /* Same rule as draw_lines: the handle sample copies the real
                   line's style, capstyle included. */
                gc.cap = line->linestyle == LineStyle::Solid ? CapStyle::Projecting
                                                             : CapStyle::Butt;
                for (const auto &[on, off] : line->dashes()) {
                    gc.dashes.emplace_back(on, off);
                }
                renderer.draw_path(gc, Path::segment(hx0, cy, hx1, cy), Affine2D(), nullptr);
            }
            draw_marker(line->marker, line->markersize,
                        line->has_markerfacecolor ? line->markerfacecolor : line->color,
                        line->has_markeredgecolor ? line->markeredgecolor : line->color,
                        line->markeredgewidth);
        } else if (entry.scatter != nullptr) {
            const ScatterCollection *sc = entry.scatter;
            /* Per-point sizes have no single legend answer; the representative
               marker uses the collection's base size. */
            draw_marker(sc->marker, std::sqrt(std::max(0.0, sc->size)), sc->color,
                        sc->has_edgecolor ? sc->edgecolor : sc->color, sc->linewidth);
        } else if (entry.patch != nullptr) {
            /* A patch shows as a filled swatch filling the handle box, as
               HandlerPatch's Rectangle does: handlelength by handleheight. */
            const PolyPatch *patch = entry.patch;
            GraphicsContext gc;
            gc.alpha = patch->alpha;
            gc.color = patch->linewidth > 0.0 ? patch->edgecolor : Color::none();
            gc.linewidth = patch->linewidth;
            gc.join = JoinStyle::Miter;
            const Bbox swatch(hx0, cy - handleheight / 2.0, hx1, cy + handleheight / 2.0);
            renderer.draw_path(gc, Path::rectangle(swatch), Affine2D(), &patch->facecolor);
        }

        GraphicsContext text_gc;
        text_gc.color = Color::rgb(0.0, 0.0, 0.0);
        renderer.draw_text(text_gc, hx1 + handletextpad, baseline, *entry.label, font, 0.0,
                           HAlign::Left, VAlign::Baseline);

        row_top -= row_height + labelspacing;
        }
        col_x += col_entries_w[static_cast<std::size_t>(c)] + columnspacing;
    }
}

void Axes::draw(Renderer &renderer)
{
    const Bbox box = display_box(renderer.width(), renderer.height());
    const double px_per_pt = renderer.dpi() / 72.0;

    /* Axes background. A twinned axes has none: it would paint over the data of
       the axes it sits on top of. */
    if (patch_visible && facecolor.visible()) {
        GraphicsContext gc;
        gc.color = Color::none();
        gc.linewidth = 0.0;
        gc.antialiased = false;
        renderer.draw_path(gc, Path::rectangle(box), Affine2D(), &facecolor);
    }

    /* A table goes down first, under everything: Table takes Artist's default
       zorder 0 (below the grid). Its default position tucks it under the bottom
       spine, so drawing it last would let its cell fills paint over the tick
       marks and label descenders. */
    draw_tables(renderer, box, px_per_pt);

    /* Draw order follows matplotlib's z-order. Grid/ticks move: axisbelow places
       them at zorder 0.5, 1.5 or 2.5, deciding which calls below they sit between.
       Images are always first (AxesImage.zorder 0, below axisbelow=On's 0.5). */
    draw_images(renderer, box);

    if (axisbelow == AxisBelow::On) {
        draw_grid_and_ticks(renderer, box, px_per_pt);
    }

    /* The patch/collection tier: Patch.zorder and Collection's base zorder are
       both 1, so contours, spans, bars/fills and scatter-like collections sit
       together on one side of the 1.5 boundary. */
    draw_contours(renderer, box, px_per_pt, /*line_tier=*/false);
    draw_spans(renderer, box);
    draw_patches(renderer, box, px_per_pt);
    draw_collections(renderer, box, px_per_pt);

    if (axisbelow == AxisBelow::Line) {
        /* The default: above bars and fills, below the data lines drawn next
           (grid crosses the bars but not the curve). */
        draw_grid_and_ticks(renderer, box, px_per_pt);
    }

    /* The line tier: Line2D and LineCollection are both zorder 2. */
    draw_reference_lines(renderer, box, px_per_pt);
    draw_contours(renderer, box, px_per_pt, /*line_tier=*/true);
    draw_segments(renderer, box);
    draw_patches(renderer, box, px_per_pt, /*line_tier=*/true);
    draw_lines(renderer, box);
    draw_quivers(renderer, box, px_per_pt);

    if (axisbelow == AxisBelow::Off) {
        draw_grid_and_ticks(renderer, box, px_per_pt);
    }

    /* Inset connectors, drawn here rather than in draw_patches: one end is in data
       coordinates and the other in figure fractions, so they need the figure's
       pixel size, not just this axes' box. */
    if (!inset_connectors.empty()) {
        const Affine2D to_display = scale_to_display(box);
        for (const InsetConnector &c : inset_connectors) {
            const Point a = to_display(Point{scale_x(c.data_x), scale_y(c.data_y)});
            const double bx = c.inset_x * static_cast<double>(renderer.width());
            const double by = c.inset_y * static_cast<double>(renderer.height());
            if (!std::isfinite(a.x) || !std::isfinite(a.y)) {
                continue;
            }
            GraphicsContext gc;
            gc.color = Color::rgb(0.5, 0.5, 0.5);
            gc.alpha = 0.5;
            gc.linewidth = 1.0;
            renderer.draw_path(gc, Path::segment(a.x, a.y, bx, by), Affine2D(), nullptr);
        }
    }

    /* Annotations sit above the data they point at. */
    draw_texts(renderer, box, px_per_pt);

    /* Axis off gates the frame, axis labels, and title (the data and background
       above are already drawn). The legend is outside this gate: it is a separate
       child artist that set_axis_off does not touch. */
    if (axis_on) {

    /* Frame drawn after the data so the spines sit on top, as four separate
       spines so any can be turned off on its own. */
    GraphicsContext spine_gc;
    spine_gc.color = edgecolor;
    spine_gc.linewidth = linewidth;
    spine_gc.join = JoinStyle::Miter;

    if (spine_left && spine_right && spine_top && spine_bottom) {
        /* All four: one closed path, so the corners join cleanly. */
        renderer.draw_path(spine_gc, Path::rectangle(box), Affine2D(), nullptr);
    } else {
        if (spine_bottom) {
            renderer.draw_path(spine_gc, Path::segment(box.x0, box.y0, box.x1, box.y0),
                               Affine2D(), nullptr);
        }
        if (spine_top) {
            renderer.draw_path(spine_gc, Path::segment(box.x0, box.y1, box.x1, box.y1),
                               Affine2D(), nullptr);
        }
        if (spine_left) {
            renderer.draw_path(spine_gc, Path::segment(box.x0, box.y0, box.x0, box.y1),
                               Affine2D(), nullptr);
        }
        if (spine_right) {
            renderer.draw_path(spine_gc, Path::segment(box.x1, box.y0, box.x1, box.y1),
                               Affine2D(), nullptr);
        }
    }

    /* Axis labels and title. */
    GraphicsContext text_gc;
    text_gc.color = Color::rgb(0.0, 0.0, 0.0);

    FontProps label_font;
    label_font.size = axis_label_size;

    /* Axis labels sit labelpad beyond where the tick labels stop, not at a fixed
       font-size multiple, so wide numbers push the y label further out
       ({X,Y}Axis._update_label_position). */
    const double labelpad_px = label_pad * px_per_pt;
    const TickReach reach =
        (!xlabel.empty() || !ylabel.empty()) ? tick_reach(renderer, px_per_pt) : TickReach{};

    /* Where this axes' tick columns/rows end, or the group's furthest tick edge
       when align_{x,y}labels has joined it to siblings, so a column of panels with
       differing number widths still lines its labels up. */
    double y_edge_left = box.x0 - reach.left;
    double y_edge_right = box.x1 + reach.left;
    for (const Axes *s : ylabel_align_group) {
        const Bbox sb = s->display_box(renderer.width(), renderer.height());
        const double sl = s->tick_reach(renderer, px_per_pt).left;
        y_edge_left = std::min(y_edge_left, sb.x0 - sl);
        y_edge_right = std::max(y_edge_right, sb.x1 + sl);
    }
    double x_edge_below = box.y0 - reach.below;
    double x_edge_above = box.y1 + reach.below;
    for (const Axes *s : xlabel_align_group) {
        const Bbox sb = s->display_box(renderer.width(), renderer.height());
        const double sbelow = s->tick_reach(renderer, px_per_pt).below;
        x_edge_below = std::min(x_edge_below, sb.y0 - sbelow);
        x_edge_above = std::max(x_edge_above, sb.y1 + sbelow);
    }

    if (!xlabel.empty()) {
        if (x_ticks_top) {
            renderer.draw_text(text_gc, (box.x0 + box.x1) / 2.0,
                               x_edge_above + labelpad_px, xlabel, label_font,
                               0.0, HAlign::Center, VAlign::Bottom);
        } else {
            renderer.draw_text(text_gc, (box.x0 + box.x1) / 2.0,
                               x_edge_below - labelpad_px, xlabel, label_font,
                               0.0, HAlign::Center, VAlign::Top);
        }
    }
    if (!ylabel.empty()) {
        /* rotation_mode='anchor' (matplotlib sets this on the y axis label):
           alignment happens in the text's own frame with the offset rotated with
           it. On the right the rotation flips so the label is not upside down. */
        if (y_ticks_right) {
            renderer.draw_text(text_gc, y_edge_right + labelpad_px,
                               (box.y0 + box.y1) / 2.0, ylabel, label_font, 270.0,
                               HAlign::Center, VAlign::Bottom, RotationMode::Anchor);
        } else {
            renderer.draw_text(text_gc, y_edge_left - labelpad_px,
                               (box.y0 + box.y1) / 2.0, ylabel, label_font, 90.0,
                               HAlign::Center, VAlign::Bottom, RotationMode::Anchor);
        }
    }

    } /* if (axis_on) -- frame and axis labels */

    /* The title is outside the axis_on gate: it is a child Text of the axes, not
       part of the axis, and set_axis_off leaves it visible. */
    if (!title.empty()) {
        GraphicsContext title_gc;
        title_gc.color = Color::rgb(0.0, 0.0, 0.0);
        /* axes.titlepad above the frame, on the baseline (va='baseline'), so a
           title with a descender does not sit higher than one without. */
        FontProps title_font;
        title_font.size = title_size;
        /* align_titles lifts every title in a row to the group's topmost frame,
           so titles over panels of differing height share one baseline. */
        double title_top = box.y1;
        for (const Axes *s : title_align_group) {
            title_top = std::max(title_top, s->display_box(renderer.width(), renderer.height()).y1);
        }
        renderer.draw_text(title_gc, (box.x0 + box.x1) / 2.0, title_top + title_pad * px_per_pt,
                           title, title_font, 0.0, HAlign::Center, VAlign::Baseline);
    }

    if (legend_visible) {
        draw_legend(renderer, box, px_per_pt);
    }
}

unsigned Figure::pixel_width() const
{
    return static_cast<unsigned>(std::lround(width_inch * dpi));
}

unsigned Figure::pixel_height() const
{
    return static_cast<unsigned>(std::lround(height_inch * dpi));
}

namespace
{

/* Where cell `index` of an nrows x ncols grid lands inside `grid`. Pulled out
   so add_subplot and subplots_adjust cannot disagree about it. */
Bbox grid_cell(const Bbox &grid, double wspace, double hspace,
               unsigned nrows, unsigned ncols, unsigned index)
{
    const unsigned cell = index - 1;
    const unsigned row = cell / ncols;
    const unsigned col = cell % ncols;

    const double cw = grid.width() / (ncols + wspace * (ncols - 1));
    const double ch = grid.height() / (nrows + hspace * (nrows - 1));
    const double sep_w = wspace * cw;
    const double sep_h = hspace * ch;

    /* Index 1 is the top-left cell, so rows count down from the top. */
    const double x0 = grid.x0 + col * (cw + sep_w);
    const double y1 = grid.y1 - row * (ch + sep_h);
    return Bbox(x0, y1 - ch, x0 + cw, y1);
}

} // namespace

void Figure::subplots_adjust()
{
    const Bbox grid{subplot_params.left, subplot_params.bottom, subplot_params.right,
                    subplot_params.top};
    for (const std::unique_ptr<Axes> &ax : axes) {
        if (ax->grid_rows == 0 || ax->grid_cols == 0) {
            continue; /* placed by hand: not ours to move */
        }
        ax->position = grid_cell(grid, subplot_params.wspace, subplot_params.hspace,
                                 ax->grid_rows, ax->grid_cols, ax->grid_index);
    }
}

bool Figure::remove_axes(const Axes *ax)
{
    for (std::size_t i = 0; i < axes.size(); ++i) {
        if (axes[i].get() != ax) {
            continue;
        }
        axes.erase(axes.begin() + static_cast<std::ptrdiff_t>(i));
        /* Keep the current index inside the list, and pointing at the same
           axes it did where it can. */
        if (current_axes > i) {
            --current_axes;
        }
        if (current_axes >= axes.size()) {
            current_axes = axes.empty() ? 0 : axes.size() - 1;
        }
        return true;
    }
    return false;
}

Axes &Figure::add_axes(double left, double bottom, double width, double height)
{
    auto ax = std::make_unique<Axes>();
    ax->position = Bbox(left, bottom, left + width, bottom + height);
    /* Zero rows marks it as outside the grid, so subplots_adjust skips it. */
    ax->grid_rows = 0;
    ax->grid_cols = 0;
    ax->grid_index = 0;
    axes.push_back(std::move(ax));
    return *axes.back();
}

Axes &Figure::inset_axes(const Axes &parent, double x, double y, double width,
                         double height)
{
    /* `bounds` are in the parent's axes fractions; add_axes wants figure
       fractions, converted through the parent's box. The position is read once
       (unlike matplotlib's locator), so create the inset after any tight_layout. */
    const double px = parent.position.x0;
    const double py = parent.position.y0;
    const double pw = parent.position.width();
    const double ph = parent.position.height();

    Axes &ax = add_axes(px + x * pw, py + y * ph, width * pw, height * ph);
    /* An inset sits on top of what it magnifies, so it needs its own opaque
       background (matplotlib gives it zorder 5, above the parent's data). */
    ax.patch_visible = true;
    return ax;
}

Axes &Figure::secondary_xaxis(const Axes &base, bool top, double scale, double offset)
{
    /* A second x scale that is a function of the first. Only the affine case,
       `secondary = scale * primary + offset` (matplotlib allows any inverse
       pair via FuncScale; a nonlinear one would need uneven tick spacing).
       Underneath it is a twin: same box and data, its own labelled axis on the
       far side, limits derived from the base's. */
    Axes &ax = twiny(base);
    const Bbox view = base.view_limits();
    double lo = scale * view.x0 + offset;
    double hi = scale * view.x1 + offset;
    /* A negative scale reverses the order; the axis must run the same way round
       the box, so the limits are swapped back. */
    if ((view.x0 < view.x1) != (lo < hi)) {
        std::swap(lo, hi);
    }
    ax.set_xlim(lo, hi);
    ax.x_ticks_top = top;
    return ax;
}

Axes &Figure::secondary_yaxis(const Axes &base, bool right, double scale, double offset)
{
    Axes &ax = twinx(base);
    const Bbox view = base.view_limits();
    double lo = scale * view.y0 + offset;
    double hi = scale * view.y1 + offset;
    if ((view.y0 < view.y1) != (lo < hi)) {
        std::swap(lo, hi);
    }
    ax.set_ylim(lo, hi);
    ax.y_ticks_right = right;
    return ax;
}

void Figure::suptitle(const std::string &text, double x, double y, double fontsize)
{
    figure_title = text;
    figure_title_x = x;
    figure_title_y = y;
    if (fontsize > 0.0) {
        figure_title_size = fontsize;
    }
}

void Figure::supxlabel(const std::string &text, double x, double y, double fontsize)
{
    figure_xlabel = text;
    figure_xlabel_x = x;
    figure_xlabel_y = y;
    figure_xlabel_size = fontsize > 0.0 ? fontsize : figure_label_size;
}

void Figure::supylabel(const std::string &text, double x, double y, double fontsize)
{
    figure_ylabel = text;
    figure_ylabel_x = x;
    figure_ylabel_y = y;
    figure_ylabel_size = fontsize > 0.0 ? fontsize : figure_label_size;
}

namespace
{

/* The subplot column (for y labels) or row (for x labels and titles) an axes
   sits in, derived from its 1-based add_subplot index the way matplotlib reads
   it off the SubplotSpec: index counts left to right, top to bottom. */
unsigned align_column(const Axes &ax)
{
    return ax.grid_cols == 0 ? 0 : (ax.grid_index - 1) % ax.grid_cols;
}
unsigned align_row(const Axes &ax)
{
    return ax.grid_cols == 0 ? 0 : (ax.grid_index - 1) / ax.grid_cols;
}

} // namespace

/* Join each axes to the siblings it shares a column (y) or row (x) with, so the
   draw code can hang every label in a group off the group's furthest tick edge.
   O(n^2) grouping by key. An empty request means the whole figure (axs=None). */
void Figure::align_ylabels(const std::vector<Axes *> &axs)
{
    std::vector<Axes *> group;
    if (axs.empty()) {
        for (const auto &up : axes) {
            group.push_back(up.get());
        }
    } else {
        for (Axes *ax : axs) {
            if (ax != nullptr) {
                group.push_back(ax);
            }
        }
    }
    for (Axes *ax : group) {
        const unsigned col = align_column(*ax);
        std::vector<const Axes *> members;
        for (Axes *other : group) {
            if (align_column(*other) == col) {
                members.push_back(other);
            }
        }
        ax->ylabel_align_group = members;
    }
}

void Figure::align_xlabels(const std::vector<Axes *> &axs)
{
    std::vector<Axes *> group;
    if (axs.empty()) {
        for (const auto &up : axes) {
            group.push_back(up.get());
        }
    } else {
        for (Axes *ax : axs) {
            if (ax != nullptr) {
                group.push_back(ax);
            }
        }
    }
    for (Axes *ax : group) {
        const unsigned row = align_row(*ax);
        std::vector<const Axes *> members;
        for (Axes *other : group) {
            if (align_row(*other) == row) {
                members.push_back(other);
            }
        }
        ax->xlabel_align_group = members;
    }
}

void Figure::align_titles(const std::vector<Axes *> &axs)
{
    std::vector<Axes *> group;
    if (axs.empty()) {
        for (const auto &up : axes) {
            group.push_back(up.get());
        }
    } else {
        for (Axes *ax : axs) {
            if (ax != nullptr) {
                group.push_back(ax);
            }
        }
    }
    for (Axes *ax : group) {
        const unsigned row = align_row(*ax);
        std::vector<const Axes *> members;
        for (Axes *other : group) {
            if (align_row(*other) == row) {
                members.push_back(other);
            }
        }
        ax->title_align_group = members;
    }
}

void Figure::align_labels(const std::vector<Axes *> &axs)
{
    align_xlabels(axs);
    align_ylabels(axs);
}

FigureImage &Figure::figimage(const double *values, std::size_t rows, std::size_t cols,
                              double xo, double yo)
{
    FigureImage img;
    img.rows = rows;
    img.cols = cols;
    img.xo = xo;
    img.yo = yo;
    if (values != nullptr) {
        img.values.assign(values, values + rows * cols);
    }
    figure_images.push_back(std::move(img));
    return figure_images.back();
}

void Figure::subplots(unsigned nrows, unsigned ncols, Axes **out)
{
    if (nrows == 0 || ncols == 0 || out == nullptr) {
        return;
    }
    /* add_subplot's index is 1-based and runs left to right, top to bottom;
       the output array is 0-based in the same order. */
    for (unsigned i = 0; i < nrows * ncols; ++i) {
        out[i] = &add_subplot(nrows, ncols, i + 1);
    }
}

Axes &Figure::add_subplot(unsigned nrows, unsigned ncols, unsigned index)
{
    nrows = std::max(1u, nrows);
    ncols = std::max(1u, ncols);
    index = std::max(1u, index);

    auto ax = std::make_unique<Axes>();

    const Bbox grid{subplot_params.left, subplot_params.bottom, subplot_params.right,
                    subplot_params.top};
    ax->position = grid_cell(grid, subplot_params.wspace, subplot_params.hspace, nrows,
                             ncols, index);
    ax->grid_rows = nrows;
    ax->grid_cols = ncols;
    ax->grid_index = index;

    axes.push_back(std::move(ax));
    return *axes.back();
}

Axes &Figure::twinx(const Axes &base)
{
    auto ax = std::make_unique<Axes>();

    /* Same box, same x, its own y on the right. */
    ax->position = base.position;
    ax->grid_rows = base.grid_rows;
    ax->grid_cols = base.grid_cols;
    ax->grid_index = base.grid_index;

    ax->shared_x = &base;
    ax->y_ticks_right = true;
    ax->x_axis_visible = false;
    ax->patch_visible = false;

    /* axes.grid would double every gridline, and the first axes already drew
       them at its own y values. */
    ax->xgrid_major = ax->ygrid_major = ax->xgrid_minor = ax->ygrid_minor = false;

    axes.push_back(std::move(ax));
    return *axes.back();
}

Axes &Figure::twiny(const Axes &base)
{
    auto ax = std::make_unique<Axes>();

    /* Same box, same y, its own x along the top. */
    ax->position = base.position;
    ax->grid_rows = base.grid_rows;
    ax->grid_cols = base.grid_cols;
    ax->grid_index = base.grid_index;

    ax->shared_y = &base;
    ax->x_ticks_top = true;
    ax->y_axis_visible = false;
    ax->patch_visible = false;
    ax->xgrid_major = ax->ygrid_major = ax->xgrid_minor = ax->ygrid_minor = false;

    axes.push_back(std::move(ax));
    return *axes.back();
}

Axes &Figure::colorbar(Axes &parent,
                       Colormap cmap,
                       double vmin,
                       double vmax,
                       double fraction,
                       double pad,
                       double aspect,
                       bool horizontal)
{
    /* matplotlib's make_axes: 0.05 for a colorbar beside its parent, 0.15 below
       it, since a horizontal strip needs more room to clear the parent's x tick
       labels. */
    if (pad < 0.0) {
        pad = horizontal ? 0.15 : 0.05;
    }

    const Bbox slot_source = parent.position;
    Bbox slot;
    if (!horizontal) {
        /* make_axes splits the parent's box: it keeps 1 - fraction - pad of
           the width, the gap takes `pad`, and the colorbar gets `fraction`. */
        const double w = slot_source.width();
        parent.position = Bbox(slot_source.x0, slot_source.y0,
                               slot_source.x0 + w * (1.0 - fraction - pad),
                               slot_source.y1);
        slot = Bbox(slot_source.x0 + w * (1.0 - fraction), slot_source.y0, slot_source.x1,
                    slot_source.y1);
    } else {
        /* Same split, turned 90 degrees: the colorbar takes the BOTTOM
           `fraction` of the height, the parent keeps the rest above the gap. */
        const double h = slot_source.height();
        parent.position = Bbox(slot_source.x0, slot_source.y0 + h * (fraction + pad),
                               slot_source.x1, slot_source.y1);
        slot = Bbox(slot_source.x0, slot_source.y0, slot_source.x1,
                    slot_source.y0 + h * fraction);
    }

    /* set_aspect(aspect, adjustable='box') shrinks the slot to the requested
       long-over-short ratio and anchors it to the edge nearest the parent (left
       vertical, top horizontal). The ratio is in pixels, so the figure's shape
       enters here. matplotlib inverts `aspect` for a horizontal bar, since "long
       over short" flips from height/width to width/height. */
    if (aspect > 0.0) {
        const double px_w = slot.width() * width_inch * dpi;
        const double px_h = slot.height() * height_inch * dpi;
        if (!horizontal) {
            const double want_px_w = px_h / aspect;
            if (want_px_w < px_w) {
                slot = Bbox(slot.x0, slot.y0, slot.x0 + slot.width() * (want_px_w / px_w),
                            slot.y1);
            }
        } else {
            const double want_px_h = px_w / aspect;
            if (want_px_h < px_h) {
                /* Anchored to the TOP: that edge sits against the gap and the
                   parent, the bottom edge is the free one that moves up. */
                const double shrunk = slot.height() * (want_px_h / px_h);
                slot = Bbox(slot.x0, slot.y1 - shrunk, slot.x1, slot.y1);
            }
        }
    }

    auto ax = std::make_unique<Axes>();
    ax->position = slot;

    /* The gradient is a one-row-or-column image, 256 steps deep (a colormap's
       lookup size), matching matplotlib's pcolormesh so both band identically. */
    constexpr std::size_t kSteps = 256;
    std::vector<double> ramp(kSteps);

    ImageGrid *strip = nullptr;
    if (!horizontal) {
        ax->y_ticks_right = true;
        ax->x_axis_visible = false;
        for (std::size_t i = 0; i < kSteps; ++i) {
            /* Row 0 is drawn at the top, and a colorbar's high end is at the
               top. */
            ramp[i] = vmax - (vmax - vmin) * (static_cast<double>(i) + 0.5) / kSteps;
        }
        strip = &ax->imshow(ramp.data(), kSteps, 1);
        strip->x0 = 0.0;
        strip->x1 = 1.0;
        /* The extent's y1 is its TOP edge, where row 0 goes -- and row 0 of
           the ramp is the high end of the scale. Unlike an image, a
           colorbar's own axis is not inverted: values increase upwards, as
           its tick labels do. */
        strip->y0 = vmin;
        strip->y1 = vmax;
        ax->set_xlim(0.0, 1.0);
        ax->set_ylim(vmin, vmax);
    } else {
        /* No row-0-at-the-top trick needed: image COLUMNS already run left to
           right, the same direction a colorbar's value increases in, so
           column i is simply the i-th step from vmin to vmax. */
        ax->y_axis_visible = false;
        for (std::size_t i = 0; i < kSteps; ++i) {
            ramp[i] = vmin + (vmax - vmin) * (static_cast<double>(i) + 0.5) / kSteps;
        }
        strip = &ax->imshow(ramp.data(), 1, kSteps);
        strip->y0 = 0.0;
        strip->y1 = 1.0;
        strip->x0 = vmin;
        strip->x1 = vmax;
        ax->set_xlim(vmin, vmax);
        ax->set_ylim(0.0, 1.0);
    }
    strip->cmap = cmap;
    strip->has_vlimits = true;
    strip->vmin = vmin;
    strip->vmax = vmax;

    /* imshow asks for square pixels; a colorbar's strip is deliberately not
       square, and its shape was already settled by `aspect` above. */
    ax->aspect = 0.0;

    axes.push_back(std::move(ax));
    return *axes.back();
}

Axes::TickReach Axes::tick_reach(Renderer &renderer, double px_per_pt) const
{
    TickReach reach;
    /* Only the part of a tick outside the spine takes up room. pad is the gap
       between the tick and its label, so it counts only when a label is present
       (label_outer hides the label but keeps the tick), matching matplotlib's
       tight bbox over visible children only. */
    reach.below = (xtick.ticks_visible ? xtick.outward(xtick.size) : 0.0) * px_per_pt +
                  (xtick.labels_visible ? xtick.pad * px_per_pt : 0.0);
    reach.left = (ytick.ticks_visible ? ytick.outward(ytick.size) : 0.0) * px_per_pt +
                 (ytick.labels_visible ? ytick.pad * px_per_pt : 0.0);

    FontProps x_tick_font;
    x_tick_font.size = xtick.label_size;
    FontProps y_tick_font;
    y_tick_font.size = ytick.label_size;

    const Bbox view = view_limits();

    std::vector<double> xticks;
    std::vector<double> yticks;
    std::vector<std::string> xlabels;
    std::vector<std::string> ylabels;
    compute_ticks(display_box(renderer.width(), renderer.height()), px_per_pt, xticks,
                  yticks, xlabels, ylabels);

    const Bbox box = display_box(renderer.width(), renderer.height());
    const Affine2D to_display = scale_to_display(box);

    /* x tick labels hang below their anchor by the whole line box (drawn top-at-
       anchor); the outermost, centred on the tick, overhangs the frame by half
       its width. */
    double tallest = 0.0;
    for (std::size_t i = 0; xtick.labels_visible && i < xticks.size(); ++i) {
        if (!within(xticks[i], view.x0, view.x1)) {
            continue;
        }
        const LineMetrics line = renderer.text_line_metrics(xlabels[i], x_tick_font);
        tallest = std::max(tallest, line.ascent + line.descent);

        const double half = renderer.measure_text(xlabels[i], x_tick_font).width / 2.0;
        const double centre = to_display(Point{scale_x(xticks[i]), scale_y(view.y0)}).x;
        reach.right = std::max(reach.right, centre + half - box.x1);
    }
    reach.below += tallest;

    /* y tick labels are right-aligned against their anchor, so the widest sets how
       far left the column reaches; the topmost overhangs the frame by half its
       ascent. */
    double widest = 0.0;
    for (std::size_t i = 0; ytick.labels_visible && i < yticks.size(); ++i) {
        if (!within(yticks[i], view.y0, view.y1)) {
            continue;
        }
        widest = std::max(widest, renderer.measure_text(ylabels[i], y_tick_font).width);

        const LineMetrics line = renderer.text_line_metrics(ylabels[i], y_tick_font);
        const double centre = to_display(Point{scale_x(view.x0), scale_y(yticks[i])}).y;
        reach.above = std::max(reach.above, centre + line.ascent / 2.0 - box.y1);
    }
    reach.left += widest;

    return reach;
}

Axes::Margins Axes::measure_margins(Renderer &renderer) const
{
    /* Derived from the same quantities the drawing code uses: tick_reach for the
       tick labels, then labelpad and titlepad for what hangs off them. */
    Margins m;
    const double px_per_pt = renderer.dpi() / 72.0;
    const double labelpad_px = label_pad * px_per_pt;

    const TickReach reach = tick_reach(renderer, px_per_pt);
    if (y_axis_visible) {
        (y_ticks_right ? m.right : m.left) = reach.left;
    }

    /* A twiny axes carries its x decorations along the top, so the room is
       reserved there rather than underneath. */
    if (x_axis_visible) {
        (x_ticks_top ? m.top : m.bottom) = reach.below;
        /* The outermost tick labels overhang the corners they sit at. */
        m.right = std::max(m.right, reach.right);
    }
    m.top = std::max(m.top, reach.above);

    FontProps label_font;
    label_font.size = axis_label_size;

    if (!ylabel.empty()) {
        /* Rotated a quarter turn, so the label's line height eats horizontal
           room, not its width. */
        const LineMetrics line = renderer.text_line_metrics(ylabel, label_font);
        (y_ticks_right ? m.right : m.left) += labelpad_px + line.ascent + line.descent;
    }
    if (!xlabel.empty()) {
        const LineMetrics line = renderer.text_line_metrics(xlabel, label_font);
        (x_ticks_top ? m.top : m.bottom) += labelpad_px + line.ascent + line.descent;
    }
    if (!title.empty()) {
        /* Drawn on its baseline titlepad above the frame, so it reaches its ascent
           further up. */
        FontProps title_font;
        title_font.size = title_size;
        const LineMetrics line = renderer.text_line_metrics(title, title_font);
        m.top = title_pad * px_per_pt + line.ascent;
    }

    return m;
}

void Figure::tight_layout(Renderer &renderer, double pad)
{
    if (axes.empty()) {
        return;
    }

    const double fig_w = renderer.width();
    const double fig_h = renderer.height();
    if (fig_w <= 0.0 || fig_h <= 0.0) {
        return;
    }

    /* tight_layout's pad is in font-size units, and the exact arithmetic matters:
       compute font_size/72 inches, multiply by pad, divide by the figure size in
       inches. Folding to pixels is algebraically identical but a different double,
       and a one-ULP shift can move a spine across a pixel boundary. */
    const double font_size_inch = axes.front()->xtick.label_size / 72.0;
    const double pad_inch = pad * font_size_inch;
    const double pad_px = pad_inch * renderer.dpi();

    unsigned rows = 1;
    unsigned cols = 1;
    for (const std::unique_ptr<Axes> &ax : axes) {
        rows = std::max(rows, ax->grid_rows);
        cols = std::max(cols, ax->grid_cols);
    }

    /* tight_layout computes subplot params, not positions: matplotlib works out
       left/right/bottom/top and wspace/hspace as figure fractions and hands them
       to subplots_adjust. Doing it in fractions (not pixels) keeps the ULP-level
       agreement that decides which pixel column a spine lands in. Margins are
       uniform across the grid: every cell the same size, outer margins take the
       widest decoration per edge, one wspace covers every interior gap.

       hspaces is rows x (cols + 1): one entry per column boundary per row. An
       interior boundary accumulates the right overhang of the column on its left
       and the left overhang of the column on its right (a sum, since both must fit
       side by side); the two outer boundaries have a single contributor each. */
    std::vector<double> hspaces(static_cast<std::size_t>(rows) * (cols + 1), 0.0);
    std::vector<double> vspaces(static_cast<std::size_t>(rows + 1) * cols, 0.0);

    for (const std::unique_ptr<Axes> &ax : axes) {
        if (ax->grid_rows != rows || ax->grid_cols != cols) {
            /* Mixed grids would need a real constraint solve; leave them alone. */
            return;
        }
        const unsigned cell = ax->grid_index - 1;
        const unsigned r = cell / cols;
        const unsigned c = cell % cols;
        if (r >= rows || c >= cols) {
            continue;
        }

        const Axes::Margins m = ax->measure_margins(renderer);
        /* The overhang is a difference of two fractions (`pos.x0 - tight_x0/W`),
           not a pixel size divided by the width, to match matplotlib's double
           exactly (same reason as the pad above). */
        const Bbox &pos = ax->position;
        const double tight_x0 = pos.x0 * fig_w - m.left;
        const double tight_x1 = pos.x1 * fig_w + m.right;
        const double tight_y0 = pos.y0 * fig_h - m.bottom;
        const double tight_y1 = pos.y1 * fig_h + m.top;

        hspaces[static_cast<std::size_t>(r) * (cols + 1) + c] += pos.x0 - tight_x0 / fig_w;
        hspaces[static_cast<std::size_t>(r) * (cols + 1) + c + 1] +=
            tight_x1 / fig_w - pos.x1;
        vspaces[static_cast<std::size_t>(r) * cols + c] += tight_y1 / fig_h - pos.y1;
        vspaces[static_cast<std::size_t>(r + 1) * cols + c] += pos.y0 - tight_y0 / fig_h;
    }

    /* Each boundary then takes the widest demand across the rows (or
       columns) that share it, so the panels stay lined up. */
    auto max_hspace = [&](unsigned boundary) {
        double v = 0.0;
        for (unsigned r = 0; r < rows; ++r) {
            v = std::max(v, hspaces[static_cast<std::size_t>(r) * (cols + 1) + boundary]);
        }
        return v;
    };
    auto max_vspace = [&](unsigned boundary) {
        double v = 0.0;
        for (unsigned c = 0; c < cols; ++c) {
            v = std::max(v, vspaces[static_cast<std::size_t>(boundary) * cols + c]);
        }
        return v;
    };

    /* Divided by the figure size in inches, as matplotlib does, not by the pixel
       width. See the note on pad_inch. */
    const double hpad = pad_inch / width_inch;
    const double vpad = pad_inch / height_inch;

    const double margin_left = max_hspace(0) + hpad;
    const double margin_right = max_hspace(cols) + hpad;
    const double margin_top = max_vspace(0) + vpad;
    const double margin_bottom = max_vspace(rows) + vpad;

    if (margin_left + margin_right >= 1.0 || margin_bottom + margin_top >= 1.0) {
        /* The decorations do not fit; leave the layout untouched rather than make
           negative-sized axes (matplotlib warns and gives up too). */
        return;
    }

    SubplotParams p;
    p.left = margin_left;
    p.right = 1.0 - margin_right;
    p.bottom = margin_bottom;
    p.top = 1.0 - margin_top;
    p.wspace = subplot_params.wspace;
    p.hspace = subplot_params.hspace;

    /* wspace and hspace are ratios of the cell size, not absolute gaps, so each is
       divided back out by a cell width derived from the margins. That round trip
       must be reproduced to match matplotlib's rounding. */
    if (cols > 1) {
        double gap = 0.0;
        for (unsigned c = 1; c < cols; ++c) {
            gap = std::max(gap, max_hspace(c));
        }
        gap += hpad;
        const double h_axes =
            (1.0 - margin_right - margin_left - gap * (cols - 1)) / cols;
        if (h_axes <= 0.0) {
            return;
        }
        p.wspace = gap / h_axes;
    }
    if (rows > 1) {
        double gap = 0.0;
        for (unsigned r = 1; r < rows; ++r) {
            gap = std::max(gap, max_vspace(r));
        }
        gap += vpad;
        const double v_axes =
            (1.0 - margin_top - margin_bottom - gap * (rows - 1)) / rows;
        if (v_axes <= 0.0) {
            return;
        }
        p.hspace = gap / v_axes;
    }

    subplot_params = p;
    subplots_adjust();

    /* VPL_LAYOUT_DEBUG=1 prints the subplot params and every panel box in pixels,
       to compare against matplotlib's SubplotParams and ax.get_window_extent(). */
    if (std::getenv("VPL_LAYOUT_DEBUG") != nullptr) {
        std::fprintf(stderr, "left=%.17g right=%.17g bottom=%.17g top=%.17g\n", p.left,
                     p.right, p.bottom, p.top);
        std::fprintf(stderr, "wspace=%.17g hspace=%.17g\n", p.wspace, p.hspace);
        for (const std::unique_ptr<Axes> &ax : axes) {
            std::fprintf(stderr, "panel x0=%.17g x1=%.17g y0=%.17g y1=%.17g\n",
                         ax->position.x0 * fig_w, ax->position.x1 * fig_w,
                         ax->position.y0 * fig_h, ax->position.y1 * fig_h);
        }
    }
}

void Figure::draw(Renderer &renderer)
{
    /* figimage first, behind everything (FigureImage has a low zorder). Each array
       element is one device pixel, drawn as a 1x1 quad on integer coordinates for
       an exact blit. `xo`, `yo` are the lower-left corner in the renderer's y-up
       device space. */
    for (const FigureImage &img : figure_images) {
        if (img.rows == 0 || img.cols == 0 || img.values.empty()) {
            continue;
        }
        double vmin = img.vmin;
        double vmax = img.vmax;
        if (!img.has_vlimits) {
            vmin = std::numeric_limits<double>::infinity();
            vmax = -vmin;
            for (double v : img.values) {
                if (!std::isfinite(v)) {
                    continue;
                }
                vmin = std::min(vmin, v);
                vmax = std::max(vmax, v);
            }
            if (!std::isfinite(vmin)) {
                continue;
            }
        }
        for (std::size_t r = 0; r < img.rows; ++r) {
            /* origin='upper' puts row 0 at the top, so it lands at the highest
               device y; origin='lower' counts up from yo. */
            const double row_from_bottom =
                img.origin_upper ? static_cast<double>(img.rows - 1 - r) : static_cast<double>(r);
            const double y0 = img.yo + row_from_bottom;
            for (std::size_t c = 0; c < img.cols; ++c) {
                Color fill = colormap_lookup(img.cmap, normalize(img.value_at(r, c), vmin, vmax));
                if (!fill.visible()) {
                    continue;
                }
                const double x0 = img.xo + static_cast<double>(c);
                GraphicsContext gc;
                gc.alpha = img.alpha;
                gc.color = Color::none();
                gc.linewidth = 0.0;
                /* Edges on integer pixels, so a hard rasterizer covers each pixel
                   exactly (the raster blit figimage is meant to be). */
                gc.antialiased = false;
                renderer.draw_path(gc, Path::rectangle(Bbox(x0, y0, x0 + 1.0, y0 + 1.0)),
                                   Affine2D(), &fill);
            }
        }
    }

    for (const std::unique_ptr<Axes> &ax : axes) {
        ax->draw(renderer);
    }

    /* The figure's own edge, drawn after the axes so a panel reaching the edge
       cannot paint over it, and only when frameon. */
    if (frameon && linewidth > 0.0) {
        GraphicsContext gc;
        gc.color = edgecolor;
        gc.linewidth = linewidth;
        gc.join = JoinStyle::Miter;
        renderer.draw_path(gc, Path::rectangle(Bbox(0.0, 0.0, renderer.width(),
                                                    renderer.height())),
                           Affine2D(), nullptr);
    }

    /* The figure title last, in figure coordinates: centred at x = 0.5 with its
       top on y = 0.98, as matplotlib puts it. Drawn after the axes so a tall panel
       cannot paint over it. */
    if (!figure_title.empty()) {
        FontProps font;
        font.size = figure_title_size;

        GraphicsContext gc;
        renderer.draw_text(gc, figure_title_x * renderer.width(),
                           figure_title_y * renderer.height(), figure_title, font, 0.0,
                           HAlign::Center, VAlign::Top);
    }

    /* One x and one y label for the whole figure (supxlabel/supylabel), for a
       grid of panels sharing a scale. Each is anchored by an edge, not centred on
       its box (x label's bottom on y = 0.01, y label's left on x = 0.02), so a
       taller string grows inward and never off the canvas. */
    if (!figure_xlabel.empty()) {
        FontProps font;
        font.size = figure_xlabel_size;
        GraphicsContext gc;
        renderer.draw_text(gc, figure_xlabel_x * renderer.width(),
                           figure_xlabel_y * renderer.height(), figure_xlabel, font, 0.0,
                           HAlign::Center, VAlign::Bottom);
    }
    if (!figure_ylabel.empty()) {
        FontProps font;
        font.size = figure_ylabel_size;
        GraphicsContext gc;
        /* Rotated a quarter turn, left-aligned along its baseline (the bottom of
           the string on screen after rotation). */
        renderer.draw_text(gc, figure_ylabel_x * renderer.width(),
                           figure_ylabel_y * renderer.height(), figure_ylabel, font, 90.0,
                           HAlign::Left, VAlign::Center);
    }
}

} // namespace vpl
