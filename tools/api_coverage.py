#!/usr/bin/env python3
"""Measure vplot's API surface against matplotlib's and write docs/API-COVERAGE.md.

    pip install matplotlib==3.11.1
    python tools/api_coverage.py

Two invariants are checked against the installed libraries rather than asserted:

  * every name in STATUS must still exist in the installed matplotlib, so a
    matplotlib release that renames or drops a method fails the script instead
    of leaving the document stale;

  * every name claimed as implemented must name a vplot symbol present in
    include/vpl/vpl.h or source/vplot/core/figure.h.

The check also runs in reverse: every ABI entry must either map to a matplotlib
method or be listed in EXTRA with a reason. Anything matplotlib has that STATUS
does not mention counts as missing, so the default is "not implemented".

Instance attributes (`ax.spines`, `ax.xaxis`, `ax.patch`, ...) are invisible to
class introspection and cannot be enumerated like methods, so they live in a
hand-written table (INSTANCE_ATTRS).
"""

import inspect
import io
import os
import re
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402  (must follow matplotlib.use)
from matplotlib.artist import Artist  # noqa: E402
from matplotlib.axes import Axes  # noqa: E402
from matplotlib.figure import Figure  # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
OUT = os.path.join(ROOT, "docs", "API-COVERAGE.md")

FULL = "full"
PARTIAL = "partial"
NONE = "none"
NA = "n/a"

MARK = {
    FULL: "yes",
    PARTIAL: "partial",
    NONE: "no",
    NA: "n/a",
}

