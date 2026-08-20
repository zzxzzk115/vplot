/*
 * The demo catalogue: one builder per feature, written against the public C ABI
 * only.
 *
 * Shared between the interactive browser (examples/demo) and the batch exporter
 * (tools/export_demos) so the two build the same figures and cannot drift apart.
 */

#ifndef VPL_DEMOS_H
#define VPL_DEMOS_H

#include "vpl/vpl.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifndef VPL_DEMO_PI
#    define VPL_DEMO_PI 3.14159265358979323846
#endif

/*
 * The catalogue is TWO positional lists -- this enum and kVplDemos below -- and
 * the compiler does not check that they agree. Adding an entry to one and not
 * the other shifts every later panel onto the wrong figure, so keep them in the
 * same order and add to both.
 */
typedef enum VplDemoId
{
    VplDemo_Lines = 0,
    VplDemo_LineStyles,
    VplDemo_Markers,
    VplDemo_Colors,
    VplDemo_FormatStrings,
    VplDemo_ErrorBars,
    VplDemo_FillBetween,
    VplDemo_FillWhere,
    VplDemo_Scatter,
    VplDemo_Bars,
    VplDemo_Boxes,
    VplDemo_Table,
    VplDemo_Quiver,
    VplDemo_Barbs,
    VplDemo_Stream,
    VplDemo_Correlate,
    VplDemo_GroupedBar,
    VplDemo_Pie,
    VplDemo_Spectral,
    VplDemo_Specgram,
    VplDemo_Inset,
    VplDemo_Secondary,
    VplDemo_TextAnnotate,
    VplDemo_ReferenceLines,
    VplDemo_GridLegend,
    VplDemo_LegendTitle,
    VplDemo_LegendNCols,
    VplDemo_SetBound,
    VplDemo_BarLabelFull,
    VplDemo_ArtistLookup,
    VplDemo_Imshow,
    VplDemo_Contour,
    VplDemo_Clabel,
    VplDemo_Hexbin,
    VplDemo_Triplot,
    VplDemo_TriContour,
    VplDemo_TriContourF,
    VplDemo_TriPcolor,
    VplDemo_MinorTicks,
    VplDemo_LogScale,
    VplDemo_AxisLimits,
    VplDemo_TwinX,
    VplDemo_Colorbar,
    VplDemo_ColorbarHorizontal,
    /* The distributions. */
    VplDemo_Histogram,
    VplDemo_Ecdf,
    VplDemo_Violins,
    VplDemo_BoxStats,

    /* Series drawn as something other than a curve. */
    VplDemo_Stem,
    VplDemo_Stairs,
    VplDemo_Steps,
    VplDemo_StackedAreas,
    VplDemo_EventPlot,

    /* Bars and regions. */
    VplDemo_HorizontalBars,
    VplDemo_Intervals,
    VplDemo_Polygons,
    VplDemo_Spans,
    VplDemo_Rules,
    VplDemo_Arrow,

    /* Fields. */
    VplDemo_ContourFilled,
    VplDemo_Matrix,
    VplDemo_Mesh,

    /* The rest of the spectral family. */
    VplDemo_SpectrumParts,
    VplDemo_CrossSpectrum,
    VplDemo_CrossCorrelation,

    /* The axes furniture. */
    VplDemo_Spines,
    VplDemo_TickPositions,
    VplDemo_Inverted,
    VplDemo_AspectMargins,
    VplDemo_GridDepth,
    VplDemo_ErrorBox,

    /* The figure furniture. */
    VplDemo_FigureLabels,
    VplDemo_SharedAxes,
    VplDemo_SubplotsShared,
    VplDemo_AlignLabels,
    VplDemo_FigImage,
    VplDemo_TwinY,
    VplDemo_InsetIndicator,
    VplDemo_PageSize,
    VplDemo_TickLabelFormat,
    VplDemo_BoxAspect,
    VplDemo_PropCycle,
    VplDemo_PropCycleFull,
    VplDemo_LocatorParamsFull,
    VplDemo_FigurePatch,
    VplDemo_AutoscaleOff,

    VplDemo_Subplots,
    VplDemo_Count,
} VplDemoId;

typedef struct VplDemoInfo
{
    const char *name;
    const char *slug;   /* file-name friendly */
    const char *blurb;  /* what the demo is showing */
    const char *api;    /* the calls it exercises */
} VplDemoInfo;

