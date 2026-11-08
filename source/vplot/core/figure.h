/*
 * Figure, Axes and Line2D.
 *
 * A minimal stand-in for matplotlib's figure.py / axes/_base.py / lines.py:
 * enough structure to draw line plots with markers, a frame, grid, ticks,
 * labels and a legend. Defaults are taken from matplotlib's
 * mpl-data/matplotlibrc.
 */

#ifndef VPL_CORE_FIGURE_H
#define VPL_CORE_FIGURE_H

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "colormap.h"
#include "delaunay.h"
#include "geometry.h"
#include "markers.h"
#include "path.h"
#include "renderer.h"
#include "spectral.h"
#include "ticker.h" /* TickFormatOptions, on the Axes below */

namespace vpl
{

/* matplotlib's default property cycle, 'tab10'. */
const Color *tab10_palette();
std::size_t tab10_size();

enum class ScaleType
{
    Linear,
    Log,
};

/* Where fill_between puts the step when it fills a staircase rather than sloped
   segments -- matplotlib's step={'pre','post','mid'}, plus None for the slope. */
enum class FillStep
{
    None,
    Pre,
    Post,
    Mid,
};

/* hist's histtype: touching bars (side by side for several datasets), bars
   stacked on each other, an unfilled outline, or a filled outline. */
enum class HistType
{
    Bar,
    BarStacked,
    Step,
    StepFilled,
};

/* hist's align: where each bar sits relative to its bin. */
enum class HistAlign
{
    Mid,
    Left,
    Right,
};

/*
 * Everything hist takes beyond the data -- matplotlib's keyword arguments.
 * The defaults reproduce hist(x).
 */
struct HistOptions
{
    /* Equal-width bins over the range, unless explicit edges are given. */
    std::size_t bins = 10;
    /* Monotonically increasing bin edges; overrides bins and range. */
    const double *edges = nullptr;
    std::size_t edge_count = 0;

    /* The span the equal bins cover. Unset means the data's own range. */
    bool has_range = false;
    double range_lo = 0.0;
    double range_hi = 0.0;

    bool density = false;
    /* 0 off, positive accumulates left to right, negative right to left. */
    int cumulative = 0;
    HistType type = HistType::Bar;
    HistAlign align = HistAlign::Mid;
    /* orientation='horizontal': bars along x, bins up y. */
    bool horizontal = false;
    /* Bar width as a fraction of the bin; NaN for matplotlib's automatic
       choice (0.8 for several side-by-side datasets, 1 otherwise). */
    double rwidth = std::numeric_limits<double>::quiet_NaN();
    bool stacked = false;
    double bottom = 0.0;
    /* log=True: the value axis becomes a log scale (clipping non-positive
       values), as bar(log=True) and the step branch both do. */
    bool log = false;
};

/* violinplot's keyword arguments. The defaults reproduce violinplot(data). */
struct ViolinOptions
{
    /* One per group; null for 1, 2, 3, ... */
    const double *positions = nullptr;
    /* One per group; null for `width` everywhere. */
    const double *widths = nullptr;
    double width = 0.5;
    bool showmeans = false;
    bool showextrema = true;
    bool showmedians = false;
    /* quantiles: `quantile_counts[g]` fractions per group, laid end to end in
       `quantiles`; null for none. */
    const double *quantiles = nullptr;
    const std::size_t *quantile_counts = nullptr;
    std::size_t points = 100;
    /* bw_method: Scott's rule, Silverman's, or a fixed factor (> 0). */
    enum class Bandwidth { Scott, Silverman, Scalar } bandwidth = Bandwidth::Scott;
    double bw_factor = 0.0;
    /* side: both halves, or only the low or high one. */
    enum class Side { Both, Low, High } side = Side::Both;
    bool horizontal = false;
    /* Unset means the next line-cycle colour (the body at 30% alpha). */
    std::vector<Color> facecolors;
    std::vector<Color> linecolors;
};

/* hexbin's reduce_C_function, for the reductions worth naming. */
enum class HexReduce
{
    Mean,
    Sum,
    Max,
    Min,
    Median,
    Std,
};

/* hexbin's keyword arguments. The defaults reproduce hexbin(x, y). */
struct HexbinOptions
{
    std::size_t nx = 100;
    /* 0 means nx / sqrt(3), which keeps the hexagons regular. */
    std::size_t ny = 0;
    /* One value per point to reduce per hexagon instead of counting. */
    const double *C = nullptr;
    HexReduce reduce = HexReduce::Mean;
    /* Hide hexagons with fewer points than this; 0 is mincnt=None (which
       means 1 once C is given). */
    std::size_t mincnt = 0;
    bool has_extent = false;
    double extent[4] = {0.0, 0.0, 0.0, 0.0}; /* xmin, xmax, ymin, ymax */
    Colormap cmap = Colormap::Viridis;
    bool has_vlimits = false;
    double vmin = 0.0;
    double vmax = 0.0;
    /* bins='log': a LogNorm, under which empty hexagons are not painted. */
    bool log_bins = false;
    /* bins=N: colour by which of N equal value bins a hexagon falls in. */
    std::size_t bins = 0;
    /* xscale/yscale='log': bin in log10 space (the extent is in log10 units
       too) and switch the axis to a log scale. */
    bool xlog = false;
    bool ylog = false;
    /* marginals=True: a strip of bars along the bottom and the left edge,
       each the reduction of C (ones without C) over an x or y bin. */
    bool marginals = false;
};

/* contourf's extend: which ends of the level range get an open-ended band
   reaching past the outermost level, coloured with the colormap's extremes. */
enum class ContourExtend
{
    Neither,
    Min,
    Max,
    Both,
};

/* pie's keyword arguments. The defaults reproduce pie(x). */
struct PieOptions
{
    /* Per-wedge offset along its middle angle, in radii; null for none. */
    const double *explode = nullptr;
    /* Wedge colours, cycled; empty means the patch cycle. */
    std::vector<Color> colors;
    double startangle = 0.0; /* degrees, from 3 o'clock */
    double radius = 1.0;
    bool counterclock = true;
    /* normalize=False takes the values as fractions of the whole circle. */
    bool normalize = true;
    double center_x = 0.0;
    double center_y = 0.0;
    /* frame=True keeps the axes and autoscales to the pie instead of hiding
       the frame and pinning the limits at +/-1.25 around the centre. */
    bool frame = false;

    /* wedgeprops: width > 0 makes each wedge a ring segment of that radial
       width (a donut); an edge colour strokes the wedges at `linewidth`;
       alpha applies to the wedges. */
    double width = 0.0;
    bool has_edgecolor = false;
    Color edgecolor;
    double linewidth = 1.0; /* patch.linewidth */
    double alpha = 1.0;

    /* shadow=True (or a dict): a darkened copy of each wedge behind the pie,
       at alpha 0.5, shifted by (ox, oy) data units scaled by dpi / 72. */
    bool shadow = false;
    double shadow_ox = -0.02;
    double shadow_oy = -0.02;
    double shadow_shade = 0.7;
};

/* pie_label's alignment: centred on the point, or pushed outwards from it
   (left-aligned on the right half, right-aligned on the left). Auto is outer
   past distance 1 and centred inside it. */
enum class PieLabelAlign
{
    Auto,
    Center,
    Outer,
};

/* Where a stackplot puts its baseline -- matplotlib's baseline argument. Zero
   is a plain stack; the other three are streamgraph layouts. */
enum class StackBaseline
{
    Zero,
    Sym,
    Wiggle,
    WeightedWiggle,
};

/* Which gridlines a grid() call touches -- matplotlib's which and axis. */
enum class GridWhich
{
    Major,
    Minor,
    Both,
};
enum class GridAxis
{
    Both,
    X,
    Y,
};

/*
 * Whether the grid and tick marks sit above or below most artists --
 * axisbelow. matplotlib's three states, by zorder:
 *
 *   On   (True,  zorder 0.5) -- below patches AND lines. Still above images.
 *   Line (default, zorder 1.5) -- above patches (bars, fills), below lines
 *        and markers. axes.axisbelow's rcParam default.
 *   Off  (False, zorder 2.5) -- above everything.
 */
enum class AxisBelow
{
    On,
    Line,
    Off,
};

/* bar_label's label_type: at the bar's tip (edge, the default) or centred
   inside it, the latter for a stacked bar's segment length. */
enum class BarLabelType
{
    Edge,
    Center,
};

/*
 * matplotlib's ten legend positions, in its own numeric order. Each names a
 * compass point of the axes box, shrunk inwards by legend.borderaxespad
 * (offsetbox._get_anchored_bbox).
 */
enum class LegendLoc
{
    /* matplotlib's default: whichever position the data covers least. */
    Best,
    UpperRight,
    UpperLeft,
    LowerLeft,
    LowerRight,
    Right,
    CenterLeft,
    CenterRight,
    LowerCenter,
    UpperCenter,
    Center,
};

struct Line2D
{
    std::vector<double> xdata;
    std::vector<double> ydata;

    Color color = Color::rgb(0.121569, 0.466667, 0.705882); /* C0, #1f77b4 */
    double linewidth = 1.5;                                 /* lines.linewidth */
    LineStyle linestyle = LineStyle::Solid;

    MarkerStyle marker = MarkerStyle::None;
    double markersize = 6.0;      /* lines.markersize, points */
    double markeredgewidth = 1.0; /* lines.markeredgewidth */
    /* Unset means "follow the line colour", which is what matplotlib's
       markerfacecolor/markeredgecolor default of "auto" resolves to. */
    bool has_markerfacecolor = false;
    Color markerfacecolor;
    bool has_markeredgecolor = false;
    Color markeredgecolor;

    std::string label;

    /* Error bars, empty when there are none. matplotlib models these as a
       separate LineCollection inside an ErrorbarContainer. `yerr` is the
       downward extent; `yerr_upper` the upward one, or empty for symmetric
       bars (likewise for x: left, then right). */
    std::vector<double> yerr;
    std::vector<double> xerr;
    std::vector<double> yerr_upper;
    std::vector<double> xerr_upper;
    double capsize = 0.0;    /* errorbar.capsize, points; 0 means no caps */
    double elinewidth = 0.0; /* 0 follows the line's own width, as errorbar does */
    /* ecolor: unset means the line's own colour. */
    bool has_ecolor = false;
    Color ecolor;
    /* capthick: negative means markeredgewidth, the errorbar.capthick None. */
    double capthick = -1.0;
    /* errorevery=(offset, step): bars only on every step-th point from offset. */
    std::size_t errorevery_offset = 0;
    std::size_t errorevery_step = 1;
    /* barsabove: the bars over the data line rather than under it. */
    bool barsabove = false;
    /* elinestyle: the bars' own line style. */
    LineStyle elinestyle = LineStyle::Solid;
    /* lolims/uplims (and xlolims/xuplims): per-point flags, empty for none.
       A limited point's bar runs one way only -- lolims drops the lower
       extent -- and ends in a caret pointing on along the bar. */
    std::vector<std::uint8_t> lolims;
    std::vector<std::uint8_t> uplims;
    std::vector<std::uint8_t> xlolims;
    std::vector<std::uint8_t> xuplims;
    /* fmt='none': only the error bars are drawn, and only they count towards
       the limits. */
    bool errorbars_only = false;

    /* solid_capstyle='butt', as boxplot's medians and mean lines have it,
       instead of lines.solid_capstyle's projecting end. */
    bool butt_cap = false;

    /* Whether point i carries an error bar under errorevery. */
    bool error_drawn(std::size_t i) const
    {
        return i >= errorevery_offset && (i - errorevery_offset) % errorevery_step == 0;
    }
    static bool flag(const std::vector<std::uint8_t> &v, std::size_t i)
    {
        return i < v.size() && v[i] != 0;
    }
    /* The bar's extents below and above point i (or left and right). A limit
       zeroes the side it removes, as matplotlib's low/high do. */
    double yerr_lo(std::size_t i) const { return flag(lolims, i) ? 0.0 : std::fabs(yerr[i]); }
    double yerr_hi(std::size_t i) const
    {
        return flag(uplims, i) ? 0.0 : std::fabs(yerr_upper.empty() ? yerr[i] : yerr_upper[i]);
    }
    double xerr_lo(std::size_t i) const { return flag(xlolims, i) ? 0.0 : std::fabs(xerr[i]); }
    double xerr_hi(std::size_t i) const
    {
        return flag(xuplims, i) ? 0.0 : std::fabs(xerr_upper.empty() ? xerr[i] : xerr_upper[i]);
    }

