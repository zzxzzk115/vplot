/*
 * Ported from matplotlib's lib/matplotlib/ticker.py.
 * Copyright (c) 2012- Matplotlib Development Team; All Rights Reserved.
 * See THIRD-PARTY-NOTICES.
 */

#include "ticker.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

#include "geometry.h"

namespace vpl
{
namespace
{

/* Python's // and divmod floor toward negative infinity, unlike C++'s / and
   fmod; the locator relies on floor semantics for negative limits. */
double py_floordiv(double x, double y)
{
    return std::floor(x / y);
}

void py_divmod(double x, double y, double &quotient, double &remainder)
{
    quotient = std::floor(x / y);
    remainder = x - quotient * y;
}

/* axes.unicode_minus defaults to True: tick labels use U+2212 MINUS SIGN, not
   the ASCII hyphen (the two glyphs differ in width and height). */
std::string unicode_minus(const std::string &s)
{
    static const char *kMinusSign = "\xE2\x88\x92"; /* U+2212 in UTF-8 */

    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '-') {
            out += kMinusSign;
        } else {
            out += c;
        }
    }
    return out;
}

/* ticker.scale_range */
void scale_range(double vmin, double vmax, int n, double &scale, double &offset,
                 double threshold = 100.0)
{
    const double dv = std::fabs(vmax - vmin);
    const double meanv = (vmax + vmin) / 2.0;

    if (std::fabs(meanv) / dv < threshold) {
        offset = 0.0;
    } else {
        offset = std::copysign(std::pow(10.0, py_floordiv(std::log10(std::fabs(meanv)), 1.0)),
                               meanv);
    }
    scale = std::pow(10.0, py_floordiv(std::log10(dv / n), 1.0));
}

/* ticker._Edge_integer: integer multiples of a step, with tolerance that grows
   when the offset dwarfs the step. */
class EdgeInteger
{
  public:
    EdgeInteger(double step, double offset) : m_step(step), m_offset(std::fabs(offset)) {}

    double le(double x) const
    {
        double d, m;
        py_divmod(x, m_step, d, m);
        return closeto(m / m_step, 1.0) ? d + 1.0 : d;
    }

    double ge(double x) const
    {
        double d, m;
        py_divmod(x, m_step, d, m);
        return closeto(m / m_step, 0.0) ? d : d + 1.0;
    }

  private:
    bool closeto(double ms, double edge) const
    {
        double tol;
        if (m_offset > 0.0) {
            const double digits = std::log10(m_offset / m_step);
            tol = std::max(1e-10, std::pow(10.0, digits - 12.0));
            tol = std::min(0.4999, tol);
        } else {
            tol = 1e-10;
        }
        return std::fabs(ms - edge) < tol;
    }

    double m_step;
    double m_offset;
};

} // namespace

int auto_nbins(double length_px, double dpi, double label_size, bool horizontal)
{
    if (!(dpi > 0.0) || !(length_px > 0.0)) {
        return 9;
    }
    /* get_tick_space works in points: the axis length in inches times 72. */
    const double length_pt = length_px / dpi * 72.0;
    const double size = label_size * (horizontal ? 3.0 : 2.0);
    if (!(size > 0.0)) {
        return 9;
    }
    const int n = static_cast<int>(std::floor(length_pt / size));
    /* Clipped to [max(1, min_n_ticks - 1), 9], with MaxNLocator's default
       min_n_ticks of 2. */
    return std::min(std::max(n, 1), 9);
}

/* ticker.MaxNLocator._staircase */
std::vector<double> MaxNLocator::extended_steps() const
{
    /* matplotlib normalizes `steps` to start at 1 and end at 10 first. */
    std::vector<double> s = steps;
    if (s.empty()) {
        s = {1.0, 2.0, 2.5, 5.0, 10.0};
    }
    if (s.front() != 1.0) {
        s.insert(s.begin(), 1.0);
    }
    if (s.back() != 10.0) {
        s.push_back(10.0);
    }

    std::vector<double> out;
    out.reserve(s.size() + s.size());
    for (std::size_t i = 0; i + 1 < s.size(); ++i) {
        out.push_back(0.1 * s[i]);
    }
    for (double v : s) {
        out.push_back(v);
    }
    out.push_back(10.0 * s[1]);
    return out;
}

