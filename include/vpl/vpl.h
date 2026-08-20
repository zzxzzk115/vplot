/*
 * vpl.h - vplot's public C ABI.
 *
 * The whole library is reached through opaque handles: no C++ type crosses this
 * boundary and no exception escapes, so a consumer may build with a different
 * compiler or C++ standard library than vplot was built with.
 *
 * Coordinates are matplotlib's: data space for plotting, and an RGBA8888 render
 * buffer whose row 0 is the image's top row.
 */

#ifndef VPL_H
#define VPL_H

#include "vpl_base.h"

VPL_EXTERN_C_BEGIN

/* -------------------------------------------------------------------------
 * Handles
 *
 * Forward-declared incomplete structs, used only by pointer. Axes and line
 * handles are owned by their figure and die with it.
 * ------------------------------------------------------------------------- */

typedef struct VplFigure VplFigure;
typedef struct VplAxes VplAxes;
typedef struct VplLine VplLine;
typedef struct VplPatch VplPatch;
typedef struct VplScatter VplScatter;
typedef struct VplImage VplImage;
typedef struct VplContour VplContour;

/* -------------------------------------------------------------------------
 * Results
 * ------------------------------------------------------------------------- */

typedef enum VplResult
{
    VplResult_Success = 0,
    VplResult_InvalidArgument = 1,
    VplResult_OutOfMemory = 2,
    VplResult_FontUnavailable = 3,
    VplResult_BufferTooSmall = 4,
    VplResult_InternalError = 5,
} VplResult;

/* Human-readable detail for the most recent failure on this thread. Never null;
   returns "" when nothing has failed. The pointer is valid until the next
   failing call on the same thread. */
VPL_API const char *VPL_CALL vplGetLastError(void);

VPL_API const char *VPL_CALL vplResultToString(VplResult result);

/* -------------------------------------------------------------------------
 * Fonts
 *
 * vplot looks for its bundled DejaVu Sans relative to the working directory. If
 * that is not where your assets live, point it at the file before rendering.
 * Text is skipped rather than failing the draw when no font is available.
 * ------------------------------------------------------------------------- */

VPL_API VplResult VPL_CALL vplSetFontPath(const char *ttfPath);

/* -------------------------------------------------------------------------
 * Figure
 * ------------------------------------------------------------------------- */

typedef struct VplFigureDesc
{
    /* Set to sizeof(VplFigureDesc); lets the library accept structs from
       callers built against an older or newer header. */
    size_t structSize;

    /* Zero means matplotlib's default: 6.4 x 4.8 inches at 100 dpi. */
    double widthInches;
    double heightInches;
    double dpi;

    /* RGBA in 0..1. All-zero (fully transparent) is read as "unset" and
       becomes opaque white, matching figure.facecolor. */
    float faceColor[4];
} VplFigureDesc;

/* desc may be NULL for all defaults. */
VPL_API VplResult VPL_CALL vplCreateFigure(const VplFigureDesc *desc, VplFigure **outFigure);
VPL_API void VPL_CALL vplDestroyFigure(VplFigure *figure);

VPL_API VplResult VPL_CALL vplFigureGetPixelSize(const VplFigure *figure,
                                                 uint32_t *outWidth,
                                                 uint32_t *outHeight);

/* Re-renders the figure and copies RGBA8888 into `buffer`. Row 0 is the top
   row, so the result can go straight to a PNG encoder or a GPU texture with no
   flip. Returns VplResult_BufferTooSmall (and the required size) if it does not
   fit; pass buffer = NULL to query the size. */
VPL_API VplResult VPL_CALL vplFigureRenderRGBA(VplFigure *figure,
                                               uint8_t *buffer,
                                               size_t bufferSize,
                                               size_t *outRequiredSize);

typedef enum VplFormat
{
    /* Inferred from the file extension. */
    VplFormat_Auto = 0,
    VplFormat_SVG = 1,
    VplFormat_PDF = 2,
    VplFormat_PNG = 3,
} VplFormat;

typedef struct VplSaveDesc
{
    size_t structSize;
    VplFormat format;

    /* Vector formats are written at 72 dpi (one unit = one point); this field
       applies to the raster formats only. */
    double dpi;
} VplSaveDesc;

/* Writes the figure to disk. desc may be NULL to infer everything from the path.
   Text is embedded as glyph outlines, so the result renders identically on
   machines without the font installed, matching matplotlib's default
   (svg.fonttype "path", pdf.fonttype 3). */
VPL_API VplResult VPL_CALL vplFigureSaveFig(VplFigure *figure,
                                            const char *path,
                                            const VplSaveDesc *desc);

/* Repack the subplot grid so titles and tick labels stop colliding --
   matplotlib's tight_layout. Depends on font metrics, so call it once
   everything has been plotted. */
VPL_API VplResult VPL_CALL vplFigureTightLayout(VplFigure *figure);

/* Grid placement in the style of matplotlib's add_subplot: index is 1-based and
   runs left to right, top to bottom. The axes is owned by the figure. */
VPL_API VplResult VPL_CALL vplFigureAddSubplot(VplFigure *figure,
                                               uint32_t nrows,
                                               uint32_t ncols,
                                               uint32_t index,
                                               VplAxes **outAxes);

/* A second axes over the same box, sharing `base`'s x axis and carrying its own
   y scale on the right -- matplotlib's twinx. It draws no x axis or background
   and follows `base`'s x limits, so it must not outlive it. Both are owned by
   the figure. */
VPL_API VplResult VPL_CALL vplFigureTwinX(VplFigure *figure,
                                          VplAxes *base,
                                          VplAxes **outAxes);

/* A second axes over the same box, sharing `base`'s y axis and carrying its own
   x scale along the top -- matplotlib's twiny. It draws no y axis or background
   and follows `base`'s y limits, so it must not outlive it. */
VPL_API VplResult VPL_CALL vplFigureTwinY(VplFigure *figure,
                                          VplAxes *base,
                                          VplAxes **outAxes);

/* An axes at an explicit position in figure fractions -- add_axes, for inset
   panels and hand-placed colorbars. It belongs to no grid, so
   vplFigureSubplotsAdjust and vplFigureTightLayout leave it where it was put. */
VPL_API VplResult VPL_CALL vplFigureAddAxes(VplFigure *figure,
                                            double left,
                                            double bottom,
                                            double width,
                                            double height,
                                            VplAxes **outAxes);

/* The bounds every subplot grid is laid out inside -- matplotlib's
   SubplotParams, in figure fractions. `wspace` and `hspace` are fractions of a
   CELL, not of the figure (cell = span / (n + spacing * (n - 1))). */
typedef struct VplSubplotParams
{
    double left;   /* default 0.125 */
    double bottom; /* default 0.11 */
    double right;  /* default 0.9 */
    double top;    /* default 0.88 */
    double wspace; /* default 0.2 */
    double hspace; /* default 0.2 */
} VplSubplotParams;

/* Read the current parameters, so a caller can change one and keep the rest. */
VPL_API VplResult VPL_CALL vplFigureGetSubplotParams(const VplFigure *figure,
                                                     VplSubplotParams *outParams);

/* Set them and RE-LAY every subplot already added -- matplotlib's
   subplots_adjust. */
VPL_API VplResult VPL_CALL vplFigureSubplotsAdjust(VplFigure *figure,
                                                   const VplSubplotParams *params);

/* A title across the whole figure -- matplotlib's suptitle. `x`, `y` anchor it
   in figure fractions (default 0.5, 0.98, top on y) and `fontsize` sets its
   size in points, or 0 to keep the 'large' default. Pass NULL or "" to remove
   it. */
VPL_API VplResult VPL_CALL vplFigureSuptitle(VplFigure *figure, const char *text, double x,
                                             double y, double fontsize);

/* vplFigureColorbar is declared with the colormaps, further down. */

/* -------------------------------------------------------------------------
 * Axes
 * ------------------------------------------------------------------------- */

typedef enum VplLineStyle
{
    VplLineStyle_Solid = 0,   /* '-'  */
    VplLineStyle_Dashed = 1,  /* '--' */
    VplLineStyle_DashDot = 2, /* '-.' */
    VplLineStyle_Dotted = 3,  /* ':'  */
    /* Markers only, no connecting line. */
    VplLineStyle_None = 4,
} VplLineStyle;

typedef enum VplMarker
{
    VplMarker_None = 0,
    VplMarker_Circle = 1,       /* 'o' */
    VplMarker_Square = 2,       /* 's' */
    VplMarker_TriangleUp = 3,   /* '^' */
    VplMarker_TriangleDown = 4, /* 'v' */
    VplMarker_Diamond = 5,      /* 'D' */
    VplMarker_Plus = 6,         /* '+' */
    VplMarker_Cross = 7,        /* 'x' */
    VplMarker_Star = 8,         /* '*' */
} VplMarker;

typedef enum VplLineField
{
    VplLineField_Color = 1u << 0,
    VplLineField_Width = 1u << 1,
    VplLineField_Label = 1u << 2,
    VplLineField_LineStyle = 1u << 3,
    VplLineField_Marker = 1u << 4,
    VplLineField_MarkerSize = 1u << 5,
    /* A colour spec string, as an alternative to the float array. */
    VplLineField_ColorSpec = 1u << 6,
    /* A Matlab-style format string. Applied first, so the explicit fields above
       override anything it set. */
    VplLineField_Format = 1u << 7,
} VplLineField;

typedef struct VplLineDesc
{
    size_t structSize;

    /* Which fields below are meaningful. Zero is a legal value for every one of
       them, so an explicit mask is the only way to tell "set to 0" from
       "not set". */
    uint32_t set;

    float color[4]; /* RGBA 0..1; default is the next colour in the tab10 cycle */
    double width;   /* points; default 1.5, matching lines.linewidth */
    const char *label;

    VplLineStyle linestyle;
    VplMarker marker;
    double markersize; /* points across; default 6, matching lines.markersize */

    /* "C0".."C9", "tab:blue", "#rrggbb", "r"/"g"/"b"/"c"/"m"/"y"/"k"/"w", a
       grayscale level like "0.5", a common CSS name, or "none".
       Note the single letters carry matplotlib's values, not CSS ones: "g" is a
       dark green and "c"/"m"/"y" are three-quarter saturated. */
    const char *colorSpec;

    /* "r--o", "g:", "ko" -- colour, line style and marker in any order. A
       format naming a marker but no line style draws markers only, which is
       matplotlib's rule. */
    const char *format;
} VplLineDesc;

/* x and y hold `count` values each and are copied. desc may be NULL. outLine
   may be NULL if you do not need to restyle the line afterwards. */
/* plot(y): the one-argument form, with x taken as 0, 1, 2, ... n-1. */
VPL_API VplResult VPL_CALL vplAxesPlotY(VplAxes *axes,
                                        const double *y,
                                        size_t count,
                                        const VplLineDesc *desc,
                                        VplLine **outLine);

VPL_API VplResult VPL_CALL vplAxesPlot(VplAxes *axes,
                                       const double *x,
                                       const double *y,
                                       size_t count,
                                       const VplLineDesc *desc,
                                       VplLine **outLine);

typedef enum VplScatterField
{
    VplScatterField_Color = 1u << 0,
    VplScatterField_ColorSpec = 1u << 1,
    VplScatterField_Marker = 1u << 2,
    VplScatterField_Size = 1u << 3,
    VplScatterField_EdgeColor = 1u << 4,
    VplScatterField_EdgeWidth = 1u << 5,
    VplScatterField_Alpha = 1u << 6,
    VplScatterField_Label = 1u << 7,
} VplScatterField;