    /* Dash pattern in points, resolved from linestyle and linewidth. */
    std::vector<std::pair<double, double>> dashes() const
    {
        return dash_pattern(linestyle, linewidth);
    }
};

/* A filled polygon in data coordinates: fill_between regions and bars. */
struct PolyPatch
{
    Path path;
    Color facecolor = Color::rgb(0.121569, 0.466667, 0.705882); /* patch.facecolor C0 */
    Color edgecolor = Color::rgb(0.0, 0.0, 0.0);                /* patch.edgecolor */
    /* Patches do not draw edges by default (patch.force_edgecolor is False), so
       this starts at zero rather than patch.linewidth. */
    double linewidth = 0.0;
    double alpha = 1.0;

    /* Patches mitre, Collections round: Patch's default joinstyle is "miter"
       and Collection's is "round". The difference shows on sharp corners,
       where a mitre throws a long spike. */
    JoinStyle join = JoinStyle::Miter;

    /* Off for a mesh of abutting cells: antialiasing a shared edge leaves a
       pale seam where the two half-covered boundary pixels composite lighter
       than either. matplotlib does the same (tripcolor's PolyCollection sets
       antialiased=False; hexbin's stays True since its cells do not abut). */
    bool antialiased = true;

    /*
     * Whether this shape counts towards the axes' data limits. Almost
     * everything does; an inset indicator does not, matching matplotlib's
     * InsetIndicator, which is a plain Artist and never reaches dataLim.
     */
    bool counts_towards_limits = true;

    /*
     * Drawn with the lines (zorder 2) rather than the patches (zorder 1). A
     * step histogram's outline is a Polygon that matplotlib lifts to
     * Line2D.zorder so it stays visible over filled shapes.
     */
    bool line_tier = false;

    /* A shift applied at draw time, in data units per point of dpi/72 --
       a pie Shadow's (ox, oy), which matplotlib scales by points_to_pixels. */
    double shift_pt_x = 0.0;
    double shift_pt_y = 0.0;
    /* A coordinate in axes fraction (0..1 across the frame) rather than in
       data, per direction -- the blended get_xaxis_transform('grid') that
       hexbin's marginal bars use. Such a patch never counts towards limits. */
    bool x_in_axes = false;
    bool y_in_axes = false;
    /* clip_on: clipped to the axes box. A pie's wedges are not, so an
       exploded wedge can reach past the frame. */
    bool clip = true;

    std::string label;
};

/*
 * A scatter collection: markers whose colour and size can vary per point,
 * which a Line2D cannot express. matplotlib draws these through PathCollection.
 */
struct ScatterCollection
{
    std::vector<double> xdata;
    std::vector<double> ydata;

    /* Marker area in points squared, matching scatter's `s`: the diameter is
       its square root, so the default of 36 gives the same 6-point marker
       plot() draws. Empty means uniform at `size`; one entry per point
       otherwise. */
    std::vector<double> sizes;
    double size = 36.0;

    /* Empty means uniform at `color`; one entry per point otherwise. */
    std::vector<Color> colors;
    Color color = Color::rgb(0.121569, 0.466667, 0.705882);

    MarkerStyle marker = MarkerStyle::Circle;
    Color edgecolor = Color::rgb(0.0, 0.0, 0.0);
    bool has_edgecolor = false;
    /* patch.linewidth, what a Collection falls back to -- NOT lines.linewidth
       or lines.markeredgewidth. Since scatter's edgecolors default to "face",
       this stroke adds to the marker's apparent size. */
    double linewidth = 1.0;
    double alpha = 1.0;
    std::string label;

    Color color_at(std::size_t i) const
    {
        return colors.empty() ? color : colors[i % colors.size()];
    }
    double size_at(std::size_t i) const
    {
        return sizes.empty() ? size : sizes[i % sizes.size()];
    }
};

/*
 * A 2D scalar field drawn as coloured cells -- heatmaps, matrices, sampled
 * functions.
 *
 * Emitted as one filled quad per cell rather than a resampled raster,
 * differing from matplotlib's imshow: quads keep SVG and PDF output vector and
 * need no image embedding. A large photographic array would produce a huge
 * file; a raster path through RendererAgg::draw_image is not implemented yet.
 */
struct ImageGrid
{
    /* Row-major, `rows` x `cols`. Row 0 is drawn at the TOP, matching imshow's
       default origin='upper'. */
    std::vector<double> values;
    std::size_t rows = 0;
    std::size_t cols = 0;

    /* Data-space extent: left, right, bottom, top -- imshow's `extent`. */
    double x0 = 0.0;
    double x1 = 1.0;
    double y0 = 0.0;
    double y1 = 1.0;

    /*
     * Explicit cell boundaries -- pcolormesh, where the grid need not be
     * regular. cols + 1 and rows + 1 entries when set; empty means the cells
     * are evenly spaced across the extent above, which is what imshow assumes.
     */
    std::vector<double> xedges;
    std::vector<double> yedges;

    Colormap cmap = Colormap::Viridis;
    /* Unset means autoscale to the data's own min and max, as Normalize does. */
    bool has_vlimits = false;
    double vmin = 0.0;
    double vmax = 1.0;
    double alpha = 1.0;
    std::string label;

    double value_at(std::size_t r, std::size_t c) const
    {
        return values[r * cols + c];
    }
};

/*
 * An image painted straight onto the figure's pixels, bypassing every axes --
 * figimage. One array element is one device pixel, never resampled: for logos,
 * watermarks and pre-rendered rasters rather than data on a scale.
 *
 * `xo` is the left edge in device pixels and `yo` the bottom edge, counted from
 * the lower-left corner as matplotlib does. origin='upper' (the default) draws
 * row 0 at the TOP of the block, matching imshow.
 */
struct FigureImage
{
    std::vector<double> values; /* row-major, rows x cols */
    std::size_t rows = 0;
    std::size_t cols = 0;
    double xo = 0.0;
    double yo = 0.0;
    bool origin_upper = true;
    Colormap cmap = Colormap::Viridis;
    bool has_vlimits = false;
    double vmin = 0.0;
    double vmax = 1.0;
    double alpha = 1.0;

    double value_at(std::size_t r, std::size_t c) const
    {
        return values[r * cols + c];
    }
};

/*
 * Iso-lines through a 2D scalar field: contour().
 *
 * The segments are extracted once by marching squares and cached, because the
 * field does not change once set -- only the view does.
 */
/*
 * Where a table sits relative to the axes -- matplotlib's eighteen codes. The
 * first ten place it INSIDE the axes, inset by AXESPAD; the next seven place
 * it outside, against one edge. `Bottom` is the default: under the plot,
 * centred.
 */
enum class TableLoc
{
    Best = 0,
    UpperRight,
    UpperLeft,
    LowerLeft,
    LowerRight,
    CenterLeft,
    CenterRight,
    LowerCenter,
    UpperCenter,
    Center,
    TopRight,
    TopLeft,
    BottomLeft,
    BottomRight,
    Right,
    Left,
    Top,
    Bottom
};

/*
 * A grid of text cells drawn in AXES coordinates -- matplotlib's table.
 *
 * Proportional to the axes rather than the data, so it neither moves with a
 * pan nor counts towards the limits. Row height comes from the font size, not
 * from measuring the text: fontsize/72 * dpi / axes_height * 1.2, matplotlib's
 * _approx_text_height. Only the row-label column is measured, when labels are
 * given.
 */
struct Table
{
    std::size_t rows = 0;
    std::size_t cols = 0;
    /* rows * cols, row-major. */
    std::vector<std::string> cells;

    /* A header row above the data, and a header column to its left. Either may
       be empty for "none"; when present they must be cols and rows long. */
    std::vector<std::string> col_labels;
    std::vector<std::string> row_labels;

    /* One per column, in axes units. Empty means 1/cols each. */
    std::vector<double> col_widths;

    double fontsize = 10.0;
    TableLoc loc = TableLoc::Bottom;

    /* matplotlib's defaults, and they differ per region: data cells are right
       aligned, row labels left, column labels centred. */
    HAlign cell_loc = HAlign::Right;
    HAlign row_loc = HAlign::Left;
    HAlign col_loc = HAlign::Center;

    Color edgecolor = Color::rgb(0.0, 0.0, 0.0);
    Color facecolor = Color::rgb(1.0, 1.0, 1.0);
    double linewidth = 1.0;
};

/*
 * One box's worth of summary -- what matplotlib's boxplot_stats produces and
 * what bxp consumes. `whislo` and `whishi` are where the whiskers END: for
 * boxplot's default rule, the furthest observation within 1.5 IQR of the
 * nearer quartile, a real datum rather than the 1.5 IQR bound itself.
 */
/* Which part of the arrow sits on the data point. */
enum class QuiverPivot
{
    Tail,
    Middle,
    Tip
};

/* quiver's units and scale_units: what one unit is, in pixels --
   matplotlib's Quiver._dots_per_unit. */
enum class QuiverUnits
{
    Width,  /* the axes width */
    Height, /* the axes height */
    Dots,   /* one pixel */
    Inches,
    X,      /* one x data unit */
    Y,      /* one y data unit */
    XY,     /* the view's diagonal, as data; as scale_units, the data itself */
};

/* quiver's angles: from (u, v) as given, from the arrow's (x, y) to
   (x + u, y + v) on screen, or fixed degrees per arrow. */
enum class QuiverAngles
{
    UV,
    XY,
    Fixed,
};

/*
 * A field of arrows -- quiver.
 *
 * An arrow's shape is defined in "arrow width units", 1 being the shaft width;
 * the head is 5 long and 3 wide. The shaft WIDTH is a fraction of the axes
 * width and the LENGTH is the vector magnitude over a scale derived from the
 * mean magnitude, so both depend on the on-screen axes size and cannot be
 * resolved before draw time.
 *
 * The auto constants are matplotlib's: width = 0.06 / clip(sqrt(N), 8, 25) and
 * scale = 1.8 * mean|uv| * max(10, sqrt(N)). `scale` and `width` override them.
 */
struct QuiverField
{
    std::vector<double> x;
    std::vector<double> y;
    std::vector<double> u;
    std::vector<double> v;

    /* Unset means the crude auto-scaling above. Larger `scale` means SHORTER
       arrows -- it is the data units per arrow-width-unit of length. */
    bool has_scale = false;
    double scale = 0.0;
    /* Shaft width as a fraction of the axes width. */
    bool has_width = false;
    double width = 0.0;

    Color color = Color::rgb(0.0, 0.0, 0.0);
    /* Per-arrow colour: quiver(X, Y, U, V, C) maps `c` through `cmap`
       (normalised over C unless vmin/vmax are given); `colors` is an explicit
       colour per arrow. Either takes over from `color` when non-empty. */
    std::vector<double> c;
    Colormap cmap = Colormap::Viridis;
    bool has_vlimits = false;
    double vmin = 0.0;
    double vmax = 0.0;
    std::vector<Color> colors;
    QuiverPivot pivot = QuiverPivot::Tail;

    /* units= (what width and the arrow geometry are measured in),
       scale_units= (unset: arrow units) and angles=. */
    QuiverUnits units = QuiverUnits::Width;
    bool has_scale_units = false;
    QuiverUnits scale_units = QuiverUnits::Width;
    QuiverAngles angles = QuiverAngles::UV;
    std::vector<double> angle_degrees; /* angles=Fixed, one per arrow */