/* ticker.MaxNLocator._raw_ticks */
std::vector<double> MaxNLocator::raw_ticks(double vmin, double vmax) const
{
    const int nb = std::max(1, nbins);

    double scale = 1.0;
    double offset = 0.0;
    scale_range(vmin, vmax, nb, scale, offset);

    const double _vmin = vmin - offset;
    const double _vmax = vmax - offset;

    std::vector<double> scaled_steps = extended_steps();
    for (double &s : scaled_steps) {
        s *= scale;
    }

    if (integer) {
        /* Keep the sub-unit steps and, above 1, only the ones that landed on a
           whole number after scaling. */
        std::vector<double> kept;
        for (double s : scaled_steps) {
            if (s < 1.0 || std::fabs(s - std::round(s)) < 0.001) {
                kept.push_back(s);
            }
        }
        scaled_steps.swap(kept);
        if (scaled_steps.empty()) {
            scaled_steps.push_back(1.0);
        }
    }

    const double raw_step = (_vmax - _vmin) / nb;

    /* Index of the smallest step that is still >= raw_step. */
    std::size_t istep = scaled_steps.size() - 1;
    for (std::size_t i = 0; i < scaled_steps.size(); ++i) {
        if (scaled_steps[i] >= raw_step) {
            istep = i;
            break;
        }
    }

    /* Walk back down from that step until one yields enough visible ticks. */
    std::vector<double> ticks;
    for (std::size_t k = istep + 1; k-- > 0;) {
        double step = scaled_steps[k];
        if (integer && std::floor(_vmax) - std::ceil(_vmin) >=
                           static_cast<double>(min_n_ticks) - 1.0) {
            /* Never finer than 1 once the range has room for enough whole
               numbers. */
            step = std::max(1.0, step);
        }
        const double best_vmin = py_floordiv(_vmin, step) * step;

        const EdgeInteger edge(step, offset);
        const double low = edge.le(_vmin - best_vmin);
        const double high = edge.ge(_vmax - best_vmin);

        /* low and high are whole numbers; count them out as integers to avoid
           double drift over long ranges. */
        const long long n = static_cast<long long>(std::llround(high - low));

        ticks.clear();
        int nticks = 0;
        for (long long i = 0; i <= n; ++i) {
            const double t = (low + static_cast<double>(i)) * step + best_vmin;
            ticks.push_back(t);
            if (t <= _vmax && t >= _vmin) {
                ++nticks;
            }
        }

        if (nticks >= min_n_ticks || k == 0) {
            break;
        }
    }

    for (double &t : ticks) {
        t += offset;
    }
    return ticks;
}

std::vector<double> MaxNLocator::tick_values(double vmin, double vmax) const
{
    Bbox::nonsingular(vmin, vmax, 1e-13, 1e-14);
    return raw_ticks(vmin, vmax);
}

std::vector<double> LogLocator::tick_values(double vmin, double vmax) const
{
    std::vector<double> ticks;

    /* A log axis cannot show non-positive values; clip the view as matplotlib
       does rather than refusing to draw. */
    if (!(vmax > 0.0)) {
        return ticks;
    }
    if (!(vmin > 0.0)) {
        vmin = vmax / 1000.0;
    }
    if (vmin > vmax) {
        std::swap(vmin, vmax);
    }

    const double log_base = std::log(base);
    const double lo = std::floor(std::log(vmin) / log_base);
    const double hi = std::ceil(std::log(vmax) / log_base);

    auto decades_only = [&]() {
        std::vector<double> out;
        for (double e = lo; e <= hi + 0.5; e += 1.0) {
            out.push_back(std::pow(base, e));
        }
        return out;
    };

    ticks = decades_only();

    int visible = 0;
    for (double t : ticks) {
        if (t >= vmin && t <= vmax) {
            ++visible;
        }
    }

    /* Too few decades in view: subdivide, as LogLocator does. */
    if (visible < min_n_ticks) {
        std::vector<double> subdivided;
        for (double e = lo; e <= hi + 0.5; e += 1.0) {
            const double decade = std::pow(base, e);
            for (int s = 1; s < static_cast<int>(base); ++s) {
                subdivided.push_back(decade * s);
            }
        }
        ticks = std::move(subdivided);
    }

    return ticks;
}