typedef struct VplScatterDesc
{
    size_t structSize;
    uint32_t set;

    float color[4];
    const char *colorSpec;
    VplMarker marker;

    /* Marker AREA in points squared, matching scatter's `s`: the diameter is
       its square root. The default of 36 is a 6-point marker, the same size
       plot() draws at lines.markersize. */
    double size;

    float edgeColor[4];
    double edgeWidth;
    double alpha;
    const char *label;
} VplScatterDesc;

/*
 * Markers whose colour and size may vary per point. `sizes`, when non-NULL,
 * holds `count` areas in points squared; `colors`, when non-NULL, holds `count`
 * RGBA quadruples in 0..1 (4 * count floats). Either may be NULL to use the
 * uniform value from `desc`.
 */
VPL_API VplResult VPL_CALL vplAxesScatter(VplAxes *axes,
                                          const double *x,
                                          const double *y,
                                          size_t count,
                                          const double *sizes,
                                          const float *colors,
                                          const VplScatterDesc *desc,
                                          VplScatter **outScatter);

/* y +/- yerr through each point. `desc` styles the line itself, exactly as for
   vplAxesPlot. capsize is in points and defaults to 0 -- matplotlib's
   errorbar.capsize -- so pass a positive value for the T-shaped caps. */
VPL_API VplResult VPL_CALL vplAxesErrorBar(VplAxes *axes,
                                           const double *x,
                                           const double *y,
                                           const double *yerr,
                                           size_t count,
                                           double capsize,
                                           const VplLineDesc *desc,
                                           VplLine **outLine);

typedef enum VplPatchField
{
    VplPatchField_FaceColor = 1u << 0,
    VplPatchField_EdgeColor = 1u << 1,
    VplPatchField_EdgeWidth = 1u << 2,
    VplPatchField_Alpha = 1u << 3,
    VplPatchField_Label = 1u << 4,
    VplPatchField_FaceColorSpec = 1u << 5,
} VplPatchField;

typedef struct VplPatchDesc
{
    size_t structSize;
    uint32_t set;

    float faceColor[4];
    float edgeColor[4];
    double edgeWidth; /* points; 0 draws no edge, as matplotlib's patches default to */
    double alpha;
    const char *label;
    const char *faceColorSpec;
} VplPatchDesc;

/* The band between two curves: confidence intervals, min/max envelopes.
   Defaults to the next cycle colour at 30% alpha. */
/* How fill_between fills between samples: a slope by default, or a staircase
   that steps before, after, or at the midpoint of each x -- matplotlib's
   step={'pre','post','mid'}. */
typedef enum VplFillStep
{
    VplFillStep_None = 0,
    VplFillStep_Pre = 1,
    VplFillStep_Post = 2,
    VplFillStep_Mid = 3,
} VplFillStep;

/* `where` is an optional per-point mask (non-zero fills): the band spans x[i]
   to x[i+1] only where both ends are set, so each run of the mask is its own
   region and an isolated point fills nothing. Pass NULL to fill everywhere.
   `interpolate` (non-zero) puts a masked region's edges on the point where the
   two curves cross rather than clipping at the last in-mask sample. `step`
   fills a staircase rather than sloped segments. */
VPL_API VplResult VPL_CALL vplAxesFillBetween(VplAxes *axes,
                                              const double *x,
                                              const double *y1,
                                              const double *y2,
                                              size_t count,
                                              const uint8_t *where,
                                              int32_t interpolate,
                                              VplFillStep step,
                                              const VplPatchDesc *desc,
                                              VplPatch **outPatch);

/* Bars along x rather than up from it: `width` is the value and `height` the
   thickness, in data units. The sticky baseline moves to the x axis with them.

   `left` may be NULL for bars starting at zero, or hold one value per bar to
   stack a series against the previous one. */
VPL_API VplResult VPL_CALL vplAxesBarH(VplAxes *axes,
                                       const double *y,
                                       const double *width,
                                       double height,
                                       const double *left,
                                       size_t count,
                                       const VplPatchDesc *desc,
                                       VplPatch **outPatch);

/* fill_between's mirror: the band between two x curves along a shared y. */
/* `where`, `step` and `interpolate` work as in vplAxesFillBetween, along y.
   Note the order: matplotlib's fill_betweenx takes step before interpolate,
   the reverse of fill_between, and vplot follows it so ported code lines up. */
VPL_API VplResult VPL_CALL vplAxesFillBetweenX(VplAxes *axes,
                                               const double *y,
                                               const double *x1,
                                               const double *x2,
                                               size_t count,
                                               const uint8_t *where,
                                               VplFillStep step,
                                               int32_t interpolate,
                                               const VplPatchDesc *desc,
                                               VplPatch **outPatch);

/* Where a staircase's riser sits relative to the point it belongs to --
   matplotlib's drawstyle. */
typedef enum VplStepWhere
{
    /* The value reaches BACK to the previous x; the riser stands at x[i].
       matplotlib's default. */
    VplStepWhere_Pre = 0,
    /* The value holds FORWARD to the next x; the riser stands at x[i+1]. */
    VplStepWhere_Post = 1,
    /* The riser is halfway between, so each point sits centred on its tread. */
    VplStepWhere_Mid = 2,
} VplStepWhere;

/* A staircase rather than a straight line between points. */
VPL_API VplResult VPL_CALL vplAxesStep(VplAxes *axes,
                                       const double *x,
                                       const double *y,
                                       size_t count,
                                       VplStepWhere where,
                                       const VplLineDesc *desc,
                                       VplLine **outLine);

/* Bands across the whole axes: between two y values, or two x values. They span
   the frame in the other direction however the view moves, and only the
   direction they are measured in counts towards the limits. */
VPL_API VplResult VPL_CALL vplAxesAxHSpan(VplAxes *axes, double ymin, double ymax);
VPL_API VplResult VPL_CALL vplAxesAxVSpan(VplAxes *axes, double xmin, double xmax);

/* A histogram: `bins` equal-width bars over the data's own range, rising from
   0. Pass 0 for matplotlib's default of ten. */
/* `density` (non-zero) scales the bars so their total area is 1 -- matplotlib's
   density=True -- rather than leaving them as raw counts. */
VPL_API VplResult VPL_CALL vplAxesHist(VplAxes *axes,
                                       const double *x,
                                       size_t count,
                                       size_t bins,
                                       int32_t density,
                                       const VplPatchDesc *desc,
                                       VplPatch **outPatch);

/* Tick positions of your own, replacing the automatic ones. `labels` may be
   NULL to keep the automatic formatting of those positions.

   A NULL `ticks` hands the axis back to its locator; a non-NULL `ticks` with a
   count of 0 means NO ticks, which is what set_xticks([]) does and how a figure
   without axis decorations is drawn.

   Setting ticks does not move the limits, so a tick outside the view is simply
   not drawn. */
VPL_API VplResult VPL_CALL vplAxesSetXTicks(VplAxes *axes,
                                            const double *ticks,
                                            const char *const *labels,
                                            size_t count);
VPL_API VplResult VPL_CALL vplAxesSetYTicks(VplAxes *axes,
                                            const double *ticks,
                                            const char *const *labels,
                                            size_t count);

/* The tick positions currently in effect -- get_xticks / get_yticks (major
   ticks only). When no fixed ticks were set these are the locator's output,
   computed against the axes' pixel size at its last render. Pass buffer = NULL
   to ask for the count first.

   This is also how to implement set_xticklabels/set_yticklabels: read the
   current ticks back, then call vplAxesSetXTicks/vplAxesSetYTicks with those
   same positions and the new label strings. */
VPL_API VplResult VPL_CALL vplAxesGetXTicks(const VplAxes *axes,
                                            double *buffer,
                                            size_t bufferCount,
                                            size_t *outCount);
VPL_API VplResult VPL_CALL vplAxesGetYTicks(const VplAxes *axes,
                                            double *buffer,
                                            size_t bufferCount,
                                            size_t *outCount);


typedef enum VplSpine
{
    VplSpine_Left = 0,
    VplSpine_Right = 1,
    VplSpine_Top = 2,
    VplSpine_Bottom = 3,
} VplSpine;

/* Which axis a setting applies to. */
typedef enum VplAxis
{
    VplAxis_X = 0,
    VplAxis_Y = 1,
    VplAxis_Both = 2,
} VplAxis;

/* Which side of the spine a tick falls on -- {x,y}tick.direction.

   This moves the tick LABELS as well: matplotlib measures their pad from the
   spine and adds back however much of the tick lies outside it, so a tick
   turned inwards pulls its label in by the tick's whole length. */
typedef enum VplTickDirection
{
    VplTickDirection_Out = 0,
    VplTickDirection_In = 1,
    VplTickDirection_InOut = 2,
} VplTickDirection;

/* One axis's tick styling -- the drawing half of matplotlib's tick_params. */
typedef struct VplTickParams
{
    VplTickDirection direction; /* {x,y}tick.direction, default out */
    double length;              /* {x,y}tick.major.size, points, default 3.5 */
    double width;               /* {x,y}tick.major.width, default 0.8 */
    double pad;                 /* {x,y}tick.major.pad, points, default 3.5 */
    double labelSize;           /* {x,y}tick.labelsize, points, default 10 */
    int ticksVisible;           /* draw the tick marks at all */
    int labelsVisible;          /* draw the tick labels at all */

    /* tick_params(colors=, labelcolor=) -- RGBA 0..1, matplotlib's separate
       {x,y}tick.color and {x,y}tick.labelcolor. A spine's own edgecolor
       (axes.edgecolor) is independent of both. Default black. */
    float color[4];
    float labelColor[4];
} VplTickParams;

/* Read the current tick styling of ONE axis (VplAxis_Both is rejected, since
   the two can differ). Pair it with vplAxesSetTickParams to change one field
   and leave the rest alone. */
VPL_API VplResult VPL_CALL vplAxesGetTickParams(const VplAxes *axes,
                                                VplAxis axis,
                                                VplTickParams *outParams);

VPL_API VplResult VPL_CALL vplAxesSetTickParams(VplAxes *axes,
                                                VplAxis axis,
                                                const VplTickParams *params);

/* Run an axis backwards -- invert_xaxis / invert_yaxis. A property of the axes
   rather than a swap of the limits, so it survives autoscaling. */
VPL_API VplResult VPL_CALL vplAxesSetXInverted(VplAxes *axes, int inverted);
VPL_API VplResult VPL_CALL vplAxesSetYInverted(VplAxes *axes, int inverted);

/* A collection of disjoint line segments in one style -- matplotlib's
   LineCollection, and the handle hlines, vlines and eventplot return. */
typedef struct VplSegments VplSegments;

/* Style for a segment collection. NULL takes the matplotlib defaults: lines.color
   (a fixed C0, NOT the next colour of the cycle), lines.linewidth, solid. */
typedef struct VplSegmentsDesc
{
    const char *color;
    double lineWidth;
    VplLineStyle lineStyle;
    const char *label;
} VplSegmentsDesc;

/* Horizontal rules at `count` places, each running between its own pair of x
   values. Unlike vplAxesAxHLine these are in data coordinates at both ends. */
VPL_API VplResult VPL_CALL vplAxesHLines(VplAxes *axes,
                                         const double *y,
                                         const double *xmin,
                                         const double *xmax,
                                         size_t count,
                                         const VplSegmentsDesc *desc,
                                         VplSegments **outSegments);