    /* In shaft widths. matplotlib's defaults; head is 5 long, 3 wide, and the
       barb where the head meets the shaft is 4.5 along. */
    double headwidth = 3.0;
    double headlength = 5.0;
    double headaxislength = 4.5;
    /* Below minshaft * headlength the whole arrow shrinks rather than the head
       eating the shaft; below minlength it degenerates to a dot. */
    double minshaft = 1.0;
    double minlength = 1.0;

    std::string label;

    /* Resolved at draw time and stored so quiverkey can scale its key arrow
       exactly like the field it labels. */
    mutable double resolved_scale = 0.0;
    mutable double resolved_width = 0.0;
    mutable double resolved_trans_scale = 0.0;   /* pixels per arrow unit */
    mutable double resolved_widthu_per_lenu = 1.0;
};

/*
 * Wind barbs -- barbs.
 *
 * A meteorological convention: the staff points INTO the wind, and magnitude
 * is spelled out in tick marks rather than length. Every staff is the same
 * length; what varies is how many flags, full barbs and half barbs hang off
 * it, at 50, 10 and 5 units each. Magnitudes are rounded to the nearest
 * half-barb before decomposition, as the convention intends.
 */
struct BarbField
{
    std::vector<double> x;
    std::vector<double> y;
    std::vector<double> u;
    std::vector<double> v;

    /* Staff length in barb units; everything else is a fraction of it. */
    double length = 7.0;
    /* What each feature is worth. */
    double half = 5.0;
    double full = 10.0;
    double flag = 50.0;
    /* Round to the nearest half-barb rather than truncating. */
    bool rounding = true;
    /* Put the features on the other side of the staff -- southern hemisphere. */
    bool flip = false;
    /* Fill the circle that stands for a calm reading, rather than outlining it. */
    bool fill_empty = false;
    /* 'tip' puts the data point at the staff's tip; 'middle' at its centre;
       a number puts it that far along the staff (pivot=float). */
    bool pivot_middle = false;
    bool has_pivot_value = false;
    double pivot_value = 0.0;
    /* flip_barb as an array: one flag per barb (overrides `flip`). */
    std::vector<std::uint8_t> flips;

    /* sizes=: fractions of the length. */
    double spacing = 0.125;
    double height = 0.4;
    double width = 0.25;
    double emptybarb = 0.15;

    Color color = Color::rgb(0.0, 0.0, 0.0);
    /* barbcolor / flagcolor. One alone colours everything (edges follow the
       face); both give the edges barbcolor and the faces flagcolor. */
    bool has_barbcolor = false;
    Color barbcolor;
    bool has_flagcolor = false;
    Color flagcolor;
    /* barbs(X, Y, U, V, C): the face colour per barb through `cmap`; a barb
       whose C is not finite is dropped, as matplotlib masks it. */
    std::vector<double> c;
    Colormap cmap = Colormap::Viridis;
    bool has_vlimits = false;
    double vmin = 0.0;
    double vmax = 0.0;
    double linewidth = 1.0;
    std::string label;
};

/*
 * Streamlines through a vector field -- streamplot.
 *
 * Lines are integrated at CALL time: the integrator works in GRID coordinates
 * (0..nx-1, 0..ny-1) and streamline spacing comes from a mask whose resolution
 * is set by `density`, not pixels. Only the arrowheads are sized in points and
 * wait for the draw.
 *
 * Seeding tries mask cells in a spiral from the outside in, so boundary lines
 * are laid down first and the interior fills what is left. A line stops when
 * it leaves the grid, the field goes to zero, or it enters a mask cell another
 * line already claimed.
 */
struct StreamPlot
{
    /* Whole streamlines in DATA coordinates. */
    std::vector<Path> lines;

    struct Arrow
    {
        double tail_x, tail_y;
        double head_x, head_y;
    };
    std::vector<Arrow> arrows;

    Color color = Color::rgb(0.121569, 0.466667, 0.705882); /* C0 */
    double linewidth = 1.5;                                 /* lines.linewidth */
    double arrowsize = 1.0;

    /* The grid's own extent, which streamplot pins the view to exactly --
       matplotlib sets sticky edges here, so there are no 5% margins. */
    double x0 = 0.0, x1 = 1.0, y0 = 0.0, y1 = 1.0;
};

/*
 * The legend arrow for a quiver field -- quiverkey.
 *
 * Drawn at a position in AXES coordinates with a label beneath it, and sized
 * by the field's own scale so that its length means what it says.
 */
struct QuiverKey
{
    /* Index into Axes::quivers. */
    std::size_t field = 0;
    double x = 0.9;
    double y = 0.9;
    /* The magnitude the key arrow stands for. */
    double magnitude = 1.0;
    std::string label;
    double fontsize = 10.0;
};

struct BoxStats
{
    double q1 = 0.0;
    double med = 0.0;
    double q3 = 0.0;
    double whislo = 0.0;
    double whishi = 0.0;
    /* Drawn individually beyond the whiskers. Usually empty. */
    std::vector<double> fliers;
    /* Only needed for showmeans and shownotches; NaN when not known. */
    double mean = std::numeric_limits<double>::quiet_NaN();
    double cilo = std::numeric_limits<double>::quiet_NaN();
    double cihi = std::numeric_limits<double>::quiet_NaN();
};

/* bxp's (and so boxplot's) keyword arguments. Defaults reproduce bxp(stats). */
struct BoxOptions
{
    const double *positions = nullptr; /* one per box; null for 1, 2, ... */
    const double *widths = nullptr;    /* one per box; null for the automatic width */
    bool horizontal = false;
    bool patch_artist = false; /* a filled C0 box instead of an outline */
    bool shownotches = false;
    bool showmeans = false;
    bool meanline = false; /* the mean as a dashed line across the box, not a marker */
    bool showcaps = true;
    bool showbox = true;
    bool showfliers = true;
    bool manage_ticks = true;
    /* Tick labels, one per box; null labels each tick with its position. */
    const std::string *labels = nullptr;
};

struct ContourSet
{
    std::vector<double> values;
    std::size_t rows = 0;
    std::size_t cols = 0;

    double x0 = 0.0;
    double x1 = 1.0;
    double y0 = 0.0;
    double y1 = 1.0;

    std::vector<double> levels;
    Colormap cmap = Colormap::Viridis;
    /* contour.linewidth is unset by default, which falls through to
       lines.linewidth -- not to the 1.0 that patches use. */
    double linewidth = 1.5;
    /* Unset means colour each level by its position in the level range. */
    bool has_color = false;
    Color color;
    std::string label;

    /* One flat path per level, in data coordinates, built by marching squares. */
    std::vector<Path> level_paths;

    /*
     * Filled bands rather than lines -- contourf.
     *
     * There are levels.size() - 1 of them, each covering [levels[i],
     * levels[i+1]] as ONE path holding every cell fragment of that band. One
     * path rather than one per cell avoids a seam: abutting polygons filled in
     * separate passes leave the shared edge pale, while rasterizing them
     * together makes it cancel.
     */
    bool filled = false;
    std::vector<Path> band_paths;
    /* The band boundaries: the levels, plus +/-1e250 at an extended end, as
       matplotlib's _levels. Empty for line contours. */
    std::vector<double> band_levels;
    ContourExtend extend = ContourExtend::Neither;

    /*
     * clabel: contour labels.
     *
     * Only the request is recorded here; placement happens at DRAW time in
     * draw_contours, because clabel's questions are all in pixels (is this line
     * longer than the label, which stretch runs straightest, is there already a
     * label within 1.2 label widths) and the data-to-pixel transform is not
     * known until the axes has a box. matplotlib answers them at clabel() call
     * time, which is why clabel() before tight_layout() leaves labels where the
     * old layout put them.
     */
    bool labeled = false;
    /* font.size. */
    double label_size = 10.0;
    /* Break the line so the label sits in a gap, and how much clear space to
       leave either side of it in points. */
    bool label_inline = true;
    double label_inline_spacing = 5.0;
    /* Never let a label come out upside down. */
    bool rightside_up = true;
    /* Unset means each label takes its own level's line colour. */
    bool label_has_color = false;
    Color label_color;
};

/*
 * A set of disjoint line segments sharing one style -- matplotlib's
 * LineCollection.
 *
 * hlines, vlines, eventplot and the stalks of a stem plot are all this; a
 * Line2D cannot stand in, being one polyline that would join the segments.
 *
 * Draw order caveat: matplotlib sorts one insertion-ordered artist list stably
 * by zorder, while vplot keeps a list per artist type and draws the types in a
 * fixed order. The zorder is right (these are zorder 2, with the lines) but a
 * segment collection is always drawn before the Line2Ds regardless of
 * insertion order, visible only where the two overlap in different colours.
 */
struct SegmentCollection
{
    /* One move_to/line_to pair per segment. */
    Path path;

    /* hlines and friends take colors=None, resolving to lines.color -- a fixed
       C0, NOT the next colour of the property cycle. Calling hlines twice gives
       two blue collections, as in matplotlib. */
    Color color = Color::rgb(0.121569, 0.466667, 0.705882);
    double linewidth = 1.5; /* lines.linewidth */
    LineStyle linestyle = LineStyle::Solid;
    /* A Collection's butt caps, unless asked otherwise (a one-sided violin's
       lines are projecting). */
    CapStyle cap = CapStyle::Butt;
    std::string label;

    std::vector<std::pair<double, double>> dashes() const
    {
        return dash_pattern(linestyle, linewidth);
    }
};

/*
 * The styling a stem() call resolves from its linefmt, markerfmt and basefmt --
 * matplotlib's three format strings -- plus the bottom the stalks rise from.
 * The defaults are matplotlib's: C0 stalks and heads, a C3 baseline, circle
 * heads, and a marker colour that follows the stalk colour unless the markerfmt
 * gives one of its own.
 */
/*
 * triplot's format: the fmt string's colour, line style and marker. Without a
 * colour, the edges and the nodes each take the next colour of the line cycle,
 * as matplotlib's two ax.plot calls do.
 */
struct TriplotStyle
{
    bool has_color = false;
    Color color;
    LineStyle linestyle = LineStyle::Solid;
    MarkerStyle marker = MarkerStyle::None;
};

/* tripcolor's colour mapping. */
struct TripcolorOptions
{
    Colormap cmap = Colormap::Viridis;
    bool has_vlimits = false;
    double vmin = 0.0;
    double vmax = 0.0;
    double alpha = 1.0;
};

struct StemStyle
{
    double bottom = 0.0;
    Color line_color = Color::rgb(0.121569, 0.466667, 0.705882);   /* C0 */
    LineStyle line_style = LineStyle::Solid;
    MarkerStyle marker = MarkerStyle::Circle;
    Color marker_color = Color::rgb(0.121569, 0.466667, 0.705882); /* C0 */
    Color base_color = Color::rgb(0.839216, 0.152941, 0.156863);   /* C3 */
    LineStyle base_style = LineStyle::Solid;
};

/*
 * Free-standing text, optionally with a leader line to the thing it describes.
 *
 * matplotlib splits these into Text and Annotation; the difference is only
 * whether an arrow is drawn, so one artist covers both here.
 */
struct TextArtist
{
    /* Where the text sits, in data coordinates. */
    double x = 0.0;
    double y = 0.0;
    std::string text;

    double size = 10.0; /* font.size */
    Color color = Color::rgb(0.0, 0.0, 0.0);
    double rotation = 0.0;
    /* matplotlib's text() defaults: ha='left', va='baseline'. */
    HAlign halign = HAlign::Left;
    VAlign valign = VAlign::Baseline;

    /* annotate(): the point being pointed AT, in data coordinates. */
    bool has_arrow = false;
    double arrow_x = 0.0;
    double arrow_y = 0.0;
    double arrow_linewidth = 1.0;