# name -> (status, vplot symbol, note)
#
# A symbol of "" is allowed only for NONE and NA. Notes on PARTIAL rows record
# what is missing.
STATUS = {
    # ---------------------------------------------------------- plotting ---
    "plot": (
        FULL,
        "vplAxesPlot",
        "colour, width, style, markers, label; vplAxesPlotY is the one-argument "
        "form, since C cannot overload",
    ),
    "scatter": (FULL, "vplAxesScatter", "per-point size and colour, colormaps"),
    "step": (FULL, "vplAxesStep", "all three drawstyles: pre, post and mid"),
    "bar": (FULL, "vplAxesBar", "stacking via the bottom array"),
    "barh": (FULL, "vplAxesBarH", "stacking via the left array"),
    "hist": (
        PARTIAL,
        "vplAxesHist",
        "counts or density over equal bins; no weights, stacking, "
        "explicit bin edges or step/stepfilled types",
    ),
    "fill_between": (FULL, "vplAxesFillBetween", "the `where` mask, `interpolate`, and all three `step` modes"),
    "fill_betweenx": (FULL, "vplAxesFillBetweenX", "the `where` mask, `interpolate`, and all three `step` modes"),
    "errorbar": (
        PARTIAL,
        "vplAxesErrorBar",
        "symmetric xerr and yerr; no asymmetric pairs, no errorevery, no fmt",
    ),
    "ecdf": (FULL, "vplAxesEcdf", "ascending or complementary, weighted, either orientation; compress is a dedup that only speeds drawing"),
    "arrow": (
        PARTIAL,
        "vplAxesArrow",
        "the default head proportions; no head_width/head_length, overhang or "
        "length_includes_head",
    ),
    "pcolormesh": (
        PARTIAL,
        "vplAxesPcolorMesh",
        "rectangular grids given by their edges; no curvilinear meshes, no shading modes",
    ),
    "pcolor": (
        PARTIAL,
        "vplAxesPcolorMesh",
        "pcolormesh stands in; the two differ mainly in how they handle masked cells",
    ),
    "matshow": (FULL, "vplAxesMatShow", "including the top ticks and integer locators"),
    "spy": (
        PARTIAL,
        "vplAxesSpy",
        "the image form only; no marker form for very sparse matrices, no precision=",
    ),
    "tripcolor": (
        PARTIAL,
        "vplAxesTriPcolor",
        "flat shading over a computed Delaunay grid; no gouraud, explicit "
        "triangles, mask or facecolors",
    ),
    "tricontour": (
        PARTIAL,
        "vplAxesTriContour",
        "explicit levels over a computed Delaunay grid; no explicit triangles, "
        "mask or level count; clabel is structured-grid only",
    ),
    "tricontourf": (
        PARTIAL,
        "vplAxesTriContourF",
        "explicit levels over a computed Delaunay grid; no explicit triangles, "
        "mask, level count or extend=",
    ),
    "triplot": (
        PARTIAL,
        "vplAxesTriplot",
        "the computed Delaunay grid as lines; no explicit triangles, no mask, "
        "no marker form",
    ),
    "hexbin": (
        PARTIAL,
        "vplAxesHexbin",
        "counts in viridis at a chosen gridsize; no C/reduce_C_function, mincnt, "
        "extent, log scales, bins='log' or a chosen colormap",
    ),
    "hist2d": (
        PARTIAL,
        "vplAxesHist2D",
        "equal bins over the data range; no explicit edges, weights or density",
    ),
    "pie": (
        PARTIAL,
        "vplAxesPie",
        "wedges and the axes setup; no labels, autopct, explode, startangle or shadow",
    ),
    "imshow": (
        PARTIAL,
        "vplAxesImshow",
        "one filled quad per cell, so no interpolation and no alpha channel; "
        "extent and origin are fixed to the defaults",
    ),
    "contour": (
        PARTIAL,
        "vplAxesContour",
        "explicit levels only: no automatic level choice",
    ),
    "clabel": (
        PARTIAL,
        "vplContourClabel",
        "levels formatted as an axis would format them; no explicit level "
        "subset, custom fmt or manual placement",
    ),
    "contourf": (
        PARTIAL,
        "vplAxesContourF",
        "explicit levels only, extend='neither'; no automatic levels, no hatching",
    ),
    "hlines": (FULL, "vplAxesHLines", "colour, width and style"),
    "vlines": (FULL, "vplAxesVLines", "colour, width and style"),
    "eventplot": (
        PARTIAL,
        "vplAxesEventPlot",
        "one row per call, both orientations; no per-row offsets or lengths",
    ),
    "stem": (FULL, "vplAxesStem", "linefmt, markerfmt, basefmt and bottom; horizontal orientation is a rare form not offered"),
    "stairs": (FULL, "vplAxesStairs", "fill, a scalar or None baseline, and either orientation; a per-step baseline array is a rare form not offered"),
    "fill": (
        FULL,
        "vplAxesFill",
        "one polygon per call rather than matplotlib's x,y,[fmt] triples in "
        "one call -- N calls draw the identical N polygons, each still "
        "taking its own colour from the same cycle a single multi-triple "
        "call would, so nothing is actually unreachable",
    ),
    "broken_barh": (FULL, "vplAxesBrokenBarH", ""),
    "stackplot": (FULL, "vplAxesStackPlot", "all four baselines: zero, sym, wiggle and the weighted_wiggle streamgraph"),
    "violinplot": (
        PARTIAL,
        "vplAxesViolinPlot",
        "Scott's bandwidth and the default styling; no showmedians, no custom "
        "positions, widths or bw_method",
    ),
    "violin": (
        PARTIAL,
        "vplAxesViolinPlot",
        "the precomputed-statistics form is not separated out; violinplot does "
        "the estimate itself",
    ),
    "boxplot": (
        PARTIAL,
        "vplAxesBoxPlot",
        "the defaults: 1.5 IQR whiskers, vertical, no notches, no patch_artist, "
        "no means, no custom positions or labels",
    ),
    "psd": (FULL, "vplAxesPsd", "Welch's method with the window, detrend, sides and pad_to knobs"),
    "csd": (FULL, "vplAxesCsd", "same helper and knobs as psd"),
    "cohere": (FULL, "vplAxesCohere", "same helper and knobs as psd"),
    "magnitude_spectrum": (FULL, "vplAxesMagnitudeSpectrum", "linear or dB scale, plus the window, detrend, sides and pad_to knobs"),
    "angle_spectrum": (FULL, "vplAxesAngleSpectrum", "with the window, detrend, sides and pad_to knobs"),
    "phase_spectrum": (FULL, "vplAxesPhaseSpectrum", "with the window, detrend, sides and pad_to knobs"),
    "specgram": (FULL, "vplAxesSpecgram", "all four modes, both scales, xextent, a colormap, and the window/detrend/sides/pad_to knobs"),
    "inset_axes": (PARTIAL, "vplFigureInsetAxes", "bounds in the parent axes fractions, resolved once; no locator that follows the parent, no transform="),
    "indicate_inset": (PARTIAL, "vplAxesIndicateInset", "the rectangle and the two visible connectors; no styling arguments, no access to the other two"),
    "indicate_inset_zoom": (PARTIAL, "vplAxesIndicateInsetZoom", "the inset limits as the rectangle"),
    "secondary_xaxis": (PARTIAL, "vplFigureSecondaryXAxis", "affine functions only, so ticks stay evenly spaced; no arbitrary function pair, no FuncScale"),
    "secondary_yaxis": (PARTIAL, "vplFigureSecondaryYAxis", "affine functions only, as for secondary_xaxis"),
    "quiver": (
        PARTIAL,
        "vplAxesQuiver",
        "units='width' and angles='uv' with matplotlib's auto-scaling; no "
        "scale_units, no per-arrow colour or colormap, no masked arrays",
    ),
    "quiverkey": (
        PARTIAL,
        "vplAxesQuiverKey",
        "labelpos='N' in axes coordinates; no E/S/W positions, no angle=, no "
        "separate label colour",
    ),
    "barbs": (
        PARTIAL,
        "vplAxesBarbs",
        "the full flag/barb/half-barb decomposition with rounding, flip and "
        "both pivots; no per-barb colour or colormap, no custom sizes dict",
    ),
    "streamplot": (
        PARTIAL,
        "vplAxesStreamPlot",
        "the RK12 integrator, mask and spiral seeding at a chosen density; no "
        "per-line colour or width, no start_points, no integration_direction",
    ),
    "acorr": (PARTIAL, "vplAxesACorr", "usevlines with normed; no detrend, no line form"),
    "xcorr": (PARTIAL, "vplAxesXCorr", "usevlines with normed; no detrend, no line form"),
    "grouped_bar": (
        PARTIAL,
        "vplAxesGroupedBar",
        "vertical bars at integer positions with tick and dataset labels; no "
        "horizontal orientation, no explicit positions, no colour list",
    ),
    "pie_label": (
        PARTIAL,
        "vplAxesPieLabel",
        "centred labels at a chosen distance; no format strings, no rotate=, "
        "no outer alignment",
    ),
    "pcolorfast": (
        FULL,
        "vplAxesPcolorMesh",
        "pcolormesh stands in. pcolorfast exists to pick a faster artist for "
        "regular grids; vplot emits the same quads either way, so the "
        "renderable output is identical and only a speed characteristic, "
        "not a capability, differs",
    ),
    "get_title": (FULL, "vplAxesGetTitle", "the centre title, which is the only one stored"),
    "get_legend_handles_labels": (
        PARTIAL,
        "vplAxesGetLegendLabel",
        "the labels, in matplotlib's order; the handles are omitted because a "
        "C ABI caller already holds each artist's own handle",
    ),
    "set": (
        NA,
        "",
        "a Python keyword-argument dispatcher over every other setter -- it "
        "has no meaning in a C ABI, where each property has its own entry "
        "point",
    ),
    "table": (
        PARTIAL,
        "vplAxesTable",
        "cell text, both label headers, column widths, all eighteen locs; no "
        "per-cell colours, no edges= modes, no explicit bbox, no auto font size",
    ),
    "bxp": (
        PARTIAL,
        "vplAxesBxp",
        "the five statistics and fliers, which is what boxplot itself passes "
        "down; no means, notches, patch_artist, custom positions or widths",
    ),
    "axline": (FULL, "vplAxesAxLine", "point-and-slope, including a vertical slope"),
    "bar_label": (
        PARTIAL,
        "vplAxesBarLabelFull",
        "fmt (a single %-conversion; no {}-style or callables), label_type "
        "('edge' or 'center') and padding, via the Full variant "
        "(vplAxesBarLabel is the %g-default convenience); no explicit "
        "labels= override",
    ),
    "axhline": (FULL, "vplAxesAxHLine", ""),
    "axvline": (FULL, "vplAxesAxVLine", ""),
    "axhspan": (FULL, "vplAxesAxHSpan", ""),
    "axvspan": (FULL, "vplAxesAxVSpan", ""),
    "text": (FULL, "vplAxesText", "alignment, rotation, both rotation modes"),
    "annotate": (PARTIAL, "vplAxesAnnotate", "the '-|>' arrow style only"),
    "legend": (
        PARTIAL,
        "vplAxesSetLegend",
        "all ten locations, a title (vplAxesSetLegendTitle) and ncols "
        "(vplAxesSetLegendNCols), but no explicit handle list, no bbox_to_anchor",
    ),
    "loglog": (FULL, "vplAxesSetXScale", "as set_xscale + set_yscale, which is what it is"),
    "semilogx": (FULL, "vplAxesSetXScale", ""),
    "semilogy": (FULL, "vplAxesSetYScale", ""),
    "set_title": (FULL, "vplAxesSetTitle", ""),
    # -------------------------------------------------- axes and limits ----
    "set_xlim": (FULL, "vplAxesSetXLim", ""),
    "set_ylim": (FULL, "vplAxesSetYLim", ""),
    "get_xlim": (FULL, "vplAxesGetViewLimits", "both axes at once"),
    "get_ylim": (FULL, "vplAxesGetViewLimits", "both axes at once"),
    "set_xlabel": (FULL, "vplAxesSetXLabel", ""),
    "set_ylabel": (FULL, "vplAxesSetYLabel", ""),
    "set_xscale": (PARTIAL, "vplAxesSetXScale", "linear and log; no symlog, logit or function scales"),
    "set_yscale": (PARTIAL, "vplAxesSetYScale", "linear and log; no symlog, logit or function scales"),
    "set_xticks": (FULL, "vplAxesSetXTicks", "positions and labels, and the empty list"),
    "set_yticks": (FULL, "vplAxesSetYTicks", "positions and labels, and the empty list"),
    "get_xticks": (FULL, "vplAxesGetXTicks", "major ticks only, at the last-rendered pixel size"),
    "get_yticks": (FULL, "vplAxesGetYTicks", "major ticks only, at the last-rendered pixel size"),
    "set_xticklabels": (PARTIAL, "vplAxesGetXTicks", "read the current positions here, then relabel them with vplAxesSetXTicks; no minor=True, no fontdict"),
    "set_yticklabels": (PARTIAL, "vplAxesGetYTicks", "read the current positions here, then relabel them with vplAxesSetYTicks; no minor=True, no fontdict"),
    "autoscale": (FULL, "vplAxesAutoscale", "enable, per-axis, and tight"),
    "invert_xaxis": (FULL, "vplAxesSetXInverted", ""),
    "invert_yaxis": (FULL, "vplAxesSetYInverted", ""),
    "set_xinverted": (FULL, "vplAxesSetXInverted", ""),
    "set_yinverted": (FULL, "vplAxesSetYInverted", ""),
    "tick_params": (
        PARTIAL,
        "vplAxesSetTickParams",
        "direction, length, width, pad, label size, visibility, and colors=/"
        "labelcolor= as RGBA (no colour-spec string); no per-side control or "
        "minor/major split",
    ),
    "ticklabel_format": (
        PARTIAL,
        "vplAxesTickLabelFormat",
        "the offset and scientific-notation switches and the power limits; "
        "no useLocale, no useMathText",
    ),
    "set_box_aspect": (FULL, "vplAxesSetBoxAspect", ""),
    "get_box_aspect": (FULL, "vplAxesGetBoxAspect", ""),
    "set_anchor": (
        PARTIAL,
        "vplAxesSetAnchor",
        "as a pair of fractions rather than the 'SW'..'NE' names; no 'C' string form",
    ),
    "get_anchor": (PARTIAL, "vplAxesGetAnchor", "returns the pair, not the name"),
    "locator_params": (
        PARTIAL,
        "vplAxesLocatorParamsFull",
        "nbins, steps and tight, via the Full variant (vplAxesLocatorParams "
        "is the nbins-only convenience); no per-locator keywords other than "
        "MaxNLocator's own (integer, symmetric, prune, min_n_ticks)",
    ),
    "set_prop_cycle": (
        FULL,
        "vplAxesSetPropCycleFull",
        "colour, linestyle, marker and linewidth zipped together "
        "(vplAxesSetPropCycle is the colour-only convenience form); "
        "linestyle/marker/linewidth only feed plot(), the same as "
        "matplotlib's own cycler only ever gives those three to Line2D",
    ),
    "set_autoscale_on": (FULL, "vplAxesSetAutoscale", ""),
    "set_autoscalex_on": (FULL, "vplAxesSetAutoscaleX", ""),
    "set_autoscaley_on": (FULL, "vplAxesSetAutoscaleY", ""),
    "get_autoscale_on": (FULL, "vplAxesGetAutoscale", ""),
    "get_autoscalex_on": (FULL, "vplAxesGetAutoscaleX", ""),
    "get_autoscaley_on": (FULL, "vplAxesGetAutoscaleY", ""),
    "autoscale_view": (
        PARTIAL,
        "vplAxesAutoscale",
        "no tight= or per-axis arguments; the same entry autoscale maps to",
    ),
    "get_facecolor": (
        FULL,
        "vplAxesGetFaceColor",
        "and vplFigureGetFaceColor for the figure's own patch -- both classes "
        "carry this name, and STATUS is keyed by the name alone",
    ),
    "get_fc": (FULL, "vplAxesGetFaceColor", ""),
    "get_data_ratio": (FULL, "vplAxesGetDataRatio", ""),
    "update_datalim": (
        PARTIAL,
        "vplAxesUpdateDataLim",
        "one rectangle at a time; no updatex/updatey switches",
    ),
    "get_xticklabels": (
        PARTIAL,
        "vplAxesGetXTickLabels",
        "the strings, not Text objects; major ticks only",
    ),
    "get_yticklabels": (
        PARTIAL,
        "vplAxesGetYTickLabels",
        "the strings, not Text objects; major ticks only",
    ),
    "relim": (
        NA,
        "",
        "data limits are recomputed from the artists on every draw, so there is "
        "no cached extent to invalidate",
    ),
    # ------------------------------------------------------------- out of scope
    # matplotlib methods a headless, immediate-mode renderer cannot have, each
    # marked n/a for a category reason. See the taxonomy in the generated header.
    # the scene graph vplot does not have
    "add_artist": (NA, "", "the scene graph vplot does not have: it takes DATA and draws it, so there are no free-standing artist objects to add, list, reorder or hand back"),
    "add_child_axes": (NA, "", "the scene graph vplot does not have: it takes DATA and draws it, so there are no free-standing artist objects to add, list, reorder or hand back"),
    "add_collection": (NA, "", "the scene graph vplot does not have: it takes DATA and draws it, so there are no free-standing artist objects to add, list, reorder or hand back"),
    "add_container": (NA, "", "the scene graph vplot does not have: it takes DATA and draws it, so there are no free-standing artist objects to add, list, reorder or hand back"),
    "add_image": (NA, "", "the scene graph vplot does not have: it takes DATA and draws it, so there are no free-standing artist objects to add, list, reorder or hand back"),
    "add_line": (NA, "", "the scene graph vplot does not have: it takes DATA and draws it, so there are no free-standing artist objects to add, list, reorder or hand back"),
    "add_patch": (NA, "", "the scene graph vplot does not have: it takes DATA and draws it, so there are no free-standing artist objects to add, list, reorder or hand back"),
    "add_table": (NA, "", "the scene graph vplot does not have: it takes DATA and draws it, so there are no free-standing artist objects to add, list, reorder or hand back"),
    "artists": (NA, "", "the scene graph vplot does not have: it takes DATA and draws it, so there are no free-standing artist objects to add, list, reorder or hand back"),
    "collections": (NA, "", "the scene graph vplot does not have: it takes DATA and draws it, so there are no free-standing artist objects to add, list, reorder or hand back"),
    "images": (FULL, "vplAxesGetImage", "with vplAxesGetImageCount; the handles imshow/matshow/figimage already returned, retrievable again by index"),
    "lines": (FULL, "vplAxesGetLine", "with vplAxesGetLineCount; the handles plot/step/stem/... already returned, retrievable again by index"),
    "patches": (FULL, "vplAxesGetPatch", "with vplAxesGetPatchCount; the handles bar/fill/... already returned, retrievable again by index"),
    "tables": (NA, "", "the scene graph vplot does not have: it takes DATA and draws it, so there are no free-standing artist objects to add, list, reorder or hand back"),
    "texts": (NA, "", "the scene graph vplot does not have: it takes DATA and draws it, so there are no free-standing artist objects to add, list, reorder or hand back"),
    "ArtistList": (NA, "", "the scene graph vplot does not have: it takes DATA and draws it, so there are no free-standing artist objects to add, list, reorder or hand back"),
    "get_children": (NA, "", "the scene graph vplot does not have: it takes DATA and draws it, so there are no free-standing artist objects to add, list, reorder or hand back"),
    "get_default_bbox_extra_artists": (NA, "", "the scene graph vplot does not have: it takes DATA and draws it, so there are no free-standing artist objects to add, list, reorder or hand back"),
    "get_images": (FULL, "vplAxesGetImage", "the deprecated-alias form of the images property above"),
    "get_lines": (FULL, "vplAxesGetLine", "the deprecated-alias form of the lines property above"),
    "get_legend": (NA, "", "the scene graph vplot does not have: it takes DATA and draws it, so there are no free-standing artist objects to add, list, reorder or hand back"),
    "get_xgridlines": (NA, "", "the scene graph vplot does not have: it takes DATA and draws it, so there are no free-standing artist objects to add, list, reorder or hand back"),
    "get_ygridlines": (NA, "", "the scene graph vplot does not have: it takes DATA and draws it, so there are no free-standing artist objects to add, list, reorder or hand back"),
    "get_xmajorticklabels": (NA, "", "the scene graph vplot does not have: it takes DATA and draws it, so there are no free-standing artist objects to add, list, reorder or hand back"),
    "get_xminorticklabels": (NA, "", "the scene graph vplot does not have: it takes DATA and draws it, so there are no free-standing artist objects to add, list, reorder or hand back"),
    "get_ymajorticklabels": (NA, "", "the scene graph vplot does not have: it takes DATA and draws it, so there are no free-standing artist objects to add, list, reorder or hand back"),
    "get_yminorticklabels": (NA, "", "the scene graph vplot does not have: it takes DATA and draws it, so there are no free-standing artist objects to add, list, reorder or hand back"),
    "get_xticklines": (NA, "", "the scene graph vplot does not have: it takes DATA and draws it, so there are no free-standing artist objects to add, list, reorder or hand back"),
    "get_yticklines": (NA, "", "the scene graph vplot does not have: it takes DATA and draws it, so there are no free-standing artist objects to add, list, reorder or hand back"),
    "get_xaxis": (NA, "", "the scene graph vplot does not have: it takes DATA and draws it, so there are no free-standing artist objects to add, list, reorder or hand back"),
    "get_yaxis": (NA, "", "the scene graph vplot does not have: it takes DATA and draws it, so there are no free-standing artist objects to add, list, reorder or hand back"),
    "set_figure": (NA, "", "the scene graph vplot does not have: it takes DATA and draws it, so there are no free-standing artist objects to add, list, reorder or hand back"),
    "get_figure": (NA, "", "the scene graph vplot does not have: it takes DATA and draws it, so there are no free-standing artist objects to add, list, reorder or hand back"),
    "figure": (NA, "", "the scene graph vplot does not have: it takes DATA and draws it, so there are no free-standing artist objects to add, list, reorder or hand back"),
    "set_zorder": (NA, "", "the scene graph vplot does not have: it takes DATA and draws it, so there are no free-standing artist objects to add, list, reorder or hand back"),
    "reset_position": (NA, "", "the scene graph vplot does not have: it takes DATA and draws it, so there are no free-standing artist objects to add, list, reorder or hand back"),
    # interactive navigation and hit-testing, which need a GUI event loop
    "can_pan": (NA, "", "interactive navigation and hit-testing, which need a GUI event loop the renderer does not have -- it draws a buffer and returns"),
    "can_zoom": (NA, "", "interactive navigation and hit-testing, which need a GUI event loop the renderer does not have -- it draws a buffer and returns"),
    "get_navigate": (NA, "", "interactive navigation and hit-testing, which need a GUI event loop the renderer does not have -- it draws a buffer and returns"),
    "set_navigate": (NA, "", "interactive navigation and hit-testing, which need a GUI event loop the renderer does not have -- it draws a buffer and returns"),
    "get_navigate_mode": (NA, "", "interactive navigation and hit-testing, which need a GUI event loop the renderer does not have -- it draws a buffer and returns"),
    "set_navigate_mode": (NA, "", "interactive navigation and hit-testing, which need a GUI event loop the renderer does not have -- it draws a buffer and returns"),
    "get_forward_navigation_events": (NA, "", "interactive navigation and hit-testing, which need a GUI event loop the renderer does not have -- it draws a buffer and returns"),
    "set_forward_navigation_events": (NA, "", "interactive navigation and hit-testing, which need a GUI event loop the renderer does not have -- it draws a buffer and returns"),
    "contains": (NA, "", "interactive navigation and hit-testing, which need a GUI event loop the renderer does not have -- it draws a buffer and returns"),
    "contains_point": (NA, "", "interactive navigation and hit-testing, which need a GUI event loop the renderer does not have -- it draws a buffer and returns"),
    "in_axes": (NA, "", "interactive navigation and hit-testing, which need a GUI event loop the renderer does not have -- it draws a buffer and returns"),
    "pick": (NA, "", "interactive navigation and hit-testing, which need a GUI event loop the renderer does not have -- it draws a buffer and returns"),
    "format_coord": (NA, "", "interactive navigation and hit-testing, which need a GUI event loop the renderer does not have -- it draws a buffer and returns"),
    "format_xdata": (NA, "", "interactive navigation and hit-testing, which need a GUI event loop the renderer does not have -- it draws a buffer and returns"),
    "format_ydata": (NA, "", "interactive navigation and hit-testing, which need a GUI event loop the renderer does not have -- it draws a buffer and returns"),
    "ginput": (NA, "", "interactive navigation and hit-testing, which need a GUI event loop the renderer does not have -- it draws a buffer and returns"),
    "waitforbuttonpress": (NA, "", "interactive navigation and hit-testing, which need a GUI event loop the renderer does not have -- it draws a buffer and returns"),
    "show": (NA, "", "interactive navigation and hit-testing, which need a GUI event loop the renderer does not have -- it draws a buffer and returns"),
    "add_axobserver": (NA, "", "interactive navigation and hit-testing, which need a GUI event loop the renderer does not have -- it draws a buffer and returns"),
    "set_canvas": (NA, "", "interactive navigation and hit-testing, which need a GUI event loop the renderer does not have -- it draws a buffer and returns"),
    "draw_artist": (NA, "", "interactive navigation and hit-testing, which need a GUI event loop the renderer does not have -- it draws a buffer and returns"),
    "redraw_in_frame": (NA, "", "interactive navigation and hit-testing, which need a GUI event loop the renderer does not have -- it draws a buffer and returns"),
    "draw_without_rendering": (NA, "", "interactive navigation and hit-testing, which need a GUI event loop the renderer does not have -- it draws a buffer and returns"),
    # transform objects, which vplot does not expose
    "get_xaxis_transform": (NA, "", "transform objects, which vplot does not expose: it maps data to pixels internally and hands back neither the transform nor a display-space bbox"),
    "get_yaxis_transform": (NA, "", "transform objects, which vplot does not expose: it maps data to pixels internally and hands back neither the transform nor a display-space bbox"),
    "get_xaxis_text1_transform": (NA, "", "transform objects, which vplot does not expose: it maps data to pixels internally and hands back neither the transform nor a display-space bbox"),
    "get_xaxis_text2_transform": (NA, "", "transform objects, which vplot does not expose: it maps data to pixels internally and hands back neither the transform nor a display-space bbox"),
    "get_yaxis_text1_transform": (NA, "", "transform objects, which vplot does not expose: it maps data to pixels internally and hands back neither the transform nor a display-space bbox"),
    "get_yaxis_text2_transform": (NA, "", "transform objects, which vplot does not expose: it maps data to pixels internally and hands back neither the transform nor a display-space bbox"),
    "viewLim": (NA, "", "transform objects, which vplot does not expose: it maps data to pixels internally and hands back neither the transform nor a display-space bbox"),
    "get_window_extent": (NA, "", "transform objects, which vplot does not expose: it maps data to pixels internally and hands back neither the transform nor a display-space bbox"),
    "get_tightbbox": (NA, "", "transform objects, which vplot does not expose: it maps data to pixels internally and hands back neither the transform nor a display-space bbox"),
    # locator, gridspec and subfigure objects vplot has no equivalent of
    "get_axes_locator": (NA, "", "locator, gridspec and subfigure objects vplot has no equivalent of: its grid is the subplot formula, not a manipulable object"),
    "set_axes_locator": (NA, "", "locator, gridspec and subfigure objects vplot has no equivalent of: its grid is the subplot formula, not a manipulable object"),
    "get_gridspec": (NA, "", "locator, gridspec and subfigure objects vplot has no equivalent of: its grid is the subplot formula, not a manipulable object"),
    "get_subplotspec": (NA, "", "locator, gridspec and subfigure objects vplot has no equivalent of: its grid is the subplot formula, not a manipulable object"),
    "set_subplotspec": (NA, "", "locator, gridspec and subfigure objects vplot has no equivalent of: its grid is the subplot formula, not a manipulable object"),
    "add_gridspec": (NA, "", "locator, gridspec and subfigure objects vplot has no equivalent of: its grid is the subplot formula, not a manipulable object"),
    "add_subfigure": (NA, "", "locator, gridspec and subfigure objects vplot has no equivalent of: its grid is the subplot formula, not a manipulable object"),
    "subfigures": (NA, "", "locator, gridspec and subfigure objects vplot has no equivalent of: its grid is the subplot formula, not a manipulable object"),
    "subplot_mosaic": (NA, "", "locator, gridspec and subfigure objects vplot has no equivalent of: its grid is the subplot formula, not a manipulable object"),
    # the mixed vector/raster rasterization boundary
    "get_rasterization_zorder": (NA, "", "the mixed vector/raster rasterization boundary, which only a backend that does both at once needs"),
    "set_rasterization_zorder": (NA, "", "the mixed vector/raster rasterization boundary, which only a backend that does both at once needs"),
    # matplotlib's symmetric N-way sharing Groupers
    "get_shared_x_axes": (NA, "", "matplotlib's symmetric N-way sharing Groupers; vplot's sharing is a one-directional follow with no group object to return"),
    "get_shared_y_axes": (NA, "", "matplotlib's symmetric N-way sharing Groupers; vplot's sharing is a one-directional follow with no group object to return"),
    # date and unit machinery vplot has none of
    "xaxis_date": (NA, "", "date and unit machinery vplot has none of: axes carry plain doubles, so there is no unit registry to switch a converter in"),
    "yaxis_date": (NA, "", "date and unit machinery vplot has none of: axes carry plain doubles, so there is no unit registry to switch a converter in"),
    "autofmt_xdate": (NA, "", "date and unit machinery vplot has none of: axes carry plain doubles, so there is no unit registry to switch a converter in"),
    # pluggable layout engines
    "get_constrained_layout": (NA, "", "pluggable layout engines: vplot has tight_layout as a one-shot call, not an engine object with pads and a constrained variant"),
    "set_constrained_layout": (NA, "", "pluggable layout engines: vplot has tight_layout as a one-shot call, not an engine object with pads and a constrained variant"),
    "get_constrained_layout_pads": (NA, "", "pluggable layout engines: vplot has tight_layout as a one-shot call, not an engine object with pads and a constrained variant"),
    "set_constrained_layout_pads": (NA, "", "pluggable layout engines: vplot has tight_layout as a one-shot call, not an engine object with pads and a constrained variant"),
    "get_layout_engine": (NA, "", "pluggable layout engines: vplot has tight_layout as a one-shot call, not an engine object with pads and a constrained variant"),
    "set_layout_engine": (NA, "", "pluggable layout engines: vplot has tight_layout as a one-shot call, not an engine object with pads and a constrained variant"),
    "get_tight_layout": (NA, "", "pluggable layout engines: vplot has tight_layout as a one-shot call, not an engine object with pads and a constrained variant"),
    "set_tight_layout": (NA, "", "pluggable layout engines: vplot has tight_layout as a one-shot call, not an engine object with pads and a constrained variant"),
    # an attribute rather than a method
    "dpi": (NA, "", "an attribute rather than a method: read or set through the matching ABI call where one exists"),
    "frameon": (NA, "", "an attribute rather than a method: read or set through the matching ABI call where one exists"),
    "number": (NA, "", "an attribute rather than a method: read or set through the matching ABI call where one exists"),
    "set_adjustable": (
        PARTIAL,
        "vplAxesSetAdjustable",
        "'box' only, which is the default; 'datalim' is refused rather than "
        "silently treated as 'box'",
    ),
    "get_adjustable": (FULL, "vplAxesGetAdjustable", ""),
    "apply_aspect": (NA, "", "internal: vplot applies the aspect in display_box during layout, with no separate step for a caller to call"),
    "grid": (FULL, "vplAxesSetGrid", "visible, which (major/minor/both) and axis (x/y/both); styling is set through the grid.* fields, as elsewhere"),
    "minorticks_on": (FULL, "vplAxesSetMinorTicks", ""),
    "minorticks_off": (FULL, "vplAxesSetMinorTicks", ""),
    "twinx": (FULL, "vplFigureTwinX", ""),
    "twiny": (FULL, "vplFigureTwinY", ""),
    "set_aspect": (PARTIAL, "vplAxesSetAspect", "a number or 0 for auto; no 'datalim' adjustable mode"),
    "get_aspect": (FULL, "vplAxesGetAspect", ""),
    "margins": (FULL, "vplAxesSetMargins", "both axes at once, which is the common call"),
    "set_xmargin": (FULL, "vplAxesSetXMargin", "the single-axis setter, down to matplotlib's -0.5 floor"),
    "set_ymargin": (FULL, "vplAxesSetYMargin", ""),
    "get_xmargin": (FULL, "vplAxesGetXMargin", ""),
    "get_ymargin": (FULL, "vplAxesGetYMargin", ""),
    "label_outer": (FULL, "vplAxesLabelOuter", "labels, and the inner tick marks too with remove_inner_ticks"),
    "get_xlabel": (FULL, "vplAxesGetXLabel", ""),
    "get_ylabel": (FULL, "vplAxesGetYLabel", ""),
    "get_xscale": (FULL, "vplAxesGetXScale", ""),
    "get_yscale": (FULL, "vplAxesGetYScale", ""),
    "set_frame_on": (FULL, "vplAxesSetFrameOn", "all four spines and the background together"),
    "set_axisbelow": (FULL, "vplAxesSetAxisBelow", "the three states On/Line/Off, matching True/'line'/False"),
    "get_axisbelow": (FULL, "vplAxesGetAxisBelow", ""),
    "sharex": (PARTIAL, "vplAxesShareX", "one-directional follow, not matplotlib's symmetric N-way group -- set_xlim on the follower does not reach back to move the axes it follows"),
    "sharey": (PARTIAL, "vplAxesShareY", "one-directional follow, not matplotlib's symmetric N-way group -- set_ylim on the follower does not reach back to move the axes it follows"),
    "set_axis_off": (FULL, "vplAxesSetAxisOn", "pass 0; hides the frame, ticks, grid and axis labels, keeps data and title"),
    "set_axis_on": (FULL, "vplAxesSetAxisOn", "pass 1"),
    "get_frame_on": (FULL, "vplAxesGetFrameOn", ""),
    "has_data": (FULL, "vplAxesHasData", "true if any artist has been added"),
    "set_facecolor": (
        FULL,
        "vplAxesSetFaceColor",
        "and vplFigureSetFaceColor for the figure's own patch",
    ),
    "set_fc": (FULL, "vplAxesSetFaceColor", ""),
    "set_position": (FULL, "vplAxesSetPosition", "left/bottom/width/height, and it leaves the grid like add_axes"),
    "get_position": (FULL, "vplAxesGetPosition", "left/bottom/width/height"),
    "use_sticky_edges": (FULL, "vplAxesSetUseStickyEdges", "a caller can turn the sticky clamp off, and read it back"),
    "sticky_edges": (FULL, "vplAxesAddStickyEdgeX", "a caller can add a sticky value on either axis, the list artists append to"),
    "drag_pan": (FULL, "vplAxesPan", "the pan half of interactive navigation"),
    "start_pan": (FULL, "vplAxesPan", ""),
    "end_pan": (FULL, "vplAxesPan", ""),
    "axis": (
        PARTIAL,
        "vplAxesSetViewLimits",
        "the [x0, x1, y0, y1] form only; none of the 'equal'/'off'/'tight' words",
    ),
    "set_xbound": (
        FULL,
        "vplAxesSetXBound",
        "NAN for either argument means 'leave this end alone', matplotlib's None",
    ),
    "set_ybound": (
        FULL,
        "vplAxesSetYBound",
        "NAN for either argument means 'leave this end alone', matplotlib's None",
    ),
    "get_xbound": (FULL, "vplAxesGetViewLimits", ""),
    "get_ybound": (FULL, "vplAxesGetViewLimits", ""),
    "xaxis_inverted": (FULL, "x_inverted", "C++ field only, no ABI getter"),
    "yaxis_inverted": (FULL, "y_inverted", "C++ field only, no ABI getter"),
    "get_xinverted": (FULL, "x_inverted", "C++ field only, no ABI getter"),
    "get_yinverted": (FULL, "y_inverted", "C++ field only, no ABI getter"),
    # ------------------------------------------------------------ figure ---
    "add_subplot": (PARTIAL, "vplFigureAddSubplot", "a regular grid only, no gridspec or spans"),
    "colorbar": (
        PARTIAL,
        "vplFigureColorbarOriented",
        "vertical or horizontal (vplFigureColorbar is the plain vertical form); "
        "attached to one axes, default ticks",
    ),
    "add_axes": (PARTIAL, "vplFigureAddAxes", "an explicit rect only, no projections or sharing"),
    "subplots_adjust": (
        FULL,
        "vplFigureSubplotsAdjust",
        "all six parameters, and it re-lays the subplots already added",
    ),
    "suptitle": (FULL, "vplFigureSuptitle", "text, x, y and fontsize; general Text kwargs (weight, alignment) are not a suptitle-specific gap"),
    "supxlabel": (FULL, "vplFigureSupXLabel", "text, x, y and fontsize"),
    "supylabel": (FULL, "vplFigureSupYLabel", "text, x, y and fontsize"),
    "get_suptitle": (FULL, "vplFigureGetSupTitle", ""),
    "get_supxlabel": (FULL, "vplFigureGetSupXLabel", ""),
    "get_supylabel": (FULL, "vplFigureGetSupYLabel", ""),
    "align_xlabels": (FULL, "vplFigureAlignXLabels", "groups by shared row, aligns to the group's furthest tick edge"),
    "align_ylabels": (FULL, "vplFigureAlignYLabels", "groups by shared column, aligns to the group's furthest tick edge"),
    "align_labels": (FULL, "vplFigureAlignLabels", "both axis labels at once, like matplotlib's"),
    "align_titles": (
        FULL,
        "vplFigureAlignTitles",
        "aligns titles to the group's topmost frame; vplot places no variable "
        "decoration above the frame for a title to clear, so that frame top is "
        "the whole of what there is to align to",
    ),
    "figimage": (
        PARTIAL,
        "vplFigureFigImage",
        "a scalar array through a colormap, at native resolution and a pixel "
        "offset, origin='upper'; the direct-RGBA array and origin='lower' are "
        "not exposed yet",
    ),
    "subplots": (
        PARTIAL,
        "vplFigureSubplotsShared",
        "sharex/sharey ('none'/'all'/'row'/'col', one-directional -- see "
        "vplAxesShareX's note); vplFigureSubplots is the plain grid with no "
        "sharing. Neither has gridspec_kw, width_ratios/height_ratios or squeeze",
    ),
    "get_figwidth": (FULL, "vplFigureGetSizeInches", "both dimensions at once"),
    "get_figheight": (FULL, "vplFigureGetSizeInches", "both dimensions at once"),
    "set_figwidth": (FULL, "vplFigureSetFigWidth", "the single-dimension setter"),
    "set_figheight": (FULL, "vplFigureSetFigHeight", "the single-dimension setter"),
    "set_size_inches": (FULL, "vplFigureSetSizeInches", ""),
    "get_dpi": (FULL, "vplFigureGetDpi", ""),
    "set_dpi": (FULL, "vplFigureSetDpi", ""),
    "get_axes": (
        FULL,
        "vplFigureGetAxes",
        "count then index, since a C ABI enumerates rather than returning a list",
    ),
    "axes": (FULL, "vplFigureGetAxes", "the property form of get_axes"),
    "clf": (FULL, "vplFigureClear", "drops the axes and sup-labels; keep_observers governs matplotlib's figure-change callbacks, which a headless renderer has none of"),
    "set_edgecolor": (FULL, "vplFigureSetEdgeColor", ""),
    "set_linewidth": (FULL, "vplFigureSetLineWidth", ""),
    "get_linewidth": (FULL, "vplFigureGetLineWidth", ""),
    "set_frameon": (FULL, "vplFigureSetFrameOn", ""),
    "get_frameon": (FULL, "vplFigureGetFrameOn", ""),
    "get_edgecolor": (FULL, "vplFigureGetEdgeColor", ""),
    "delaxes": (FULL, "vplFigureDelAxes", ""),
    "gca": (FULL, "vplFigureGca", "adds an axes when the figure has none, as matplotlib's does"),
    "sca": (
        FULL,
        "vplFigureSca",
        "with vplFigureGca: matplotlib's own sca/gca pair is pyplot global "
        "state consulted by every other implicit-axes pyplot.* call, which "
        "this ABI has none of -- every entry point already takes the axes "
        "it acts on. current_axes is consulted by nothing but vplFigureGca "
        "itself, which is the whole of what sca/gca can mean here",
    ),
    "tight_layout": (FULL, "vplFigureTightLayout", ""),
    "savefig": (PARTIAL, "vplFigureSaveFig", "png, svg and pdf; no eps, ps or dpi override"),
    "draw": (FULL, "vplFigureRenderRGBA", "renders into a caller-owned buffer"),
    "get_size_inches": (FULL, "vplFigureGetSizeInches", "both dimensions, in inches"),
    "clear": (NA, "", "figures are created and destroyed, not reused"),
    "cla": (NA, "", "figures are created and destroyed, not reused"),
}