VPL_API VplResult VPL_CALL vplAxesVLines(VplAxes *axes,
                                         const double *x,
                                         const double *ymin,
                                         const double *ymax,
                                         size_t count,
                                         const VplSegmentsDesc *desc,
                                         VplSegments **outSegments);

/* A rug of event marks: one tick per position, `lineLength` long and centred on
   `lineOffset` across the other axis. Pass horizontal = 1 for marks standing up
   from an x position, 0 for marks lying across a y position.

   The cross axis autoscales to a WHOLE lineLength either side of the offset
   (though each mark covers only half of that), so the rug never touches the
   frame, as in matplotlib. */
VPL_API VplResult VPL_CALL vplAxesEventPlot(VplAxes *axes,
                                            const double *positions,
                                            size_t count,
                                            double lineOffset,
                                            double lineLength,
                                            int horizontal,
                                            const VplSegmentsDesc *desc,
                                            VplSegments **outSegments);

/* Stalks from zero up to each value, circular heads, and a C3 baseline across
   the x range. Three artists, so there is no single handle to return. */
/* `linefmt`, `markerfmt` and `basefmt` are matplotlib's format strings for the
   stalks, the heads and the baseline -- e.g. "r--", "gs", "k-". Pass NULL for a
   default (C0 stalks and heads, a C3 baseline, circle heads). A markerfmt with
   no colour takes the stalk colour. `bottom` is the level the stalks rise from. */
VPL_API VplResult VPL_CALL vplAxesStem(VplAxes *axes,
                                       const double *x,
                                       const double *y,
                                       size_t count,
                                       const char *linefmt,
                                       const char *markerfmt,
                                       const char *basefmt,
                                       double bottom);

/* A step OUTLINE over bin edges: `values` has `count` entries and `edges` has
   count + 1. Not filled -- matplotlib's stairs draws the outline only, down to
   the baseline at each end, and makes the baseline a sticky edge. */
/* `horizontal` (non-zero) lays the steps along y. `baseline` and `baselineCount`
   give the level the steps close down to: count 0 is matplotlib's default of the
   scalar 0, count 1 a scalar, and a single NaN means None -- an open top line
   with no descent. `fill` (non-zero) fills to the baseline instead of drawing
   only the outline. */
VPL_API VplResult VPL_CALL vplAxesStairs(VplAxes *axes,
                                         const double *values,
                                         const double *edges,
                                         size_t count,
                                         int32_t horizontal,
                                         const double *baseline,
                                         size_t baselineCount,
                                         int32_t fill,
                                         const VplPatchDesc *desc,
                                         VplPatch **outPatch);

/* An arbitrary filled polygon, closed for you. Takes the next colour of the
   patch cycle. */
VPL_API VplResult VPL_CALL vplAxesFill(VplAxes *axes,
                                       const double *x,
                                       const double *y,
                                       size_t count,
                                       const VplPatchDesc *desc,
                                       VplPatch **outPatch);

/* A row of intervals given as (start, width) pairs, all at height `height`
   starting at `y0` -- timelines and Gantt charts. Fixed at C0; unlike fill it
   does not consume the cycle. */
VPL_API VplResult VPL_CALL vplAxesBrokenBarH(VplAxes *axes,
                                             const double *xStart,
                                             const double *xWidth,
                                             size_t count,
                                             double y0,
                                             double height,
                                             const VplPatchDesc *desc,
                                             VplPatch **outPatch);

/* Series stacked on the running total below them: `ys` is `seriesCount` rows of
   `count` values, row-major, sharing one x. Each band takes the next colour of
   the cycle, and the stack sits on zero. */
/* Where the stack's baseline sits -- matplotlib's baseline argument. Zero is a
   plain stacked area; the other three are streamgraph layouts that centre or
   balance the bands. */
typedef enum VplStackBaseline
{
    VplStackBaseline_Zero = 0,
    VplStackBaseline_Sym = 1,
    VplStackBaseline_Wiggle = 2,
    VplStackBaseline_WeightedWiggle = 3,
} VplStackBaseline;

VPL_API VplResult VPL_CALL vplAxesStackPlot(VplAxes *axes,
                                            const double *x,
                                            const double *ys,
                                            size_t seriesCount,
                                            size_t count,
                                            VplStackBaseline baseline);

/* Box and whiskers per group. `values` is the groups laid end to end and
   `counts` gives each group's length, since C has no ragged array; the total
   read is the sum of `counts`. Groups sit at 1..groupCount and the x limits are
   pinned half a unit either side, as matplotlib pins them. */
VPL_API VplResult VPL_CALL vplAxesBoxPlot(VplAxes *axes,
                                          const double *values,
                                          const size_t *counts,
                                          size_t groupCount);

typedef enum VplQuiverPivot
{
    VplQuiverPivot_Tail = 0,
    VplQuiverPivot_Middle = 1,
    VplQuiverPivot_Tip = 2,
} VplQuiverPivot;

typedef enum VplQuiverField
{
    VplQuiverField_Scale = 1u << 0,
    VplQuiverField_Width = 1u << 1,
    VplQuiverField_ColorSpec = 1u << 2,
    VplQuiverField_Pivot = 1u << 3,
    VplQuiverField_Label = 1u << 4,
} VplQuiverField;

typedef struct VplQuiverDesc
{
    size_t structSize;
    uint32_t set;

    /* Data units per arrow-width-unit of length. LARGER means SHORTER arrows.
       Unset uses matplotlib's auto-scaling from the mean magnitude. */
    double scale;
    /* Shaft width as a fraction of the axes width. */
    double width;
    const char *colorSpec;
    VplQuiverPivot pivot;
    const char *label;
} VplQuiverDesc;

/* A field of arrows -- quiver. Only the arrow POSITIONS count towards the data
   limits, since an arrow is sized in axes fractions. Sizing happens at draw
   time, as both width and length are fractions of the axes as drawn. */
VPL_API VplResult VPL_CALL vplAxesQuiver(VplAxes *axes,
                                         const double *x,
                                         const double *y,
                                         const double *u,
                                         const double *v,
                                         size_t count,
                                         const VplQuiverDesc *desc);

/* A reference arrow for the most recent quiver field, at (x, y) in AXES
   coordinates, standing for a vector of `magnitude` -- quiverkey. It borrows
   the field's resolved scale, and its arrow always pivots on its MIDDLE
   (labelpos='N'). */
VPL_API VplResult VPL_CALL vplAxesQuiverKey(VplAxes *axes,
                                            double x,
                                            double y,
                                            double magnitude,
                                            const char *label,
                                            double fontSize);

typedef enum VplBarbsField
{
    VplBarbsField_Length = 1u << 0,
    VplBarbsField_Increments = 1u << 1, /* all three of half, full, flag */
    VplBarbsField_Rounding = 1u << 2,
    VplBarbsField_Flip = 1u << 3,
    VplBarbsField_FillEmpty = 1u << 4,
    VplBarbsField_PivotMiddle = 1u << 5,
    VplBarbsField_ColorSpec = 1u << 6,
    VplBarbsField_LineWidth = 1u << 7,
} VplBarbsField;

typedef struct VplBarbsDesc
{
    size_t structSize;
    uint32_t set;

    double length;
    double half;
    double full;
    double flag;
    int rounding;
    int flip;
    int fillEmpty;
    int pivotMiddle;
    const char *colorSpec;
    double lineWidth;
} VplBarbsDesc;

/* Wind barbs -- barbs. A meteorological convention, not a general vector plot:
   the staff points INTO the wind, every staff is the same length, and the
   magnitude is spelled out in flags (50), full barbs (10) and half barbs (5).
   Magnitudes are rounded to the nearest half barb first. */
VPL_API VplResult VPL_CALL vplAxesBarbs(VplAxes *axes,
                                        const double *x,
                                        const double *y,
                                        const double *u,
                                        const double *v,
                                        size_t count,
                                        const VplBarbsDesc *desc);

/* Bars side by side within each group -- grouped_bar.

   `heights` is datasetCount * groupCount, ROW-MAJOR by dataset. The bar width
   is derived from the two spacings (both measured in bar widths); pass 1.5 and
   0 for matplotlib's defaults.

   `tickLabels` (groupCount of them) and `labels` (datasetCount) may be NULL. */
VPL_API VplResult VPL_CALL vplAxesGroupedBar(VplAxes *axes,
                                             const double *heights,
                                             size_t datasetCount,
                                             size_t groupCount,
                                             const char *const *tickLabels,
                                             const char *const *labels,
                                             double groupSpacing,
                                             double barSpacing);

/* Cross-correlation of two equal-length series, as a stalk per lag plus a rule
   along zero -- xcorr. A direct sum, not an FFT. `normed` divides by
   sqrt(sum x^2 * sum y^2), which puts an autocorrelation's zero lag at 1. */
VPL_API VplResult VPL_CALL vplAxesXCorr(VplAxes *axes,
                                        const double *x,
                                        const double *y,
                                        size_t count,
                                        int maxLags,
                                        int normed);

/* A series against itself -- acorr. */
VPL_API VplResult VPL_CALL vplAxesACorr(VplAxes *axes,
                                        const double *x,
                                        size_t count,
                                        int maxLags,
                                        int normed);

/* One x or y label for the WHOLE figure -- supxlabel / supylabel, for a grid
   whose panels share a scale. Each is anchored by an edge rather than centred
   (the x label's bottom at 1% of the figure height, the y label's left at 2%
   of its width), so a taller string grows inward. `x`, `y` move the anchor in
   figure fractions and `fontsize` (points, or 0 for the default) resizes. Pass
   NULL or "" to remove. */
VPL_API VplResult VPL_CALL vplFigureSupXLabel(VplFigure *figure, const char *text, double x,
                                              double y, double fontsize);
VPL_API VplResult VPL_CALL vplFigureSupYLabel(VplFigure *figure, const char *text, double x,
                                              double y, double fontsize);

/* Line up the axis labels or titles of subplots that share a row or column --
   align_xlabels / align_ylabels / align_titles, and align_labels for both axis
   labels at once. Aligning gives a column one y-label position (or a row one
   x-label position), so the labels form a straight margin.

   Pass `axes` = NULL or `count` = 0 to align every axes on the figure
   (matplotlib's axs=None). Only axes in the same column (y) or row (x) group
   together. */
VPL_API VplResult VPL_CALL vplFigureAlignXLabels(VplFigure *figure,
                                                 VplAxes *const *axes,
                                                 size_t count);
VPL_API VplResult VPL_CALL vplFigureAlignYLabels(VplFigure *figure,
                                                 VplAxes *const *axes,
                                                 size_t count);
VPL_API VplResult VPL_CALL vplFigureAlignLabels(VplFigure *figure,
                                                VplAxes *const *axes,
                                                size_t count);
VPL_API VplResult VPL_CALL vplFigureAlignTitles(VplFigure *figure,
                                                VplAxes *const *axes,
                                                size_t count);

/* Read them back -- get_suptitle / get_supxlabel / get_supylabel. The same
   "fill a buffer, report the length needed" convention as vplAxesGetTitle:
   pass buffer = NULL to ask for the size first. */
VPL_API VplResult VPL_CALL vplFigureGetSupTitle(const VplFigure *figure,
                                                char *buffer,
                                                size_t bufferSize,
                                                size_t *outRequiredSize);