    /* bar_label's padding: a shift in POINTS applied to the anchor in DISPLAY
       space after the data->pixel transform, not to (x, y), since a points
       offset has no fixed meaning in view-dependent data units. Zero except
       for bar_label, the only artist that sets it. */
    double offset_x_pt = 0.0;
    double offset_y_pt = 0.0;
};

/*
 * axhspan / axvspan: a band across the whole axes at a constant range in the
 * other coordinate.
 *
 * Kept apart from PolyPatch because one direction is in AXES coordinates (it
 * always spans the frame) while only the other counts towards the data limits.
 * matplotlib expresses this with a blended transform.
 */
struct AxesSpan
{
    bool horizontal = true; /* a band between two y values */
    double lo = 0.0;
    double hi = 0.0;
    Color facecolor = Color::rgb(0.121569, 0.466667, 0.705882);
    double alpha = 1.0;
    std::string label;
};

/* axhline / axvline: a line spanning the axes at a constant data value. Kept
   separate from Line2D because it has to be re-derived from the view on every
   draw rather than carrying fixed endpoints. */
struct ReferenceLine
{
    /* Which kind: a rule across the axes, or an infinite line through a point
       at a given slope -- axhline/axvline versus axline. */
    bool horizontal = true;
    double value = 0.0;

    /*
     * axline: through (px, py) at `slope`, clipped to whatever the view turns
     * out to be. `vertical_slope` covers the case a slope cannot express.
     *
     * Like the other reference lines it does NOT enter the data limits: an
     * infinite line would make autoscaling meaningless.
     */
    bool through = false;
    double px = 0.0;
    double py = 0.0;
    double slope = 1.0;
    bool vertical_slope = false;
    /* axhline builds a plain Line2D, so it takes the Line2D defaults --
       lines.color (which is C0, not black) at lines.linewidth. A reference line
       is not an axis decoration and does not inherit the spine's styling. */
    Color color = Color::rgb(0.121569, 0.466667, 0.705882);
    double linewidth = 1.5;
    LineStyle linestyle = LineStyle::Solid;
};

/*
 * Which way a tick points -- matplotlib's {x,y}tick.direction.
 *
 * Not only cosmetic: it moves the tick LABELS too, since matplotlib measures
 * their pad from the spine plus however much of the tick lies outside it
 * (Tick.get_tick_padding). A tick turned inwards pulls its label in by the
 * tick's whole length.
 */
enum class TickDirection
{
    Out,
    In,
    InOut,
};

/*
 * One axis's worth of tick styling -- the part of tick_params that changes what
 * is drawn.
 *
 * Held per axis, as matplotlib does: tick_params(axis='x') leaves y alone, and
 * the xtick.* and ytick.* rcParam families are separate. `label_size` is
 * {x,y}tick.labelsize, NOT axes.labelsize -- they merely share a default of
 * 'medium'.
 */
struct TickParams
{
    double size = 3.5;        /* {x,y}tick.major.size, points */
    double width = 0.8;       /* {x,y}tick.major.width */
    double pad = 3.5;         /* {x,y}tick.major.pad */
    double label_size = 10.0; /* {x,y}tick.labelsize, 'medium' = font.size */
    TickDirection direction = TickDirection::Out;

    bool ticks_visible = true;  /* tick_params(bottom=False) */
    bool labels_visible = true; /* tick_params(labelbottom=False) */

    /* tick_params(colors=, labelcolor=) -- two separate rcParams in
       matplotlib ({x,y}tick.color, {x,y}tick.labelcolor), independent of the
       spine's own edgecolor. Both default black, the same as those rcParams'
       own defaults. */
    Color color = Color::rgb(0.0, 0.0, 0.0);
    Color label_color = Color::rgb(0.0, 0.0, 0.0);

    /* How far a tick of this length reaches OUTSIDE the spine. This is the
       quantity the label pad is measured from, and the quantity the layout has
       to reserve room for -- Tick.get_tick_padding. */
    double outward(double length) const
    {
        switch (direction) {
            case TickDirection::In:
                return 0.0;
            case TickDirection::InOut:
                return length / 2.0;
            case TickDirection::Out:
            default:
                return length;
        }
    }

    /* And how far inside, which is the rest of it. */
    double inward(double length) const { return length - outward(length); }
};

class Axes
{
  public:
    /* Position as a fraction of the figure, matching
       figure.subplot.{left,bottom,right,top}. */
    Bbox position{0.125, 0.11, 0.9, 0.88};

    Color facecolor = Color::rgb(1.0, 1.0, 1.0); /* axes.facecolor */
    Color edgecolor = Color::rgb(0.0, 0.0, 0.0); /* axes.edgecolor */
    double linewidth = 0.8;                      /* axes.linewidth */

    /* Tick styling, one set per axis -- see TickParams. */
    TickParams xtick;
    TickParams ytick;

    double axis_label_size = 10.0; /* axes.labelsize, 'medium' = font.size */
    double legend_size = 10.0;     /* legend.fontsize, 'medium' = font.size */
    double title_size = 12.0;      /* axes.titlesize 'large' = 1.2 * font.size */
    double label_pad = 4.0;        /* axes.labelpad, points */
    double title_pad = 6.0;        /* axes.titlepad, points */

    /* axes.{x,y}margin: the 5% breathing room around the data. */
    double xmargin = 0.05;
    double ymargin = 0.05;

    /*
     * Skip the margins entirely -- matplotlib's autoscale_view(tight=True).
     * Set by artists that fill a region rather than trace one: a contour or
     * image ends exactly at its grid, since a 5% pad would imply the field
     * extends further than it does.
     */
    bool tight_view = false;

    /*
     * Data units per pixel to hold, as set_aspect does. Zero means 'auto' --
     * the box just fills the position it was given.
     *
     * imshow sets 1.0 (image.aspect defaults to 'equal'), making a square cell
     * square: the drawn box is shrunk inside its position until the ratio holds
     * and centred in what is left over.
     */
    double aspect = 0.0;

    /*
     * set_box_aspect: the BOX's own height/width in physical units, whatever
     * the data says. Zero means unset.
     *
     * Not the same as `aspect` above, which asks for square DATA units and so
     * depends on the view. box_aspect of 1 gives a square panel whatever it
     * holds.
     */
    double box_aspect = 0.0;

    /* set_adjustable: false is 'box' (shrink the panel to the aspect, the
       default and the only mode drawn); true would be 'datalim'. Stored so the
       getter can report it; the drawing path only honours 'box'. */
    bool adjustable_datalim = false;

    /*
     * Where a shrunk box sits inside the position it was given -- set_anchor.
     * (0, 0) is lower left and (1, 1) upper right; matplotlib spells these 'SW'
     * through 'NE' and defaults to 'C'. Only matters once an aspect above
     * shrinks the box.
     */
    double anchor_x = 0.5;
    double anchor_y = 0.5;
    /* The name the anchor was set by ('C', 'SW', ...), or empty when it was
       set as a pair -- get_anchor hands back whichever form it was given. */
    std::string anchor_code = "C";

    /*
     * locator_params(nbins=...): how many intervals the tick locator may use,
     * instead of deriving it from how much room the axis has. Zero keeps the
     * derived value, which is what matplotlib's 'auto' does.
     */
    int x_nbins = 0;
    int y_nbins = 0;

    /*
     * set_prop_cycle: the properties plot(), bar() and the rest draw from, in
     * order. Empty colours means matplotlib's tab10; the other three empty
     * means "not part of this cycle", as omitting a property from the cycler
     * does.
     *
     * One colour list serves both the line and patch cycles (line_cycle and
     * patch_cycle below index it independently). linestyle/marker/linewidth
     * apply only through the LINE cycle, since matplotlib's cycler feeds those
     * only to Line2D.
     */
    struct PropCycle
    {
        std::vector<Color> colors;
        std::vector<LineStyle> linestyles;
        std::vector<MarkerStyle> markers;
        std::vector<double> linewidths;
    };
    PropCycle prop_cycle;

    /*
     * Values the margin may not expand past -- matplotlib's Artist.sticky_edges.
     * A bar chart's baseline is the case that matters: the bars sit ON the
     * axis, a clamp against a specific value rather than a smaller margin.
     */
    std::vector<double> sticky_x;
    std::vector<double> sticky_y;

    /* use_sticky_edges: when a caller turns it off, the view ignores the sticky
       values and the margin applies everywhere -- matplotlib's escape hatch for
       putting air under a bar chart. */
    bool use_sticky_edges = true;

    /*
     * Corners pushed into the data limits by hand -- matplotlib's
     * update_datalim, for an artist whose limits are not the extent of what it
     * draws. eventplot is the case: its marks span half a linelength either
     * side of the offset, but the limits it reports run a WHOLE linelength
     * either side, so the rug never reaches the frame.
     */
    std::vector<Bbox> extra_datalim;

    ScaleType xscale = ScaleType::Linear;
    ScaleType yscale = ScaleType::Linear;

    /*
     * set_xticks / set_yticks: a fixed list that replaces the locator's output
     * entirely, and optional labels that replace the formatter's.
     *
     * Setting ticks does NOT move the limits -- matplotlib installs a
     * FixedLocator and leaves the view alone, so a tick outside the view is
     * simply not drawn.
     */
    std::vector<double> xticks_fixed;
    std::vector<double> yticks_fixed;
    std::vector<std::string> xticklabels_fixed;
    std::vector<std::string> yticklabels_fixed;

    /*
     * Whether the fixed list is in force, which is NOT the same as its being
     * non-empty: set_xticks([]) hides the ticks entirely, and that is how a
     * bare figure or an image without axes is drawn. An empty list with the
     * flag clear means "nobody set any", and the locator runs.
     */
    bool xticks_fixed_set = false;
    bool yticks_fixed_set = false;

    /*
     * Which spines are drawn, as ax.spines[...].set_visible() controls. Hiding
     * the top and right pair is the common case.
     */
    bool spine_left = true;
    bool spine_right = true;
    bool spine_top = true;
    bool spine_bottom = true;

    /*
     * Which side the y ticks and the y label go on. twinx puts the second
     * axes' scale on the right, where it does not collide with the first's.
     */
    bool y_ticks_right = false;

    /*
     * A twinned axes draws neither the x axis nor its own background: the axes
     * it was twinned from owns both, and redrawing them would double the x tick
     * labels and paint over its data.
     */
    /*
     * The whole axis apparatus -- frame, ticks, tick labels, grid, axis labels
     * and title -- as one switch. False is matplotlib's set_axis_off (a single
     * `_axis_on` flag): it leaves the DATA and background alone, so an image or
     * decorative panel keeps its content and loses only the furniture.
     */
    bool axis_on = true;

    /*
     * Whether the grid/ticks draw above or below patches and lines --
     * axisbelow. Default Line, matching axes.axisbelow's rcParam default of
     * 'line'. Independent of axis_on: this only matters when the grid or
     * ticks are visible at all.
     */
    AxisBelow axisbelow = AxisBelow::Line;

    bool x_axis_visible = true;
    bool patch_visible = true;

    /*
     * invert_xaxis / invert_yaxis: the axis runs the other way. A flag rather
     * than a limit swap, because matplotlib's inversion SURVIVES autoscaling --
     * recomputed limits come back inverted too.
     */
    bool x_inverted = false;
    bool y_inverted = false;

    /*
     * Set by twinx/twiny, or directly by sharex/sharey: view_limits() takes
     * this axis's range from the pointed-to axes instead of its own m_xview/
     * m_yview, so re-limiting either one keeps them locked.
     *
     * One-directional (a raw pointer, not a group): sharex makes THIS axes
     * follow the other, matching `ax2 = fig.add_subplot(212, sharex=ax1)`.
     * matplotlib's real sharex is a symmetric N-way group, which this does not
     * implement: set_xlim on the FOLLOWING axes is ignored by view_limits() and
     * does not reach back to the base.
     */
    const Axes *shared_x = nullptr;
    const Axes *shared_y = nullptr;

    /* twiny puts the second x axis along the top, and hides the y axis it does
       not own -- the mirror of what twinx does. */
    bool x_ticks_top = false;

    /* Tick MARKS on both x edges, with the labels still on one -- matshow's
       set_ticks_position('both'), which frames a matrix top and bottom. */
    bool x_ticks_both = false;