std::vector<double> auto_minor_ticks(const std::vector<double> &majors, double vmin,
                                     double vmax)
{
    std::vector<double> minors;
    if (majors.size() < 2) {
        return minors;
    }

    const double majorstep = majors[1] - majors[0];
    if (!(majorstep > 0.0) || !std::isfinite(majorstep)) {
        return minors;
    }

    /* 10 ** (log10(step) mod 1): the step's leading digits, ignoring its
       magnitude. */
    double mantissa = std::pow(10.0, std::log10(majorstep) - std::floor(std::log10(majorstep)));
    int ndivs = 4;
    for (double candidate : {1.0, 2.5, 5.0, 10.0}) {
        if (std::fabs(mantissa - candidate) < 1e-9 * std::max(1.0, candidate)) {
            ndivs = 5;
            break;
        }
    }

    const double minorstep = majorstep / ndivs;
    const double t0 = majors.front();

    const long long tmin = static_cast<long long>(std::llround((vmin - t0) / minorstep));
    const long long tmax = static_cast<long long>(std::llround((vmax - t0) / minorstep)) + 1;

    /* Guard against an unbounded count from a pathological view (matplotlib's
       raise_if_exceeds). */
    if (tmax - tmin > 10000) {
        return minors;
    }

    for (long long i = tmin; i < tmax; ++i) {
        const double v = static_cast<double>(i) * minorstep + t0;
        if (v < vmin || v > vmax) {
            continue;
        }
        /* Skip positions that coincide with a major tick. */
        bool is_major = false;
        for (double m : majors) {
            if (std::fabs(v - m) < minorstep * 1e-6) {
                is_major = true;
                break;
            }
        }
        if (!is_major) {
            minors.push_back(v);
        }
    }

    return minors;
}

std::vector<double> log_minor_ticks(double vmin, double vmax, double base)
{
    std::vector<double> minors;
    if (!(base > 2.0) || !(vmax > 0.0)) {
        return minors;
    }
    if (!(vmin > 0.0)) {
        vmin = vmax / 1000.0;
    }
    if (vmin > vmax) {
        std::swap(vmin, vmax);
    }

    const double log_base = std::log(base);
    const double emin = std::ceil(std::log(vmin) / log_base);
    const double emax = std::floor(std::log(vmax) / log_base);

    /* The count of whole decades in view. */
    if (emax - emin + 1.0 >= 10.0) {
        return minors;
    }

    /* One decade beyond each end so the subdivisions reach the frame. */
    for (double e = emin - 1.0; e <= emax + 1.0; e += 1.0) {
        const double decade = std::pow(base, e);
        for (int s = 2; s < static_cast<int>(base); ++s) {
            const double v = decade * s;
            if (v >= vmin && v <= vmax) {
                minors.push_back(v);
            }
        }
    }
    return minors;
}

std::vector<std::string> format_log_ticks(const std::vector<double> &locs)
{
    std::vector<std::string> out;
    out.reserve(locs.size());

    /*
     * LogFormatterSciNotation (matplotlib's default on a log axis): a decade is
     * written as the base raised to the exponent, as mathtext, which the text
     * pipeline parses back out (see FontEngine::layout).
     */
    for (double v : locs) {
        const double a = std::fabs(v);
        if (!(a > 0.0)) {
            out.emplace_back(R"($\mathdefault{0}$)");
            continue;
        }

        const double fx = std::log10(a);
        const double rounded = std::round(fx);
        char buf[64];
        if (std::fabs(fx - rounded) < 1e-10) {
            std::snprintf(buf, sizeof(buf), R"($\mathdefault{%s10^{%d}}$)",
                          v < 0.0 ? "-" : "", static_cast<int>(rounded));
        } else {
            /* A non-decade tick (appears only when the locator subdivided);
               matplotlib writes the exponent to two places. */
            std::snprintf(buf, sizeof(buf), R"($\mathdefault{%s10^{%.2f}}$)",
                          v < 0.0 ? "-" : "", fx);
        }
        out.emplace_back(buf);
    }
    return out;
}