VPL_API VplResult VPL_CALL vplFigureGetSupXLabel(const VplFigure *figure,
                                                 char *buffer,
                                                 size_t bufferSize,
                                                 size_t *outRequiredSize);
VPL_API VplResult VPL_CALL vplFigureGetSupYLabel(const VplFigure *figure,
                                                 char *buffer,
                                                 size_t bufferSize,
                                                 size_t *outRequiredSize);

/* A whole grid in one call -- subplots. `outAxes` receives nrows * ncols
   handles, left to right and top to bottom. */
VPL_API VplResult VPL_CALL vplFigureSubplots(VplFigure *figure,
                                             uint32_t nrows,
                                             uint32_t ncols,
                                             VplAxes **outAxes);

/* subplots' sharex/sharey modes -- matplotlib's bool-or-{'none','all','row',
   'col'}. 'all' links every axes to the first; 'row'/'col' link each row (or
   column) to its own first member. Like vplAxesShareX, this is a
   one-directional follow, not a symmetric group. */
typedef enum VplShareMode
{
    VplShareMode_None = 0,
    VplShareMode_All = 1,
    VplShareMode_Row = 2,
    VplShareMode_Col = 3,
} VplShareMode;

/* subplots with sharex/sharey wired in, for building a shared grid in one call
   instead of sharing each axes by hand after vplFigureSubplots. Also strips the
   interior tick labels, as matplotlib's subplots() does when either is
   requested. */
VPL_API VplResult VPL_CALL vplFigureSubplotsShared(VplFigure *figure,
                                                    uint32_t nrows,
                                                    uint32_t ncols,
                                                    VplShareMode shareX,
                                                    VplShareMode shareY,
                                                    VplAxes **outAxes);

/* Figure geometry -- get_figwidth / set_figwidth and friends. The size is in
   INCHES and the dpi converts to pixels, so a figure keeps its proportions when
   the dpi changes. Setting either re-sizes the canvas, so call these before
   rendering. */
VPL_API VplResult VPL_CALL vplFigureGetSizeInches(const VplFigure *figure,
                                                  double *outWidth,
                                                  double *outHeight);
VPL_API VplResult VPL_CALL vplFigureSetSizeInches(VplFigure *figure,
                                                  double w,
                                                  double h);
/* One dimension at a time -- set_figwidth / set_figheight. */
VPL_API VplResult VPL_CALL vplFigureSetFigWidth(VplFigure *figure, double width);
VPL_API VplResult VPL_CALL vplFigureSetFigHeight(VplFigure *figure, double height);
VPL_API VplResult VPL_CALL vplFigureGetDpi(const VplFigure *figure, double *outDpi);
VPL_API VplResult VPL_CALL vplFigureSetDpi(VplFigure *figure, double dpi);

/* How many axes the figure holds, and each by index -- get_axes. The order is
   the order they were added, which for a grid is left to right, top to
   bottom. */
VPL_API VplResult VPL_CALL vplFigureGetAxesCount(const VplFigure *figure,
                                                 size_t *outCount);
VPL_API VplResult VPL_CALL vplFigureGetAxes(VplFigure *figure,
                                            size_t index,
                                            VplAxes **outAxes);

/* Drop every axes and every figure-level label -- clf. Any VplAxes handle
   obtained from this figure is dangling afterwards. */
VPL_API VplResult VPL_CALL vplFigureClear(VplFigure *figure);

/* Keep tick labels and axis labels only on the grid's outer edges --
   label_outer. A panel not in the last row loses its x tick labels and xlabel,
   one not in the first column its y tick labels and ylabel. Tick MARKS stay
   (unless removeInnerTicks is set). Does nothing on a hand-placed axes, which
   is in no row and no column. */
/* `removeInnerTicks` (non-zero) also hides the tick MARKS on the inner edges,
   not just their labels -- matplotlib's remove_inner_ticks. */
VPL_API VplResult VPL_CALL vplAxesLabelOuter(VplAxes *axes, int removeInnerTicks);

/* The whole axis apparatus as one switch -- set_axis_off / set_axis_on
   (matplotlib's axis('off')). Off hides the frame, ticks, tick labels, grid,
   axis labels and title, leaving the DATA and background untouched. The legend
   is not affected, matching matplotlib. */
VPL_API VplResult VPL_CALL vplAxesSetAxisOn(VplAxes *axes, int on);
VPL_API VplResult VPL_CALL vplAxesGetAxisOn(const VplAxes *axes, int *outOn);

/* Whether the grid and tick marks sit above or below patches and lines --
   set_axisbelow / get_axisbelow, matplotlib's three states by grid zorder:
   On (True, 0.5) is below patches AND lines but above images; Line (the
   default, 1.5) is above patches/bars/fills but below data lines -- a grid
   crossing bars but not curves; Off (False, 2.5) is above everything. */
typedef enum VplAxisBelow
{
    VplAxisBelow_On = 0,
    VplAxisBelow_Line = 1,
    VplAxisBelow_Off = 2,
} VplAxisBelow;

VPL_API VplResult VPL_CALL vplAxesSetAxisBelow(VplAxes *axes, VplAxisBelow mode);
VPL_API VplResult VPL_CALL vplAxesGetAxisBelow(const VplAxes *axes, VplAxisBelow *outMode);

/* sharex / sharey: lock this axes' x (or y) range to another axes' current
   view. Unlike twinx/twiny this hides none of the axes' own furniture -- both
   keep their frame, ticks and y-axis, they just refuse to disagree about the x
   range. Pass NULL as other to undo it.

   This is a one-directional follow, not matplotlib's symmetric group: this axes
   follows other, but vplAxesSetXLim on it afterwards is ignored rather than
   reaching back to move other. Set up the base axes first, then share the rest
   onto it (the `sharex=ax1` pattern). */
VPL_API VplResult VPL_CALL vplAxesShareX(VplAxes *axes, const VplAxes *other);
VPL_API VplResult VPL_CALL vplAxesShareY(VplAxes *axes, const VplAxes *other);

/* The 5% breathing room autoscale leaves around the data -- set_xmargin /
   set_ymargin / margins. Zero pins the view to the data exactly; matplotlib
   rejects anything below it. */
VPL_API VplResult VPL_CALL vplAxesSetMargins(VplAxes *axes, double xmargin, double ymargin);
VPL_API VplResult VPL_CALL vplAxesGetMargins(const VplAxes *axes,
                                             double *outXMargin,
                                             double *outYMargin);
/* One axis at a time -- set_xmargin / set_ymargin / get_xmargin / get_ymargin.
   The setters accept a margin down to -0.5, matplotlib's floor. */
VPL_API VplResult VPL_CALL vplAxesSetXMargin(VplAxes *axes, double margin);
VPL_API VplResult VPL_CALL vplAxesSetYMargin(VplAxes *axes, double margin);
VPL_API VplResult VPL_CALL vplAxesGetXMargin(const VplAxes *axes, double *outMargin);
VPL_API VplResult VPL_CALL vplAxesGetYMargin(const VplAxes *axes, double *outMargin);

/* The axes rectangle in figure fractions -- set_position / get_position. Setting
   it takes the axes out of any subplot grid, so the layout leaves it put, the
   same as add_axes. */
VPL_API VplResult VPL_CALL vplAxesSetPosition(VplAxes *axes, double left, double bottom,
                                              double width, double height);
VPL_API VplResult VPL_CALL vplAxesGetPosition(const VplAxes *axes, double *outLeft,
                                              double *outBottom, double *outWidth,
                                              double *outHeight);

/* Read back what the setters set. The axis labels use the same "fill a buffer,
   report the length needed" convention as vplAxesGetTitle. */
VPL_API VplResult VPL_CALL vplAxesGetXLabel(const VplAxes *axes,
                                            char *buffer,
                                            size_t bufferSize,
                                            size_t *outRequiredSize);
VPL_API VplResult VPL_CALL vplAxesGetYLabel(const VplAxes *axes,
                                            char *buffer,
                                            size_t bufferSize,
                                            size_t *outRequiredSize);

/* vplAxesGetViewLimits already exists, further down with the limit setters.

   The scale getters live beside vplAxesSetXScale rather than here, because
   VplScale is not declared until then. */

/* Whether the frame is drawn at all -- get_frame_on / set_frame_on. Turning it
   off hides all four spines together, which is not the same as turning each off
   by hand, because it also stops the background being painted. */
VPL_API VplResult VPL_CALL vplAxesSetFrameOn(VplAxes *axes, int on);
VPL_API VplResult VPL_CALL vplAxesGetFrameOn(const VplAxes *axes, int *outOn);

/* The aspect ratio -- get_aspect / set_aspect. 1.0 is matplotlib's 'equal',
   0 is 'auto'. */
VPL_API VplResult VPL_CALL vplAxesSetAspect(VplAxes *axes, double aspect);
VPL_API VplResult VPL_CALL vplAxesGetAspect(const VplAxes *axes, double *outAspect);

/* Whether this axes has anything plotted on it -- has_data. Useful for
   deciding whether a panel of a grid is worth decorating. */
VPL_API VplResult VPL_CALL vplAxesHasData(const VplAxes *axes, int *outHasData);

/* The spectral family. `nfft` is the segment length (default 256), `noverlap`
   how much consecutive segments share (0, or 128 for a spectrogram), and `fs`
   the sampling frequency (2). psd and csd average the per-segment periodograms
   (Welch's method). Each sets the axis labels matplotlib sets, and psd/csd also
   pin whole-number decibel ticks. */

typedef enum VplColormap
{
    /* The perceptually uniform maps: equal steps in value look like equal steps
       in brightness, and they survive greyscale printing. */
    VplColormap_Viridis = 0,
    VplColormap_Magma = 1,
    VplColormap_Inferno = 2,
    VplColormap_Plasma = 3,
    VplColormap_Gray = 4,
} VplColormap;

/* The knobs the spectral family shares -- matplotlib's window, detrend, sides
   and pad_to. Every field's zero value is its matplotlib default (Hanning
   window, no detrend, one-sided, no padding), so a zero-initialised or NULL
   desc takes them all. */
typedef enum VplSpectralWindow
{
    VplSpectralWindow_Hanning = 0,
    VplSpectralWindow_Hamming = 1,
    VplSpectralWindow_Blackman = 2,
    VplSpectralWindow_Bartlett = 3,
    VplSpectralWindow_Boxcar = 4,
} VplSpectralWindow;
typedef enum VplSpectralDetrend
{
    VplSpectralDetrend_None = 0,
    VplSpectralDetrend_Mean = 1,
    VplSpectralDetrend_Linear = 2,
} VplSpectralDetrend;
typedef struct VplSpectralDesc
{
    size_t structSize;
    VplSpectralWindow window;
    VplSpectralDetrend detrend;
    int twoSided;   /* non-zero for a two-sided spectrum */
    size_t padTo;   /* 0 to pad to nfft (no padding) */
} VplSpectralDesc;

VPL_API VplResult VPL_CALL vplAxesPsd(VplAxes *axes,
                                      const double *x,
                                      size_t count,
                                      size_t nfft,
                                      double fs,
                                      size_t noverlap,
                                      const VplSpectralDesc *desc);

VPL_API VplResult VPL_CALL vplAxesCsd(VplAxes *axes,
                                      const double *x,
                                      const double *y,
                                      size_t count,
                                      size_t nfft,
                                      double fs,
                                      size_t noverlap,
                                      const VplSpectralDesc *desc);