    /*
     * Locator overrides. matshow sets all four: whole-number positions only,
     * and matplotlib's [1, 2, 5, 10] steps rather than AutoLocator's
     * [1, 2, 2.5, 5, 10], since 2.5 of a matrix index means nothing.
     */
    /*
     * How the tick labels are allowed to be scaled -- matplotlib's
     * ticklabel_format, per axis.
     *
     * The defaults are the rcParams: an offset when it saves four digits, and
     * scientific notation outside 10^-5 .. 10^6.
     */
    TickFormatOptions x_tick_format;
    TickFormatOptions y_tick_format;

    bool integer_x_ticks = false;
    bool integer_y_ticks = false;
    std::vector<double> x_tick_steps; /* empty: AutoLocator's own */
    std::vector<double> y_tick_steps;
    bool y_axis_visible = true;

    /* {x,y}tick.minor.visible defaults to False; minor.size 2.0, width 0.6. */
    bool minor_x_visible = false;
    bool minor_y_visible = false;
    double minor_tick_size = 2.0;
    double minor_tick_width = 0.6;

    /* axes.grid defaults to False; grid.* supply the styling. Held per axis and
       per major/minor, the way matplotlib holds it, so grid(axis='y') or
       grid(which='minor') can turn on just one set. */
    bool xgrid_major = false;
    bool ygrid_major = false;
    bool xgrid_minor = false;
    bool ygrid_minor = false;
    Color grid_color = Color::rgb(0.690196, 0.690196, 0.690196); /* #b0b0b0 */
    double grid_linewidth = 0.8;
    LineStyle grid_linestyle = LineStyle::Solid;
    double grid_alpha = 1.0;

    /* Turn gridlines on or off for one or both axes and for the major or minor
       ticks -- matplotlib's grid(visible, which, axis). */
    void set_grid(bool visible, GridWhich which = GridWhich::Major,
                  GridAxis axis = GridAxis::Both);

    bool legend_visible = false;
    LegendLoc legend_loc = LegendLoc::Best;
    /* Legend.set_title: a heading centred above the entries on its own row, at
       the entry font size -- legend.title_fontsize's None default and
       legend.fontsize's 'medium' both resolve to font.size, so no separate
       field is needed. Empty means no title row. */
    std::string legend_title;
    /* legend(ncols=): entries flow down each column before starting the
       next, matplotlib's np.array_split -- the first (count % ncols)
       columns get one extra entry, not the last ones. Clamped to at least 1
       and at most the entry count when drawn, so this can be set before
       anything has a label yet. */
    int legend_ncols = 1;

    std::string xlabel;
    std::string ylabel;
    std::string title;

    /*
     * matplotlib keeps TWO property cycles per axes -- Axes._get_lines and
     * Axes._get_patches_for_fill -- and which artist draws from which is not
     * guessable from appearance:
     *
     *   line cycle    plot (one per line), violinplot (one per call),
     *                 stackplot (one per SERIES)
     *   patch cycle   bar, barh, hist, fill_between, fill_betweenx, fill,
     *                 and scatter
     *   neither       stem, boxplot, hlines, vlines, eventplot, stairs,
     *                 broken_barh, axhspan/axvspan, all of which take a fixed
     *                 colour and leave both cycles where they were
     *
     * Advance per cycle draw, not per artist: a boxplot builds a dozen Line2Ds,
     * so counting artists would shift the next plot()'s colour by a dozen.
     */
    std::size_t line_cycle = 0;
    std::size_t patch_cycle = 0;

    Color next_line_color();
    Color next_patch_color();

    /* Held indirectly so that references handed out by plot() -- and the
       handles the C ABI derives from them -- survive later plot() calls. */
    std::vector<std::unique_ptr<Line2D>> lines;
    std::vector<std::unique_ptr<PolyPatch>> patches;
    std::vector<std::unique_ptr<ScatterCollection>> collections;
    std::vector<std::unique_ptr<SegmentCollection>> segment_collections;
    std::vector<ReferenceLine> reference_lines;
    std::vector<AxesSpan> spans;
    std::vector<std::unique_ptr<TextArtist>> texts;
    std::vector<std::unique_ptr<ImageGrid>> images;
    std::vector<std::unique_ptr<ContourSet>> contours;
    std::vector<std::unique_ptr<Table>> tables;
    std::vector<std::unique_ptr<QuiverField>> quivers;
    std::vector<QuiverKey> quiver_keys;
    std::vector<std::unique_ptr<BarbField>> barb_fields;

    /* Wedge angles in degrees and where each wedge sits, left by pie() for
       pie_label() to read. */
    struct PieWedge
    {
        double theta1;
        double theta2;
        double cx = 0.0;
        double cy = 0.0;
        double r = 1.0;
        double frac = 0.0; /* its share of the whole, for autopct */
        double value = 0.0; /* the value as given, for pie_label's {absval} */
    };
    std::vector<PieWedge> pie_wedges;

    /*
     * A line from a corner of the indicator rectangle to the matching corner
     * of the inset axes -- indicate_inset's connectors. One end is in DATA
     * coordinates and the other in FIGURE fractions, so it cannot be an
     * ordinary Line2D, like matplotlib's ConnectionPatch.
     */
    struct InsetConnector
    {
        double data_x, data_y;
        double inset_x, inset_y;
    };
    std::vector<InsetConnector> inset_connectors;
    std::vector<std::unique_ptr<StreamPlot>> streams;

    Line2D &plot(const double *x, const double *y, std::size_t n);

    /* Markers with optionally per-point colour and size. */
    ScatterCollection &scatter(const double *x, const double *y, std::size_t n);

    /* y +/- yerr, drawn as vertical bars through each point. Pass capsize > 0
       for the T-shaped caps; matplotlib's errorbar.capsize default is 0. */
    Line2D &errorbar(const double *x, const double *y, const double *yerr, std::size_t n);

    /* The band between two curves -- confidence intervals, min/max envelopes.
       `where` (null for all points) masks which stretches to fill, and
       `interpolate` puts the filled region's edges on the curves' crossing
       rather than clipping at the last in-mask sample. `step` fills a staircase
       rather than sloped segments, stepping before, after or at the midpoint of
       each x. */
    PolyPatch &fill_between(const double *x, const double *y1, const double *y2,
                            std::size_t n, const std::uint8_t *where = nullptr,
                            bool interpolate = false, FillStep step = FillStep::None);

    /* Vertical bars of the given width, centred on each x. */
    /* `bottom` may be null for the usual bars rising from zero; give it a value
       per bar to stack one series on top of another. */
    PolyPatch &bar(const double *x,
                   const double *height,
                   std::size_t n,
                   double width,
                   const double *bottom = nullptr);

    /*
     * A histogram, as one patch of touching bars.
     *
     * matplotlib's defaults: ten bins spanning the data's own range, bars that
     * meet, and the same zero-anchored autoscale bar() gets. `density` scales
     * the bars to enclose unit area rather than count.
     */
    PolyPatch &hist(const double *values, std::size_t n, std::size_t bins = 10,
                    bool density = false);

    /*
     * The general histogram: one or more datasets binned on shared edges,
     * matplotlib's hist with all its keyword arguments. `weights` may be null,
     * or hold one array (itself possibly null) per dataset. Returns one patch
     * per dataset, in dataset order. `out_tops` receives the bar heights
     * (datasets x bins, after stacking and accumulation) and `out_edges` the
     * bin edges -- the n and bins matplotlib returns.
     */
    std::vector<PolyPatch *> hist(const double *const *datasets,
                                  const std::size_t *counts,
                                  const double *const *weights,
                                  std::size_t ndatasets,
                                  const HistOptions &options,
                                  std::vector<double> *out_tops = nullptr,
                                  std::vector<double> *out_edges = nullptr);

    /* Bars along x instead of up from it: the width is the value and the
       height is the thickness, and the sticky baseline moves to the x axis. */
    PolyPatch &barh(const double *y,
                    const double *width,
                    std::size_t n,
                    double height,
                    const double *left = nullptr);

    /* The band between two curves, with x as the varying pair -- fill_between's
       mirror, for a spread around a vertical trend. `where`, `interpolate` and
       `step` work as they do there. */
    PolyPatch &fill_betweenx(const double *y, const double *x1, const double *x2,
                             std::size_t n, const std::uint8_t *where = nullptr,
                             bool interpolate = false, FillStep step = FillStep::None);

    /*
     * A staircase rather than a straight line between points, at matplotlib's
     * default of "steps-pre": the value changes BEFORE the x it belongs to, so
     * each y reaches back to the previous x.
     */
    /*
     * Where the riser sits relative to the point -- matplotlib's drawstyle.
     *
     *   Pre    the value reaches BACK to the previous x; the riser is at x[i]
     *   Post   the value holds FORWARD to the next x; the riser is at x[i+1]
     *   Mid    the riser is halfway between, so each point sits centred on its
     *          own tread
     */
    enum class StepWhere
    {
        Pre,
        Post,
        Mid,
    };

    Line2D &step(const double *x, const double *y, std::size_t n,
                 StepWhere where = StepWhere::Pre);

    /*
     * The empirical distribution: the fraction of the sample at or below each
     * value, as a post-step -- matplotlib's ecdf.
     *
     * The curve starts at the smallest value with a height of zero and rises by
     * 1/n at each observation, so its first x is repeated. It sticks to 0 and
     * 1, which is why the y axis of an ecdf has no margin at either end.
     */
    Line2D &ecdf(const double *values, std::size_t n, bool complementary = false,
                 const double *weights = nullptr, bool horizontal = false);

    /*
     * A single arrow from (x, y) along (dx, dy) -- matplotlib's arrow, which is
     * a FancyArrow patch and not a line with a marker.
     *
     * The head defaults to three times `width` across and one and a half times
     * that long, and -- with length_includes_head false, as matplotlib defaults
     * -- it is added BEYOND the endpoint, so the arrow is longer than the
     * vector it was given.
     */
    PolyPatch &arrow(double x, double y, double dx, double dy, double width);

    /*
     * Horizontal or vertical rules at many places at once -- matplotlib's
     * hlines/vlines. Unlike axhline these take DATA endpoints rather than
     * spanning the axes, and unlike a Line2D they do not join up.
     */
    SegmentCollection &hlines(const double *y,
                              const double *xmin,
                              const double *xmax,
                              std::size_t n);
    SegmentCollection &vlines(const double *x,
                              const double *ymin,
                              const double *ymax,
                              std::size_t n);

    /* A rug of event marks: one tick per position, `linelength` long and
       centred on `lineoffset`, across the other axis. */
    SegmentCollection &eventplot(const double *positions,
                                 std::size_t n,
                                 double lineoffset,
                                 double linelength,
                                 bool horizontal);

    /*
     * A stem plot, which is three artists rather than one: the stalks, the
     * heads, and a baseline in C3 that spans the x range at the bottom. Returns
     * nothing because there is no single object to hand back -- matplotlib
     * returns a tuple of all three.
     */
    void stem(const double *x, const double *y, std::size_t n, const StemStyle &style = {});

    /*
     * A step outline over bin edges -- matplotlib's stairs. `values` has n
     * entries and `edges` has n + 1.
     *
     * NOT filled by default (StepPatch takes fill=False), so what is drawn is
     * the outline at patch linewidth 1.0, including the drop at each end down
     * to the baseline. The baseline is a sticky edge, so the axis starts there.
     */
    PolyPatch &stairs(const double *values, const double *edges, std::size_t n,
                      bool fill = false, const double *baseline = nullptr,
                      std::size_t baseline_n = 0, bool horizontal = false);

    /* An arbitrary filled polygon, closed for you. Takes the next colour of the
       patch cycle, as matplotlib's fill does. */
    PolyPatch &fill(const double *x, const double *y, std::size_t n);

    /* A row of horizontal bars given as (start, width) pairs, all at the same
       height -- broken_barh, for timelines and Gantt charts. Fixed at C0: it
       does not consume the cycle. */
    PolyPatch &broken_barh(const double *xstart,
                           const double *xwidth,
                           std::size_t n,
                           double y0,
                           double height);

    /*
     * Series stacked one on the next, each filled between the running total
     * below it and its own top. `ys` is nseries rows of n values, row-major.
     * One patch per series, each taking the next colour of the cycle.
     */
    void stackplot(const double *x, const double *ys, std::size_t nseries, std::size_t n,
                   StackBaseline baseline = StackBaseline::Zero);