static const VplDemoInfo kVplDemos[VplDemo_Count] = {
    {"Lines", "lines",
     "The baseline: plot() with the tab10 colour cycle, and the one-argument "
     "form where x becomes 0, 1, 2, ...",
     "vplAxesPlot, vplAxesPlotY"},
    {"Line styles", "line_styles",
     "Solid, dashed, dash-dot and dotted. Dash patterns scale with line width, "
     "as matplotlib's lines.scale_dashes does.",
     "VplLineField_LineStyle"},
    {"Markers", "markers", "All eight marker shapes. '+' and 'x' stroke only; the rest fill.",
     "VplLineField_Marker / VplLineField_MarkerSize"},
    {"Colours", "colours",
     "Every colour spec form: cycle index, tableau name, hex, grayscale and the "
     "single letters (which carry matplotlib's values, not CSS ones).",
     "VplLineField_ColorSpec"},
    {"Format strings", "format_strings",
     "Matlab-style formats. A format naming a marker but no line style draws "
     "markers only.",
     "VplLineField_Format"},
    {"Error bars", "error_bars", "Symmetric y error with optional caps.",
     "vplAxesErrorBar"},
    {"Fill between", "fill_between", "Confidence bands and envelopes.",
     "vplAxesFillBetween"},
    {"Fill where", "fill_where",
     "Shade only where one curve is above the other, with interpolate=True so "
     "each band's edges sit on the actual crossing.",
     "vplAxesFillBetween"},
    {"Scatter", "scatter",
     "Per-point colour and size. `s` is an AREA in points squared, so the "
     "diameter is its square root -- the default 36 matches plot()'s 6-point "
     "marker.",
     "vplAxesScatter"},
    {"Bars", "bars", "Vertical bars rising from zero.", "vplAxesBar"},
    {"Box plots", "boxes",
     "Quartiles, whiskers and fliers from raw samples. The whisker ends on the "
     "furthest real observation inside 1.5 IQR, never on 1.5 IQR itself, so a "
     "whisker always lands on a datum. vplAxesBxp draws exactly this from the "
     "five statistics alone -- boxplot is that call with a summarizer in front "
     "-- which is what to reach for when the samples are gone and only the "
     "summary survives.",
     "vplAxesBoxPlot / vplAxesBxp"},
    {"Table", "table",
     "A grid of text under the axes, positioned in axes units rather than data "
     "units -- so it neither moves with a pan nor counts towards the limits. "
     "Row height comes from the font size rather than from measuring the text, "
     "which is why every row is the same height. The row-label column is the "
     "one exception: it is measured, and it hangs off the left of the centred "
     "grid rather than being centred with it.",
     "vplAxesTable"},
    {"Quiver", "quiver",
     "A field of arrows, with a key showing what one unit looks like. Both the "
     "shaft width and the arrow length are fractions of the axes rather than "
     "of the data, so neither can be worked out until the figure has a size -- "
     "which is why the scale is resolved at draw time and the key borrows it.",
     "vplAxesQuiver / vplAxesQuiverKey"},
    {"Wind barbs", "barbs",
     "The meteorological convention rather than a general vector plot: the "
     "staff points INTO the wind, every staff is the same length, and the "
     "speed is spelled out in flags (50), full barbs (10) and half barbs (5). "
     "Speeds are rounded to the nearest half barb first, so 23 and 27 draw the "
     "same. A calm reading is a bare circle.",
     "vplAxesBarbs"},
    {"Streamlines", "streamplot",
     "Streamlines integrated through a vector field with an adaptive RK12 "
     "step. What keeps them evenly spaced is a mask: the plane is divided into "
     "cells and a line stops the moment it enters one another line has already "
     "claimed. Seeds are tried in a spiral from the outside in, so the "
     "boundary lines are laid down first and the interior fills what is left.",
     "vplAxesStreamPlot"},
    {"Correlation", "correlate",
     "Autocorrelation as a stalk per lag. Normed, so the zero lag is exactly "
     "1. This is a direct sum rather than a spectrum -- it sits beside psd and "
     "csd in matplotlib's documentation but shares none of their machinery and "
     "needs no FFT.",
     "vplAxesACorr / vplAxesXCorr"},
    {"Grouped bars", "grouped_bar",
     "Bars side by side within each group. Nothing here says how wide a bar "
     "is: it falls out of fitting every dataset's bar, the gaps between them "
     "and the gap to the next group into one group's width -- and both "
     "spacings are measured in bar widths, not data units.",
     "vplAxesGroupedBar"},
    {"Pie", "pie",
     "Wedges in the patch colour cycle, starting at 3 o'clock and running "
     "counter-clockwise, with a label at each wedge's middle angle. A pie "
     "reconfigures the axes rather than living in one: square aspect, no "
     "frame, no ticks.",
     "vplAxesPie / vplAxesPieLabel"},
    {"Power spectrum", "spectral",
     "Welch's method: cut the signal into segments, window each, transform, "
     "and average the periodograms -- which is what turns one noisy transform "
     "into an estimate. A one-sided density doubles every bin except DC and "
     "Nyquist, then divides by Fs and by sum(window^2) to undo the windowing "
     "loss.",
     "vplAxesPsd / vplAxesCsd / vplAxesCohere"},
    {"Spectrogram", "specgram",
     "The same spectrum against time, as an image. The array is flipped before "
     "it is drawn so the lowest frequency lands at the bottom, and the time "
     "axis is padded by half a segment at each end -- a segment's value "
     "belongs to its whole span, not to its centre.",
     "vplAxesSpecgram"},
    {"Inset axes", "inset",
     "An axes inside another's box, magnifying part of it. The bounds are "
     "fractions of the PARENT rather than of the figure. Two of the four "
     "corner lines are drawn -- which two is decided by comparing the "
     "rectangle's box with the inset's edge by edge, since all four would "
     "cross each other.",
     "vplFigureInsetAxes / vplAxesIndicateInsetZoom"},
    {"Secondary axis", "secondary",
     "A second scale that is a function of the first, sharing the same data. "
     "Only the affine case is supported, which is the one that keeps the ticks "
     "evenly spaced and covers the usual unit conversions.",
     "vplFigureSecondaryXAxis / vplFigureSecondaryYAxis"},
    {"Text & annotate", "text_annotate",
     "Text at a data position, with alignment and rotation; annotate() adds a "
     "leader line. The arrow head is sized in points, so it keeps its size "
     "however far you zoom.",
     "vplAxesText / vplAxesAnnotate"},
    {"Reference lines", "reference_lines",
     "axhline / axvline span the view rather than the data, so they stay full "
     "width through pan and zoom and never affect autoscaling.",
     "vplAxesAxHLine / vplAxesAxVLine"},
    {"Grid & legend", "grid_legend", "Grid under the data; legend in any of four corners.",
     "vplAxesSetGrid / vplAxesSetLegend"},
    {"Legend title", "legend_title",
     "A heading centred above the entries, in the legend's own font size.",
     "vplAxesSetLegendTitle"},
    {"Legend columns", "legend_ncols",
     "5 entries in 2 columns: the first column gets the extra entry (3 then "
     "2, not 2 then 3), and each column shrinks to its own widest label "
     "rather than the legend-wide one.",
     "vplAxesSetLegendNCols"},
    {"Set bound", "set_bound",
     "set_xbound(NAN, 5): only the upper end moves, the lower end is still "
     "whatever autoscale put it at -- set_xlim's other half.",
     "vplAxesSetXBound"},
    {"Bar label (full)", "bar_label_full",
     "fmt='%.1f' with padding on ordinary bars, and label_type='center' on "
     "a stacked bar -- each segment's OWN value, not its cumulative "
     "position.",
     "vplAxesBarLabelFull"},
    {"Artist lookup", "artist_lookup",
     "ax.lines / ax.patches / ax.images: every line, bar and image plotted "
     "here has its handle thrown away at creation and fetched back by index "
     "instead -- bar_label only works if what comes back is the real bar, "
     "not a stale or wrong one.",
     "vplAxesGetLineCount, vplAxesGetLine, vplAxesGetPatchCount, "
     "vplAxesGetPatch, vplAxesGetImageCount, vplAxesGetImage"},
    {"Heatmap (imshow)", "imshow",
     "A scalar field as coloured cells, with the perceptually uniform maps. "
     "Emitted as one quad per cell, so SVG and PDF stay vector -- unlike "
     "matplotlib's imshow, which resamples into a raster.",
     "vplAxesImshow"},
    {"Contour", "contour",
     "Iso-lines by marching squares, coloured across the level range. The "
     "saddle cases are resolved by the cell's mean, as matplotlib's _contour "
     "does.",
     "vplAxesContour"},
    {"Contour labels", "clabel",
     "Each line broken so its level sits in the gap, rotated to follow the "
     "line and never upside down. Where a label goes is chosen in pixels: the "
     "straightest label-wide block of a line that no other label is already "
     "crowding.",
     "vplContourClabel"},
    {"Hexbin", "hexbin",
     "Scatter density as a honeycomb. The tiling is two interleaved lattices, "
     "and a point joins whichever centre is nearer under dx^2 + 3dy^2 -- that "
     "metric is what makes the cells hexagons. Empty bins are dropped, which is "
     "the difference from a 2-D histogram.",
     "vplAxesHexbin"},
    {"Triplot", "triplot",
     "An unstructured grid drawn as its edges. No triangles are given, so the "
     "Delaunay triangulation is computed -- the one where no point falls inside "
     "any triangle's circumcircle, which for points in general position is "
     "unique and so is the same one Qhull returns.",
     "vplAxesTriplot"},
    {"Tricontour", "tricontour",
     "Iso-lines over that same grid. Simpler than the structured case: the "
     "field is linear across a triangle, so a level either misses it or crosses "
     "exactly two edges. There is no saddle, which is the whole reason marching "
     "squares needs a tie-break rule and this does not.",
     "vplAxesTriContour"},
    {"Tricontourf", "tricontourf",
     "The bands between those levels rather than their boundaries. A linear "
     "field over a triangle means the band is that triangle clipped by two "
     "half-planes, so every piece is one convex polygon.",
     "vplAxesTriContourF"},
    {"Tripcolor", "tripcolor",
     "One flat colour per triangle, each the mean of its three vertex values. "
     "Drawn without antialiasing, as matplotlib draws it: on a mesh of abutting "
     "cells a soft edge is covered by neither side and the mesh comes out "
     "veined.",
     "vplAxesTriPcolor"},
    {"Minor ticks", "minor_ticks",
     "Unlabelled ticks between the majors. The count is 5 when the major step "
     "divides evenly into fifths and 4 otherwise -- a fixed number would land "
     "minor ticks on untidy values.",
     "vplAxesSetMinorTicks"},
    {"Log scales", "log_scale",
     "Decade ticks, and margins applied in log space so the padding looks even "
     "at both ends.",
     "vplAxesSetXScale / vplAxesSetYScale"},
    {"Axis limits", "axis_limits",
     "Pin one axis and the other keeps autoscaling.",
     "vplAxesSetXLim / vplAxesSetYLim"},
    {"Twin y axes", "twinx",
     "Two quantities in different units against one x. The second axes shares "
     "the first's x, keeps its own y on the right, and draws neither an x axis "
     "nor a background of its own.",
     "vplFigureTwinX"},
    {"Colorbar", "colorbar",
     "A colour scale beside the plot, which is narrowed to make room for it. "
     "The bar is an ordinary axes, so it takes a ylabel like any other.",
     "vplFigureColorbar"},
    {"Colorbar horizontal", "colorbar_horizontal",
     "The same scale, but under the plot instead of beside it -- "
     "orientation='horizontal'. The gap is wider by default here, to clear "
     "the x tick labels a vertical bar never has to share space with.",
     "vplFigureColorbarOriented"},
    {"Histogram", "histogram",
     "Counts over equal bins, with the bars sitting on the axis.",
     "vplAxesHist"},
    {"ECDF", "ecdf",
     "The empirical distribution: a post-step rising 1/n at each observation, "
     "pinned to 0 and 1.",
     "vplAxesEcdf"},
    {"Violins", "violins",
     "A Gaussian kernel density per group, mirrored. One colour for the whole "
     "call, not one per group.",
     "vplAxesViolinPlot"},
    {"Boxes from statistics", "box_stats",
     "bxp: the same boxes from five numbers you already have, for data too "
     "large to keep.",
     "vplAxesBxp"},

    {"Stem", "stem",
     "Stalks, heads and a C3 baseline -- three artists from one call.",
     "vplAxesStem"},
    {"Stairs", "stairs",
     "A step OUTLINE over bin edges. Not filled: matplotlib's StepPatch takes "
     "fill=False.",
     "vplAxesStairs"},
    {"Step", "steps",
     "The three step positions: pre, post and mid.",
     "vplAxesStep"},
    {"Stacked areas", "stacked_areas",
     "Each series filled between the running total below it and its own top.",
     "vplAxesStackPlot"},
    {"Event plot", "event_plot",
     "A rug of marks, both orientations. The cross axis autoscales to a whole "
     "line length either side, not the half each mark covers.",
     "vplAxesEventPlot"},

    {"Horizontal bars", "barh",
     "Bars along x, and bar_label writing each value at the bar's end.",
     "vplAxesBarH, vplAxesBarLabel"},
    {"Intervals", "intervals",
     "broken_barh: a row of (start, width) pairs -- timelines and Gantt charts.",
     "vplAxesBrokenBarH"},
    {"Polygons", "polygons",
     "An arbitrary filled polygon, and fill_between's mirror across a shared y.",
     "vplAxesFill, vplAxesFillBetweenX"},
    {"Shaded spans", "spans",
     "Bands across the whole axes at constant y or x. Always C0: a span is not "
     "part of the property cycle.",
     "vplAxesAxHSpan, vplAxesAxVSpan"},
    {"Rules", "rules",
     "hlines and vlines take data endpoints at both ends; axline is infinite "
     "and re-derived from the view.",
     "vplAxesHLines, vplAxesVLines, vplAxesAxLine"},
    {"Arrow", "arrow",
     "A filled arrow patch. Its head is added BEYOND the endpoint, so the drawn "
     "arrow is longer than the vector.",
     "vplAxesArrow"},

    {"Filled contours", "contourf",
     "Bands between consecutive levels, coloured at each band's midpoint and "
     "drawn without antialiasing.",
     "vplAxesContourF"},
    {"Matrix", "matrix",
     "matshow puts the column indices along the top; spy shows only where the "
     "matrix is non-zero.",
     "vplAxesMatShow, vplAxesSpy"},
    {"Mesh", "mesh",
     "pcolormesh on an UNEVEN grid given by its edges, and hist2d's counts.",
     "vplAxesPcolorMesh, vplAxesHist2D"},

    {"Spectrum parts", "spectrum_parts",
     "Magnitude, phase and the unwrapped angle of the same signal.",
     "vplAxesMagnitudeSpectrum, vplAxesPhaseSpectrum, vplAxesAngleSpectrum"},
    {"Cross spectrum", "cross_spectrum",
     "Cross spectral density and coherence between two signals sharing a tone.",
     "vplAxesCsd, vplAxesCohere"},
    {"Cross correlation", "cross_correlation",
     "xcorr against a lagged copy: the peak sits at the lag.",
     "vplAxesXCorr"},

    {"Spines & frame", "spines",
     "Which edges of the box are drawn. Turning the top and right pair off is "
     "the commonest thing done before a figure goes into a paper.",
     "vplAxesSetSpineVisible, vplAxesSetFrameOn, vplAxesSetAxisOn"},
    {"Tick positions", "tick_positions",
     "Tick positions of your own, with labels to match. An empty list means no "
     "ticks at all.",
     "vplAxesSetXTicks, vplAxesSetYTicks"},
    {"Inverted axes", "inverted",
     "Either axis run backwards. A property of the axes, so it survives "
     "autoscaling.",
     "vplAxesSetXInverted, vplAxesSetYInverted"},
    {"Aspect & margins", "aspect_margins",
     "Square data units, and the padding around the data -- with autoscale and "
     "an explicit view for comparison.",
     "vplAxesSetAspect, vplAxesSetMargins, vplAxesAutoscale, vplAxesSetViewLimits"},
    {"Grid depth", "grid_depth",
     "Whether the grid is drawn under the data, over it, or between the patches "
     "and the lines.",
     "vplAxesSetAxisBelow"},
    {"Error box", "error_box",
     "Error in both directions at once: the x half had been a field nothing "
     "drew.",
     "vplAxesSetXErr"},

    {"Figure labels", "figure_labels",
     "A title and axis labels for the whole figure rather than one panel.",
     "vplFigureSuptitle, vplFigureSupXLabel, vplFigureSupYLabel"},
    {"Shared axes", "shared_axes",
     "A grid built in one call, sharing scales, with the interior tick labels "
     "hidden by label_outer.",
     "vplFigureSubplots, vplAxesShareX, vplAxesShareY, vplAxesLabelOuter"},
    {"Subplots shared", "subplots_shared",
     "The grid and the sharing wired in by one call: column 0 shares x down "
     "its own column, row 0 shares y across its own row -- two independent "
     "pairs of panels, not one group of four.",
     "vplFigureSubplotsShared"},
    {"Align labels", "align_labels",
     "Two rows whose y numbers are different widths: align_ylabels pulls both "
     "y labels out to the wider column's edge so they form one margin, and the "
     "x labels and titles line up down the shared row.",
     "vplFigureAlignYLabels, vplFigureAlignXLabels, vplFigureAlignTitles"},
    {"Figure image", "figimage",
     "A raster painted straight onto the figure's pixels, behind the axes -- "
     "figimage. One array element per device pixel, never resampled.",
     "vplFigureFigImage"},
    {"Twin x axes", "twiny",
     "Two x scales against one y: the second along the top.",
     "vplFigureTwinY"},
    {"Inset indicator", "inset_indicator",
     "A hand-placed axes, with the region it magnifies marked on the parent.",
     "vplFigureAddAxes, vplAxesIndicateInset"},
    {"Page size", "page_size",
     "The figure's size in inches and its dots per inch -- the two numbers that "
     "decide how big a saved file comes out.",
     "vplFigureSetSizeInches, vplFigureSetDpi"},

    {"Tick label format", "tick_label_format",
     "A common factor pulled out of the labels and written once at the end of "
     "the axis -- and the same axis with that turned off.",
     "vplAxesTickLabelFormat"},

    {"Box aspect & anchor", "box_aspect",
     "A panel of a given SHAPE, whatever the data says -- and where the shrunk "
     "box sits in the room it was given.",
     "vplAxesSetBoxAspect, vplAxesSetAnchor, vplAxesLocatorParams"},
    {"Property cycle", "prop_cycle",
     "The colours every artist draws from, replaced. One list serves both the "
     "line cycle and the patch cycle.",
     "vplAxesSetPropCycle"},
    {"Property cycle (full)", "prop_cycle_full",
     "colour, linestyle and marker zipped into one cycle -- entry i of each "
     "list together, not three independently-advancing counters.",
     "vplAxesSetPropCycleFull"},
    {"Locator params (full)", "locator_params_full",
     "steps=[1, 5, 10] forces coarse tick spacings only -- no 2 or 2.5 "
     "multiples. tight is also exercised but changes nothing, matching real "
     "matplotlib: MaxNLocator does not override the view_limits() hook it "
     "gates.",
     "vplAxesLocatorParamsFull"},
    {"Figure patch", "figure_patch",
     "The background behind every axes, and its edge -- which is white at zero "
     "width by default, so it is invisible twice over.",
     "vplFigureSetFaceColor, vplFigureSetEdgeColor, vplFigureSetLineWidth, "
     "vplFigureSetFrameOn"},

    {"Autoscale off", "autoscale_off",
     "Pinning the view where it is, so later data does not move it -- and a "
     "rectangle pushed into the limits by hand.",
     "vplAxesSetAutoscaleX, vplAxesSetAutoscaleY, vplAxesSetAutoscale, "
     "vplAxesUpdateDataLim, vplAxesSetFaceColor"},

    {"Subplots", "subplots",
     "A grid of axes, repacked by tight_layout so titles stop colliding with "
     "the row above's tick labels.",
     "vplFigureAddSubplot / vplFigureTightLayout"},
};

/* Everything the interactive browser lets you tweak. The exporter uses the
   defaults, so the PNGs and the live demo agree. */
typedef struct VplDemoParams
{
    double lineWidth;
    double markerSize;
    double capSize;
    double bandAlpha;
    double barWidth;
    int showGrid;
    int showLegend;
    VplLegendLoc legendLoc;
    int logX;
    int logY;
    int tightLayout;
    int subplotRows;
    int subplotCols;
    double xlimLo, xlimHi;
    int pinX;
    char format[32];
} VplDemoParams;

static void vplDemoDefaults(VplDemoParams *p)
{
    memset(p, 0, sizeof(*p));
    p->lineWidth = 1.5;
    p->markerSize = 6.0;
    p->capSize = 3.0;
    p->bandAlpha = 0.25;
    p->barWidth = 0.7;
    p->showGrid = 1;
    p->showLegend = 1;
    p->legendLoc = VplLegendLoc_UpperRight;
    p->logX = 1;
    p->logY = 1;
    p->tightLayout = 1;
    p->subplotRows = 2;
    p->subplotCols = 2;
    p->xlimLo = 1.0;
    p->xlimHi = 5.0;
    p->pinX = 1;
    strcpy(p->format, "r--o");
}

/* ---- shared sample data -------------------------------------------------- */

enum
{
    kVplDemoN = 120,
    kVplDemoSparse = 18
};

static void vplDemoRamp(double *x, size_t n, double span)
{
    for (size_t i = 0; i < n; ++i) {
        x[i] = (double)i * span / (double)(n - 1);
    }
}

/* ---- the builders -------------------------------------------------------- */

/* Each returns VplResult_Success and leaves `fig` populated. The caller owns
   `fig` and destroys it. */