/* |Pxy|^2 / (Pxx * Pyy): how much of y is linearly explained by x, per
   frequency. The cross-spectrum is averaged as a COMPLEX quantity before its
   modulus is taken -- the phase cancellation between segments is precisely
   what coherence measures. */
VPL_API VplResult VPL_CALL vplAxesCohere(VplAxes *axes,
                                         const double *x,
                                         const double *y,
                                         size_t count,
                                         size_t nfft,
                                         double fs,
                                         size_t noverlap,
                                         const VplSpectralDesc *desc);

/* The three single spectra: the WHOLE signal as one segment, and no scaling by
   frequency -- which is why they are not a one-segment psd. The phase spectrum
   is the angle spectrum unwrapped, and that is their only difference. */
/* `scaleDb` (non-zero) plots the magnitude in decibels, 20*log10, matplotlib's
   scale='dB'; zero leaves it as linear energy (scale='linear'/'default'). */
VPL_API VplResult VPL_CALL vplAxesMagnitudeSpectrum(VplAxes *axes,
                                                    const double *x,
                                                    size_t count,
                                                    double fs,
                                                    int scaleDb,
                                                    const VplSpectralDesc *desc);
VPL_API VplResult VPL_CALL vplAxesAngleSpectrum(VplAxes *axes,
                                                const double *x,
                                                size_t count,
                                                double fs,
                                                const VplSpectralDesc *desc);
VPL_API VplResult VPL_CALL vplAxesPhaseSpectrum(VplAxes *axes,
                                                const double *x,
                                                size_t count,
                                                double fs,
                                                const VplSpectralDesc *desc);

/* What a spectrogram shows and how it scales it -- matplotlib's specgram mode
   and scale. dB with the angle or phase mode is rejected, as matplotlib rejects
   it. */
typedef enum VplSpecMode
{
    VplSpecMode_Psd = 0,
    VplSpecMode_Magnitude = 1,
    VplSpecMode_Angle = 2,
    VplSpecMode_Phase = 3,
} VplSpecMode;
typedef enum VplSpecScale
{
    VplSpecScale_Default = 0,
    VplSpecScale_Linear = 1,
    VplSpecScale_Db = 2,
} VplSpecScale;

/* Power against time and frequency, as an image. Reconfigures the axes: the
   view is pinned to the spectrogram's extent and the aspect released. `mode`
   and `scale` pick what is shown; `xextent` (or NULL) is an optional {xmin,
   xmax} pair overriding the time axis span. */
VPL_API VplResult VPL_CALL vplAxesSpecgram(VplAxes *axes,
                                           const double *x,
                                           size_t count,
                                           size_t nfft,
                                           double fs,
                                           size_t noverlap,
                                           const VplSpectralDesc *desc,
                                           VplSpecMode mode,
                                           VplSpecScale scale,
                                           const double *xextent,
                                           VplColormap cmap);

/* An axes inside another's box -- inset_axes. The bounds are fractions of the
   PARENT's box, not of the figure.

   The position is resolved once, so create the inset AFTER any
   vplFigureTightLayout or vplFigureSubplotsAdjust; like vplFigureAddAxes, an
   inset belongs to no grid and later layout passes leave it where it was put. */
VPL_API VplResult VPL_CALL vplFigureInsetAxes(VplFigure *figure,
                                              const VplAxes *parent,
                                              double x,
                                              double y,
                                              double width,
                                              double height,
                                              VplAxes **outAxes);

/* The rectangle marking what an inset magnifies, plus the two corner lines that
   read as a magnifier -- indicate_inset. Which two of the four corners get lines
   is chosen by comparing the rectangle with the inset edge by edge. The
   rectangle does NOT count towards the data limits, matching matplotlib. */
VPL_API VplResult VPL_CALL vplAxesIndicateInset(VplAxes *axes,
                                                double x,
                                                double y,
                                                double width,
                                                double height,
                                                const VplAxes *inset);

/* The same, with the rectangle taken from the inset's own limits --
   indicate_inset_zoom. */
VPL_API VplResult VPL_CALL vplAxesIndicateInsetZoom(VplAxes *axes,
                                                    const VplAxes *inset);

/* A second scale that is an affine function of the first: `secondary = scale *
   primary + offset`. matplotlib accepts any pair of inverse functions; only the
   affine case is here, which covers the usual unit conversions. */
VPL_API VplResult VPL_CALL vplFigureSecondaryXAxis(VplFigure *figure,
                                                   const VplAxes *base,
                                                   int top,
                                                   double scale,
                                                   double offset,
                                                   VplAxes **outAxes);

VPL_API VplResult VPL_CALL vplFigureSecondaryYAxis(VplFigure *figure,
                                                   const VplAxes *base,
                                                   int right,
                                                   double scale,
                                                   double offset,
                                                   VplAxes **outAxes);

/* Streamlines through a vector field -- streamplot.

   `u` and `v` are row-major rows x cols over the grid x[0..cols) by y[0..rows),
   which must be equally spaced and strictly increasing. `density` sets the
   spacing between lines (the crowding mask is 30 cells per unit of it). The
   lines are integrated at call time in grid coordinates, and the view is pinned
   to the grid with no margins, as matplotlib pins it. */
VPL_API VplResult VPL_CALL vplAxesStreamPlot(VplAxes *axes,
                                             const double *x,
                                             const double *y,
                                             const double *u,
                                             const double *v,
                                             size_t rows,
                                             size_t cols,
                                             double density);

/* A label per pie wedge, at `distance` of the radius along the wedge's middle
   angle -- pie_label. Call it after vplAxesPie, which leaves the wedge angles
   behind for it to read. matplotlib's default distance is 0.6, which puts the
   labels inside the wedges. */
VPL_API VplResult VPL_CALL vplAxesPieLabel(VplAxes *axes,
                                           const char *const *labels,
                                           size_t count,
                                           double distance,
                                           double fontSize);

/* One box's worth of summary: what vplAxesBoxPlot computes internally, and what
   vplAxesBxp takes instead of raw data. whisLo and whisHi are where the whiskers
   END (usually the furthest observation within 1.5 IQR of the nearer quartile,
   but nothing here enforces that). Set median to a NaN to hold a position
   without drawing a box there. */
typedef struct VplBoxStats
{
    size_t structSize;

    double q1;
    double median;
    double q3;
    double whisLo;
    double whisHi;

    /* Points drawn individually beyond the whiskers; NULL for none. */
    const double *fliers;
    size_t flierCount;
} VplBoxStats;

/* Boxes drawn from statistics you already have -- matplotlib's bxp. Useful when
   only the five-number summary survives of the data, not the samples
   vplAxesBoxPlot would need. Geometry, positions and limits are identical to
   vplAxesBoxPlot's. */
VPL_API VplResult VPL_CALL vplAxesBxp(VplAxes *axes,
                                      const VplBoxStats *stats,
                                      size_t groupCount);

/* A kernel density estimate per group, mirrored about its position -- a violin
   plot. Same ragged-array convention as vplAxesBoxPlot: `values` is the groups
   laid end to end and `counts` gives each length.

   The density is Gaussian at Scott's bandwidth, evaluated at 100 points across
   each group's own range and scaled so every violin is the same width at its
   widest. The whole call takes ONE colour from the line cycle -- not one per
   group. */
VPL_API VplResult VPL_CALL vplAxesViolinPlot(VplAxes *axes,
                                             const double *values,
                                             const size_t *counts,
                                             size_t groupCount);

/* An infinite line through (x, y) at `slope`, clipped to the view -- axline.
   Pass vertical = 1 for a vertical line (`slope` is then ignored). Re-derived on
   every draw, so it stays corner to corner under pan and zoom. Its defining
   POINT counts towards the data limits; the line itself does not. */
VPL_API VplResult VPL_CALL vplAxesAxLine(VplAxes *axes,
                                         double x,
                                         double y,
                                         double slope,
                                         int vertical,
                                         const VplLineDesc *desc);

/* Write each bar's height above it -- bar_label with matplotlib's defaults: the
   value formatted as %g, centred on the bar and sitting on its top edge (or
   under it, for a bar that goes down). Pass the patch vplAxesBar returned. */
VPL_API VplResult VPL_CALL vplAxesBarLabel(VplAxes *axes, const VplPatch *bars);

/* bar_label(fmt=, label_type=, padding=). fmt is a single %-style
   floating-point conversion -- f/F/g/G/e/E, with ordinary flags/width/precision
   ("%.2f", "%+g", ...) -- rejected otherwise, since it reaches snprintf with one
   double argument. label_type places the text at the bar's tip (Edge, the
   default) or its middle (Center, for a stacked segment). padding is a shift in
   points outward from the bar, and applies to Edge only. */
typedef enum VplBarLabelType
{
    VplBarLabelType_Edge = 0,
    VplBarLabelType_Center = 1,
} VplBarLabelType;

VPL_API VplResult VPL_CALL vplAxesBarLabelFull(VplAxes *axes, const VplPatch *bars,
                                               const char *fmt, VplBarLabelType labelType,
                                               double padding);

/* The empirical distribution: the fraction of the sample at or below each
   value, drawn as a post-step. It sticks to 0 and 1, so the y axis of an ecdf
   gets no margin at either end. */
/* `weights` (or NULL for one each) weighs each observation. `complementary`
   (non-zero) draws the survival curve 1 - CDF, descending from 1 to 0.
   `horizontal` (non-zero) lays the proportion along x and the values up y,
   matplotlib's orientation='horizontal'. */
VPL_API VplResult VPL_CALL vplAxesEcdf(VplAxes *axes,
                                       const double *x,
                                       size_t count,
                                       const double *weights,
                                       int32_t complementary,
                                       int32_t horizontal,
                                       const VplLineDesc *desc,
                                       VplLine **outLine);

/* A single arrow from (x, y) along (dx, dy), as a filled patch. The head is
   three times `width` across and one and a half times that long, added BEYOND
   the endpoint (matplotlib's length_includes_head=false), so the drawn arrow is
   longer than the vector given. */
VPL_API VplResult VPL_CALL vplAxesArrow(VplAxes *axes,
                                        double x,
                                        double y,
                                        double dx,
                                        double dy,
                                        double width,
                                        const VplPatchDesc *desc,
                                        VplPatch **outPatch);

/* Horizontal error bars on an existing line, the mirror of the vertical ones
   vplAxesErrorBar draws. Pass the handle that returned. */
VPL_API VplResult VPL_CALL vplAxesSetXErr(VplAxes *axes,
                                          VplLine *line,
                                          const double *xerr,
                                          size_t count);

/* How tick labels may be scaled -- matplotlib's ticklabel_format.

   Two independent mechanisms, both on by default:

     the OFFSET     subtracts a constant shared by every tick and writes it at
                    the end of the axis as "+1e6". It engages only when it saves
                    at least four digits and every tick has the same sign.

     SCIENTIFIC     divides every tick and writes "1e6". It engages when the
                    largest tick's exponent falls outside [sciLimitLo,
                    sciLimitHi], which default to -5 and 6 -- so values around
                    1e5 stay written out in full and 1e6 do not.

   Pass useOffset = 0 and scientific = 0 for labels that are always the plain
   number, however wide that makes them. */
VPL_API VplResult VPL_CALL vplAxesTickLabelFormat(VplAxes *axes,
                                                  VplAxis axis,
                                                  int useOffset,
                                                  int scientific,
                                                  int sciLimitLo,
                                                  int sciLimitHi);