    /*
     * A box-and-whisker plot per group -- matplotlib's boxplot with its
     * defaults, which is a surprising amount of arithmetic to agree on:
     *
     *   quartiles   numpy's linear interpolation on the sorted values, i.e.
     *               the value at fractional index (n - 1) * q;
     *   whiskers    out to the furthest value still within 1.5 IQR of the
     *               nearer quartile -- NOT to 1.5 IQR itself;
     *   fliers      whatever is left, as unfilled circles;
     *   width       clip(0.15 * (span of positions), 0.15, 0.5), so one box is
     *               narrower than three;
     *   caps        half the box width;
     *   colour      black everywhere except the median, which is C1.
     *
     * `values` is the groups laid end to end and `counts` says how long each
     * one is, since a C ABI has no ragged array. Groups sit at 1..ngroups, and
     * the x limits are pinned to half a unit either side, as matplotlib pins
     * them.
     */
    void boxplot(const double *values, const std::size_t *counts, std::size_t ngroups);
    /* `whis` reaches that many IQRs past the quartiles; with `whis_percentiles`
       (two values, 0..100) the whiskers sit at those percentiles instead. */
    void boxplot(const double *values, const std::size_t *counts, std::size_t ngroups,
                 const BoxOptions &options, double whis = 1.5,
                 const double *whis_percentiles = nullptr);

    /*
     * The same drawing from statistics already computed -- bxp.
     *
     * boxplot is this with a summarizer in front; matplotlib splits them
     * because the five numbers a box needs are often all that survives of the
     * data. Nothing here is recomputed or checked: whislo and whishi are drawn
     * where given, not clipped to 1.5 IQR, and the fliers are whatever the
     * caller says.
     */
    void bxp(const BoxStats *stats, std::size_t ngroups);
    void bxp(const BoxStats *stats, std::size_t ngroups, const BoxOptions &options);

    /*
     * A kernel density estimate per group, mirrored about its position.
     *
     * A Gaussian KDE at Scott's bandwidth (n^(-1/5) times the sample stddev,
     * variance at ddof=1), evaluated at 100 points over each group's range,
     * then scaled so the widest part of every violin is exactly `width` across.
     * Same ragged-array convention as boxplot.
     *
     * The body is the fill at 30% alpha; the extreme bars and the spine between
     * them are full strength. The whole call takes ONE colour from the line
     * cycle, not one per group.
     */
    void violinplot(const double *values, const std::size_t *counts, std::size_t ngroups);
    void violinplot(const double *values, const std::size_t *counts, std::size_t ngroups,
                    const ViolinOptions &options);

    ReferenceLine &axhline(double y);
    ReferenceLine &axvline(double x);

    /* An infinite line through (x, y) at `slope`, clipped to the view --
       matplotlib's axline. Pass vertical=true for a slope of infinity. */
    ReferenceLine &axline(double x, double y, double slope, bool vertical = false);

    /*
     * The height of each bar written above it -- matplotlib's bar_label. Reads
     * the geometry back out of the patch a bar() call returned.
     *
     * `fmt` is a single %-style floating-point conversion (the ABI layer
     * validates it: exactly one specifier consuming a double -- f/F/g/G/e/E,
     * not %s or %n). `label_type` chooses edge (the default: at the bar's tip)
     * or center (the bar's middle, for a stacked segment). `padding` is a
     * points shift, outward from the bar for edge and unused for center.
     */
    void bar_label(const PolyPatch &bars, const std::string &fmt = "%g",
                   BarLabelType label_type = BarLabelType::Edge, double padding = 0.0);

    /* Bands across the axes: axhspan between two y values, axvspan between two
       x values. Each spans the frame in the other direction. */
    AxesSpan &axhspan(double y0, double y1);
    AxesSpan &axvspan(double x0, double x1);

    /* Text at a data position. Set has_arrow and arrow_x/y on the result to turn
       it into an annotation. */
    TextArtist &text(double x, double y, const std::string &s);

    /*
     * A grid of text below (by default) the axes -- matplotlib's table.
     *
     * The returned Table carries the styling: labels, column widths, font size
     * and where it sits. `cells` is rows * cols strings, row-major.
     */
    Table &table(const std::string *cells, std::size_t rows, std::size_t cols);

    /*
     * Keep tick labels and axis labels only on the grid's outer edges --
     * label_outer. Call it on every panel of a shared-scale grid. Tick MARKS
     * stay, tying an inner panel to the scale it shares.
     */
    void label_outer(bool remove_inner_ticks = false);

    /*
     * The rectangle marking what an inset magnifies, plus the two connector
     * lines that read as a magnifier -- indicate_inset.
     *
     * `self_position` is this axes' own box in figure fractions, which the
     * connector-visibility rule needs and an Axes does not otherwise know.
     */
    void indicate_inset(double x, double y, double width, double height,
                        const Axes &inset, const Bbox &self_position);

    /*
     * A label per pie wedge, at `distance` of the radius along the wedge's
     * middle angle -- pie_label. Call it after pie(), which is what leaves the
     * wedge angles behind for it to read.
     */
    void pie_label(const std::string *labels, std::size_t n, double distance = 0.6,
                   double fontsize = 10.0, PieLabelAlign align = PieLabelAlign::Auto,
                   bool rotate = false);

    /*
     * The spectral family. All seven go through one helper -- see
     * core/spectral.h -- with different arguments and different
     * post-processing, exactly as matplotlib's do.
     *
     * `NFFT` is the segment length and `noverlap` how much consecutive
     * segments share; matplotlib's defaults are 256 and 0 (128 for specgram).
     * `Fs` is the sampling frequency, default 2, which puts Nyquist at 1.
     *
     * psd and csd AVERAGE the per-segment periodograms -- Welch's method --
     * which is what makes them estimates rather than one noisy transform.
     */
    Line2D &psd(const double *x, std::size_t n, std::size_t NFFT = 256, double Fs = 2.0,
                std::size_t noverlap = 0, const SpectralOptions &opts = {});
    Line2D &csd(const double *x, const double *y, std::size_t n, std::size_t NFFT = 256,
                double Fs = 2.0, std::size_t noverlap = 0, const SpectralOptions &opts = {});
    /* |Pxy|^2 / (Pxx * Pyy): how much of y is linearly explained by x, per
       frequency. Runs the helper three times, as matplotlib does. */
    Line2D &cohere(const double *x, const double *y, std::size_t n, std::size_t NFFT = 256,
                   double Fs = 2.0, std::size_t noverlap = 0, const SpectralOptions &opts = {});

    /* The three single spectra: the WHOLE signal as one segment, and no
       scaling by frequency -- which is why they are not a one-segment psd. */
    Line2D &magnitude_spectrum(const double *x, std::size_t n, double Fs = 2.0, bool db = false,
                               const SpectralOptions &opts = {});
    Line2D &angle_spectrum(const double *x, std::size_t n, double Fs = 2.0,
                           const SpectralOptions &opts = {});
    /* The angle spectrum unwrapped, so it reads as a continuous curve. */
    Line2D &phase_spectrum(const double *x, std::size_t n, double Fs = 2.0,
                           const SpectralOptions &opts = {});

    /* Power against time and frequency, as an image. `mode` chooses psd
       (default), magnitude, angle or phase; `scale` the dB or linear mapping;
       `xextent` (or null) overrides the time axis span. */
    ImageGrid &specgram(const double *x, std::size_t n, std::size_t NFFT = 256,
                        double Fs = 2.0, std::size_t noverlap = 128,
                        const SpectralOptions &opts = {},
                        SpectralMode mode = SpectralMode::Psd,
                        SpecgramScale scale = SpecgramScale::Default,
                        const double *xextent = nullptr,
                        Colormap cmap = Colormap::Viridis);

    /*
     * Bars side by side within each group -- grouped_bar.
     *
     * `heights` is datasets * groups, row-major by dataset, since C has no
     * ragged array. Groups sit at 0..groups-1. The bar width is DERIVED from
     * the spacing rather than given: one group has to fit every dataset's bar
     * plus the gaps, and both spacings are measured in bar widths.
     *
     * `tick_labels` (groups of them) and `labels` (datasets of them) may be
     * null.
     */
    void grouped_bar(const double *heights, std::size_t datasets, std::size_t groups,
                     const std::string *tick_labels = nullptr,
                     const std::string *labels = nullptr, double group_spacing = 1.5,
                     double bar_spacing = 0.0);

    /*
     * Cross-correlation of two equal-length series -- xcorr -- drawn as a
     * stalk per lag plus a rule along zero.
     *
     * A direct sum, not a spectrum: matplotlib documents this beside psd and
     * csd but implements it with numpy's `correlate`, so it needs no FFT.
     * `normed` divides by sqrt(sum x^2 * sum y^2), which puts an
     * autocorrelation's zero lag at exactly 1.
     */
    /* `detrend` is removed from both series first (mlab.detrend_none, _mean
       or _linear). With usevlines false the correlations are a marker-only
       plot() instead -- that Line2D is returned; otherwise null. */
    Line2D *xcorr(const double *x, const double *y, std::size_t n, int maxlags = 10,
                  bool normed = true, SpectralDetrend detrend = SpectralDetrend::None,
                  bool usevlines = true);

    /* A series against itself -- acorr. */
    Line2D *acorr(const double *x, std::size_t n, int maxlags = 10, bool normed = true,
                  SpectralDetrend detrend = SpectralDetrend::None, bool usevlines = true);

    /*
     * Streamlines through a vector field -- streamplot. `u` and `v` are
     * row-major rows x cols over the grid x[0..cols) by y[0..rows), which must
     * be equally spaced and increasing.
     */
    StreamPlot &streamplot(const double *x, const double *y, const double *u,
                           const double *v, std::size_t rows, std::size_t cols,
                           double density = 1.0);

    /* Wind barbs at (x, y) for the wind (u, v) -- barbs. */
    BarbField &barbs(const double *x, const double *y, const double *u, const double *v,
                     std::size_t n);

    /* A field of arrows at (x, y) pointing along (u, v) -- quiver. Sizing is
       resolved at draw time; see QuiverField. */
    QuiverField &quiver(const double *x, const double *y, const double *u, const double *v,
                        std::size_t n);

    /* A reference arrow for `field`, at (x, y) in AXES coordinates, standing
       for a vector of `magnitude` -- quiverkey. */
    QuiverKey &quiverkey(const QuiverField &field, double x, double y, double magnitude,
                         const std::string &label);

    /* A scalar field as coloured cells. `values` is row-major rows x cols with
       row 0 at the top, as imshow's origin='upper' has it. */
    ImageGrid &imshow(const double *values, std::size_t rows, std::size_t cols);

    /*
     * The same field on an explicit, possibly uneven grid -- pcolormesh.
     *
     * `xedges` has cols + 1 entries and `yedges` rows + 1, and they are cell
     * BOUNDARIES rather than centres. Unlike imshow the y axis is not inverted
     * (row 0 is at the bottom), the aspect is free, and the view ends exactly
     * at the outermost edges.
     */
    ImageGrid &pcolormesh(const double *values, std::size_t rows, std::size_t cols,
                          const double *xedges, const double *yedges);

    /* imshow with a matrix's conventions: the x ticks go along the TOP, where a
       matrix's column indices belong. */
    ImageGrid &matshow(const double *values, std::size_t rows, std::size_t cols);

    /* Where a matrix is non-zero -- its sparsity pattern. Same furniture as
       matshow, but on a white-to-black scale so the blanks read as blanks. */
    ImageGrid &spy(const double *values, std::size_t rows, std::size_t cols);

    /*
     * A two-dimensional histogram: counts over an even grid, drawn as a mesh.
     *
     * The bins span each axis's own data range, as numpy's histogram2d does,
     * and the result is a pcolormesh of the counts -- so an empty bin is drawn
     * in the colormap's lowest colour rather than left blank.
     */
    void hist2d(const double *x, const double *y, std::size_t n,
                std::size_t xbins, std::size_t ybins);