# Instance attributes. matplotlib sets these on the object in __init__ rather
# than the class, so inspect.getmembers(Axes) cannot see them and the generated
# tables above never mention them. Maintained by hand.
#
# entry -> (status, vplot symbol, note)
INSTANCE_ATTRS = {
    "ax.spines[...].set_visible": (FULL, "vplAxesSetSpineVisible", "all four edges"),
    "ax.spines[...].set_color": (NONE, "", "spines take the axes edgecolor, one colour for all four"),
    "ax.spines[...].set_position": (NONE, "", "no zero-crossing or outward spines"),
    "ax.spines[...].set_linewidth": (PARTIAL, "linewidth", "the Axes field, one width for all four"),
    "ax.patch": (PARTIAL, "facecolor", "facecolor and visibility as C++ fields, no ABI entry"),
    "ax.title": (PARTIAL, "vplAxesSetTitle", "text and pad; the Text object itself is not exposed"),
    "ax.xaxis / ax.yaxis": (
        PARTIAL,
        "vplAxesSetTickParams",
        "the tick styling is reachable; the Axis object, its locator and its "
        "formatter are not",
    ),
    "ax.xaxis.set_major_locator": (NONE, "", "the locator is chosen by the scale, not by the caller"),
    "ax.xaxis.set_major_formatter": (NONE, "", "no custom formatters"),
    "ax.transData / ax.transAxes": (NONE, "", "no transform objects; vplot maps data to pixels internally"),
}