/* set_box_aspect: the axes BOX's height/width in physical units, whatever the
   data says. Pass 0 to clear it. Unlike vplAxesSetAspect (which constrains the
   data units and depends on the view), this constrains the PANEL shape. Where a
   shrunk box sits inside its slot is vplAxesSetAnchor's business. */
VPL_API VplResult VPL_CALL vplAxesSetBoxAspect(VplAxes *axes, double boxAspect);
VPL_API VplResult VPL_CALL vplAxesGetBoxAspect(const VplAxes *axes, double *outBoxAspect);

/* Where a shrunk box sits inside its position: (0, 0) is the lower left corner
   and (1, 1) the upper right, matplotlib's 'SW' through 'NE' (default 'C', i.e.
   (0.5, 0.5)). Only has an effect once an aspect setting shrinks the box. */
VPL_API VplResult VPL_CALL vplAxesSetAnchor(VplAxes *axes, double x, double y);
VPL_API VplResult VPL_CALL vplAxesGetAnchor(const VplAxes *axes, double *outX, double *outY);

/* locator_params: how many intervals the tick locator may use, instead of
   deriving it from how much room the axis has. Pass 0 for either axis to go
   back to the derived value, which is matplotlib's 'auto'. */
VPL_API VplResult VPL_CALL vplAxesLocatorParams(VplAxes *axes, VplAxis axis, int nbins);

/* locator_params(steps=, tight=), alongside nbins above. `steps` restricts
   which round multiples (within one decade, 1..10) the locator may place a tick
   spacing at -- [1, 5, 10] forces steps of 1, 5, 10, 50, 100, ... only. Pass
   NULL / stepsCount 0 to leave the steps unchanged; otherwise the sequence must
   be strictly increasing with every value in [1, 10].

   `tight` is range-checked (-1, 0 or 1) but has no visible effect here, since
   MaxNLocator does not override view_limits(). This is NOT vplAxesAutoscale's
   `tight`, which really does zero the margin. */
VPL_API VplResult VPL_CALL vplAxesLocatorParamsFull(VplAxes *axes, VplAxis axis, int nbins,
                                                    const double *steps, size_t stepsCount,
                                                    int tight);

/* set_prop_cycle: the colours plot(), bar() and the rest draw from, in order.

   `colorSpecs` takes the same strings everything else does ("C0", "tab:red",
   "#1f77b4", "0.4"). Pass NULL to go back to tab10. One list serves both the
   line and the patch cycle, whose counters stay independent. */
VPL_API VplResult VPL_CALL vplAxesSetPropCycle(VplAxes *axes,
                                               const char *const *colorSpecs,
                                               size_t count);

/* set_prop_cycle(color=, linestyle=, marker=, linewidth=): every property at
   once, zipped together as matplotlib's Cycler adds them -- entry i is
   colors[i], linestyles[i], markers[i], linewidths[i]. Only plot() reads
   linestyle, marker or linewidth from the cycle; bar() and the rest take only
   colour.

   Pass NULL/0 for any array to leave that property out of the cycle. Whichever
   arrays are given must all be the same length; colorCount may be 0 only if
   colorSpecs is also NULL (colour then falls back to tab10). Always REPLACES the
   whole cycle, including properties a previous call set. */
VPL_API VplResult VPL_CALL vplAxesSetPropCycleFull(VplAxes *axes,
                                                    const char *const *colorSpecs,
                                                    size_t colorCount,
                                                    const VplLineStyle *linestyles,
                                                    size_t linestyleCount,
                                                    const VplMarker *markers,
                                                    size_t markerCount,
                                                    const double *linewidths,
                                                    size_t linewidthCount);

/* The figure's own patch -- the background behind every axes. The edge is WHITE
   at zero width by default, so setting a colour alone changes nothing until the
   width is raised. */
VPL_API VplResult VPL_CALL vplFigureSetFaceColor(VplFigure *figure, const char *colorSpec);
VPL_API VplResult VPL_CALL vplFigureSetEdgeColor(VplFigure *figure, const char *colorSpec);
VPL_API VplResult VPL_CALL vplFigureSetLineWidth(VplFigure *figure, double width);
VPL_API VplResult VPL_CALL vplFigureSetFrameOn(VplFigure *figure, int on);
VPL_API VplResult VPL_CALL vplFigureGetFrameOn(const VplFigure *figure, int *outOn);
VPL_API VplResult VPL_CALL vplFigureGetLineWidth(const VplFigure *figure, double *outWidth);

/* Whether each axis still follows its data. Turning autoscaling OFF pins the
   view where it currently is, so call it after plotting; turning it back on
   drops the pin. */
VPL_API VplResult VPL_CALL vplAxesSetAutoscaleX(VplAxes *axes, int on);
VPL_API VplResult VPL_CALL vplAxesSetAutoscaleY(VplAxes *axes, int on);
VPL_API VplResult VPL_CALL vplAxesSetAutoscale(VplAxes *axes, int on);
VPL_API VplResult VPL_CALL vplAxesGetAutoscaleX(const VplAxes *axes, int *outOn);
VPL_API VplResult VPL_CALL vplAxesGetAutoscaleY(const VplAxes *axes, int *outOn);
VPL_API VplResult VPL_CALL vplAxesGetAutoscale(const VplAxes *axes, int *outOn);

/* The axes background, behind the data and under the grid. */
VPL_API VplResult VPL_CALL vplAxesSetFaceColor(VplAxes *axes, const char *colorSpec);
VPL_API VplResult VPL_CALL vplAxesGetFaceColor(const VplAxes *axes, double *outRgba);

/* The data's own height over its width -- get_data_ratio, the number
   vplAxesSetAspect(1) makes the box match. */
VPL_API VplResult VPL_CALL vplAxesGetDataRatio(const VplAxes *axes, double *outRatio);

/* Push a rectangle into the data limits by hand -- update_datalim. For an
   artist whose limits are not the extent of what it draws, and for reserving
   room around something drawn in another coordinate system. */
VPL_API VplResult VPL_CALL vplAxesUpdateDataLim(VplAxes *axes,
                                                double xmin, double ymin,
                                                double xmax, double ymax);

/* The tick label STRINGS, as they would be drawn -- get_xticklabels. Call with
   `labels` NULL to learn the count, then again with an array of that many
   `char *`. `labelCount` is how many pointers the array holds and `bufferSize`
   how large each buffer is; a longer label is truncated, and too few pointers is
   an error. */
VPL_API VplResult VPL_CALL vplAxesGetXTickLabels(const VplAxes *axes,
                                                 char *const *labels,
                                                 size_t labelCount,
                                                 size_t bufferSize,
                                                 size_t *outCount);
VPL_API VplResult VPL_CALL vplAxesGetYTickLabels(const VplAxes *axes,
                                                 char *const *labels,
                                                 size_t labelCount,
                                                 size_t bufferSize,
                                                 size_t *outCount);

/* The figure patch's colours, read back. */
VPL_API VplResult VPL_CALL vplFigureGetFaceColor(const VplFigure *figure, double *outRgba);
VPL_API VplResult VPL_CALL vplFigureGetEdgeColor(const VplFigure *figure, double *outRgba);

/* Remove an axes from the figure -- delaxes. Every handle to it, and to
   anything it drew, is dangling afterwards. */
VPL_API VplResult VPL_CALL vplFigureDelAxes(VplFigure *figure, VplAxes *axes);

/* The figure's current axes -- gca, which ADDS one if the figure has none, and
   sca, which makes an existing axes current. "Current" here means only "the one
   gca returns"; nothing else in this ABI consults it. For porting code that
   leans on matplotlib's implicit state. */
VPL_API VplResult VPL_CALL vplFigureGca(VplFigure *figure, VplAxes **outAxes);
VPL_API VplResult VPL_CALL vplFigureSca(VplFigure *figure, VplAxes *axes);

/* Whether a fixed aspect ratio is honoured by resizing the BOX or by widening
   the data LIMITS -- matplotlib's set_adjustable. 'box' (the default) shrinks
   the panel inside its slot until the aspect holds. 'datalim' is not
   implemented and is rejected rather than silently treated as 'box'. */
typedef enum VplAdjustable
{
    VplAdjustable_Box = 0,
    VplAdjustable_Datalim = 1,
} VplAdjustable;

VPL_API VplResult VPL_CALL vplAxesSetAdjustable(VplAxes *axes, VplAdjustable adjustable);
VPL_API VplResult VPL_CALL vplAxesGetAdjustable(const VplAxes *axes, VplAdjustable *outAdjustable);

/* Show or hide one edge of the frame -- commonly the top and right pair. */
VPL_API VplResult VPL_CALL vplAxesSetSpineVisible(VplAxes *axes,
                                                  VplSpine spine,
                                                  int visible);

/* Vertical bars of `width` in data units, centred on each x.

   `bottom` may be NULL for the usual bars rising from zero, or hold one value
   per bar -- pass the running total of the series below to stack them. Each bar
   sticks to its OWN bottom, so a stack starting away from zero autoscales the
   way matplotlib's does. */
VPL_API VplResult VPL_CALL vplAxesBar(VplAxes *axes,
                                      const double *x,
                                      const double *height,
                                      double width,
                                      const double *bottom,
                                      size_t count,
                                      const VplPatchDesc *desc,
                                      VplPatch **outPatch);

typedef enum VplHAlign
{
    VplHAlign_Left = 0,
    VplHAlign_Center = 1,
    VplHAlign_Right = 2,
} VplHAlign;

typedef enum VplVAlign
{
    VplVAlign_Baseline = 0,
    VplVAlign_Bottom = 1,
    VplVAlign_Center = 2,
    VplVAlign_Top = 3,
} VplVAlign;


/* Where a table sits relative to the axes -- matplotlib's eighteen codes, in
   its own numeric order. The first ten place it INSIDE the axes, inset by 2% of
   the axes; the rest place it outside, against one edge. */
typedef enum VplTableLoc
{
    VplTableLoc_Best = 0,
    VplTableLoc_UpperRight,
    VplTableLoc_UpperLeft,
    VplTableLoc_LowerLeft,
    VplTableLoc_LowerRight,
    VplTableLoc_CenterLeft,
    VplTableLoc_CenterRight,
    VplTableLoc_LowerCenter,
    VplTableLoc_UpperCenter,
    VplTableLoc_Center,
    VplTableLoc_TopRight,
    VplTableLoc_TopLeft,
    VplTableLoc_BottomLeft,
    VplTableLoc_BottomRight,
    VplTableLoc_Right,
    VplTableLoc_Left,
    VplTableLoc_Top,
    VplTableLoc_Bottom, /* the default: under the axes, centred */
} VplTableLoc;

typedef enum VplTableField
{
    VplTableField_ColLabels = 1u << 0,
    VplTableField_RowLabels = 1u << 1,
    VplTableField_ColWidths = 1u << 2,
    VplTableField_FontSize = 1u << 3,
    VplTableField_Loc = 1u << 4,
    VplTableField_CellLoc = 1u << 5,
    VplTableField_RowLoc = 1u << 6,
    VplTableField_ColLoc = 1u << 7,
    VplTableField_EdgeColorSpec = 1u << 8,
    VplTableField_FaceColorSpec = 1u << 9,
    VplTableField_LineWidth = 1u << 10,
} VplTableField;