    /*
     * Hexagonal binning: the density of a scatter, as a honeycomb.
     *
     * The tiling is matplotlib's, and it is not one grid but two interleaved --
     * a rectangular lattice plus a second offset by half a cell in both
     * directions. A point goes to whichever centre is nearer under the metric
     * dx^2 + 3 dy^2, and that metric is what makes the cells hexagons rather
     * than rectangles. `gridsize` counts cells across x; the y count follows as
     * gridsize / sqrt(3), which is what keeps the hexagons regular.
     *
     * Empty bins are dropped rather than drawn in the colormap's lowest colour.
     * That is the difference between a hexbin and a 2-D histogram.
     */

    /*
     * An unstructured triangular grid, drawn as its edges -- triplot.
     *
     * The triangulation is computed here, as matplotlib's does when no
     * triangles are given. Each edge is its own segment rather than one
     * polyline with NaN breaks, which would reach the SVG and PDF writers as
     * the literal text "nan".
     */
    SegmentCollection &triplot(const double *x, const double *y, std::size_t n);
    /* triplot(x, y, triangles, fmt): the given (already unmasked) triangles,
       edges as one collection and the nodes as a marker-only line. */
    void triplot(const double *x, const double *y, std::size_t n,
                 const std::vector<Triangle> &tris, const TriplotStyle &style);

    /*
     * Iso-lines over an unstructured triangular grid -- tricontour.
     *
     * Simpler than the structured case: the field is linear across each
     * triangle, so a level either misses it or crosses exactly two edges. There
     * is no saddle to resolve, so unlike marching squares it needs no tie-break
     * rule. The triangulation is computed here, as matplotlib's does when no
     * triangles are given.
     */
    ContourSet &tricontour(const double *x, const double *y, const double *z,
                           std::size_t n, const double *levels, std::size_t nlevels);
    ContourSet &tricontour(const double *x, const double *y, const double *z,
                           std::size_t n, const std::vector<Triangle> &tris,
                           const double *levels, std::size_t nlevels);

    /*
     * Filled bands over the same triangulation -- tricontourf.
     *
     * Unlike the structured contourf, no boundary walking: the field is linear
     * over a triangle, so a band is that triangle clipped by two half-planes,
     * always a single convex polygon.
     */
    ContourSet &tricontourf(const double *x, const double *y, const double *z,
                            std::size_t n, const double *levels, std::size_t nlevels);
    ContourSet &tricontourf(const double *x, const double *y, const double *z,
                            std::size_t n, const std::vector<Triangle> &tris,
                            const double *levels, std::size_t nlevels,
                            ContourExtend extend);

    /*
     * Label the lines of a contour set -- clabel.
     *
     * Marks the set as labelled and records the styling; the placement happens
     * in draw_contours, for the reason given on ContourSet::labeled.
     */
    void clabel(ContourSet &cs);

    /*
     * A scalar field over an unstructured grid, one flat colour per triangle --
     * tripcolor with matplotlib's default shading.
     *
     * Each triangle takes the mean of its three vertex values. Like
     * matplotlib's, this turns the grid off: a mesh of filled cells has its own
     * structure and grid lines only fight it.
     */
    void tripcolor(const double *x, const double *y, const double *z, std::size_t n);
    /* tripcolor over given triangles with one value per triangle (`facevals`,
       parallel to `tris`): the mean of its vertices, or a face colour. The data
       limits take in every point, used or not, as matplotlib's do. */
    void tripcolor(const double *x, const double *y, std::size_t n,
                   const std::vector<Triangle> &tris, const std::vector<double> &facevals,
                   const TripcolorOptions &options);
    void hexbin(const double *x, const double *y, std::size_t n, std::size_t gridsize);
    void hexbin(const double *x, const double *y, std::size_t n, const HexbinOptions &options);

    /*
     * A pie chart. Each value becomes a wedge proportional to its share of the
     * total, starting at 3 o'clock and running counter-clockwise, in the patch
     * cycle's colours.
     *
     * It reconfigures the axes as matplotlib's does -- square aspect, no frame,
     * no ticks, limits pinned at +/-1.25.
     */
    void pie(const double *values, std::size_t n);

    /* pie with its keyword arguments. Values are expected non-negative and
       finite with a positive sum (the C ABI checks); without normalize their
       sum must not exceed 1. */
    void pie(const double *values, std::size_t n, const PieOptions &options);

    /* Iso-lines through a scalar field. Levels are extracted by marching squares
       when this is called, so later changes to `values` are not picked up --
       and the extent has to be known now, since the segments are built in data
       coordinates. Passing all zeros for the extent uses index coordinates. */
    /*
     * Filled contours. Same arguments as contour, and the same levels, but what
     * is drawn is the bands BETWEEN consecutive levels: n levels give n - 1
     * bands, and anything outside [levels.front(), levels.back()] is left
     * unpainted -- matplotlib's extend='neither'.
     */
    ContourSet &contourf(const double *values, std::size_t rows, std::size_t cols,
                         const double *levels, std::size_t nlevels,
                         double x0 = 0.0, double x1 = 0.0,
                         double y0 = 0.0, double y1 = 0.0,
                         ContourExtend extend = ContourExtend::Neither);

    /*
     * The levels contour picks when given a count rather than a list --
     * contour(Z, N), and contour(Z) with N = 7. A MaxNLocator(N + 1) over the
     * data's range, trimmed to one level either side of it (one fewer at an
     * extended end, unless that would leave fewer than three).
     */
    static std::vector<double> auto_contour_levels(const double *values, std::size_t count,
                                                   std::size_t n,
                                                   ContourExtend extend = ContourExtend::Neither);

    ContourSet &contour(const double *values, std::size_t rows, std::size_t cols,
                        const double *levels, std::size_t nlevels,
                        double x0, double x1, double y0, double y1);

    /* Data limits across every artist, and the view after margins. */
    Bbox data_limits() const;
    Bbox view_limits() const;
    /* view_limits before the twinned axis is substituted in. */
    Bbox unshared_view_limits() const;
    /* ...and that, with invert_xaxis / invert_yaxis applied. */
    Bbox oriented_view_limits() const;

    /* Overrides autoscaling; used by pan/zoom. */
    void set_view_limits(const Bbox &view);
    bool has_explicit_view() const { return m_has_xview && m_has_yview; }
    void autoscale()
    {
        m_has_xview = false;
        m_has_yview = false;
    }

    /*
     * The full autoscale(enable, axis, tight). `enable` turns autoscaling on (1)
     * for the chosen axis, off (0) pinning the current view, or leaves it (-1).
     * `tight` drops the axis margin to zero (1), restores the 0.05 default (0),
     * or leaves it (-1) -- which is how matplotlib's tight fits the view to the
     * data exactly. `axis_x`/`axis_y` choose which axes to touch.
     */
    void autoscale(int enable, bool axis_x, bool axis_y, int tight)
    {
        if (axis_x) {
            if (enable == 1) {
                m_has_xview = false;
            } else if (enable == 0) {
                set_autoscale_x(false);
            }
            if (tight == 1) {
                xmargin = 0.0;
            } else if (tight == 0) {
                xmargin = 0.05;
            }
        }
        if (axis_y) {
            if (enable == 1) {
                m_has_yview = false;
            } else if (enable == 0) {
                set_autoscale_y(false);
            }
            if (tight == 1) {
                ymargin = 0.0;
            } else if (tight == 0) {
                ymargin = 0.05;
            }
        }
    }

    /* Per-axis limits, matching set_xlim / set_ylim: pinning one axis leaves
       the other autoscaling. */
    void set_xlim(double lo, double hi);
    void set_ylim(double lo, double hi);

    /*
     * set_xbound / set_ybound: set_xlim's other half. Where set_xlim reads the
     * ARGUMENT ORDER as orientation (set_xlim(1, 0) inverts), bound moves the
     * numbers and leaves the current orientation alone. Either argument may be
     * NaN, matplotlib's `lower=None`/`upper=None` for "leave this end alone" --
     * NaN rather than a sentinel because an axis bound can legitimately be
     * negative.
     */
    void set_xbound(double lower, double upper);
    void set_ybound(double lower, double upper);
    void autoscale_x() { m_has_xview = false; }

    /*
     * set_autoscalex_on / set_autoscaley_on.
     *
     * Turning autoscaling OFF pins whatever the view currently is, which is why
     * it needs the renderer's size: the view is only a number once the data has
     * been measured. Turning it back on simply drops the pin.
     */
    bool autoscale_x_on() const { return !m_has_xview; }
    bool autoscale_y_on() const { return !m_has_yview; }
    void set_autoscale_x(bool on)
    {
        if (on) {
            m_has_xview = false;
            return;
        }
        if (!m_has_xview) {
            const Bbox v = view_limits();
            m_xview[0] = std::min(v.x0, v.x1);
            m_xview[1] = std::max(v.x0, v.x1);
            m_has_xview = true;
        }
    }
    void set_autoscale_y(bool on)
    {
        if (on) {
            m_has_yview = false;
            return;
        }
        if (!m_has_yview) {
            const Bbox v = view_limits();
            m_yview[0] = std::min(v.y0, v.y1);
            m_yview[1] = std::max(v.y0, v.y1);
            m_has_yview = true;
        }
    }

    /* get_data_ratio: the data's own height over its width, which is the number
       set_aspect(1) makes the box match. */
    double data_ratio() const
    {
        const Bbox v = view_limits();
        const double dx = std::fabs(v.x1 - v.x0);
        const double dy = std::fabs(v.y1 - v.y0);
        return dx > 0.0 ? dy / dx : 1.0;
    }
    void autoscale_y() { m_has_yview = false; }

    /* Which cell of add_subplot's grid this axes came from. Only tight_layout
       needs it, but it has to be recorded at creation because the position
       alone cannot say which axes share a row or column. */
    unsigned grid_rows = 1;
    unsigned grid_cols = 1;
    unsigned grid_index = 1;

    /*
     * Siblings this axes aligns its decorations with -- populated by
     * Figure::align_{x,y}labels / align_titles, empty when alignment is off.
     *
     * matplotlib's align_ylabels joins the axes into a Grouper read at draw
     * time, so every label ends up labelpad beyond the group's far tick-box
     * edge rather than its own. These vectors are that group, self included,
     * and Axes::draw does the same union at draw time. A lone member is a no-op.
     */
    std::vector<const Axes *> ylabel_align_group;
    std::vector<const Axes *> xlabel_align_group;
    std::vector<const Axes *> title_align_group;

    /* Space the decorations outside the frame need, in pixels: tick labels,
       axis labels and the title. Used by Figure::tight_layout. */
    struct Margins
    {
        double left = 0.0;
        double right = 0.0;
        double bottom = 0.0;
        double top = 0.0;
    };
    Margins measure_margins(Renderer &renderer) const;

    /*
     * How far the tick labels reach beyond the frame, in pixels. The axis
     * labels hang off this: matplotlib's {X,Y}Axis._update_label_position
     * unions the tick label boxes with the spine and puts the axis label
     * labelpad beyond the far edge, so wider numbers push the label out.
     */
    struct TickReach
    {
        double below = 0.0; /* below the frame's bottom edge */
        double left = 0.0;  /* left of the frame's left edge */
        /*
         * How far the outermost tick labels stick out past the OTHER two edges.
         * The last x tick label is centred on the frame's corner and hangs half
         * its width beyond it; matplotlib's tight bbox counts that.
         */
        double right = 0.0;
        double above = 0.0;
    };
    TickReach tick_reach(Renderer &renderer, double px_per_pt) const;

    /* Where this axes lands on the canvas, in display pixels. */
    Bbox display_box(double fig_width, double fig_height) const;

    /* Data space -> scale space. Log axes are plotted on their logarithm, so
       every path and tick position goes through this before the affine mapping
       to the canvas. */
    double scale_x(double v) const;
    double scale_y(double v) const;

    /* The affine from scale space to display pixels. */
    Affine2D scale_to_display(const Bbox &box) const;