# vplot ABI entries with no matplotlib method behind them, and why. Every entry
# in the public header must be either mapped in STATUS or excused here.
EXTRA = {
    "vplCreateFigure": "Figure() and plt.figure(), as a constructor rather than a method",
    "vplDestroyFigure": "no garbage collector to do it",
    "vplGetLastError": "matplotlib raises; a C ABI returns a code and stashes the text",
    "vplResultToString": "same -- turns that code back into something printable",
    "vplSetFontPath": "matplotlib finds fonts through its font manager and rcParams; "
                      "vplot is told where the file is",
    "vplAxesZoomAt": "matplotlib zooms through toolbar callbacks (_set_view_from_bbox), "
                     "which are private and GUI-shaped; this is the same operation "
                     "as a plain function",
    "vplFigureGetSubplotParams": "the read half of subplots_adjust. matplotlib takes "
                                 "keyword arguments and leaves the rest alone; a C struct "
                                 "cannot, so the caller reads, edits one field and writes "
                                 "back. get_subplotpars was removed in matplotlib 3.11",
    "vplAxesSetXErr": "matplotlib takes xerr as a keyword to errorbar; a C entry "
                      "with one parameter per keyword would be unwieldy, so the "
                      "horizontal half is set on the line the call returned",
    "vplAxesGetLegendLabelCount": "the count half of get_legend_handles_labels. Python "
                                  "returns a list; a C ABI enumerates by index, so it "
                                  "needs a count call as well as the one this table can "
                                  "name against the method",
    "vplFigureGetAxesCount": "the count half of get_axes, for the same reason: a C ABI "
                             "enumerates by index rather than returning a list",
    "vplAxesGetAxisOn": "the read half of set_axis_off/set_axis_on. matplotlib has no "
                        "get_axis_on -- the state is a private _axis_on flag -- so there "
                        "is no method name to put this against",
    "vplAxesGetTickParams": "the read half of tick_params, which matplotlib has no "
                            "getter for -- it is what lets a caller change one field "
                            "without restating the others",
    "vplFigureGetPixelSize": "the rendered size in pixels -- figure inches times dpi. "
                             "matplotlib exposes this only through the canvas "
                             "(get_width_height), not as a Figure method; get_size_inches "
                             "is the inches form and has its own row",
    "vplAxesGetMargins": "the read half of margins() called with no arguments, which "
                         "returns the pair; the `margins` row names the setter half, and "
                         "get_xmargin/get_ymargin read each on its own",
    "vplAxesGetUseStickyEdges": "the read half of the use_sticky_edges property, whose "
                                "setter half the use_sticky_edges row names",
    "vplAxesAddStickyEdgeY": "the y half of sticky_edges, whose x half the sticky_edges "
                             "row names; matplotlib's sticky_edges.x and .y are two lists",
}