typedef struct VplTableDesc
{
    size_t structSize;
    uint32_t set;

    /* A header row above the data; must have `cols` entries. */
    const char *const *colLabels;
    /* A header column to its left; must have `rows` entries. Its width is
       MEASURED from the labels, unlike every other column. */
    const char *const *rowLabels;
    /* One per column, in axes units. Unset means 1/cols each. */
    const double *colWidths;

    double fontSize;
    VplTableLoc loc;

    /* matplotlib's defaults differ per region: data cells right, row labels
       left, column labels centred. */
    VplHAlign cellLoc;
    VplHAlign rowLoc;
    VplHAlign colLoc;

    const char *edgeColorSpec;
    const char *faceColorSpec;
    double lineWidth;
} VplTableDesc;

/* A grid of text, positioned in AXES coordinates -- matplotlib's table. Not in
   data coordinates, so it neither moves with a pan nor counts towards the
   limits. Row height is derived from the font size rather than measured, as in
   matplotlib. `cellText` is rows * cols strings, ROW-MAJOR. Drawn beneath
   everything else, matching matplotlib's zorder. */
VPL_API VplResult VPL_CALL vplAxesTable(VplAxes *axes,
                                        const char *const *cellText,
                                        size_t rows,
                                        size_t cols,
                                        const VplTableDesc *desc);

typedef enum VplTextField
{
    VplTextField_Color = 1u << 0,
    VplTextField_ColorSpec = 1u << 1,
    VplTextField_Size = 1u << 2,
    VplTextField_Rotation = 1u << 3,
    VplTextField_Align = 1u << 4,
} VplTextField;

typedef struct VplTextDesc
{
    size_t structSize;
    uint32_t set;

    float color[4];
    const char *colorSpec;
    double size;     /* points; default 10, matching font.size */
    double rotation; /* degrees, counterclockwise */
    VplHAlign halign;
    VplVAlign valign;
} VplTextDesc;

/* Text at a data position. matplotlib's text() defaults are ha='left',
   va='baseline', which is what you get with no desc. */
VPL_API VplResult VPL_CALL vplAxesText(VplAxes *axes,
                                       double x,
                                       double y,
                                       const char *text,
                                       const VplTextDesc *desc);

/* Text at (textX, textY) with a leader line to (x, y) -- matplotlib's
   annotate(text, xy=(x, y), xytext=(textX, textY), arrowprops=...). The arrow
   head is sized in points, so it stays the same size on screen no matter how
   far the axes are zoomed. */
VPL_API VplResult VPL_CALL vplAxesAnnotate(VplAxes *axes,
                                           const char *text,
                                           double x,
                                           double y,
                                           double textX,
                                           double textY,
                                           const VplTextDesc *desc);

/* Reference lines spanning the axes at a constant value. They follow the view
   rather than the data, so they stay full width through pan and zoom, and they
   do not affect autoscaling. */
VPL_API VplResult VPL_CALL vplAxesAxHLine(VplAxes *axes, double y, const VplLineDesc *desc);
VPL_API VplResult VPL_CALL vplAxesAxVLine(VplAxes *axes, double x, const VplLineDesc *desc);

/* Pin one axis while the other keeps autoscaling, matching set_xlim/set_ylim. */
VPL_API VplResult VPL_CALL vplAxesSetXLim(VplAxes *axes, double left, double right);
VPL_API VplResult VPL_CALL vplAxesSetYLim(VplAxes *axes, double bottom, double top);

/* set_xbound / set_ybound: like set_xlim but leaves the current orientation
   alone (where set_xlim reads the argument ORDER as the orientation). Pass NAN
   for either argument to mean "leave this end where it is". */
VPL_API VplResult VPL_CALL vplAxesSetXBound(VplAxes *axes, double lower, double upper);
VPL_API VplResult VPL_CALL vplAxesSetYBound(VplAxes *axes, double lower, double upper);

VPL_API VplResult VPL_CALL vplAxesSetTitle(VplAxes *axes, const char *title);

/* Read the title back. Fills `buffer` with a NUL-terminated UTF-8 string and
   writes the length INCLUDING the terminator to outRequiredSize; pass
   buffer = NULL to ask for the size first. Returns VplResult_BufferTooSmall
   without writing if it does not fit -- the "fill a buffer, return the needed
   length" convention used everywhere else in this header. */
VPL_API VplResult VPL_CALL vplAxesGetTitle(const VplAxes *axes,
                                           char *buffer,
                                           size_t bufferSize,
                                           size_t *outRequiredSize);

/* How many artists on this axes carry a legend label, and what those labels
   are -- get_legend_handles_labels without the handles. Order matches
   matplotlib's: lines, then patches, then scatter and the other collections,
   each in the order added. Artists with an empty label are skipped. */
VPL_API VplResult VPL_CALL vplAxesGetLegendLabelCount(const VplAxes *axes,
                                                      size_t *outCount);

VPL_API VplResult VPL_CALL vplAxesGetLegendLabel(const VplAxes *axes,
                                                 size_t index,
                                                 char *buffer,
                                                 size_t bufferSize,
                                                 size_t *outRequiredSize);

/* ax.lines / ax.patches / ax.images: the artists this axes already holds,
   retrievable again by index if the caller did not keep the handle a plotting
   call returned. One call per artist TYPE, since the C ABI has no single handle
   type spanning Line2D/Patch/Image. Order matches matplotlib's: the order each
   artist was added in. */
VPL_API VplResult VPL_CALL vplAxesGetLineCount(const VplAxes *axes, size_t *outCount);
VPL_API VplResult VPL_CALL vplAxesGetLine(const VplAxes *axes, size_t index,
                                          VplLine **outLine);
VPL_API VplResult VPL_CALL vplAxesGetPatchCount(const VplAxes *axes, size_t *outCount);
VPL_API VplResult VPL_CALL vplAxesGetPatch(const VplAxes *axes, size_t index,
                                           VplPatch **outPatch);
VPL_API VplResult VPL_CALL vplAxesGetImageCount(const VplAxes *axes, size_t *outCount);
VPL_API VplResult VPL_CALL vplAxesGetImage(const VplAxes *axes, size_t index,
                                           VplImage **outImage);

VPL_API VplResult VPL_CALL vplAxesSetXLabel(VplAxes *axes, const char *label);
VPL_API VplResult VPL_CALL vplAxesSetYLabel(VplAxes *axes, const char *label);

typedef enum VplScale
{
    VplScale_Linear = 0,
    VplScale_Log = 1, /* base 10 */
} VplScale;

/* On a log axis, non-positive data is excluded from the limits rather than
   drawn, the same way matplotlib handles it. */
VPL_API VplResult VPL_CALL vplAxesSetXScale(VplAxes *axes, VplScale scale);
VPL_API VplResult VPL_CALL vplAxesSetYScale(VplAxes *axes, VplScale scale);

/* Read them back -- get_xscale / get_yscale. */
VPL_API VplResult VPL_CALL vplAxesGetXScale(const VplAxes *axes, VplScale *outScale);
VPL_API VplResult VPL_CALL vplAxesGetYScale(const VplAxes *axes, VplScale *outScale);

/* Off by default, matching axes.grid. Grid lines are drawn under the data. */
/* Which gridlines a grid() call touches -- matplotlib's which and axis. */
typedef enum VplGridWhich
{
    VplGridWhich_Major = 0,
    VplGridWhich_Minor = 1,
    VplGridWhich_Both = 2,
} VplGridWhich;
typedef enum VplGridAxis
{
    VplGridAxis_Both = 0,
    VplGridAxis_X = 1,
    VplGridAxis_Y = 2,
} VplGridAxis;

/* Turn gridlines on or off -- grid(visible, which, axis). `which` picks the
   major ticks, the minor ticks, or both; `axis` picks x, y, or both. Minor
   gridlines appear only where minor ticks do (see vplAxesSetMinorTicks). */
VPL_API VplResult VPL_CALL vplAxesSetGrid(VplAxes *axes, int visible, VplGridWhich which,
                                          VplGridAxis axis);

/* Unlabelled ticks between the major ones, off by default to match
   {x,y}tick.minor.visible. The subdivision count is chosen the way
   AutoMinorLocator does: 5 when the major step divides evenly into fifths,
   4 otherwise. Ignored on log axes, which already tick per decade. */
VPL_API VplResult VPL_CALL vplAxesSetMinorTicks(VplAxes *axes, int onX, int onY);

/* matplotlib's ten positions, with its own numbering: each names a compass
   point of the axes box shrunk inwards by legend.borderaxespad. */
typedef enum VplLegendLoc
{
    /* Whichever position the plotted data covers least -- matplotlib's default. */
    VplLegendLoc_Best = 0,
    VplLegendLoc_UpperRight = 1,
    VplLegendLoc_UpperLeft = 2,
    VplLegendLoc_LowerLeft = 3,
    VplLegendLoc_LowerRight = 4,
    VplLegendLoc_Right = 5,
    VplLegendLoc_CenterLeft = 6,
    VplLegendLoc_CenterRight = 7,
    VplLegendLoc_LowerCenter = 8,
    VplLegendLoc_UpperCenter = 9,
    VplLegendLoc_Center = 10,
} VplLegendLoc;

/* One entry per labelled line, scatter or patch. */
VPL_API VplResult VPL_CALL vplAxesSetLegend(VplAxes *axes, int visible, VplLegendLoc loc);

/* Legend.set_title: a heading centred above the entries, in the legend's own
   font size. Pass "" or NULL to remove it. */
VPL_API VplResult VPL_CALL vplAxesSetLegendTitle(VplAxes *axes, const char *title);

/* legend(ncols=): entries flow down each column before starting the next --
   matplotlib's np.array_split, so 5 entries with ncols=2 give a first column of
   3 and a second of 2. Defaults to 1. */
VPL_API VplResult VPL_CALL vplAxesSetLegendNCols(VplAxes *axes, int ncols);

/* A colour scale beside `parent`, which is narrowed to make room for it, after
   matplotlib's colorbar defaults (15% of the width, 5% of it as the gap, and a
   height-to-width ratio of 20). The returned axes is an ordinary one: set a
   ylabel on it to caption the scale. Owned by the figure. */
VPL_API VplResult VPL_CALL vplFigureColorbar(VplFigure *figure,
                                             VplAxes *parent,
                                             VplColormap cmap,
                                             double vmin,
                                             double vmax,
                                             VplAxes **outAxes);

/* Same, but the strip runs below `parent` instead of beside it -- pass
   horizontal = 1 for matplotlib's orientation='horizontal'. The gap is wider by
   default here (15% of the height rather than 5%) to clear the parent's x tick
   labels. */
VPL_API VplResult VPL_CALL vplFigureColorbarOriented(VplFigure *figure,
                                                      VplAxes *parent,
                                                      VplColormap cmap,
                                                      double vmin,
                                                      double vmax,
                                                      int horizontal,
                                                      VplAxes **outAxes);

typedef enum VplFieldField
{
    VplFieldField_Colormap = 1u << 0,
    VplFieldField_VLimits = 1u << 1, /* both vmin and vmax */
    VplFieldField_Extent = 1u << 2,
    VplFieldField_Alpha = 1u << 3,
    VplFieldField_Label = 1u << 4,
    VplFieldField_LineWidth = 1u << 5, /* contour only */
    VplFieldField_ColorSpec = 1u << 6, /* contour only: one colour for all levels */
} VplFieldField;

typedef struct VplFieldDesc
{
    size_t structSize;
    uint32_t set;

    VplColormap cmap;
    double vmin;
    double vmax;
    /* x0, x1, y0, y1 in data coordinates. */
    double extent[4];
    double alpha;
    const char *label;
    double lineWidth;
    const char *colorSpec;
} VplFieldDesc;