    /*
     * The tick values and labels for both axes, at the size this axes will
     * actually be drawn: the locator's bin count comes from the axis's length,
     * so ticks cannot be computed without knowing the box.
     *
     * Shared by the drawing code, the measuring code, and the C ABI's
     * get_xticks/get_yticks -- public for that last reason: it is a pure query
     * (no Renderer, no side effect), the same category as display_box and
     * view_limits above.
     */
    void compute_ticks(const Bbox &box,
                       double px_per_pt,
                       std::vector<double> &xticks,
                       std::vector<double> &yticks,
                       std::vector<std::string> &xlabels,
                       std::vector<std::string> &ylabels) const;

    /*
     * The common factor ScalarFormatter pulls out of each axis's labels, if
     * any -- "1e6" or "+1e6", written once at the end of the axis instead of
     * on every tick. Empty when the labels stand on their own.
     *
     * Computed alongside the labels, so it cannot disagree with them.
     */
    void tick_offset_texts(const Bbox &box,
                           double px_per_pt,
                           std::string &x_offset,
                           std::string &y_offset) const;

    void draw(Renderer &renderer);

  private:
    void draw_grid_and_ticks(Renderer &renderer, const Bbox &box, double px_per_pt);
    /* `line_tier` picks the patches lifted to zorder 2 (step outlines) rather
       than the ordinary zorder-1 ones. */
    void draw_patches(Renderer &renderer, const Bbox &box, double px_per_pt,
                      bool line_tier = false);
    void draw_spans(Renderer &renderer, const Bbox &box);
    void draw_collections(Renderer &renderer, const Bbox &box, double px_per_pt);
    void draw_segments(Renderer &renderer, const Bbox &box);
    void draw_texts(Renderer &renderer, const Bbox &box, double px_per_pt);
    void draw_images(Renderer &renderer, const Bbox &box);
    /* Filled sets in the patch tier (zorder 1), line sets with their labels
       in the line tier (a LineCollection's zorder 2). */
    void draw_contours(Renderer &renderer, const Bbox &box, double px_per_pt,
                       bool line_tier);
    double quiver_dots_per_unit(QuiverUnits units, const Bbox &box, double px_per_pt) const;

    void draw_tables(Renderer &renderer, const Bbox &box, double px_per_pt);

    /* psd and csd pin decibel ticks at whole numbers; see the definition. */
    void spectral_yticks();

    void draw_quivers(Renderer &renderer, const Bbox &box, double px_per_pt);
    void draw_reference_lines(Renderer &renderer, const Bbox &box, double px_per_pt);
    void draw_lines(Renderer &renderer, const Bbox &box);
    void draw_legend(Renderer &renderer, const Bbox &box, double px_per_pt);

    /* Tracked per axis so set_xlim can pin x while y keeps autoscaling. */
    double m_xview[2] = {0.0, 1.0};
    double m_yview[2] = {0.0, 1.0};
    bool m_has_xview = false;
    bool m_has_yview = false;
};

class Figure
{
  public:
    double width_inch = 6.4; /* figure.figsize */
    double height_inch = 4.8;
    double dpi = 100.0;                          /* figure.dpi */
    Color facecolor = Color::rgb(1.0, 1.0, 1.0); /* figure.facecolor */

    /*
     * The rest of the figure's own patch. The edge defaults to WHITE and
     * zero-width, so setting a colour alone changes nothing until the width is
     * raised too.
     */
    Color edgecolor = Color::rgb(1.0, 1.0, 1.0); /* figure.edgecolor */
    double linewidth = 0.0;                      /* figure.linewidth */
    bool frameon = true;                         /* figure.frameon */

    /* What the canvas is cleared to. frameon=false leaves the figure patch
       undrawn, so the background is transparent rather than white. */
    Color background() const { return frameon ? facecolor : Color::none(); }

    std::vector<std::unique_ptr<Axes>> axes;

    /*
     * The index gca returns. Nothing else in the library consults it, since
     * every call takes the axes it acts on; it is here for callers porting code
     * that leans on matplotlib's implicit state.
     */
    std::size_t current_axes = 0;

    /* delaxes. Every reference to the removed axes is dangling afterwards,
       which is the same bargain matplotlib's makes. */
    bool remove_axes(const Axes *ax);

    unsigned pixel_width() const;
    unsigned pixel_height() const;

    /* Grid placement in the style of add_subplot(nrows, ncols, index), with
       index 1-based and running left-to-right, top-to-bottom. */
    /*
     * The figure.subplot.* parameters, which bound the grid every subplot is
     * laid out inside -- matplotlib's SubplotParams, settable through
     * subplots_adjust.
     *
     * wspace and hspace are fractions of a CELL, not of the figure: the grid
     * formula is cell = span / (n + spacing * (n - 1)), leaving n cells and
     * n - 1 gaps. Reading them as a flat percentage makes neighbouring panels
     * overlap.
     */
    struct SubplotParams
    {
        double left = 0.125;
        double bottom = 0.11;
        double right = 0.9;
        double top = 0.88;
        double wspace = 0.2;
        double hspace = 0.2;
    };
    SubplotParams subplot_params;

    /*
     * Move the grid bounds and re-lay every subplot already added, as
     * matplotlib's subplots_adjust does -- not only a default for subplots
     * added afterwards. Axes placed by add_axes keep their own position.
     */
    void subplots_adjust();

    Axes &add_subplot(unsigned nrows = 1, unsigned ncols = 1, unsigned index = 1);

    /*
     * An axes at an explicit position in figure fractions -- add_axes([left,
     * bottom, width, height]). Inset panels and hand-placed colorbars.
     *
     * It is not part of any grid, so subplots_adjust and tight_layout leave it
     * where it was put.
     */
    Axes &add_axes(double left, double bottom, double width, double height);

    /* A title across the whole figure -- matplotlib's suptitle. Default anchor
       (0.5, 0.98) in figure fractions, its TOP on y; pass x, y to move it and a
       positive fontsize (points) to resize it. */
    void suptitle(const std::string &text, double x = 0.5, double y = 0.98,
                  double fontsize = 0.0);

    /* One x or y label for the whole figure -- supxlabel / supylabel. The
       defaults match matplotlib (x label low-centre, y label left-centre);
       x, y move the anchor and a positive fontsize resizes. */
    void supxlabel(const std::string &text, double x = 0.5, double y = 0.01,
                   double fontsize = 0.0);
    void supylabel(const std::string &text, double x = 0.02, double y = 0.5,
                   double fontsize = 0.0);

    /*
     * Line up the x labels, y labels or titles of subplots that share a row or
     * column -- Figure::align_xlabels / align_ylabels / align_titles, and
     * align_labels for both axis labels at once.
     *
     * When each panel's y numbers are a different width the y labels sit at
     * different distances from the frame; aligning gives the whole column one
     * label position, the far edge of the widest column of numbers in the
     * group. x labels do the same down a shared row, titles across the top.
     *
     * `axs` empty means every axes on the figure, matplotlib's axs=None. Only
     * axes in the same column (y) or row (x) are grouped, so panels from
     * different columns each align within their own column.
     */
    void align_xlabels(const std::vector<Axes *> &axs = {});
    void align_ylabels(const std::vector<Axes *> &axs = {});
    void align_titles(const std::vector<Axes *> &axs = {});
    void align_labels(const std::vector<Axes *> &axs = {});

    /*
     * An image straight onto the figure's pixels -- figimage. One scalar per
     * device pixel, run through the colormap and drawn behind the axes; `xo`,
     * `yo` are its lower-left corner in pixels. Returns the stored image so the
     * caller can tweak it before the draw.
     */
    FigureImage &figimage(const double *values,
                          std::size_t rows,
                          std::size_t cols,
                          double xo = 0.0,
                          double yo = 0.0);
    std::vector<FigureImage> figure_images;

    /*
     * A whole grid in one call -- subplots. `out` receives nrows * ncols
     * pointers in matplotlib's order: left to right, top to bottom. A
     * convenience for the add_subplot loop, with its 1-based index handled.
     */
    void subplots(unsigned nrows, unsigned ncols, Axes **out);
    std::string figure_title;
    double figure_title_size = 12.0; /* figure.titlesize 'large' */
    /* Anchor in figure fractions -- matplotlib's suptitle x, y defaults. */
    double figure_title_x = 0.5;
    double figure_title_y = 0.98;

    /*
     * One x and one y label for the whole figure -- supxlabel/supylabel.
     *
     * They exist for a grid whose panels share a scale: label_outer strips the
     * per-panel labels, and these say the thing once for the figure.
     */
    std::string figure_xlabel;
    std::string figure_ylabel;
    double figure_label_size = 12.0; /* figure.labelsize 'large', the default */
    /* Independent sizes, since matplotlib's are separate Text objects. */
    double figure_xlabel_size = 12.0;
    double figure_ylabel_size = 12.0;
    /* Anchors in figure fractions -- supxlabel and supylabel's defaults. */
    double figure_xlabel_x = 0.5;
    double figure_xlabel_y = 0.01;
    double figure_ylabel_x = 0.02;
    double figure_ylabel_y = 0.5;

    /*
     * A second axes sharing `base`'s x axis, with its own y scale on the right.
     *
     * The classic two-units-one-x plot. The returned axes covers exactly the
     * same box, draws no x axis and no background of its own, and follows
     * `base`'s x limits -- so it must not outlive it.
     */
    /*
     * An axes inside another's box -- inset_axes. `x`, `y`, `width` and
     * `height` are fractions of the PARENT's box.
     *
     * The position is resolved once, so create the inset after any layout
     * pass; see the note on the definition.
     */
    Axes &inset_axes(const Axes &parent, double x, double y, double width, double height);

    /*
     * A second scale that is an affine function of the first --
     * secondary_xaxis / secondary_yaxis, for `secondary = scale * primary +
     * offset`.
     *
     * matplotlib takes any pair of inverse functions; only the affine case is
     * here, which is the one that keeps the ticks evenly spaced and covers the
     * usual unit conversions.
     */
    Axes &secondary_xaxis(const Axes &base, bool top, double scale, double offset);
    Axes &secondary_yaxis(const Axes &base, bool right, double scale, double offset);

    Axes &twinx(const Axes &base);

    /*
     * A second axes sharing `base`'s y axis, with its own x scale along the
     * top -- twinx's mirror, for two x quantities against one y.
     */
    Axes &twiny(const Axes &base);

    /*
     * A colour scale beside `parent` (or below it, if horizontal), which is
     * narrowed to make room for it.
     *
     * Geometry follows matplotlib's make_axes defaults: the colorbar takes
     * `fraction` of the parent's width (vertical) or height (horizontal), with
     * `pad` between them, is shrunk to `aspect` (long over short), and pinned
     * to the edge nearest the parent. matplotlib's default `pad` differs by
     * orientation (0.05 vertical, 0.15 horizontal); a negative `pad` here means
     * "use that orientation's default".
     *
     * The strip is an ordinary axes holding a one-row-or-column image, as
     * matplotlib's Colorbar is, so it picks up ticks, labels and the frame for
     * free.
     */
    Axes &colorbar(Axes &parent,
                   Colormap cmap,
                   double vmin,
                   double vmax,
                   double fraction = 0.15,
                   double pad = -1.0,
                   double aspect = 20.0,
                   bool horizontal = false);

    /*
     * Repack the subplot grid so nothing collides, after matplotlib's
     * tight_layout.
     *
     * The stock spacing (figure.subplot.hspace/wspace of 0.2) is a fixed
     * fraction that ignores how tall the labels are, so a 2x2 grid with titles
     * and tick labels overlaps. The fix is to measure the decorations and give
     * each row and column exactly the room it needs.
     *
     * Needs a renderer because the answer depends on font metrics; call it
     * after the artists are added and before drawing. `pad` is in font-size
     * units, matching matplotlib's default of 1.08.
     */
    void tight_layout(Renderer &renderer, double pad = 1.08);

    void draw(Renderer &renderer);
};

} // namespace vpl

#endif /* VPL_CORE_FIGURE_H */