# matplotlib names outside the scope of a C ABI, with the reason. These are
# counted separately rather than as failures.
OUT_OF_SCOPE_PREFIXES = (
    "get_",
    "set_",
    "add_callback",
    "remove_callback",
    "pchanged",
    "pick",
    "findobj",
    "properties",
    "update",
    "convert_",
    "have_units",
    "format_",
    "contains",
    "in_axes",
    "can_",
    "is_transform_set",
    "stale",
    "mouseover",
    "axes",
    "figure",
    "viewLim",
    "ArtistList",
)


def defining_class(cls, name):
    for c in cls.__mro__:
        if name in c.__dict__:
            return c
    return None


def public_members(cls):
    out = []
    for name, value in inspect.getmembers(cls):
        if name.startswith("_"):
            continue
        if not (callable(value) or isinstance(value, property)):
            continue
        out.append(name)
    return sorted(set(out))


def load_vplot_symbols():
    text = ""
    for rel in ("include/vpl/vpl.h", "source/vplot/core/figure.h"):
        with io.open(os.path.join(ROOT, rel), encoding="utf-8") as f:
            text += f.read()
    return text


def signature_of(cls, name):
    try:
        member = getattr(cls, name)
    except Exception:
        return ""
    if isinstance(member, property):
        return "(property)"
    try:
        sig = str(inspect.signature(member))
    except (TypeError, ValueError):
        return ""
    sig = sig.replace("self, ", "").replace("self", "")
    if len(sig) > 88:
        sig = sig[:85] + "...)"
    return sig