/*
 * A scalar field as coloured cells. `values` is row-major, rows * cols, with row
 * 0 drawn at the TOP -- imshow's origin='upper'. Emits one filled quad per cell
 * rather than a resampled raster, so SVG and PDF output stays vector; ideal for
 * heatmaps and matrices, but a large photographic array would produce a very big
 * file.
 */
VPL_API VplResult VPL_CALL vplAxesImshow(VplAxes *axes,
                                         const double *X,
                                         size_t rows,
                                         size_t cols,
                                         const VplFieldDesc *desc,
                                         VplImage **outImage);

/* An image painted straight onto the FIGURE's pixels -- figimage. Every array
   element is ONE device pixel, never resampled: for a logo, watermark or
   pre-rendered raster, not for data on an axis. `X` is row-major, rows x cols;
   `xo` and `yo` are the lower-left corner in device pixels from the figure's
   lower-left. Row 0 is drawn at the TOP (origin='upper'), behind the axes.

   `desc` supplies the colormap and its limits, as for vplAxesImshow; pass NULL
   for the viridis default autoscaled to the data. */
VPL_API VplResult VPL_CALL vplFigureFigImage(VplFigure *figure,
                                             const double *X,
                                             size_t rows,
                                             size_t cols,
                                             double xo,
                                             double yo,
                                             const VplFieldDesc *desc);

/* The same field on an explicit, possibly UNEVEN grid -- pcolormesh.

   `xEdges` has cols + 1 entries and `yEdges` rows + 1, and they are cell
   BOUNDARIES rather than centres. Three things differ from vplAxesImshow: row 0
   is at the bottom rather than the top, the aspect is free rather than square,
   and the view ends exactly at the outermost edges. */
VPL_API VplResult VPL_CALL vplAxesPcolorMesh(VplAxes *axes,
                                             const double *X,
                                             const double *Y,
                                             const double *C,
                                             size_t rows,
                                             size_t cols,
                                             const VplFieldDesc *desc,
                                             VplImage **outImage);

/* imshow with a matrix's furniture: column indices along the TOP, tick marks on
   both horizontal edges, and whole-number tick positions on both axes. */
VPL_API VplResult VPL_CALL vplAxesMatShow(VplAxes *axes,
                                          const double *Z,
                                          size_t rows,
                                          size_t cols,
                                          const VplFieldDesc *desc,
                                          VplImage **outImage);

/* Where a matrix is non-zero -- its sparsity pattern, on a white-to-black scale
   so the blanks read as blanks. Same furniture as vplAxesMatShow. */
VPL_API VplResult VPL_CALL vplAxesSpy(VplAxes *axes,
                                      const double *Z,
                                      size_t rows,
                                      size_t cols,
                                      const VplFieldDesc *desc,
                                      VplImage **outImage);

/* A two-dimensional histogram, drawn as a mesh of counts over an even grid
   spanning each axis's own data range. */
VPL_API VplResult VPL_CALL vplAxesHist2D(VplAxes *axes,
                                         const double *x,
                                         const double *y,
                                         size_t count,
                                         size_t xBins,
                                         size_t yBins);

/* Hexagonal binning: the density of a scatter drawn as a honeycomb, coloured by
   count in viridis.

   `gridsize` counts cells across x and defaults to matplotlib's 100 when 0 is
   passed; the y count follows as gridsize / sqrt(3) so the hexagons stay
   regular. Empty bins are dropped rather than drawn in the lowest colour --
   that is what distinguishes a hexbin from vplAxesHist2D.

   Like matplotlib's, this pins the axes to the bin extent with no margin. */
VPL_API VplResult VPL_CALL vplAxesHexbin(VplAxes *axes,
                                         const double *x,
                                         const double *y,
                                         size_t count,
                                         size_t gridsize);

/* An unstructured triangular grid drawn as its edges -- triplot with no
   triangles given, so the Delaunay triangulation is computed for you.

   Drawn in lines.color (a fixed C0, like the other line collections) rather
   than from the property cycle. */
VPL_API VplResult VPL_CALL vplAxesTriplot(VplAxes *axes,
                                          const double *x,
                                          const double *y,
                                          size_t count);

/* Iso-lines over an unstructured triangular grid -- tricontour with no triangles
   given, so the Delaunay triangulation is computed for you.

   Levels are coloured across their range from the colormap, as contour does.
   The field is linear across each triangle, so unlike the structured case there
   are no saddles to resolve. */
VPL_API VplResult VPL_CALL vplAxesTriContour(VplAxes *axes,
                                             const double *x,
                                             const double *y,
                                             const double *z,
                                             size_t count,
                                             const double *levels,
                                             size_t levelCount,
                                             const VplFieldDesc *desc,
                                             VplContour **outContour);

/* Filled bands over the same triangulation -- tricontourf. n levels give n - 1
   bands, each coloured by the colormap at its midpoint, and anything outside
   the level range is left unpainted, as with vplAxesContourF.

   A triangle's field is linear, so the band within it is the triangle clipped
   by two half-planes: one convex polygon, with none of the ambiguity a
   structured cell can present. */
VPL_API VplResult VPL_CALL vplAxesTriContourF(VplAxes *axes,
                                              const double *x,
                                              const double *y,
                                              const double *z,
                                              size_t count,
                                              const double *levels,
                                              size_t levelCount,
                                              const VplFieldDesc *desc,
                                              VplContour **outContour);

/* A scalar field over an unstructured grid, one flat colour per triangle --
   tripcolor with the default shading, so each triangle takes the mean of its
   three vertex values. The Delaunay triangulation is computed for you.

   Like matplotlib's, this turns the axes grid off. */
VPL_API VplResult VPL_CALL vplAxesTriPcolor(VplAxes *axes,
                                            const double *x,
                                            const double *y,
                                            const double *z,
                                            size_t count);

/* Wedges proportional to each value's share of the total, starting at 3 o'clock
   and running counter-clockwise in the patch cycle's colours.

   This RECONFIGURES the axes, as matplotlib's pie does: square aspect, no
   frame, no ticks, limits pinned at +/-1.25. Do not expect to plot anything
   else on the same axes afterwards. */
VPL_API VplResult VPL_CALL vplAxesPie(VplAxes *axes, const double *x, size_t count);

/* Iso-lines through a scalar field, extracted by marching squares. Levels are
   computed at call time, so later edits to `values` are not reflected. */
VPL_API VplResult VPL_CALL vplAxesContour(VplAxes *axes,
                                          const double *values,
                                          size_t rows,
                                          size_t cols,
                                          const double *levels,
                                          size_t levelCount,
                                          const VplFieldDesc *desc,
                                          VplContour **outContour);

/* Filled contours: the same field and the same levels, but what is drawn is the
   BANDS between consecutive levels. n levels give n - 1 bands, each coloured by
   the colormap at the band's midpoint, and anything outside the level range is
   left unpainted -- matplotlib's extend='neither'.

   Drawn without antialiasing, as matplotlib draws them: neighbouring bands
   share a boundary, and two half-covered colours compositing over each other
   leave a pale seam along every level line. */
VPL_API VplResult VPL_CALL vplAxesContourF(VplAxes *axes,
                                           const double *values,
                                           size_t rows,
                                           size_t cols,
                                           const double *levels,
                                           size_t levelCount,
                                           const VplFieldDesc *desc,
                                           VplContour **outContour);

typedef enum VplClabelField
{
    VplClabelField_FontSize = 1u << 0,
    VplClabelField_Inline = 1u << 1,
    VplClabelField_InlineSpacing = 1u << 2,
    VplClabelField_RightSideUp = 1u << 3,
    VplClabelField_ColorSpec = 1u << 4, /* one colour for every label */
} VplClabelField;

typedef struct VplClabelDesc
{
    size_t structSize;
    uint32_t set;

    double fontSize;
    /* Break each line so its label sits in a gap rather than on top of the
       line. On by default, as in matplotlib. */
    int inlineLabels;
    /* Clear space either side of the label, in pixels. */
    double inlineSpacing;
    /* Rotate labels to (-90, 90] so none comes out upside down. On by
       default. */
    int rightSideUp;
    const char *colorSpec;
} VplClabelDesc;

/* Label the lines of a contour set -- clabel.

   Each label's text is the level, formatted as an axis tick. Placement is
   deferred to draw time, since it is decided in pixels (which lines are long
   enough, where each runs straightest, whether two would crowd). This differs
   from matplotlib, which decides at clabel() call time. Only line contours carry
   labels; on a filled set this has no effect. */
VPL_API VplResult VPL_CALL vplContourClabel(VplContour *contour,
                                            const VplClabelDesc *desc);

/* View limits in data coordinates. Setting them pins the view; autoscale
   returns to fitting the data with matplotlib's 5% margins. */
VPL_API VplResult VPL_CALL vplAxesSetViewLimits(VplAxes *axes,
                                                double xmin, double ymin,
                                                double xmax, double ymax);
VPL_API VplResult VPL_CALL vplAxesGetViewLimits(const VplAxes *axes,
                                                double *outXmin, double *outYmin,
                                                double *outXmax, double *outYmax);
/* Which axis an autoscale touches. */
typedef enum VplAxisSel
{
    VplAxisSel_Both = 0,
    VplAxisSel_X = 1,
    VplAxisSel_Y = 2,
} VplAxisSel;

/* autoscale(enable, axis, tight). `enable` is 1 to autoscale the axis, 0 to pin
   the current view, -1 to leave it. `tight` is 1 to fit the view to the data
   with no margin, 0 to restore the 0.05 default margin, -1 to leave it. */
VPL_API VplResult VPL_CALL vplAxesAutoscale(VplAxes *axes, int enable, VplAxisSel axis,
                                            int tight);

/* Whether the view honours the sticky edges artists set -- use_sticky_edges.
   Off (0) lets the margin apply everywhere, so a bar chart gets air under its
   baseline instead of sitting on the axis. */
VPL_API VplResult VPL_CALL vplAxesSetUseStickyEdges(VplAxes *axes, int on);
VPL_API VplResult VPL_CALL vplAxesGetUseStickyEdges(const VplAxes *axes, int *outOn);

/* Add a value the view will not let a margin cross -- a caller-set entry in
   sticky_edges.x / sticky_edges.y, the same list artists append their baselines
   to. */
VPL_API VplResult VPL_CALL vplAxesAddStickyEdgeX(VplAxes *axes, double value);
VPL_API VplResult VPL_CALL vplAxesAddStickyEdgeY(VplAxes *axes, double value);

/* -------------------------------------------------------------------------
 * Interaction
 *
 * Pixel coordinates here are the ones a UI toolkit reports: origin at the TOP
 * left of the rendered image, y increasing downward. That matches the buffer
 * layout from vplFigureRenderRGBA, so an ImGui host can pass mouse positions
 * straight through.
 * ------------------------------------------------------------------------- */

/* Drag the data under the cursor: the point beneath (fromX, fromY) ends up
   beneath (toX, toY). */
VPL_API VplResult VPL_CALL vplAxesPan(VplAxes *axes,
                                      double fromX, double fromY,
                                      double toX, double toY);

/* Positive `steps` zooms in about (pixelX, pixelY), as a scroll wheel would. */
VPL_API VplResult VPL_CALL vplAxesZoomAt(VplAxes *axes,
                                         double pixelX, double pixelY,
                                         double steps);

VPL_EXTERN_C_END

#endif /* VPL_H */