static VplResult vplBuildDemo(VplDemoId id, const VplDemoParams *p, VplFigure **outFigure)
{
    VplFigureDesc fd;
    memset(&fd, 0, sizeof(fd));
    fd.structSize = sizeof(fd);
    fd.widthInches = 7.2;
    fd.heightInches = 5.4;
    fd.dpi = 100.0;

    VplFigure *fig = NULL;
    VplResult r = vplCreateFigure(&fd, &fig);
    if (r != VplResult_Success) {
        return r;
    }

    static double x[kVplDemoN], a[kVplDemoN], b[kVplDemoN], c[kVplDemoN];
    static double sx[kVplDemoSparse], sa[kVplDemoSparse], sb[kVplDemoSparse],
        se[kVplDemoSparse];

    vplDemoRamp(x, kVplDemoN, 2.0 * VPL_DEMO_PI);
    for (int i = 0; i < kVplDemoN; ++i) {
        a[i] = sin(x[i]);
        b[i] = cos(x[i]);
        c[i] = sin(x[i]) * exp(-x[i] / 6.0);
    }
    vplDemoRamp(sx, kVplDemoSparse, 2.0 * VPL_DEMO_PI);
    for (int i = 0; i < kVplDemoSparse; ++i) {
        sa[i] = sin(sx[i]);
        sb[i] = cos(sx[i]);
        se[i] = 0.08 + 0.06 * fabs(cos(sx[i] * 1.7));
    }

    VplLineDesc ld;
    VplPatchDesc pd;

    /* Most panels are one axes. These build their own grid instead, so they
       skip the single-axes path entirely. */
    const int builds_own_axes = (id == VplDemo_Subplots || id == VplDemo_SharedAxes ||
                                 id == VplDemo_SubplotsShared ||
                                 id == VplDemo_FigureLabels || id == VplDemo_AlignLabels ||
                                 id == VplDemo_FigImage || id == VplDemo_ArtistLookup ||
                                 id == VplDemo_SetBound || id == VplDemo_BarLabelFull);

    if (!builds_own_axes) {
        VplAxes *ax = NULL;
        r = vplFigureAddSubplot(fig, 1, 1, 1, &ax);
        if (r != VplResult_Success) {
            vplDestroyFigure(fig);
            return r;
        }
        vplAxesSetTitle(ax, kVplDemos[id].name);

        switch (id) {
            case VplDemo_Lines: {
                /* The one-argument form first: x becomes 0, 1, 2, ... which is
                   how most matplotlib code is written. */
                vplAxesPlotY(ax, c, kVplDemoN, NULL, NULL);

                double *series[3] = {a, b, c};
                const char *labels[3] = {"sin", "cos", "damped"};
                for (int k = 0; k < 3; ++k) {
                    memset(&ld, 0, sizeof(ld));
                    ld.structSize = sizeof(ld);
                    ld.set = VplLineField_Width | VplLineField_Label;
                    ld.width = p->lineWidth;
                    ld.label = labels[k];
                    vplAxesPlot(ax, x, series[k], kVplDemoN, &ld, NULL);
                }
                vplAxesSetXLabel(ax, "t");
                vplAxesSetYLabel(ax, "value");
                break;
            }

            case VplDemo_LineStyles: {
                const VplLineStyle styles[4] = {VplLineStyle_Solid, VplLineStyle_Dashed,
                                                VplLineStyle_DashDot, VplLineStyle_Dotted};
                const char *labels[4] = {"solid", "dashed", "dashdot", "dotted"};
                for (int k = 0; k < 4; ++k) {
                    static double y[kVplDemoN];
                    for (int i = 0; i < kVplDemoN; ++i) {
                        y[i] = sin(x[i]) - k * 0.7;
                    }
                    memset(&ld, 0, sizeof(ld));
                    ld.structSize = sizeof(ld);
                    ld.set = VplLineField_LineStyle | VplLineField_Label | VplLineField_Width;
                    ld.linestyle = styles[k];
                    ld.label = labels[k];
                    ld.width = p->lineWidth;
                    vplAxesPlot(ax, x, y, kVplDemoN, &ld, NULL);
                }
                break;
            }

            case VplDemo_Markers: {
                const VplMarker markers[8] = {
                    VplMarker_Circle, VplMarker_Square, VplMarker_TriangleUp,
                    VplMarker_TriangleDown, VplMarker_Diamond, VplMarker_Plus,
                    VplMarker_Cross, VplMarker_Star};
                const char *labels[8] = {"o", "s", "^", "v", "D", "+", "x", "*"};
                for (int k = 0; k < 8; ++k) {
                    static double y[kVplDemoSparse];
                    for (int i = 0; i < kVplDemoSparse; ++i) {
                        y[i] = (double)(7 - k);
                    }
                    memset(&ld, 0, sizeof(ld));
                    ld.structSize = sizeof(ld);
                    ld.set = VplLineField_Marker | VplLineField_MarkerSize |
                             VplLineField_LineStyle | VplLineField_Label;
                    ld.marker = markers[k];
                    ld.markersize = p->markerSize;
                    ld.linestyle = VplLineStyle_None;
                    ld.label = labels[k];
                    vplAxesPlot(ax, sx, y, kVplDemoSparse, &ld, NULL);
                }
                break;
            }

            case VplDemo_Colors: {
                const char *specs[8] = {"C0",   "C3",  "tab:green", "#9467bd",
                                        "0.35", "r",   "g",         "c"};
                for (int k = 0; k < 8; ++k) {
                    static double y[kVplDemoN];
                    for (int i = 0; i < kVplDemoN; ++i) {
                        y[i] = sin(x[i]) - k * 0.55;
                    }
                    memset(&ld, 0, sizeof(ld));
                    ld.structSize = sizeof(ld);
                    ld.set = VplLineField_ColorSpec | VplLineField_Label | VplLineField_Width;
                    ld.colorSpec = specs[k];
                    ld.label = specs[k];
                    ld.width = 2.0;
                    vplAxesPlot(ax, x, y, kVplDemoN, &ld, NULL);
                }
                break;
            }

            case VplDemo_FormatStrings: {
                const char *formats[4] = {p->format, "g:s", "k^", "b-"};
                for (int k = 0; k < 4; ++k) {
                    static double y[kVplDemoSparse];
                    for (int i = 0; i < kVplDemoSparse; ++i) {
                        y[i] = sin(sx[i]) - k * 0.8;
                    }
                    memset(&ld, 0, sizeof(ld));
                    ld.structSize = sizeof(ld);
                    ld.set = VplLineField_Format | VplLineField_Label;
                    ld.format = formats[k];
                    ld.label = formats[k];
                    /* A bad format is reported rather than silently ignored, so
                       the live editor can show the error. */
                    if (vplAxesPlot(ax, sx, y, kVplDemoSparse, &ld, NULL) !=
                        VplResult_Success) {
                        memset(&ld, 0, sizeof(ld));
                        ld.structSize = sizeof(ld);
                        ld.set = VplLineField_Label;
                        ld.label = "(invalid format)";
                        vplAxesPlot(ax, sx, y, kVplDemoSparse, &ld, NULL);
                    }
                }
                break;
            }

            case VplDemo_ErrorBars: {
                memset(&ld, 0, sizeof(ld));
                ld.structSize = sizeof(ld);
                ld.set = VplLineField_Format | VplLineField_Label;
                ld.format = "o-";
                ld.label = "measured";
                vplAxesErrorBar(ax, sx, sa, se, kVplDemoSparse, p->capSize, &ld, NULL);
                vplAxesSetXLabel(ax, "t");
                vplAxesSetYLabel(ax, "signal");
                break;
            }

            case VplDemo_FillBetween: {
                static double lo[kVplDemoN], hi[kVplDemoN];
                for (int i = 0; i < kVplDemoN; ++i) {
                    const double band = 0.12 + 0.05 * x[i];
                    lo[i] = a[i] - band;
                    hi[i] = a[i] + band;
                }
                memset(&pd, 0, sizeof(pd));
                pd.structSize = sizeof(pd);
                pd.set = VplPatchField_FaceColorSpec | VplPatchField_Alpha;
                pd.faceColorSpec = "tab:blue";
                pd.alpha = p->bandAlpha;
                vplAxesFillBetween(ax, x, lo, hi, kVplDemoN, NULL, 0, VplFillStep_None, &pd, NULL);

                memset(&ld, 0, sizeof(ld));
                ld.structSize = sizeof(ld);
                ld.set = VplLineField_ColorSpec | VplLineField_Label;
                ld.colorSpec = "tab:blue";
                ld.label = "mean";
                vplAxesPlot(ax, x, a, kVplDemoN, &ld, NULL);
                break;
            }

            case VplDemo_FillWhere: {
                /* Shade where sin > cos, letting interpolate put each band's
                   edges on the crossing rather than the nearest sample. */
                static uint8_t mask[kVplDemoN];
                for (int i = 0; i < kVplDemoN; ++i) {
                    mask[i] = a[i] > b[i] ? 1 : 0;
                }
                memset(&pd, 0, sizeof(pd));
                pd.structSize = sizeof(pd);
                pd.set = VplPatchField_FaceColorSpec | VplPatchField_Alpha;
                pd.faceColorSpec = "tab:green";
                pd.alpha = p->bandAlpha;
                vplAxesFillBetween(ax, x, a, b, kVplDemoN, mask, 1, VplFillStep_None, &pd, NULL);

                memset(&ld, 0, sizeof(ld));
                ld.structSize = sizeof(ld);
                ld.set = VplLineField_Width;
                ld.width = p->lineWidth;
                vplAxesPlot(ax, x, a, kVplDemoN, &ld, NULL);
                vplAxesPlot(ax, x, b, kVplDemoN, &ld, NULL);
                break;
            }

            case VplDemo_Scatter: {
                /* A deterministic scatter: no RNG, so the exported PNG is
                   byte-identical run to run and can be diffed. */
                enum { M = 90 };
                static double px_[M], py_[M], ps[M];
                static float pc[4 * M];
                for (int i = 0; i < M; ++i) {
                    const double t = i * 0.7;
                    px_[i] = 5.0 + 4.0 * cos(t) * (0.3 + 0.7 * (i / (double)M));
                    py_[i] = 5.0 + 4.0 * sin(t * 1.1) * (0.3 + 0.7 * (i / (double)M));
                    /* Area sweeps from a 4pt to a 22pt marker. */
                    const double d = 4.0 + 18.0 * (i / (double)M);
                    ps[i] = d * d;
                    const float u = (float)i / (float)M;
                    pc[4 * i + 0] = 0.15f + 0.75f * u;
                    pc[4 * i + 1] = 0.35f;
                    pc[4 * i + 2] = 0.9f - 0.7f * u;
                    pc[4 * i + 3] = 1.0f;
                }
                VplScatterDesc sd;
                memset(&sd, 0, sizeof(sd));
                sd.structSize = sizeof(sd);
                sd.set = VplScatterField_Alpha | VplScatterField_EdgeWidth |
                         VplScatterField_Label;
                sd.alpha = 0.8;
                sd.edgeWidth = 0.0;
                sd.label = "per-point colour + size";
                vplAxesScatter(ax, px_, py_, M, ps, pc, &sd, NULL);

                /* A second, uniform collection to show the plain form. */
                static double qx[12], qy[12];
                for (int i = 0; i < 12; ++i) {
                    qx[i] = 0.6 + i * 0.85;
                    qy[i] = 0.7;
                }
                memset(&sd, 0, sizeof(sd));
                sd.structSize = sizeof(sd);
                sd.set = VplScatterField_ColorSpec | VplScatterField_Marker |
                         VplScatterField_Size | VplScatterField_Label;
                sd.colorSpec = "tab:red";
                sd.marker = VplMarker_TriangleUp;
                sd.size = 120.0;
                sd.label = "uniform";
                vplAxesScatter(ax, qx, qy, 12, NULL, NULL, &sd, NULL);

                vplAxesSetXLabel(ax, "x");
                vplAxesSetYLabel(ax, "y");
                break;
            }

            case VplDemo_Spectral:
            case VplDemo_Specgram: {
                enum { SN = 2048 };
                static double sig[SN];
                for (int i = 0; i < SN; ++i) {
                    const double t = i / 200.0;
                    sig[i] = sin(2.0 * VPL_DEMO_PI * 18.0 * t) +
                             0.6 * sin(2.0 * VPL_DEMO_PI * 47.0 * t) +
                             0.25 * sin(2.0 * VPL_DEMO_PI * 9.5 * t + 0.8);
                }
                if (id == VplDemo_Spectral) {
                    vplAxesPsd(ax, sig, SN, 256, 200.0, 0, NULL);
                } else {
                    vplAxesSpecgram(ax, sig, SN, 256, 200.0, 128, NULL, VplSpecMode_Psd, VplSpecScale_Default, NULL, VplColormap_Viridis);
                    vplAxesSetXLabel(ax, "time (s)");
                    vplAxesSetYLabel(ax, "frequency (Hz)");
                }
                break;
            }

            case VplDemo_Inset: {
                enum { IN = 401 };
                static double ix[IN], iy[IN];
                for (int i = 0; i < IN; ++i) {
                    ix[i] = i * 8.0 / (IN - 1);
                    iy[i] = sin(ix[i]) * exp(-ix[i] * 0.12);
                }
                vplAxesPlot(ax, ix, iy, IN, NULL, NULL);

                VplAxes *ins = NULL;
                if (vplFigureInsetAxes(fig, ax, 0.52, 0.58, 0.42, 0.36, &ins) ==
                    VplResult_Success) {
                    vplAxesPlot(ins, ix, iy, IN, NULL, NULL);
                    vplAxesSetXLim(ins, 1.0, 2.2);
                    vplAxesSetYLim(ins, 0.7, 0.9);
                    vplAxesIndicateInsetZoom(ax, ins);
                }
                vplAxesSetXLabel(ax, "t");
                break;
            }

            case VplDemo_Secondary: {
                enum { CN = 121 };
                static double cx[CN], cy[CN];
                for (int i = 0; i < CN; ++i) {
                    cx[i] = i * 12.0 / (CN - 1);
                    cy[i] = 12.0 + 14.0 * sin(cx[i] * 0.6);
                }
                vplAxesPlot(ax, cx, cy, CN, NULL, NULL);
                vplAxesSetXLabel(ax, "hours");
                vplAxesSetYLabel(ax, "degrees C");
                /* Celsius to Fahrenheit on the right, hours to minutes on
                   top -- both affine, which is the case this supports. The
                   handle is required even when unused, as it is for every
                   other axes-creating call. */
                VplAxes *secY = NULL;
                VplAxes *secX = NULL;
                vplFigureSecondaryYAxis(fig, ax, 1, 1.8, 32.0, &secY);
                vplFigureSecondaryXAxis(fig, ax, 1, 60.0, 0.0, &secX);
                break;
            }

            case VplDemo_Quiver: {
                enum { QG = 9, QN = QG * QG };
                static double qx[QN], qy[QN], qu[QN], qv[QN];
                for (int r = 0; r < QG; ++r) {
                    for (int c = 0; c < QG; ++c) {
                        const int k = r * QG + c;
                        qx[k] = -2.0 + 4.0 * c / (QG - 1);
                        qy[k] = -2.0 + 4.0 * r / (QG - 1);
                        qu[k] = -qy[k];
                        qv[k] = qx[k] + 0.3 * sin(2.0 * qx[k]);
                    }
                }
                vplAxesQuiver(ax, qx, qy, qu, qv, QN, NULL);
                vplAxesQuiverKey(ax, 0.86, 0.94, 2.0, "2 units", 9.0);
                vplAxesSetXLabel(ax, "x");
                vplAxesSetYLabel(ax, "y");
                break;
            }

            case VplDemo_Barbs: {
                /* Speeds chosen so every feature appears: a calm circle, a
                   lone half barb, full barbs, and a flag. */
                static const double mags[12] = {0.0,  4.0,  7.0,  12.0, 18.0, 23.0,
                                                27.0, 35.0, 45.0, 55.0, 65.0, 80.0};
                static double bx[12], by[12], bu[12], bv[12];
                for (int i = 0; i < 12; ++i) {
                    const double ang = 25.0 * i * VPL_DEMO_PI / 180.0;
                    bx[i] = (double)(i % 4);
                    by[i] = (double)(i / 4);
                    bu[i] = mags[i] * cos(ang);
                    bv[i] = mags[i] * sin(ang);
                }
                vplAxesBarbs(ax, bx, by, bu, bv, 12, NULL);
                vplAxesSetXLabel(ax, "station");
                break;
            }

            case VplDemo_Stream: {
                enum { SG = 25 };
                static double sx[SG], sy[SG], su[SG * SG], sv[SG * SG];
                for (int i = 0; i < SG; ++i) {
                    sx[i] = -3.0 + 6.0 * i / (SG - 1);
                    sy[i] = -3.0 + 6.0 * i / (SG - 1);
                }
                for (int r = 0; r < SG; ++r) {
                    for (int c = 0; c < SG; ++c) {
                        /* A saddle plus a swirl, so the picture has both
                           closed orbits and lines that run off the edge. */
                        su[r * SG + c] = -sy[r] + 0.4 * sx[c];
                        sv[r * SG + c] = sx[c] + 0.4 * sy[r];
                    }
                }
                vplAxesStreamPlot(ax, sx, sy, su, sv, SG, SG, 1.0);
                vplAxesSetXLabel(ax, "x");
                vplAxesSetYLabel(ax, "y");
                break;
            }

            case VplDemo_Correlate: {
                enum { CN = 80 };
                static double cs[CN];
                for (int i = 0; i < CN; ++i) {
                    cs[i] = sin(i * 0.3) + 0.4 * cos(i * 0.11);
                }
                vplAxesACorr(ax, cs, CN, 20, 1);
                vplAxesSetXLabel(ax, "lag");
                vplAxesSetYLabel(ax, "correlation");
                break;
            }

            case VplDemo_GroupedBar: {
                static const double gh[12] = {3.0, 5.0, 2.0, 4.0,
                                              4.5, 2.5, 3.5, 1.5,
                                              1.0, 3.0, 4.0, 2.5};
                static const char *const gticks[4] = {"north", "south", "east", "west"};
                static const char *const gnames[3] = {"2022", "2023", "2024"};
                vplAxesGroupedBar(ax, gh, 3, 4, gticks, gnames, 1.5, 0.0);
                vplAxesSetLegend(ax, 1, VplLegendLoc_UpperRight);
                vplAxesSetYLabel(ax, "value");
                break;
            }

            case VplDemo_Pie: {
                static const double pv[5] = {35.0, 25.0, 20.0, 12.0, 8.0};
                static const char *const pl[5] = {"alpha", "beta", "gamma", "delta",
                                                  "epsilon"};
                vplAxesPie(ax, pv, 5);
                vplAxesPieLabel(ax, pl, 5, 0.6, 10.0);
                break;
            }

            case VplDemo_Table: {
                static const double tx[5] = {2020.0, 2021.0, 2022.0, 2023.0, 2024.0};
                static const double ty[5] = {12.0, 19.0, 17.0, 26.0, 31.0};
                vplAxesPlot(ax, tx, ty, 5, NULL, NULL);

                static const char *const cellText[] = {
                    "12", "4.1", "0.31",
                    "19", "5.0", "0.44",
                    "17", "4.6", "0.39",
                    "26", "6.2", "0.58",
                    "31", "7.0", "0.66"};
                static const char *const colLabels[] = {"count", "mean", "s.e."};
                static const char *const rowLabels[] = {"2020", "2021", "2022",
                                                        "2023", "2024"};

                VplTableDesc tdsc;
                memset(&tdsc, 0, sizeof(tdsc));
                tdsc.structSize = sizeof(tdsc);
                tdsc.set = VplTableField_ColLabels | VplTableField_RowLabels |
                           VplTableField_Loc;
                tdsc.colLabels = colLabels;
                tdsc.rowLabels = rowLabels;
                tdsc.loc = VplTableLoc_Bottom;
                vplAxesTable(ax, cellText, 5, 3, &tdsc);

                /*
                 * A table needs room made for it, and the x axis turned off.
                 * Nothing does this automatically -- not here and not in
                 * matplotlib, where a table at the default position lands on
                 * top of the tick labels and runs off the bottom of the figure
                 * unless you move the axes up yourself. The row labels already
                 * say what the x axis would.
                 */
                VplTickParams tp;
                if (vplAxesGetTickParams(ax, VplAxis_X, &tp) == VplResult_Success) {
                    tp.ticksVisible = 0;
                    tp.labelsVisible = 0;
                    vplAxesSetTickParams(ax, VplAxis_X, &tp);
                }

                VplSubplotParams sp;
                if (vplFigureGetSubplotParams(fig, &sp) == VplResult_Success) {
                    sp.bottom = 0.32;
                    vplFigureSubplotsAdjust(fig, &sp);
                }

                vplAxesSetYLabel(ax, "value");
                break;
            }

            case VplDemo_Boxes: {
                /* Four groups with deliberately different shapes: a long tail
                   with an outlier, a tight symmetric one, a skewed one, and
                   one whose spread is wide enough that nothing is a flier. */
                static const double bpv[] = {
                    1.0,  2.0,  3.0,  4.0,  5.0,  6.0,  7.0, 8.0, 20.0,
                    4.0,  4.5,  5.0,  5.2,  5.5,  6.0,
                    0.0,  1.0,  1.5,  2.0,  3.0,  10.0,
                    2.0,  4.0,  6.0,  8.0,  10.0, 12.0, 14.0};
                static const size_t bpc[4] = {9, 6, 6, 7};
                vplAxesBoxPlot(ax, bpv, bpc, 4);
                vplAxesSetXLabel(ax, "group");
                vplAxesSetYLabel(ax, "value");
                break;
            }

            case VplDemo_Bars: {
                static double bx[9], bh[9];
                for (int i = 0; i < 9; ++i) {
                    bx[i] = i;
                    bh[i] = 3.0 + 2.0 * sin(i * 0.8);
                }
                memset(&pd, 0, sizeof(pd));
                pd.structSize = sizeof(pd);
                pd.set = VplPatchField_FaceColorSpec | VplPatchField_EdgeWidth |
                         VplPatchField_Label;
                pd.faceColorSpec = "C2";
                pd.edgeWidth = 0.8;
                pd.label = "counts";
                vplAxesBar(ax, bx, bh, p->barWidth, NULL, 9, &pd, NULL);
                vplAxesSetXLabel(ax, "bin");
                break;
            }

            case VplDemo_TextAnnotate: {
                memset(&ld, 0, sizeof(ld));
                ld.structSize = sizeof(ld);
                ld.set = VplLineField_Label;
                ld.label = "signal";
                vplAxesPlot(ax, x, a, kVplDemoN, &ld, NULL);

                VplTextDesc td;
                memset(&td, 0, sizeof(td));
                td.structSize = sizeof(td);
                td.set = VplTextField_Align | VplTextField_ColorSpec | VplTextField_Size;
                td.halign = VplHAlign_Center;
                td.valign = VplVAlign_Bottom;
                td.colorSpec = "tab:red";
                td.size = 11.0;
                /* sin peaks at pi/2. */
                vplAxesAnnotate(ax, "peak", VPL_DEMO_PI / 2.0, 1.0, 2.6, 0.55, &td);

                td.set = VplTextField_Align | VplTextField_ColorSpec;
                td.halign = VplHAlign_Center;
                td.valign = VplVAlign_Top;
                td.colorSpec = "tab:purple";
                vplAxesAnnotate(ax, "trough", 3.0 * VPL_DEMO_PI / 2.0, -1.0, 3.5, -0.55, &td);

                memset(&td, 0, sizeof(td));
                td.structSize = sizeof(td);
                td.set = VplTextField_Align | VplTextField_Size;
                td.halign = VplHAlign_Left;
                td.valign = VplVAlign_Center;
                td.size = 10.0;
                vplAxesText(ax, 0.2, 0.75, "plain text, left/center", &td);

                td.set = VplTextField_Align | VplTextField_Rotation | VplTextField_ColorSpec;
                td.halign = VplHAlign_Center;
                td.valign = VplVAlign_Center;
                td.rotation = 30.0;
                td.colorSpec = "0.35";
                vplAxesText(ax, 4.6, 0.6, "rotated 30 deg", &td);
                break;
            }

            case VplDemo_ReferenceLines: {
                memset(&ld, 0, sizeof(ld));
                ld.structSize = sizeof(ld);
                ld.set = VplLineField_Label;
                ld.label = "signal";
                vplAxesPlot(ax, x, a, kVplDemoN, &ld, NULL);

                memset(&ld, 0, sizeof(ld));
                ld.structSize = sizeof(ld);
                ld.set = VplLineField_ColorSpec | VplLineField_LineStyle;
                ld.colorSpec = "0.4";
                ld.linestyle = VplLineStyle_Dashed;
                vplAxesAxHLine(ax, 0.0, &ld);

                ld.colorSpec = "tab:red";
                ld.linestyle = VplLineStyle_DashDot;
                vplAxesAxVLine(ax, VPL_DEMO_PI, &ld);
                break;
            }

            case VplDemo_GridLegend: {
                memset(&ld, 0, sizeof(ld));
                ld.structSize = sizeof(ld);
                ld.set = VplLineField_Label;
                ld.label = "sin";
                vplAxesPlot(ax, x, a, kVplDemoN, &ld, NULL);
                ld.label = "cos";
                vplAxesPlot(ax, x, b, kVplDemoN, &ld, NULL);
                break;
            }

            case VplDemo_LegendTitle: {
                memset(&ld, 0, sizeof(ld));
                ld.structSize = sizeof(ld);
                ld.set = VplLineField_Label;
                ld.label = "sin";
                vplAxesPlot(ax, x, a, kVplDemoN, &ld, NULL);
                ld.label = "cos";
                vplAxesPlot(ax, x, b, kVplDemoN, &ld, NULL);
                vplAxesSetLegend(ax, 1, VplLegendLoc_Best);
                vplAxesSetLegendTitle(ax, "Signals");
                break;
            }

            case VplDemo_LegendNCols: {
                static const char *const labels[5] = {"sin", "a very long cosine label",
                                                       "x", "y squared", "z"};
                static double ys[5][kVplDemoN];
                for (int k = 0; k < 5; ++k) {
                    for (int i = 0; i < kVplDemoN; ++i) {
                        ys[k][i] = sin(x[i] + k);
                    }
                    memset(&ld, 0, sizeof(ld));
                    ld.structSize = sizeof(ld);
                    ld.set = VplLineField_Label;
                    ld.label = labels[k];
                    vplAxesPlot(ax, x, ys[k], kVplDemoN, &ld, NULL);
                }
                vplAxesSetLegend(ax, 1, VplLegendLoc_Best);
                vplAxesSetLegendNCols(ax, 2);
                break;
            }

            case VplDemo_Triplot:
            case VplDemo_TriContour:
            case VplDemo_TriContourF:
            case VplDemo_TriPcolor: {
                /* A lattice pushed off its rows on purpose. On a perfect grid
                   four points share a circumcircle, the triangulation is
                   genuinely ambiguous and which diagonal you get is down to
                   arithmetic; perturbed, it is unique and reproducible. */
                enum { TG = 7, TN = TG * TG };
                static double tx[TN], ty[TN], tz[TN];
                for (int i = 0; i < TG; ++i) {
                    for (int j = 0; j < TG; ++j) {
                        const double px = i + 0.13 * sin(2.7 * (i + j));
                        const double py = j + 0.11 * cos(1.9 * (i - j));
                        tx[i * TG + j] = px;
                        ty[i * TG + j] = py;
                        tz[i * TG + j] = sin(0.8 * px) * cos(0.6 * py);
                    }
                }
                static const double tlevels[5] = {-0.5, -0.2, 0.1, 0.4, 0.7};

                if (id == VplDemo_Triplot) {
                    vplAxesTriplot(ax, tx, ty, TN);
                } else if (id == VplDemo_TriContour) {
                    vplAxesTriContour(ax, tx, ty, tz, TN, tlevels, 5, NULL, NULL);
                } else if (id == VplDemo_TriContourF) {
                    vplAxesTriContourF(ax, tx, ty, tz, TN, tlevels, 5, NULL, NULL);
                } else {
                    vplAxesTriPcolor(ax, tx, ty, tz, TN);
                }
                vplAxesSetXLabel(ax, "x");
                vplAxesSetYLabel(ax, "y");
                break;
            }

            case VplDemo_Hexbin: {
                /* Clustered on purpose: a uniform spread would colour every
                   hexagon alike and show neither the binning nor the drop. */
                enum { HN = 400 };
                static double hx[HN], hy[HN];
                for (int i = 0; i < HN; ++i) {
                    const double t = i * 0.37;
                    hx[i] = 2.0 + cos(t) * (0.4 + 0.006 * i);
                    hy[i] = 2.0 + sin(t * 1.3) * (0.4 + 0.006 * i);
                }
                vplAxesHexbin(ax, hx, hy, HN, 14);
                vplAxesSetXLabel(ax, "x");
                vplAxesSetYLabel(ax, "y");
                break;
            }

            case VplDemo_Imshow:
            case VplDemo_Clabel:
            case VplDemo_Contour: {
                /* A smooth field with a couple of extrema, so the colours and
                   the iso-lines both have something to show. */
                enum { R = 48, C = 64 };
                static double field[R * C];
                for (int r = 0; r < R; ++r) {
                    for (int cc = 0; cc < C; ++cc) {
                        const double fx = (cc / (double)(C - 1)) * 6.0 - 3.0;
                        const double fy = (r / (double)(R - 1)) * 4.0 - 2.0;
                        field[r * C + cc] =
                            sin(fx) * cos(fy) + 0.4 * exp(-(fx * fx + fy * fy) / 2.0);
                    }
                }

                VplFieldDesc fdsc;
                memset(&fdsc, 0, sizeof(fdsc));
                fdsc.structSize = sizeof(fdsc);

                if (id == VplDemo_Imshow) {
                    fdsc.set = VplFieldField_Colormap | VplFieldField_Label;
                    fdsc.cmap = VplColormap_Viridis;
                    fdsc.label = "field";
                    vplAxesImshow(ax, field, R, C, &fdsc, NULL);
                } else {
                    static double levels[9];
                    for (int k = 0; k < 9; ++k) {
                        levels[k] = -1.2 + k * 0.3;
                    }
                    fdsc.set = VplFieldField_Colormap | VplFieldField_LineWidth |
                               VplFieldField_Label;
                    fdsc.cmap = VplColormap_Plasma;
                    fdsc.lineWidth = 1.4;
                    fdsc.label = "iso-lines";

                    VplContour *cset = NULL;
                    vplAxesContour(ax, field, R, C, levels, 9, &fdsc, &cset);

                    if (id == VplDemo_Clabel && cset != NULL) {
                        /* Nine levels over a field this smooth is more lines
                           than there is room to label; the crowding rule sorts
                           that out by itself, which is the point worth
                           showing. */
                        VplClabelDesc cdsc;
                        memset(&cdsc, 0, sizeof(cdsc));
                        cdsc.structSize = sizeof(cdsc);
                        cdsc.set = VplClabelField_FontSize;
                        cdsc.fontSize = 9.0;
                        vplContourClabel(cset, &cdsc);
                    }
                }

                vplAxesSetXLabel(ax, "column");
                vplAxesSetYLabel(ax, "row");
                break;
            }

            case VplDemo_MinorTicks: {
                memset(&ld, 0, sizeof(ld));
                ld.structSize = sizeof(ld);
                ld.set = VplLineField_Label;
                ld.label = "sin";
                vplAxesPlot(ax, x, a, kVplDemoN, &ld, NULL);
                ld.label = "damped";
                vplAxesPlot(ax, x, c, kVplDemoN, &ld, NULL);
                vplAxesSetMinorTicks(ax, 1, 1);
                vplAxesSetXLabel(ax, "minor ticks on both axes");
                break;
            }

            case VplDemo_LogScale: {
                static double lx[60], ly[60], lz[60];
                for (int i = 0; i < 60; ++i) {
                    lx[i] = pow(10.0, i * 4.0 / 59.0);
                    ly[i] = lx[i] * lx[i];
                    lz[i] = 1000.0 * sqrt(lx[i]);
                }
                memset(&ld, 0, sizeof(ld));
                ld.structSize = sizeof(ld);
                ld.set = VplLineField_Label;
                ld.label = "O(n^2)";
                vplAxesPlot(ax, lx, ly, 60, &ld, NULL);
                ld.label = "O(sqrt n)";
                vplAxesPlot(ax, lx, lz, 60, &ld, NULL);
                vplAxesSetXScale(ax, p->logX ? VplScale_Log : VplScale_Linear);
                vplAxesSetYScale(ax, p->logY ? VplScale_Log : VplScale_Linear);
                vplAxesSetXLabel(ax, "n");
                break;
            }

            case VplDemo_AxisLimits: {
                memset(&ld, 0, sizeof(ld));
                ld.structSize = sizeof(ld);
                ld.set = VplLineField_Label;
                ld.label = "sin";
                vplAxesPlot(ax, x, a, kVplDemoN, &ld, NULL);
                if (p->pinX) {
                    vplAxesSetXLim(ax, p->xlimLo, p->xlimHi);
                }
                vplAxesSetXLabel(ax, p->pinX ? "x pinned, y autoscaling" : "both autoscaling");
                break;
            }

            case VplDemo_TwinX: {
                memset(&ld, 0, sizeof(ld));
                ld.structSize = sizeof(ld);
                ld.set = VplLineField_Width | VplLineField_ColorSpec;
                ld.width = p->lineWidth;
                ld.colorSpec = "C0";
                vplAxesPlot(ax, x, a, kVplDemoN, &ld, NULL);
                vplAxesSetXLabel(ax, "t");
                vplAxesSetYLabel(ax, "sin");

                VplAxes *ax2 = NULL;
                if (vplFigureTwinX(fig, ax, &ax2) == VplResult_Success) {
                    static double scaled[kVplDemoN];
                    for (int i = 0; i < kVplDemoN; ++i) {
                        scaled[i] = 100.0 * c[i];
                    }
                    ld.colorSpec = "C1";
                    vplAxesPlot(ax2, x, scaled, kVplDemoN, &ld, NULL);
                    vplAxesSetYLabel(ax2, "damped, x100");
                }
                break;
            }

            case VplDemo_Colorbar: {
                memset(&ld, 0, sizeof(ld));
                ld.structSize = sizeof(ld);
                ld.set = VplLineField_Width;
                ld.width = p->lineWidth;
                vplAxesPlot(ax, x, a, kVplDemoN, &ld, NULL);
                vplAxesSetXLabel(ax, "t");

                VplAxes *cax = NULL;
                if (vplFigureColorbar(fig, ax, VplColormap_Viridis, 0.0, 1.0, &cax) ==
                    VplResult_Success) {
                    vplAxesSetYLabel(cax, "value");
                }
                break;
            }

            case VplDemo_ColorbarHorizontal: {
                memset(&ld, 0, sizeof(ld));
                ld.structSize = sizeof(ld);
                ld.set = VplLineField_Width;
                ld.width = p->lineWidth;
                vplAxesPlot(ax, x, a, kVplDemoN, &ld, NULL);
                vplAxesSetXLabel(ax, "t");

                VplAxes *cax = NULL;
                if (vplFigureColorbarOriented(fig, ax, VplColormap_Viridis, 0.0, 1.0,
                                              /*horizontal=*/1, &cax) == VplResult_Success) {
                    vplAxesSetXLabel(cax, "value");
                }
                break;
            }

            case VplDemo_Histogram: {
                /* A bimodal sample, built from the same closed form every time
                   so the panel does not change between runs. */
                static double sample[240];
                for (int i = 0; i < 240; ++i) {
                    const double t = (double)i / 239.0;
                    sample[i] = (i % 2 == 0) ? 2.0 + 1.4 * sin(t * 9.0)
                                             : 6.0 + 1.1 * cos(t * 7.0);
                }
                memset(&pd, 0, sizeof(pd));
                pd.structSize = sizeof(pd);
                pd.set = VplPatchField_EdgeWidth;
                pd.edgeWidth = 0.8;
                vplAxesHist(ax, sample, 240, 16, 0, &pd, NULL);
                vplAxesSetXLabel(ax, "value");
                vplAxesSetYLabel(ax, "count");
                break;
            }

            case VplDemo_Ecdf: {
                static double sample[60];
                for (int i = 0; i < 60; ++i) {
                    sample[i] = 3.0 + 2.0 * sin((double)i * 0.7) + 0.02 * (double)i;
                }
                vplAxesEcdf(ax, sample, 60, NULL, 0, 0, NULL, NULL);
                vplAxesSetXLabel(ax, "value");
                vplAxesSetYLabel(ax, "F(value)");
                break;
            }

            case VplDemo_Violins: {
                static double values[3 * 40];
                static size_t counts[3] = {40, 40, 40};
                for (int g = 0; g < 3; ++g) {
                    for (int i = 0; i < 40; ++i) {
                        const double t = (double)i / 39.0;
                        values[g * 40 + i] =
                            2.0 + (double)g + 1.2 * sin(t * (6.0 + g)) + 0.4 * cos(t * 11.0);
                    }
                }
                vplAxesViolinPlot(ax, values, counts, 3);
                vplAxesSetXLabel(ax, "group");
                break;
            }

            case VplDemo_BoxStats: {
                /* The five numbers a box needs, with no sample behind them --
                   which is the whole reason bxp exists next to boxplot. */
                static double fliers0[2] = {9.4, -1.1};
                VplBoxStats stats[3];
                memset(stats, 0, sizeof(stats));
                for (int k = 0; k < 3; ++k) {
                    stats[k].structSize = sizeof(VplBoxStats);
                    stats[k].q1 = 2.0 + 0.5 * k;
                    stats[k].median = 3.5 + 0.5 * k;
                    stats[k].q3 = 5.0 + 0.5 * k;
                    stats[k].whisLo = 0.8 + 0.5 * k;
                    stats[k].whisHi = 7.2 + 0.5 * k;
                }
                stats[0].fliers = fliers0;
                stats[0].flierCount = 2;
                vplAxesBxp(ax, stats, 3);
                vplAxesSetYLabel(ax, "value");
                break;
            }

            case VplDemo_Stem: {
                vplAxesStem(ax, sx, sa, kVplDemoSparse, NULL, NULL, NULL, 0.0);
                vplAxesSetXLabel(ax, "t");
                break;
            }

            case VplDemo_Stairs: {
                static double values[8];
                static double edges[9];
                for (int i = 0; i < 9; ++i) {
                    edges[i] = (double)i * 0.75;
                }
                for (int i = 0; i < 8; ++i) {
                    values[i] = 1.0 + 0.9 * sin((double)i * 0.8);
                }
                vplAxesStairs(ax, values, edges, 8, 0, NULL, 0, 0, NULL, NULL);
                vplAxesSetXLabel(ax, "bin edge");
                break;
            }

            case VplDemo_Steps: {
                /* All three positions on one axes, offset so they can be told
                   apart: pre steps at the left edge, post at the right, mid
                   halfway between. */
                static double shifted[kVplDemoSparse];
                const VplStepWhere where[3] = {VplStepWhere_Pre, VplStepWhere_Post,
                                               VplStepWhere_Mid};
                const char *labels[3] = {"pre", "post", "mid"};
                for (int k = 0; k < 3; ++k) {
                    for (int i = 0; i < kVplDemoSparse; ++i) {
                        shifted[i] = sa[i] + 2.5 * (double)k;
                    }
                    memset(&ld, 0, sizeof(ld));
                    ld.structSize = sizeof(ld);
                    ld.set = VplLineField_Width | VplLineField_Label;
                    ld.width = p->lineWidth;
                    ld.label = labels[k];
                    vplAxesStep(ax, sx, shifted, kVplDemoSparse, where[k], &ld, NULL);
                }
                vplAxesSetXLabel(ax, "t");
                break;
            }

            case VplDemo_StackedAreas: {
                static double ys[3 * kVplDemoSparse];
                for (int i = 0; i < kVplDemoSparse; ++i) {
                    ys[i] = 1.0 + 0.5 * sin(sx[i]);
                    ys[kVplDemoSparse + i] = 1.5 + 0.5 * cos(sx[i]);
                    ys[2 * kVplDemoSparse + i] = 0.8 + 0.3 * sin(2.0 * sx[i]);
                }
                vplAxesStackPlot(ax, sx, ys, 3, kVplDemoSparse, VplStackBaseline_Zero);
                vplAxesSetXLabel(ax, "t");
                break;
            }

            case VplDemo_EventPlot: {
                static double up[14], across[11];
                for (int i = 0; i < 14; ++i) {
                    up[i] = 0.4 + 0.35 * (double)i + 0.2 * sin((double)i);
                }
                for (int i = 0; i < 11; ++i) {
                    across[i] = 0.6 + 0.45 * (double)i + 0.25 * cos((double)i);
                }
                vplAxesEventPlot(ax, up, 14, 1.0, 1.0, 1, NULL, NULL);
                vplAxesEventPlot(ax, across, 11, 3.0, 1.0, 0, NULL, NULL);
                vplAxesSetXLabel(ax, "position");
                break;
            }

            case VplDemo_HorizontalBars: {
                static double ys[6], widths[6];
                for (int i = 0; i < 6; ++i) {
                    ys[i] = (double)i;
                    widths[i] = 1.0 + 0.7 * (double)i;
                }
                memset(&pd, 0, sizeof(pd));
                pd.structSize = sizeof(pd);
                pd.set = VplPatchField_FaceColorSpec;
                pd.faceColorSpec = "C2";
                VplPatch *bars = NULL;
                vplAxesBarH(ax, ys, widths, 0.7, NULL, 6, &pd, &bars);
                vplAxesBarLabel(ax, bars);
                vplAxesSetXLabel(ax, "width");
                break;
            }

            case VplDemo_Intervals: {
                static double starts[4] = {0.0, 1.8, 3.2, 5.0};
                static double widths[4] = {1.5, 1.0, 1.4, 1.2};
                static double starts2[3] = {0.6, 2.4, 4.6};
                static double widths2[3] = {1.2, 1.4, 1.8};
                vplAxesBrokenBarH(ax, starts, widths, 4, 10.0, 3.0, NULL, NULL);
                vplAxesBrokenBarH(ax, starts2, widths2, 3, 15.0, 3.0, NULL, NULL);
                vplAxesSetXLabel(ax, "time");
                vplAxesSetYLabel(ax, "track");
                break;
            }

            case VplDemo_Polygons: {
                static double px[24], py[24];
                for (int i = 0; i < 24; ++i) {
                    const double t = 2.0 * VPL_DEMO_PI * (double)i / 24.0;
                    px[i] = 2.0 + 1.4 * cos(t);
                    py[i] = 1.0 + 0.8 * sin(t);
                }
                vplAxesFill(ax, px, py, 24, NULL, NULL);

                static double band_y[kVplDemoSparse], left[kVplDemoSparse],
                    right[kVplDemoSparse];
                for (int i = 0; i < kVplDemoSparse; ++i) {
                    band_y[i] = sx[i] * 0.5 - 1.5;
                    left[i] = 4.5 + 0.4 * sin(sx[i]);
                    right[i] = 5.6 + 0.4 * cos(sx[i]);
                }
                memset(&pd, 0, sizeof(pd));
                pd.structSize = sizeof(pd);
                pd.set = VplPatchField_Alpha;
                pd.alpha = p->bandAlpha;
                vplAxesFillBetweenX(ax, band_y, left, right, kVplDemoSparse, NULL, VplFillStep_None, 0, &pd, NULL);
                break;
            }

            case VplDemo_Spans: {
                memset(&ld, 0, sizeof(ld));
                ld.structSize = sizeof(ld);
                ld.set = VplLineField_Width;
                ld.width = p->lineWidth;
                vplAxesPlot(ax, x, a, kVplDemoN, &ld, NULL);
                vplAxesAxHSpan(ax, 0.35, 0.75);
                vplAxesAxVSpan(ax, 1.8, 2.8);
                vplAxesSetXLabel(ax, "t");
                break;
            }

            case VplDemo_Rules: {
                static double hy[3] = {0.6, 1.4, 2.2};
                static double hx0[3] = {0.2, 0.8, 1.4};
                static double hx1[3] = {3.4, 3.0, 2.6};
                static double vxs[3] = {0.9, 1.9, 2.9};
                static double vy0[3] = {0.2, 0.4, 0.1};
                static double vy1[3] = {2.6, 2.4, 2.0};
                vplAxesHLines(ax, hy, hx0, hx1, 3, NULL, NULL);
                vplAxesVLines(ax, vxs, vy0, vy1, 3, NULL, NULL);

                memset(&ld, 0, sizeof(ld));
                ld.structSize = sizeof(ld);
                ld.set = VplLineField_ColorSpec | VplLineField_LineStyle;
                ld.colorSpec = "C3";
                ld.linestyle = VplLineStyle_Dashed;
                vplAxesAxLine(ax, 0.0, 0.0, 0.8, 0, &ld);
                break;
            }

            case VplDemo_Arrow: {
                memset(&pd, 0, sizeof(pd));
                pd.structSize = sizeof(pd);
                pd.set = VplPatchField_FaceColorSpec;
                pd.faceColorSpec = "C0";
                vplAxesArrow(ax, 0.0, 0.0, 2.0, 1.2, 0.12, &pd, NULL);
                pd.faceColorSpec = "C1";
                vplAxesArrow(ax, 0.0, 2.0, 2.4, -0.8, 0.06, &pd, NULL);
                vplAxesSetXLabel(ax, "dx");
                vplAxesSetYLabel(ax, "dy");
                break;
            }

            case VplDemo_ContourFilled: {
                enum { kGrid = 33 };
                static double field[kGrid * kGrid];
                for (int r = 0; r < kGrid; ++r) {
                    const double fy = -2.0 + 4.0 * (double)r / (double)(kGrid - 1);
                    for (int cc = 0; cc < kGrid; ++cc) {
                        const double fx = -2.0 + 4.0 * (double)cc / (double)(kGrid - 1);
                        field[r * kGrid + cc] =
                            exp(-((fx - 0.7) * (fx - 0.7) + fy * fy)) +
                            exp(-((fx + 0.7) * (fx + 0.7) + fy * fy));
                    }
                }
                static const double levels[6] = {0.1, 0.25, 0.45, 0.65, 0.85, 1.05};
                vplAxesContourF(ax, field, kGrid, kGrid, levels, 6, NULL, NULL);
                break;
            }

            case VplDemo_Matrix: {
                static double m[24];
                for (int i = 0; i < 24; ++i) {
                    m[i] = (double)((i * 7) % 11);
                }
                vplAxesMatShow(ax, m, 4, 6, NULL, NULL);

                /* Same matrix, thresholded, through spy -- shown on an inset so
                   both conventions are visible at once. */
                VplAxes *pattern = NULL;
                if (vplFigureAddAxes(fig, 0.62, 0.62, 0.3, 0.26, &pattern) ==
                    VplResult_Success) {
                    static double sparse[24];
                    for (int i = 0; i < 24; ++i) {
                        sparse[i] = (m[i] > 6.0) ? m[i] : 0.0;
                    }
                    vplAxesSpy(pattern, sparse, 4, 6, NULL, NULL);
                }
                break;
            }

            case VplDemo_Mesh: {
                static const double xedges[6] = {0.0, 0.5, 1.5, 3.0, 3.5, 5.0};
                static const double yedges[5] = {0.0, 1.0, 1.2, 2.5, 4.0};
                static double cells[20];
                for (int i = 0; i < 20; ++i) {
                    cells[i] = (double)((i * 3) % 7);
                }
                vplAxesPcolorMesh(ax, xedges, yedges, cells, 4, 5, NULL, NULL);

                VplAxes *counts = NULL;
                if (vplFigureAddAxes(fig, 0.60, 0.60, 0.32, 0.28, &counts) ==
                    VplResult_Success) {
                    static double hx[64], hy[64];
                    for (int i = 0; i < 64; ++i) {
                        hx[i] = 2.0 + 1.6 * sin((double)i * 0.9);
                        hy[i] = 1.0 + 1.3 * cos((double)i * 0.6);
                    }
                    vplAxesHist2D(counts, hx, hy, 64, 5, 5);
                }
                break;
            }

            case VplDemo_SpectrumParts: {
                static double signal[256];
                for (int i = 0; i < 256; ++i) {
                    const double t = (double)i / 256.0;
                    signal[i] = sin(2.0 * VPL_DEMO_PI * 12.0 * t) +
                                0.5 * sin(2.0 * VPL_DEMO_PI * 31.0 * t + 0.7);
                }
                vplAxesMagnitudeSpectrum(ax, signal, 256, 256.0, 0, NULL);

                VplAxes *ph = NULL;
                if (vplFigureAddAxes(fig, 0.58, 0.62, 0.33, 0.26, &ph) == VplResult_Success) {
                    vplAxesPhaseSpectrum(ph, signal, 256, 256.0, NULL);
                }
                VplAxes *an = NULL;
                if (vplFigureAddAxes(fig, 0.58, 0.26, 0.33, 0.26, &an) == VplResult_Success) {
                    vplAxesAngleSpectrum(an, signal, 256, 256.0, NULL);
                }
                vplAxesSetXLabel(ax, "frequency");
                break;
            }

            case VplDemo_CrossSpectrum: {
                static double u[256], v[256];
                for (int i = 0; i < 256; ++i) {
                    const double t = (double)i / 256.0;
                    const double shared = sin(2.0 * VPL_DEMO_PI * 18.0 * t);
                    u[i] = shared + 0.4 * sin(2.0 * VPL_DEMO_PI * 47.0 * t);
                    v[i] = shared + 0.4 * cos(2.0 * VPL_DEMO_PI * 63.0 * t);
                }
                vplAxesCsd(ax, u, v, 256, 128, 256.0, 64, NULL);

                VplAxes *coh = NULL;
                if (vplFigureAddAxes(fig, 0.58, 0.60, 0.33, 0.28, &coh) == VplResult_Success) {
                    vplAxesCohere(coh, u, v, 256, 128, 256.0, 64, NULL);
                }
                break;
            }

            case VplDemo_CrossCorrelation: {
                static double u[128], v[128];
                for (int i = 0; i < 128; ++i) {
                    u[i] = sin((double)i * 0.35) + 0.3 * sin((double)i * 1.1);
                    /* v is u delayed by eight samples, so the peak lands there. */
                    const int j = i - 8;
                    v[i] = (j < 0) ? 0.0 : sin((double)j * 0.35) + 0.3 * sin((double)j * 1.1);
                }
                vplAxesXCorr(ax, u, v, 128, 24, 1);
                vplAxesSetXLabel(ax, "lag");
                break;
            }

            case VplDemo_Spines: {
                memset(&ld, 0, sizeof(ld));
                ld.structSize = sizeof(ld);
                ld.set = VplLineField_Width;
                ld.width = p->lineWidth;
                vplAxesPlot(ax, x, a, kVplDemoN, &ld, NULL);
                vplAxesSetSpineVisible(ax, VplSpine_Top, 0);
                vplAxesSetSpineVisible(ax, VplSpine_Right, 0);
                vplAxesSetXLabel(ax, "t");

                /* Two more axes showing the other two settings, so the three
                   can be compared without switching panels. */
                VplAxes *framed = NULL;
                if (vplFigureAddAxes(fig, 0.62, 0.60, 0.28, 0.28, &framed) ==
                    VplResult_Success) {
                    vplAxesPlot(framed, x, b, kVplDemoN, &ld, NULL);
                    vplAxesSetFrameOn(framed, 0);
                }
                VplAxes *bare = NULL;
                if (vplFigureAddAxes(fig, 0.62, 0.20, 0.28, 0.28, &bare) ==
                    VplResult_Success) {
                    vplAxesPlot(bare, x, c, kVplDemoN, &ld, NULL);
                    vplAxesSetAxisOn(bare, 0);
                }
                break;
            }

            case VplDemo_TickPositions: {
                memset(&ld, 0, sizeof(ld));
                ld.structSize = sizeof(ld);
                ld.set = VplLineField_Width;
                ld.width = p->lineWidth;
                vplAxesPlot(ax, x, a, kVplDemoN, &ld, NULL);

                static const double xticks[5] = {0.0, 1.5707963267948966,
                                                 3.141592653589793, 4.71238898038469,
                                                 6.283185307179586};
                static const char *const xlabels[5] = {"0", "pi/2", "pi", "3pi/2", "2pi"};
                static const double yticks[3] = {-1.0, 0.0, 1.0};
                vplAxesSetXTicks(ax, xticks, xlabels, 5);
                vplAxesSetYTicks(ax, yticks, NULL, 3);
                break;
            }

            case VplDemo_Inverted: {
                memset(&ld, 0, sizeof(ld));
                ld.structSize = sizeof(ld);
                ld.set = VplLineField_Width;
                ld.width = p->lineWidth;
                vplAxesPlot(ax, x, c, kVplDemoN, &ld, NULL);
                vplAxesSetYInverted(ax, 1);
                vplAxesSetXLabel(ax, "t");
                vplAxesSetYLabel(ax, "down is more");

                VplAxes *both = NULL;
                if (vplFigureAddAxes(fig, 0.60, 0.60, 0.30, 0.28, &both) ==
                    VplResult_Success) {
                    vplAxesPlot(both, x, a, kVplDemoN, &ld, NULL);
                    vplAxesSetXInverted(both, 1);
                    vplAxesSetYInverted(both, 1);
                }
                break;
            }

            case VplDemo_AspectMargins: {
                /* A circle, which only looks like one when the data units are
                   square. */
                static double cx[72], cy[72];
                for (int i = 0; i < 72; ++i) {
                    const double t = 2.0 * VPL_DEMO_PI * (double)i / 71.0;
                    cx[i] = cos(t);
                    cy[i] = sin(t);
                }
                memset(&ld, 0, sizeof(ld));
                ld.structSize = sizeof(ld);
                ld.set = VplLineField_Width;
                ld.width = p->lineWidth;
                vplAxesPlot(ax, cx, cy, 72, &ld, NULL);
                vplAxesSetAspect(ax, 1.0);
                /* 'box' is the default and what vplot draws; naming it here
                   keeps the call demonstrated. */
                vplAxesSetAdjustable(ax, VplAdjustable_Box);
                vplAxesSetMargins(ax, 0.25, 0.25);
                /* The circle has no sticky edges, so honouring them (the
                   default) changes nothing here -- it just names the call. */
                vplAxesSetUseStickyEdges(ax, 1);
                vplAxesAutoscale(ax, 1, VplAxisSel_Both, -1);

                VplAxes *pinned = NULL;
                if (vplFigureAddAxes(fig, 0.62, 0.62, 0.28, 0.26, &pinned) ==
                    VplResult_Success) {
                    vplAxesPlot(pinned, cx, cy, 72, &ld, NULL);
                    /* The same circle with the view stated outright rather than
                       derived from the data. */
                    vplAxesSetViewLimits(pinned, -2.0, -1.2, 2.0, 1.2);
                }
                break;
            }

            case VplDemo_GridDepth: {
                memset(&pd, 0, sizeof(pd));
                pd.structSize = sizeof(pd);
                pd.set = VplPatchField_FaceColorSpec;
                pd.faceColorSpec = "C0";
                static double bx[6], bh[6];
                for (int i = 0; i < 6; ++i) {
                    bx[i] = (double)i;
                    bh[i] = 1.0 + 0.6 * (double)((i * 3) % 5);
                }
                vplAxesBar(ax, bx, bh, 0.7, NULL, 6, &pd, NULL);
                vplAxesSetGrid(ax, 1, VplGridWhich_Major, VplGridAxis_Both);
                /* Over everything, so the grid rules across the bars rather
                   than disappearing behind them. */
                vplAxesSetAxisBelow(ax, VplAxisBelow_Off);
                break;
            }

            case VplDemo_ErrorBox: {
                static double ex[kVplDemoSparse];
                for (int i = 0; i < kVplDemoSparse; ++i) {
                    ex[i] = 0.10 + 0.05 * fabs(sin(sx[i]));
                }
                memset(&ld, 0, sizeof(ld));
                ld.structSize = sizeof(ld);
                ld.set = VplLineField_Width | VplLineField_Marker;
                ld.width = p->lineWidth;
                ld.marker = VplMarker_Circle;

                VplLine *line = NULL;
                vplAxesErrorBar(ax, sx, sa, se, kVplDemoSparse, p->capSize, &ld, &line);
                vplAxesSetXErr(ax, line, ex, kVplDemoSparse);
                vplAxesSetXLabel(ax, "t");
                break;
            }

            case VplDemo_TwinY: {
                memset(&ld, 0, sizeof(ld));
                ld.structSize = sizeof(ld);
                ld.set = VplLineField_Width;
                ld.width = p->lineWidth;
                vplAxesPlot(ax, x, a, kVplDemoN, &ld, NULL);
                vplAxesSetXLabel(ax, "t");

                VplAxes *top = NULL;
                if (vplFigureTwinY(fig, ax, &top) == VplResult_Success) {
                    static double scaled[kVplDemoN];
                    for (int i = 0; i < kVplDemoN; ++i) {
                        scaled[i] = 100.0 * x[i];
                    }
                    ld.set |= VplLineField_ColorSpec;
                    ld.colorSpec = "C1";
                    vplAxesPlot(top, scaled, a, kVplDemoN, &ld, NULL);
                    vplAxesSetXLabel(top, "t, x100");
                }
                break;
            }

            case VplDemo_InsetIndicator: {
                memset(&ld, 0, sizeof(ld));
                ld.structSize = sizeof(ld);
                ld.set = VplLineField_Width;
                ld.width = p->lineWidth;
                vplAxesPlot(ax, x, c, kVplDemoN, &ld, NULL);
                vplAxesSetXLabel(ax, "t");

                VplAxes *zoom = NULL;
                if (vplFigureAddAxes(fig, 0.55, 0.58, 0.32, 0.28, &zoom) ==
                    VplResult_Success) {
                    vplAxesPlot(zoom, x, c, kVplDemoN, &ld, NULL);
                    vplAxesSetViewLimits(zoom, 0.4, -0.2, 1.6, 0.9);
                    /* And mark on the parent which region the inset magnifies,
                       which is the half a bare inset leaves the reader to
                       guess. */
                    vplAxesIndicateInset(ax, 0.4, -0.2, 1.2, 1.1, zoom);
                }
                break;
            }

            case VplDemo_PageSize: {
                /* Set before anything is measured: the layout is computed in
                   pixels, and both of these change what a pixel is worth. */
                vplFigureSetSizeInches(fig, 6.0, 4.0);
                vplFigureSetDpi(fig, 110.0);

                memset(&ld, 0, sizeof(ld));
                ld.structSize = sizeof(ld);
                ld.set = VplLineField_Width | VplLineField_Label;
                ld.width = p->lineWidth;
                ld.label = "sin";
                vplAxesPlot(ax, x, a, kVplDemoN, &ld, NULL);
                vplAxesSetXLabel(ax, "6.0 x 4.0 inches at 110 dpi");
                break;
            }

            case VplDemo_TickLabelFormat: {
                /* Values around a million. Left alone, every label would read
                   "1000000.0" and the axis would eat a third of the figure. */
                static double big[kVplDemoSparse];
                for (int i = 0; i < kVplDemoSparse; ++i) {
                    big[i] = 1.0e6 + 2.0e5 * sa[i];
                }
                memset(&ld, 0, sizeof(ld));
                ld.structSize = sizeof(ld);
                ld.set = VplLineField_Width;
                ld.width = p->lineWidth;
                vplAxesPlot(ax, sx, big, kVplDemoSparse, &ld, NULL);
                vplAxesSetXLabel(ax, "the 1e6 above the axis is the scale factor");

                /* The same data with both mechanisms off, for comparison. */
                VplAxes *plain = NULL;
                if (vplFigureAddAxes(fig, 0.58, 0.60, 0.34, 0.28, &plain) ==
                    VplResult_Success) {
                    vplAxesPlot(plain, sx, big, kVplDemoSparse, &ld, NULL);
                    vplAxesTickLabelFormat(plain, VplAxis_Both, 0, 0, -5, 6);
                }
                break;
            }

            case VplDemo_BoxAspect: {
                memset(&ld, 0, sizeof(ld));
                ld.structSize = sizeof(ld);
                ld.set = VplLineField_Width;
                ld.width = p->lineWidth;
                vplAxesPlot(ax, x, a, kVplDemoN, &ld, NULL);
                /* Half as tall as it is wide, pinned to the bottom left of the
                   room it was given rather than centred in it. */
                vplAxesSetBoxAspect(ax, 0.5);
                vplAxesSetAnchor(ax, 0.0, 0.0);
                /* And a coarser tick locator, since a short panel has less room
                   for labels than the derived count assumes. */
                vplAxesLocatorParams(ax, VplAxis_Both, 4);
                vplAxesSetXLabel(ax, "t");
                break;
            }

            case VplDemo_PropCycle: {
                static const char *const cycle[4] = {"#d62728", "#2ca02c", "#9467bd",
                                                     "#8c564b"};
                vplAxesSetPropCycle(ax, cycle, 4);

                double *series[3] = {a, b, c};
                for (int k = 0; k < 3; ++k) {
                    memset(&ld, 0, sizeof(ld));
                    ld.structSize = sizeof(ld);
                    ld.set = VplLineField_Width;
                    ld.width = p->lineWidth;
                    vplAxesPlot(ax, x, series[k], kVplDemoN, &ld, NULL);
                }
                /* A patch too, to show the second counter drawing from the same
                   list and keeping its own place in it. */
                memset(&pd, 0, sizeof(pd));
                pd.structSize = sizeof(pd);
                pd.set = VplPatchField_Alpha;
                pd.alpha = p->bandAlpha;
                static double lo[kVplDemoN], hi[kVplDemoN];
                for (int i = 0; i < kVplDemoN; ++i) {
                    lo[i] = -1.4;
                    hi[i] = -1.0 + 0.2 * a[i];
                }
                vplAxesFillBetween(ax, x, lo, hi, kVplDemoN, NULL, 0, VplFillStep_None, &pd, NULL);
                vplAxesSetXLabel(ax, "t");
                break;
            }

            case VplDemo_PropCycleFull: {
                static const char *const colors[3] = {"tab:blue", "tab:orange", "tab:green"};
                static const VplLineStyle styles[3] = {VplLineStyle_Solid, VplLineStyle_Dashed,
                                                       VplLineStyle_Dotted};
                vplAxesSetPropCycleFull(ax, colors, 3, styles, 3, NULL, 0, NULL, 0);

                double *series[5] = {a, b, c, a, b};
                for (int k = 0; k < 5; ++k) {
                    memset(&ld, 0, sizeof(ld));
                    ld.structSize = sizeof(ld);
                    ld.set = VplLineField_Width;
                    ld.width = p->lineWidth;
                    vplAxesPlot(ax, x, series[k], kVplDemoN, &ld, NULL);
                }
                vplAxesSetXLabel(ax, "5 lines, 3-entry (colour, linestyle) cycle");
                break;
            }

            case VplDemo_LocatorParamsFull: {
                memset(&ld, 0, sizeof(ld));
                ld.structSize = sizeof(ld);
                ld.set = VplLineField_Width;
                ld.width = p->lineWidth;
                vplAxesPlot(ax, x, a, kVplDemoN, &ld, NULL);
                /* Only steps of 1, 5, 10 (and their *10, *100, ...) allowed --
                   no 2 or 2.5 in between, a coarser grid than the derived
                   default would pick for this same data. tight is passed
                   here too (accepted, range-checked) but produces no visible
                   change -- verified against matplotlib itself, whose
                   MaxNLocator does not override the view_limits() hook that
                   argument gates, so real matplotlib renders it identically
                   either way. */
                static const double steps[3] = {1.0, 5.0, 10.0};
                vplAxesLocatorParamsFull(ax, VplAxis_Both, 0, steps, 3, /*tight=*/1);
                vplAxesSetXLabel(ax, "steps={1,5,10} -- only 1/5/10-based tick spacings allowed");
                break;
            }

            case VplDemo_FigurePatch: {
                vplFigureSetFaceColor(fig, "#f4f0e6");
                vplFigureSetEdgeColor(fig, "C3");
                vplFigureSetLineWidth(fig, 4.0);
                vplFigureSetFrameOn(fig, 1);

                memset(&ld, 0, sizeof(ld));
                ld.structSize = sizeof(ld);
                ld.set = VplLineField_Width;
                ld.width = p->lineWidth;
                vplAxesPlot(ax, x, a, kVplDemoN, &ld, NULL);
                vplAxesSetXLabel(ax, "the page has its own patch");
                break;
            }

            case VplDemo_AutoscaleOff: {
                memset(&ld, 0, sizeof(ld));
                ld.structSize = sizeof(ld);
                ld.set = VplLineField_Width;
                ld.width = p->lineWidth;
                vplAxesPlot(ax, x, a, kVplDemoN, &ld, NULL);

                /* Pin the view to what the first curve asked for. The second
                   curve is three times as tall and does NOT move it, which is
                   the whole point of turning autoscaling off. */
                vplAxesSetAutoscaleY(ax, 0);
                vplAxesSetAutoscaleX(ax, 1);

                static double tall[kVplDemoN];
                for (int i = 0; i < kVplDemoN; ++i) {
                    tall[i] = 3.0 * b[i];
                }
                ld.set |= VplLineField_ColorSpec;
                ld.colorSpec = "C3";
                vplAxesPlot(ax, x, tall, kVplDemoN, &ld, NULL);

                vplAxesSetFaceColor(ax, "#f7f7f2");
                vplAxesSetXLabel(ax, "the red curve leaves the pinned view");

                /* An inset showing the opposite: room reserved by hand, for
                   something the data limits know nothing about. */
                VplAxes *reserved = NULL;
                if (vplFigureAddAxes(fig, 0.60, 0.62, 0.30, 0.26, &reserved) ==
                    VplResult_Success) {
                    vplAxesPlot(reserved, sx, sa, kVplDemoSparse, &ld, NULL);
                    vplAxesSetAutoscale(reserved, 1);
                    vplAxesUpdateDataLim(reserved, 0.0, -3.0, 8.0, 3.0);
                }
                break;
            }

            default:
                break;
        }

        if (p->showGrid) {
            vplAxesSetGrid(ax, 1, VplGridWhich_Major, VplGridAxis_Both);
        }
        if (p->showLegend) {
            vplAxesSetLegend(ax, 1, p->legendLoc);
        }
    } else if (id == VplDemo_FigureLabels) {
        for (int k = 0; k < 2; ++k) {
            VplAxes *ax = NULL;
            if (vplFigureAddSubplot(fig, 1, 2, (uint32_t)(k + 1), &ax) != VplResult_Success) {
                continue;
            }
            memset(&ld, 0, sizeof(ld));
            ld.structSize = sizeof(ld);
            ld.set = VplLineField_Width;
            ld.width = p->lineWidth;
            vplAxesPlot(ax, x, (k == 0) ? a : b, kVplDemoN, &ld, NULL);
        }
        /* One title and one pair of axis labels for the whole page, which is
           the point: two panels measuring the same thing should say so once. */
        vplFigureSuptitle(fig, "one title for the page", 0.5, 0.98, 0.0);
        vplFigureSupXLabel(fig, "shared x label", 0.5, 0.01, 0.0);
        vplFigureSupYLabel(fig, "shared y label", 0.02, 0.5, 0.0);
        if (p->tightLayout) {
            vplFigureTightLayout(fig);
        }
    } else if (id == VplDemo_SharedAxes) {
        VplAxes *grid[4] = {NULL, NULL, NULL, NULL};
        if (vplFigureSubplots(fig, 2, 2, grid) == VplResult_Success) {
            for (int k = 0; k < 4; ++k) {
                if (grid[k] == NULL) {
                    continue;
                }
                static double y[kVplDemoN];
                for (int i = 0; i < kVplDemoN; ++i) {
                    y[i] = sin(x[i] * (0.6 + 0.4 * k));
                }
                memset(&ld, 0, sizeof(ld));
                ld.structSize = sizeof(ld);
                ld.set = VplLineField_Width;
                ld.width = p->lineWidth;
                vplAxesPlot(grid[k], x, y, kVplDemoN, &ld, NULL);

                if (k != 0) {
                    vplAxesShareX(grid[k], grid[0]);
                    vplAxesShareY(grid[k], grid[0]);
                }
                vplAxesSetXLabel(grid[k], "t");
                vplAxesSetYLabel(grid[k], "value");
                /* AFTER the labels, not before: label_outer clears the ones an
                   interior panel shares with a neighbour, and setting them
                   afterwards just puts them back. */
                vplAxesLabelOuter(grid[k], 0);
            }
        }
        if (p->tightLayout) {
            vplFigureTightLayout(fig);
        }
    } else if (id == VplDemo_SubplotsShared) {
        /* Two independent share groups on one grid: column 0 leads its own
           column's x, row 0 leads its own row's y -- (1,1) follows both at
           once, the way a real multi-quantity dashboard would. Each leader
           carries the larger data in its group, which is what makes
           vplot's one-directional follow land on the same range a real
           symmetric group would (see vplAxesShareX's note). */
        VplAxes *grid[4] = {NULL, NULL, NULL, NULL};
        if (vplFigureSubplotsShared(fig, 2, 2, VplShareMode_Col, VplShareMode_Row, grid) ==
            VplResult_Success) {
            static double y00[kVplDemoN], y01[kVplDemoN], y10[kVplDemoN], y11[kVplDemoN];
            for (int i = 0; i < kVplDemoN; ++i) {
                y00[i] = sin(x[i]);
                y01[i] = 0.05 * x[i] * x[i];
                y10[i] = 3.0 * x[i];
                y11[i] = 0.02 * x[i] * x[i] * x[i];
            }
            memset(&ld, 0, sizeof(ld));
            ld.structSize = sizeof(ld);
            ld.set = VplLineField_Width;
            ld.width = p->lineWidth;
            vplAxesPlot(grid[0], x, y00, kVplDemoN, &ld, NULL);
            vplAxesPlot(grid[1], x, y01, kVplDemoN, &ld, NULL);
            vplAxesPlot(grid[2], x, y10, kVplDemoN, &ld, NULL);
            vplAxesPlot(grid[3], x, y11, kVplDemoN, &ld, NULL);
        }
        if (p->tightLayout) {
            vplFigureTightLayout(fig);
        }
    } else if (id == VplDemo_AlignLabels) {
        /* A 2x1 column whose two panels carry y numbers of very different
           widths -- the top runs to tens of thousands, the bottom to about one.
           Left alone their y labels sit at different distances from the frame;
           align_ylabels pulls them out to the wider column's edge. */
        VplAxes *col[2] = {NULL, NULL};
        for (int k = 0; k < 2; ++k) {
            if (vplFigureAddSubplot(fig, 2, 1, (uint32_t)(k + 1), &col[k]) !=
                VplResult_Success) {
                continue;
            }
            static double y[kVplDemoN];
            const double gain = (k == 0) ? 8000.0 : 1.0;
            for (int i = 0; i < kVplDemoN; ++i) {
                y[i] = (0.5 + 0.5 * sin(x[i])) * gain;
            }
            memset(&ld, 0, sizeof(ld));
            ld.structSize = sizeof(ld);
            ld.set = VplLineField_Width;
            ld.width = p->lineWidth;
            vplAxesPlot(col[k], x, y, kVplDemoN, &ld, NULL);
            vplAxesSetYLabel(col[k], (k == 0) ? "amplitude" : "ratio");
            vplAxesSetXLabel(col[k], "t");
            vplAxesSetTitle(col[k], (k == 0) ? "wide numbers" : "narrow numbers");
        }
        /* axes=NULL, count=0 means every axes on the figure, matching
           matplotlib's align_*(axs=None). */
        vplFigureAlignYLabels(fig, NULL, 0);
        vplFigureAlignXLabels(fig, NULL, 0);
        vplFigureAlignTitles(fig, NULL, 0);
        if (p->tightLayout) {
            vplFigureTightLayout(fig);
        }
    } else if (id == VplDemo_FigImage) {
        /* A gradient block painted onto the figure's pixels, then an ordinary
           plot on top -- figimage sits BEHIND the axes, which is the z-order
           matplotlib gives it. */
        enum { fr = 48, fc = 72 };
        static double block[fr * fc];
        for (int r = 0; r < fr; ++r) {
            for (int c = 0; c < fc; ++c) {
                block[r * fc + c] = (double)(r + c) / (double)(fr + fc);
            }
        }
        vplFigureFigImage(fig, block, fr, fc, 60.0, 40.0, NULL);

        VplAxes *ax = NULL;
        if (vplFigureAddSubplot(fig, 1, 1, 1, &ax) == VplResult_Success) {
            memset(&ld, 0, sizeof(ld));
            ld.structSize = sizeof(ld);
            ld.set = VplLineField_Width;
            ld.width = p->lineWidth;
            vplAxesPlot(ax, x, a, kVplDemoN, &ld, NULL);
            vplAxesSetTitle(ax, "figimage behind the plot");
        }
        if (p->tightLayout) {
            vplFigureTightLayout(fig);
        }
    } else if (id == VplDemo_SetBound) {
        /* Left: set_xbound(NAN, 5) -- only the upper end moves. Right:
           set_ybound(-0.5, NAN) -- NAN in the OTHER position, the mirror. */
        VplAxes *ax1 = NULL, *ax2 = NULL;
        if (vplFigureAddSubplot(fig, 1, 2, 1, &ax1) == VplResult_Success) {
            memset(&ld, 0, sizeof(ld));
            ld.structSize = sizeof(ld);
            ld.set = VplLineField_Width;
            ld.width = p->lineWidth;
            vplAxesPlot(ax1, x, a, kVplDemoN, &ld, NULL);
            vplAxesSetXBound(ax1, (double)NAN, 5.0);
            vplAxesSetXLabel(ax1, "upper pinned, lower autoscaling");
        }
        if (vplFigureAddSubplot(fig, 1, 2, 2, &ax2) == VplResult_Success) {
            memset(&ld, 0, sizeof(ld));
            ld.structSize = sizeof(ld);
            ld.set = VplLineField_Width;
            ld.width = p->lineWidth;
            vplAxesPlot(ax2, x, a, kVplDemoN, &ld, NULL);
            vplAxesSetYBound(ax2, -0.5, (double)NAN);
            vplAxesSetXLabel(ax2, "lower pinned, upper autoscaling");
        }
        if (p->tightLayout) {
            vplFigureTightLayout(fig);
        }
    } else if (id == VplDemo_BarLabelFull) {
        /* Top: fmt='%.1f' and padding=6 together, so a bug in either shows
           without the other masking it. Bottom: a stacked bar labelled at
           each SEGMENT's own centre, not its tip. */
        VplAxes *ax1 = NULL, *ax2 = NULL;
        static const double bx[4] = {0.0, 1.0, 2.0, 3.0};
        static const double bh[4] = {2.0, 3.5, 1.0, 2.75};
        if (vplFigureAddSubplot(fig, 2, 1, 1, &ax1) == VplResult_Success) {
            memset(&pd, 0, sizeof(pd));
            pd.structSize = sizeof(pd);
            VplPatch *bars = NULL;
            if (vplAxesBar(ax1, bx, bh, 0.7, NULL, 4, &pd, &bars) == VplResult_Success) {
                vplAxesBarLabelFull(ax1, bars, "%.1f", VplBarLabelType_Edge, 6.0);
            }
        }
        if (vplFigureAddSubplot(fig, 2, 1, 2, &ax2) == VplResult_Success) {
            static const double lower[4] = {1.0, 1.5, 0.8, 1.2};
            static const double upper[4] = {1.6, 1.1, 1.4, 0.9};
            memset(&pd, 0, sizeof(pd));
            pd.structSize = sizeof(pd);
            VplPatch *b1 = NULL, *b2 = NULL;
            vplAxesBar(ax2, bx, lower, 0.7, NULL, 4, &pd, &b1);
            vplAxesBar(ax2, bx, upper, 0.7, lower, 4, &pd, &b2);
            if (b1 != NULL) {
                vplAxesBarLabelFull(ax2, b1, "%g", VplBarLabelType_Center, 0.0);
            }
            if (b2 != NULL) {
                vplAxesBarLabelFull(ax2, b2, "%g", VplBarLabelType_Center, 0.0);
            }
        }
        if (p->tightLayout) {
            vplFigureTightLayout(fig);
        }
    } else if (id == VplDemo_ArtistLookup) {
        /* Three panels, three artist types, each with its handle thrown away
           at creation (outLine / outPatch / outImage all NULL) and fetched
           back only through vplAxesGetLine / vplAxesGetPatch /
           vplAxesGetImage -- the same position a caller who did not keep the
           handle would be in. bar_label only labels the RIGHT bars if what
           comes back is really the one bar() drew, not a stale or wrong
           handle, so panel 2 is a functional check as well as a lookup. */
        VplAxes *ax1 = NULL, *ax2 = NULL, *ax3 = NULL;
        if (vplFigureAddSubplot(fig, 1, 3, 1, &ax1) == VplResult_Success) {
            memset(&ld, 0, sizeof(ld));
            ld.structSize = sizeof(ld);
            ld.set = VplLineField_Width;
            ld.width = p->lineWidth;
            vplAxesPlot(ax1, x, a, kVplDemoN, &ld, NULL);
            vplAxesPlot(ax1, x, b, kVplDemoN, &ld, NULL);

            size_t lineCount = 0;
            vplAxesGetLineCount(ax1, &lineCount);
            for (size_t i = 0; i < lineCount; ++i) {
                VplLine *line = NULL;
                vplAxesGetLine(ax1, i, &line);
            }
            char title1[32];
            snprintf(title1, sizeof(title1), "ax.lines: %zu", lineCount);
            vplAxesSetTitle(ax1, title1);
        }

        if (vplFigureAddSubplot(fig, 1, 3, 2, &ax2) == VplResult_Success) {
            static const double bx[4] = {0.0, 1.0, 2.0, 3.0};
            static const double bh[4] = {1.5, 2.5, 1.0, 2.0};
            memset(&pd, 0, sizeof(pd));
            pd.structSize = sizeof(pd);
            pd.set = VplPatchField_FaceColorSpec;
            pd.faceColorSpec = "C2";
            vplAxesBar(ax2, bx, bh, 0.6, NULL, 4, &pd, NULL);

            size_t patchCount = 0;
            vplAxesGetPatchCount(ax2, &patchCount);
            if (patchCount > 0) {
                VplPatch *bars = NULL;
                vplAxesGetPatch(ax2, patchCount - 1, &bars);
                vplAxesBarLabel(ax2, bars);
            }
            char title2[32];
            snprintf(title2, sizeof(title2), "ax.patches: %zu", patchCount);
            vplAxesSetTitle(ax2, title2);
        }

        if (vplFigureAddSubplot(fig, 1, 3, 3, &ax3) == VplResult_Success) {
            static double img[16];
            for (int i = 0; i < 16; ++i) {
                img[i] = (double)i;
            }
            vplAxesImshow(ax3, img, 4, 4, NULL, NULL);

            size_t imageCount = 0;
            vplAxesGetImageCount(ax3, &imageCount);
            for (size_t i = 0; i < imageCount; ++i) {
                VplImage *img_handle = NULL;
                vplAxesGetImage(ax3, i, &img_handle);
            }
            char title3[32];
            snprintf(title3, sizeof(title3), "ax.images: %zu", imageCount);
            vplAxesSetTitle(ax3, title3);
        }
        if (p->tightLayout) {
            vplFigureTightLayout(fig);
        }
    } else {
        const int rows = p->subplotRows < 1 ? 1 : p->subplotRows;
        const int cols = p->subplotCols < 1 ? 1 : p->subplotCols;
        const int total = rows * cols;

        for (int k = 0; k < total; ++k) {
            VplAxes *ax = NULL;
            if (vplFigureAddSubplot(fig, (uint32_t)rows, (uint32_t)cols, (uint32_t)(k + 1),
                                    &ax) != VplResult_Success) {
                continue;
            }
            static double y[kVplDemoN];
            for (int i = 0; i < kVplDemoN; ++i) {
                y[i] = sin(x[i] * (k + 1) * 0.6);
            }
            memset(&ld, 0, sizeof(ld));
            ld.structSize = sizeof(ld);
            ld.set = VplLineField_Label;
            ld.label = "panel";
            vplAxesPlot(ax, x, y, kVplDemoN, &ld, NULL);

            char title[32];
            snprintf(title, sizeof(title), "panel %d", k + 1);
            vplAxesSetTitle(ax, title);
            vplAxesSetXLabel(ax, "t");
            if (p->showGrid) {
                vplAxesSetGrid(ax, 1, VplGridWhich_Major, VplGridAxis_Both);
            }
        }

        if (p->tightLayout) {
            vplFigureTightLayout(fig);
        }
    }

    *outFigure = fig;
    return VplResult_Success;
}

#endif /* VPL_DEMOS_H */