/* ticker.ScalarFormatter._set_format */
int tick_decimals(const std::vector<double> &locs)
{
    if (locs.size() < 2) {
        /* matplotlib augments the list with the axis's view interval here for a
           single tick; the view is unavailable at this level and the locators
           above always return at least two. */
        return 0;
    }

    /*
     * The digit count comes from the range the ticks span, not the step between
     * them: start at three decimals for a range of order 1, then drop digits for
     * as long as rounding to that many keeps every tick within a thousandth of
     * the range's order of magnitude. (A step of 0.25 over a range of order 1
     * yields "1.00", not the "1.0" that reasoning from the step alone gives.)
     */
    const auto [lo, hi] = std::minmax_element(locs.begin(), locs.end());
    double loc_range = *hi - *lo;
    if (!std::isfinite(loc_range)) {
        return 0;
    }
    if (loc_range == 0.0) {
        loc_range = std::fabs(*hi); /* curvilinear axes can repeat a tick */
    }
    if (loc_range == 0.0) {
        loc_range = 1.0; /* ... and both of them can be zero */
    }

    const int loc_range_oom = static_cast<int>(std::floor(std::log10(loc_range)));
    int sigfigs = std::max(0, 3 - loc_range_oom);
    const double thresh = 1e-3 * std::pow(10.0, loc_range_oom);

    while (sigfigs >= 0) {
        const double scale = std::pow(10.0, sigfigs);
        double worst = 0.0;
        for (double v : locs) {
            worst = std::max(worst, std::fabs(v - std::round(v * scale) / scale));
        }
        if (worst >= thresh) {
            break;
        }
        --sigfigs;
    }
    ++sigfigs;

    /* Cap the digit count to keep the format string sane (matplotlib would
       switch to an offset or scientific notation first). */
    return std::min(std::max(sigfigs, 0), 10);
}

std::vector<std::string> format_ticks(const std::vector<double> &locs)
{
    const int decimals = tick_decimals(locs);

    std::vector<std::string> out;
    out.reserve(locs.size());
    for (double v : locs) {
        /* -0 reads badly on an axis. */
        if (v == 0.0) {
            v = 0.0;
        }
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
        out.emplace_back(unicode_minus(buf));
    }
    return out;
}

/*
 * ScalarFormatter._compute_offset. Finds the smallest power of ten at which the
 * smallest and largest visible ticks stop agreeing, i.e. how many leading digits
 * they share and therefore how much an offset would save.
 */
namespace
{

double compute_offset(const std::vector<double> &locs, double vmin, double vmax)
{
    std::vector<double> vis;
    for (double v : locs) {
        if (v >= std::min(vmin, vmax) && v <= std::max(vmin, vmax)) {
            vis.push_back(v);
        }
    }
    if (vis.empty()) {
        return 0.0;
    }

    const double lmin = *std::min_element(vis.begin(), vis.end());
    const double lmax = *std::max_element(vis.begin(), vis.end());

    /* An offset only helps with two distinct ticks of the same sign; a range
       straddling zero has no leading digits in common. */
    if (lmin == lmax || (lmin <= 0.0 && 0.0 <= lmax)) {
        return 0.0;
    }

    double abs_min = std::fabs(lmin);
    double abs_max = std::fabs(lmax);
    if (abs_min > abs_max) {
        std::swap(abs_min, abs_max);
    }
    const double sign = lmin < 0.0 ? -1.0 : 1.0;

    const double oom_max = std::ceil(std::log10(abs_max));

    auto floor_div = [](double a, double p) { return std::floor(a / std::pow(10.0, p)); };

    double oom = oom_max;
    while (oom > oom_max - 40.0 && floor_div(abs_min, oom) == floor_div(abs_max, oom)) {
        oom -= 1.0;
    }
    oom += 1.0;

    if ((abs_max - abs_min) / std::pow(10.0, oom) <= 1e-2) {
        /* Straddling a multiple of a large power of ten relative to the span:
           back off until the two differ by more than one at that precision. */
        double o = oom_max;
        while (o > oom_max - 40.0 && !(floor_div(abs_max, o) - floor_div(abs_min, o) > 1.0)) {
            o -= 1.0;
        }
        oom = o + 1.0;
    }

    /* axes.formatter.offset_threshold is 4: the offset must save four digits to
       be worth the extra label. */
    const int threshold = 4;
    const double lead = floor_div(abs_max, oom);
    if (lead >= std::pow(10.0, threshold - 1)) {
        return sign * lead * std::pow(10.0, oom);
    }
    return 0.0;
}

} // namespace