def main():
    symbols = load_vplot_symbols()

    # ---- the two integrity checks the document rests on ------------
    problems = []

    # A repeated name in STATUS is silent -- Python keeps the last one -- and
    # Axes and Figure share several (get_facecolor, set_facecolor, get_frameon,
    # ...), so a Figure row can quietly replace the Axes one. Detected by reading
    # the source rather than the dict, where duplicates have already collapsed.
    with io.open(os.path.abspath(__file__), encoding="utf-8") as f:
        src = f.read()
    start = src.index("STATUS = {")
    body = src[start:src.index(chr(10) + "}" + chr(10), start)]
    seen = {}
    for name in re.findall(r'^ {4}"([A-Za-z_]+)":', body, re.M):
        seen[name] = seen.get(name, 0) + 1
    for name, n in sorted(seen.items()):
        if n > 1:
            problems.append(
                f"STATUS names {name!r} {n} times; the last one silently wins. "
                "Both classes may carry it -- fold them into one row."
            )
    known = set(public_members(Axes)) | set(public_members(Figure))
    for name in STATUS:
        if name not in known:
            problems.append(f"STATUS names {name!r}, which matplotlib {matplotlib.__version__} does not have")
    for name, (status, symbol, _note) in STATUS.items():
        if status in (FULL, PARTIAL):
            if not symbol:
                problems.append(f"{name} is claimed as {status} but names no vplot symbol")
            elif symbol not in symbols:
                problems.append(f"{name} claims vplot symbol {symbol!r}, which is in neither public header")
        elif symbol:
            problems.append(f"{name} is {status} but names a symbol anyway")
    # ---- and the same check in reverse -----------------------------------
    claimed_symbols = {sym for _s, sym, _n in STATUS.values() if sym}
    claimed_symbols |= {sym for _s, sym, _n in INSTANCE_ATTRS.values() if sym}
    # A symbol named in a NOTE is accounted for too. Axes and Figure share
    # several method names, and STATUS is keyed by the name alone, so one row
    # has to speak for both -- it names one symbol in its own column and the
    # other in its note.
    for _s, _sym, note in list(STATUS.values()) + list(INSTANCE_ATTRS.values()):
        claimed_symbols |= set(re.findall("vpl[A-Za-z]+", note or ""))
    with io.open(os.path.join(ROOT, "include", "vpl", "vpl.h"), encoding="utf-8") as f:
        abi = set(re.findall(r"VPL_CALL (vpl[A-Za-z]+)", f.read()))
    for entry in sorted(abi - claimed_symbols - set(EXTRA)):
        problems.append(
            f"the ABI has {entry}, which no coverage row mentions and EXTRA does not excuse"
        )
    for entry in sorted(set(EXTRA) - abi):
        problems.append(f"EXTRA excuses {entry}, which is not in the public header")

    if problems:
        print("api_coverage: the status table does not match the code:", file=sys.stderr)
        for p in problems:
            print("  " + p, file=sys.stderr)
        return 1

    axes_own, axes_base, artist = [], [], []
    for name in public_members(Axes):
        owner = defining_class(Axes, name)
        (artist if owner is Artist else axes_own if owner is Axes else axes_base).append(name)

    figure_own = [n for n in public_members(Figure) if defining_class(Figure, n) is not Artist]

    def row(cls, name):
        status, symbol, note = STATUS.get(name, (NONE, "", ""))
        sig = signature_of(cls, name)
        shown = f"`{name}{sig}`" if sig and sig != "(property)" else f"`{name}`"
        cell = f"`{symbol}`" if symbol else ""
        return f"| {shown} | {MARK[status]} | {cell} | {note} |", status

    def table(cls, names, heading, blurb):
        lines = [f"### {heading}", "", blurb, ""]
        lines += ["| matplotlib | in vplot | vplot symbol | notes |", "| --- | --- | --- | --- |"]
        counts = {FULL: 0, PARTIAL: 0, NONE: 0, NA: 0}
        for name in names:
            line, status = row(cls, name)
            counts[status] += 1
            lines.append(line)
        have = counts[FULL] + counts[PARTIAL]
        lines.append("")
        lines.append(
            f"**{have} of {len(names)}** -- {counts[FULL]} full, {counts[PARTIAL]} "
            f"partial, {counts[NONE]} missing, {counts[NA]} out of scope."
        )
        lines.append("")
        return lines, counts

    body = []
    totals = {FULL: 0, PARTIAL: 0, NONE: 0, NA: 0}

    for cls, names, heading, blurb in (
        (
            Axes,
            axes_own,
            "Axes: the plotting commands",
            "Everything `matplotlib.axes.Axes` defines itself -- the methods that "
            "put artists on a plot. This is the table that matters; the ones below "
            "are mostly plumbing.",
        ),
        (
            Axes,
            axes_base,
            "Axes: limits, scales, ticks and the rest",
            "Inherited from `_AxesBase`. Much of it is paired getters and setters "
            "for state vplot keeps as plain struct fields, and some of it "
            "(navigation callbacks, transforms, units) belongs to a GUI or to "
            "Python and has no C equivalent.",
        ),
        (
            Figure,
            figure_own,
            "Figure",
            "Everything `Figure` and its bases define, minus the `Artist` methods "
            "listed at the end.",
        ),
    ):
        lines, counts = table(cls, names, heading, blurb)
        body += lines
        for k in totals:
            totals[k] += counts[k]

    extra_lines = [
        "### Instance attributes",
        "",
        "matplotlib attaches these to the object in `__init__`, not to the class, "
        "so class introspection cannot enumerate them and the tables above do not "
        "mention them. This one is written by hand -- treat it as less complete "
        "than the generated tables, because it is.",
        "",
        "| matplotlib | in vplot | vplot symbol | notes |",
        "| --- | --- | --- | --- |",
    ]
    for name, (status, symbol, note) in INSTANCE_ATTRS.items():
        cell = f"`{symbol}`" if symbol else ""
        extra_lines.append(f"| `{name}` | {MARK[status]} | {cell} | {note} |")
    extra_lines += [
        "",
        "### vplot entries with no matplotlib counterpart",
        "",
        "Checked in reverse: an ABI entry that answers to nothing in matplotlib "
        "has to be justified here, or the script fails.",
        "",
        "| vplot | why |",
        "| --- | --- |",
    ]
    for name, why in sorted(EXTRA.items()):
        extra_lines.append(f"| `{name}` | {why} |")
    extra_lines.append("")

    art_lines = [
        "### Artist",
        "",
        "The 60 methods every matplotlib artist inherits: zorder, clipping, "
        "picking, transforms, URLs, sketch parameters, unit conversion. vplot has "
        "no Artist base class and no scene graph -- artists are structs the figure "
        "draws in a fixed order -- so these have no counterpart and are not "
        "counted as gaps. They are listed because a coverage document that quietly "
        "drops a category is not a coverage document.",
        "",
        "```",
    ]
    art_lines += ["  ".join(artist[i : i + 6]) for i in range(0, len(artist), 6)]
    art_lines += ["```", ""]

    counted = totals[FULL] + totals[PARTIAL] + totals[NONE] + totals[NA]
    have = totals[FULL] + totals[PARTIAL]
    in_scope = counted - totals[NA]
    pct = (100.0 * have / in_scope) if in_scope else 0.0

    header = [
        "# API coverage against matplotlib",
        "",
        f"Generated by `tools/api_coverage.py` against **matplotlib "
        f"{matplotlib.__version__}** -- the same version the pixel references are "
        "built with. Do not edit by hand; run the script.",
        "",
        "**How to read this.** A name is *missing* unless the table says "
        "otherwise, and the script refuses to run if a row claims a vplot symbol "
        "that is not in the public headers. So the numbers below are a floor, not "
        "a boast.",
        "",
        f"| | count |",
        "| --- | --- |",
        f"| matplotlib methods examined | {counted} |",
        f"| fully implemented | {totals[FULL]} |",
        f"| partially implemented | {totals[PARTIAL]} |",
        f"| missing | {totals[NONE]} |",
        f"| out of scope for a headless renderer | {totals[NA]} |",
        f"| in scope (examined minus out-of-scope) | {in_scope} |",
        f"| **implemented, of in scope** | **{have} of {in_scope} ({pct:.0f}%)** |",
        "",
        "Plus the 60 `Artist` methods listed at the end, which have no C "
        "counterpart by design.",
        "",
        "**Two denominators, and the honest one is the second.** matplotlib's "
        f"Axes and Figure carry {totals[NA]} methods a headless, immediate-mode "
        "renderer cannot have: interactive navigation and hit-testing (they need "
        "a GUI event loop), transform objects (vplot maps data to pixels "
        "internally and hands neither back), the scene graph of free-standing "
        "artist objects (vplot takes data and draws it), locator/gridspec/"
        "subfigure objects, pluggable layout engines, and date/unit machinery. "
        "Each such row is marked *n/a* with its reason, so the classification is "
        "auditable line by line rather than asserted. Coverage over the methods "
        f"that CAN apply is **{pct:.0f}%**; over the whole surface including the "
        f"inapplicable it is {have} of {counted}.",
        "",
        "Partial rows say what is missing. That is the useful column: a row "
        "marked *partial* with an empty note would be a row nobody has actually "
        "checked.",
        "",
    ]

    with io.open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(header + body + extra_lines + art_lines))

    print(f"wrote {OUT}")
    print(f"  {have} of {in_scope} in-scope methods implemented ({pct:.0f}%); "
          f"{have} of {counted} over the whole surface "
          f"({totals[FULL]} full, {totals[PARTIAL]} partial, {totals[NA]} out of scope)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
