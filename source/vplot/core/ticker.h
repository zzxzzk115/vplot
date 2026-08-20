/*
 * Tick location and formatting.
 *
 * Ported from matplotlib's lib/matplotlib/ticker.py (MaxNLocator, AutoLocator,
 * scale_range, _Edge_integer), including its floating-point tolerance handling.
 * Copyright (c) 2012- Matplotlib Development Team; All Rights Reserved.
 * See THIRD-PARTY-NOTICES.
 */

#ifndef VPL_CORE_TICKER_H
#define VPL_CORE_TICKER_H

#include <string>
#include <vector>

namespace vpl
{

/*
 * MaxNLocator's nbins='auto', resolved against the axis's available room
 * ({X,Y}Axis.get_tick_space): a smaller axis gets fewer bins and a coarser step.
 *
 * `length_px` is the axis's own length. The two axes use different heuristics: a
 * y label needs twice its font size of vertical room, an x label is assumed no
 * wider than three times its height.
 */
int auto_nbins(double length_px, double dpi, double label_size, bool horizontal);

class MaxNLocator
{
  public:
    /* Defaults are AutoLocator's: nbins='auto' and steps=[1, 2, 2.5, 5, 10].
       Callers that know the axis's length should set nbins from auto_nbins;
       9 is matplotlib's fallback for an axis it cannot measure. */
    int nbins = 9;
    int min_n_ticks = 2;
    std::vector<double> steps{1.0, 2.0, 2.5, 5.0, 10.0};

    /*
     * Place ticks only on whole numbers (MaxNLocator(integer=True), used by
     * matshow). Two effects together: non-integer steps are dropped once scaled,
     * and the chosen step is floored at 1 whenever the range is wide enough to
     * hold min_n_ticks whole numbers.
     */
    bool integer = false;

    std::vector<double> tick_values(double vmin, double vmax) const;

  private:
    std::vector<double> extended_steps() const;
    std::vector<double> raw_ticks(double vmin, double vmax) const;
};

/*
 * Decade ticks for logarithmic axes, after ticker.LogLocator.
 *
 * Base 10 with subs=(1.0,) is matplotlib's default: ticks land on powers of ten.
 * When the range spans too few decades, the subdivisions 2..9 are brought in.
 */
class LogLocator
{
  public:
    double base = 10.0;
    int min_n_ticks = 2;

    std::vector<double> tick_values(double vmin, double vmax) const;
};

/*
 * Minor ticks between the majors, after ticker.AutoMinorLocator with n='auto'.
 *
 * The subdivision count varies: 5 when the major step's mantissa is 1, 2.5, 5 or
 * 10, and 4 otherwise. Returns empty when there are fewer than two majors.
 */
std::vector<double> auto_minor_ticks(const std::vector<double> &majors, double vmin,
                                     double vmax);

/*
 * LogLocator's minor ticks: every sub-multiple 2..base-1 inside each decade.
 * A log scale installs this locator itself, independent of the
 * {x,y}tick.minor.visible rcParam (which governs AutoMinorLocator on a linear
 * axis). Yields nothing above ten decades in view.
 */
std::vector<double> log_minor_ticks(double vmin, double vmax, double base);

/* How many decimal places the tick labels need, derived from the tick spacing.
   ScalarFormatter._set_format. */
int tick_decimals(const std::vector<double> &locs);

std::vector<std::string> format_ticks(const std::vector<double> &locs);

/*
 * ScalarFormatter's other half: the common factor pulled out of the labels and
 * written once at the end of the axis. Two mechanisms, both applied:
 *
 *   order of magnitude  divides every label and shows as "1e6"; engages when the
 *                       largest tick's exponent falls outside
 *                       axes.formatter.limits ([-5, 6]).
 *   offset              subtracts a constant and shows as "+1e6"; engages when
 *                       the ticks share leading digits, saving at least
 *                       axes.formatter.offset_threshold (4) digits, all same sign.
 *
 * `locs` are the tick positions and vmin/vmax the view; only visible ticks count.
 */
struct TickScaling
{
    double offset = 0.0;     /* subtracted from every label */
    int order_of_magnitude = 0; /* labels are divided by 10^this */

    bool engaged() const { return offset != 0.0 || order_of_magnitude != 0; }
};

/* Caller-adjustable settings (matplotlib's ticklabel_format plus the two
   rcParams behind its defaults). */
struct TickFormatOptions
{
    bool use_offset = true; /* axes.formatter.useoffset */
    bool scientific = true; /* ticklabel_format(style='plain') turns this off */
    int sci_limit_lo = -5;  /* axes.formatter.limits */
    int sci_limit_hi = 6;
};

TickScaling tick_scaling(const std::vector<double> &locs, double vmin, double vmax,
                         const TickFormatOptions &opts = TickFormatOptions());

/* The labels, with the scaling applied. */
std::vector<std::string> format_ticks_scaled(const std::vector<double> &locs,
                                             const TickScaling &scale);

/* What is written at the end of the axis: "1e6", "+1e6", or both. Empty when
   neither mechanism engaged. */
std::string tick_offset_text(const TickScaling &scale);

/* Labels for log ticks (matplotlib's default LogFormatterSciNotation renders
   these as mathtext superscripts). */
std::vector<std::string> format_log_ticks(const std::vector<double> &locs);

} // namespace vpl

#endif /* VPL_CORE_TICKER_H */