TickScaling tick_scaling(const std::vector<double> &locs, double vmin, double vmax,
                         const TickFormatOptions &opts)
{
    TickScaling out;

    std::vector<double> vis;
    for (double v : locs) {
        if (v >= std::min(vmin, vmax) && v <= std::max(vmin, vmax)) {
            vis.push_back(std::fabs(v));
        }
    }
    if (vis.empty()) {
        return out;
    }

    if (opts.use_offset) {
        out.offset = compute_offset(locs, vmin, vmax);
    }

    if (!opts.scientific) {
        /* style='plain': the offset may still apply, but nothing is divided out. */
        return out;
    }

    /* axes.formatter.limits: an exponent inside [-5, 6] is written out in full,
       one outside it is pulled into the offset text. */
    const int lo_limit = opts.sci_limit_lo;
    const int hi_limit = opts.sci_limit_hi;

    double oom = 0.0;
    if (out.offset != 0.0) {
        const double span = std::fabs(vmax - vmin);
        oom = span > 0.0 ? std::floor(std::log10(span)) : 0.0;
    } else {
        const double val = *std::max_element(vis.begin(), vis.end());
        oom = val == 0.0 ? 0.0 : std::floor(std::log10(val));
    }

    if (oom <= lo_limit || oom >= hi_limit) {
        out.order_of_magnitude = static_cast<int>(oom);
    }
    return out;
}

std::vector<std::string> format_ticks_scaled(const std::vector<double> &locs,
                                             const TickScaling &scale)
{
    if (!scale.engaged()) {
        return format_ticks(locs);
    }

    const double div = std::pow(10.0, scale.order_of_magnitude);
    std::vector<double> scaled;
    scaled.reserve(locs.size());
    for (double v : locs) {
        scaled.push_back((v - scale.offset) / div);
    }
    return format_ticks(scaled);
}

std::string tick_offset_text(const TickScaling &scale)
{
    if (!scale.engaged()) {
        return std::string();
    }

    std::string sci;
    std::string off;

    if (scale.order_of_magnitude != 0) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "1e%d", scale.order_of_magnitude);
        sci = buf;
    }
    if (scale.offset != 0.0) {
        /*
         * ScalarFormatter.format_data, not printf's %g: split the value into a
         * significand and exponent joined by a bare "e" (so a million is "1e6",
         * not %g's "1e+06"), with the sign written explicitly.
         */
        const double e = std::floor(std::log10(std::fabs(scale.offset)));
        const double sig = scale.offset / std::pow(10.0, e);

        char buf[64];
        if (sig == std::floor(sig)) {
            std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(sig));
        } else {
            std::snprintf(buf, sizeof(buf), "%1.10g", sig);
        }
        off = buf;

        if (e != 0.0) {
            std::snprintf(buf, sizeof(buf), "e%d", static_cast<int>(e));
            off += buf;
        }
        if (scale.offset > 0.0) {
            off = "+" + off;
        }
    }

    return unicode_minus(sci + off);
}

} // namespace vpl
