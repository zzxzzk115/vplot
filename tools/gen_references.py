#!/usr/bin/env python3
"""Render vplot's baseline cases with real matplotlib.

This script never ships; it generates the authoritative references the
differential harness compares against. Run it anywhere matplotlib is installed
and commit the output under tests/references/.

    pip install matplotlib==3.11.1
    python tools/gen_references.py

The matplotlib version matters. These references are byte-exact expectations,
and matplotlib changes its output between releases, which moves every RMS in the
harness. The committed set was produced by matplotlib 3.11.1; note it in the
commit if you regenerate with another version, since the numbers stop being
comparable.

Output is raw RGBA rather than PNG so the C++ side needs no image decoder:

    bytes  0..3   width,  little-endian uint32
    bytes  4..7   height, little-endian uint32
    bytes  8..    width * height * 4 bytes, row 0 first, RGBA8888

Every case must be reproducible exactly by tests/test_baseline.cpp. When adding
one, add it on both sides and generate the data from the same closed form so the
two agree bit for bit.
"""

import os
import struct
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402  (must follow matplotlib.use)

OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tests", "references")


def save(fig, name, redraw=True):
    # redraw=False for cases that have already drawn onto the canvas by hand:
    # canvas.draw() would clear the buffer and throw that work away.
    if redraw:
        fig.canvas.draw()
    buf = memoryview(fig.canvas.buffer_rgba())
    width, height = fig.canvas.get_width_height()

    path = os.path.join(OUT_DIR, name + ".rgba")
    with open(path, "wb") as f:
        f.write(struct.pack("<II", width, height))
        f.write(buf.tobytes())
    print(f"wrote {path}  ({width}x{height})")
    plt.close(fig)


def case_simple_line():
    """A bare line plot: frame, ticks, tick labels, one curve."""
    n = 201
    xs = [i * 6.0 / (n - 1) for i in range(n)]
    ys = [__import__("math").sin(x) for x in xs]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.plot(xs, ys)
    save(fig, "simple_line")


def case_labels():
    """Title and both axis labels, exercising text placement and rotation."""
    n = 201
    xs = [i * 6.0 / (n - 1) for i in range(n)]
    ys = [__import__("math").sin(x) for x in xs]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.plot(xs, ys)
    ax.set_title("vplot baseline")
    ax.set_xlabel("t")
    ax.set_ylabel("amplitude")
    save(fig, "labels")


def case_styles_markers():
    """Line styles, markers, grid and legend."""
    import math

    n = 24
    xs = [i * 6.0 / (n - 1) for i in range(n)]
    a = [math.sin(x) for x in xs]
    b = [math.cos(x) * 0.7 for x in xs]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.plot(xs, a, linestyle="-", marker="o", label="sin")
    ax.plot(xs, b, linestyle="--", marker="s", label="cos")
    ax.grid(True)
    ax.legend(loc="upper right")
    save(fig, "styles_markers")


def case_loglog():
    """Decade ticks and log-space margins."""
    import math

    n = 60
    xs = [10.0 ** (i * 4.0 / (n - 1)) for i in range(n)]
    ys = [x * x for x in xs]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.plot(xs, ys)
    ax.set_xscale("log")
    ax.set_yscale("log")
    save(fig, "loglog")


def case_fill_between():
    """A shaded band. The corpus has no simple fill_between anywhere in it."""
    import math

    n = 61
    xs = [i * 6.0 / (n - 1) for i in range(n)]
    lo = [math.sin(x) - 0.3 for x in xs]
    hi = [math.sin(x) + 0.3 for x in xs]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.fill_between(xs, lo, hi)
    save(fig, "fill_between")


def case_errorbar():
    """Error bars, including whether they count towards the autoscale."""
    import math

    n = 12
    xs = [i * 6.0 / (n - 1) for i in range(n)]
    ys = [math.sin(x) for x in xs]
    es = [0.1 + 0.05 * i for i in range(n)]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.errorbar(xs, ys, yerr=es)
    save(fig, "errorbar")


def case_scatter():
    """Uniform markers through the collection path rather than as a line."""
    import math

    n = 40
    xs = [i * 6.0 / (n - 1) for i in range(n)]
    ys = [math.sin(x) for x in xs]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.scatter(xs, ys)
    save(fig, "scatter")


def case_bar():
    """Bars: patch geometry, and the zero-anchored autoscale that goes with it."""
    n = 7
    xs = [float(i) for i in range(n)]
    hs = [1.0 + 0.5 * i for i in range(n)]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.bar(xs, hs)
    save(fig, "bar")


def case_twinx():
    """Two y scales on one x: the second axis on the right, no second x axis."""
    import math

    n = 61
    xs = [i * 6.0 / (n - 1) for i in range(n)]
    a = [math.sin(x) for x in xs]
    b = [100.0 * math.cos(x) for x in xs]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax1 = fig.add_subplot(1, 1, 1)
    ax1.plot(xs, a, color="C0")
    ax2 = ax1.twinx()
    ax2.plot(xs, b, color="C1")
    save(fig, "twinx")


def case_colorbar():
    """A colour scale beside a plot, including the room it takes from it.

    Built from a bare ScalarMappable rather than an image, so that what is
    compared is the colorbar's own geometry and not imshow's resampling.
    """
    import math

    n = 61
    xs = [i * 6.0 / (n - 1) for i in range(n)]
    ys = [math.sin(x) for x in xs]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.plot(xs, ys)

    sm = plt.cm.ScalarMappable(cmap="viridis", norm=plt.Normalize(0.0, 1.0))
    fig.colorbar(sm, ax=ax)
    save(fig, "colorbar")


def case_colorbar_horizontal():
    """orientation='horizontal': the strip runs below the plot, not beside it.

    Same axes and mappable as case_colorbar, so the only thing under test is
    the geometry change -- the wider default pad clearing the x tick labels,
    the aspect ratio inverting (long dimension is now width, not height), and
    the value axis moving from the strip's y to its x.
    """
    import math

    n = 61
    xs = [i * 6.0 / (n - 1) for i in range(n)]
    ys = [math.sin(x) for x in xs]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.plot(xs, ys)

    sm = plt.cm.ScalarMappable(cmap="viridis", norm=plt.Normalize(0.0, 1.0))
    fig.colorbar(sm, ax=ax, orientation="horizontal")
    save(fig, "colorbar_horizontal")


def case_text():
    """Free text at data positions: alignment and rotation, no arrow."""
    import math

    n = 61
    xs = [i * 6.0 / (n - 1) for i in range(n)]
    ys = [math.sin(x) for x in xs]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.plot(xs, ys)
    ax.text(1.0, 0.5, "left baseline")
    ax.text(3.0, 0.0, "centred", ha="center", va="center")
    ax.text(4.5, -0.5, "rotated", rotation=30.0)
    save(fig, "text")


def case_contour():
    """Iso-lines, their colours across the level range, and the tight limits.

    matplotlib autoscales a contour with tight=True, so the axes ends exactly
    at the grid rather than 5% beyond it.
    """
    import math

    rows, cols = 21, 25
    xs = [-3.0 + 6.0 * j / (cols - 1) for j in range(cols)]
    ys = [-2.0 + 4.0 * i / (rows - 1) for i in range(rows)]
    zz = [[math.sin(x) * math.cos(y) for x in xs] for y in ys]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.contour(xs, ys, zz, levels=[-0.6, -0.2, 0.2, 0.6])
    save(fig, "contour")


def case_clabel():
    """Contour labels: which line gets one, where along it, and at what angle.

    The same field and levels as case_contour, so the two differ in exactly one
    thing. Every decision clabel makes is made in pixels -- whether a line is
    long enough to hold the text, which stretch of it runs straightest, whether
    two labels would crowd each other -- so what this case really compares is
    the pixel-space geometry rather than the contouring.
    """
    import math

    rows, cols = 21, 25
    xs = [-3.0 + 6.0 * j / (cols - 1) for j in range(cols)]
    ys = [-2.0 + 4.0 * i / (rows - 1) for i in range(rows)]
    zz = [[math.sin(x) * math.cos(y) for x in xs] for y in ys]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    cs = ax.contour(xs, ys, zz, levels=[-0.6, -0.2, 0.2, 0.6])
    ax.clabel(cs, fontsize=10)
    save(fig, "clabel")


def case_imshow():
    """A scalar field as cells, with the square pixels and inverted y it implies.

    vplot emits one quad per cell where matplotlib resamples into a raster, so
    the cell interiors agree and their shared edges are drawn differently --
    which is what this case is here to keep an eye on.
    """
    import math

    rows, cols = 6, 8
    zz = [[math.sin(0.7 * j) * math.cos(0.5 * i) for j in range(cols)] for i in range(rows)]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.imshow(zz)
    save(fig, "imshow")


def case_tight_layout():
    """A 2x2 grid repacked so the decorations stop colliding."""
    import math

    n = 61
    xs = [i * 6.0 / (n - 1) for i in range(n)]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    for k in range(4):
        ax = fig.add_subplot(2, 2, k + 1)
        ax.plot(xs, [math.sin((k + 1) * x) for x in xs])
        ax.set_title(f"panel {k + 1}")
        ax.set_xlabel("t")
        ax.set_ylabel("value")
    fig.tight_layout()
    save(fig, "tight_layout")


def case_align_ylabels():
    """Two stacked panels whose y numbers are different widths.

    The top panel runs to 100000 and the bottom to about 1, so their y tick
    columns are five digits against three characters wide. Left alone the two
    ylabels sit at different distances from the frame; align_ylabels pulls them
    both out to the wider column's edge, so 'ratio' jumps left to meet
    'amplitude'. That 23-pixel move is the whole point of the case: render it
    without the align call and the bottom label lands in the wrong place.
    """
    xs = [i / 10.0 for i in range(11)]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax1 = fig.add_subplot(2, 1, 1)
    ax1.plot(xs, [x * 80000.0 for x in xs])
    ax1.set_ylabel("amplitude")

    ax2 = fig.add_subplot(2, 1, 2)
    ax2.plot(xs, [x for x in xs])
    ax2.set_ylabel("ratio")

    fig.align_ylabels([ax1, ax2])
    save(fig, "align_ylabels")


def case_ecdf_complementary():
    """ecdf(complementary=True): the survival curve, descending 1 -> 0.

    Where the plain ecdf climbs from 0 to 1 as a right-continuous staircase
    (steps-post), the complementary one is 1 - CDF and falls from 1 to 0 as a
    left-continuous one (steps-pre). Getting the step direction and the endpoints
    right is the whole of it.
    """
    import math

    n = 60
    vals = [math.sin(i * 1.3) + 0.5 * math.cos(i * 0.7) for i in range(n)]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.ecdf(vals, complementary=True)
    save(fig, "ecdf_complementary")


def case_ecdf_weighted():
    """ecdf with per-observation weights: the steps rise by weight, not count."""
    vals = [1.0, 2.0, 3.0, 4.0, 5.0, 2.5, 3.5]
    w = [1.0, 3.0, 1.0, 2.0, 1.0, 4.0, 2.0]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.ecdf(vals, weights=w)
    save(fig, "ecdf_weighted")


def case_ecdf_horizontal():
    """ecdf(orientation='horizontal'): proportion along x, values up y."""
    import math

    n = 40
    vals = [math.sin(i * 1.3) for i in range(n)]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.ecdf(vals, orientation="horizontal")
    save(fig, "ecdf_horizontal")


def case_fill_where():
    """fill_between with a where mask and interpolate=True.

    Shade only where sin > cos, and let interpolate put the edges of each shaded
    band on the actual crossings rather than clipping at the nearest sample.
    Several disjoint bands and a clean crossing at each end are exactly what the
    where/interpolate machinery is for.
    """
    import math

    n = 51
    xs = [i * 2.0 * math.pi / (n - 1) for i in range(n)]
    y1 = [math.sin(x) for x in xs]
    y2 = [math.cos(x) for x in xs]
    where = [a > b for a, b in zip(y1, y2)]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.plot(xs, y1)
    ax.plot(xs, y2)
    ax.fill_between(xs, y1, y2, where=where, interpolate=True)
    save(fig, "fill_where")


def case_stairs_fill():
    """stairs(fill=True) filled down to a raised scalar baseline."""
    values = [1.0, 3.0, 2.0, 4.0, 2.5]
    edges = [0, 1, 2, 3, 4, 5]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.stairs(values, edges, fill=True, baseline=0.5)
    save(fig, "stairs_fill")


def case_stairs_horizontal():
    """stairs(orientation='horizontal'): the steps run up the y axis."""
    values = [1.0, 3.0, 2.0, 4.0, 2.5]
    edges = [0, 1, 2, 3, 4, 5]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.stairs(values, edges, orientation="horizontal", fill=True)
    save(fig, "stairs_horizontal")


def case_label_outer_ticks():
    """label_outer(remove_inner_ticks=True): interior tick marks go too.

    A 2x2 grid where the inner panels drop not just their tick labels but the
    tick marks on the shared edges -- the top row's bottom ticks and the right
    column's left ticks -- which plain label_outer keeps.
    """
    import math

    n = 41
    xs = [i * 6.0 / (n - 1) for i in range(n)]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    for k in range(4):
        ax = fig.add_subplot(2, 2, k + 1)
        ax.plot(xs, [math.sin((k + 1) * x) for x in xs])
        ax.label_outer(remove_inner_ticks=True)
    save(fig, "label_outer_ticks")


def case_stem_styled():
    """stem with a linefmt, a markerfmt and a raised bottom.

    Green dashed stalks, red diamond heads, a black baseline lifted to y=1 --
    every part of the stem plot styled away from its default, which is what the
    format strings and bottom are for.
    """
    import math

    n = 12
    xs = [i * 0.5 for i in range(n)]
    ys = [1.0 + math.sin(x) for x in xs]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.stem(xs, ys, linefmt="g--", markerfmt="rs", basefmt="k-", bottom=1.0)
    save(fig, "stem_styled")


def case_no_sticky_edges():
    """use_sticky_edges=False: the bars lift off the axis and get air below 0."""
    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.bar([0, 1, 2, 3], [1.0, 3.0, 2.0, 4.0])
    ax.use_sticky_edges = False
    ax.autoscale()
    save(fig, "no_sticky_edges")


def case_autoscale_tight():
    """autoscale(tight=True): the view fits the data with no 5% margin."""
    import math

    n = 61
    xs = [i * 6.0 / (n - 1) for i in range(n)]
    ys = [math.sin(x) for x in xs]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.plot(xs, ys)
    ax.autoscale(tight=True)
    save(fig, "autoscale_tight")


def case_grid_minor():
    """grid(which='both'): major and minor gridlines together.

    Turn minor ticks on, then ask for the grid on both the major and minor
    ticks. The minor gridlines are the new thing -- fainter, denser, sitting
    where the minor ticks fall.
    """
    import math

    n = 61
    xs = [i * 6.0 / (n - 1) for i in range(n)]
    ys = [math.sin(x) for x in xs]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.plot(xs, ys)
    ax.minorticks_on()
    ax.grid(which="both")
    save(fig, "grid_minor")


def case_sup_positioned():
    """suptitle / supxlabel / supylabel moved off their defaults and resized.

    The title shifts left and down at 16pt, the figure x label rises at 14pt,
    the figure y label moves in from the edge at 14pt. Getting each anchor and
    size right is the whole of it.
    """
    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    for k in range(2):
        ax = fig.add_subplot(1, 2, k + 1)
        ax.plot([0.0, 1.0], [0.0, 1.0])
    fig.suptitle("moved title", x=0.3, y=0.92, fontsize=16)
    fig.supxlabel("low x label", y=0.04, fontsize=14)
    fig.supylabel("left y label", x=0.06, fontsize=14)
    save(fig, "sup_positioned")


def case_fill_step():
    """fill_between(step='mid'): a filled staircase, not sloped segments.

    A histogram-like band where the value is constant across each bin and the
    fill changes level halfway between the x samples. 'mid' is the fiddliest of
    the three step modes -- the step lands on the midpoint, not the sample -- so
    it is the one worth pinning.
    """
    xs = [0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0]
    lo = [0.2, 0.5, 0.4, 0.9, 0.7, 0.3, 0.6, 0.5]
    hi = [1.0, 1.4, 1.1, 1.8, 1.5, 1.2, 1.6, 1.3]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.fill_between(xs, lo, hi, step="mid")
    save(fig, "fill_step")


def case_figimage():
    """An image on the figure's own pixels -- figimage, with no axes at all.

    A 40x60 block of a smooth ramp, colormapped through viridis and dropped at
    pixel offset (40, 60) on a 200x200 canvas. Each array element is one device
    pixel; nothing is resampled, so the block is a hard rectangle of colour and
    the test is really asking whether vplot puts each pixel where matplotlib
    does -- offset, orientation (row 0 on top) and colormap included.
    """
    rows, cols = 40, 60
    X = [[(r + c) / float(rows + cols) for c in range(cols)] for r in range(rows)]

    fig = plt.figure(figsize=(2.0, 2.0), dpi=100)
    fig.figimage(X, xo=40, yo=60, cmap="viridis")
    save(fig, "figimage")


def case_reference_lines():
    """axhline / axvline: they span the view, and they are NOT black."""
    import math

    n = 61
    xs = [i * 6.0 / (n - 1) for i in range(n)]
    ys = [math.sin(x) for x in xs]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.plot(xs, ys)
    ax.axhline(0.5)
    ax.axvline(2.0)
    save(fig, "reference_lines")


def case_minor_ticks():
    """Unlabelled ticks between the majors, at AutoMinorLocator's spacing."""
    import math

    n = 61
    xs = [i * 6.0 / (n - 1) for i in range(n)]
    ys = [math.sin(x) for x in xs]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.plot(xs, ys)
    ax.minorticks_on()
    save(fig, "minor_ticks")


def case_scatter_varied():
    """Per-point colour and size, which skip the single-path optimization."""
    import math

    n = 24
    xs = [i * 6.0 / (n - 1) for i in range(n)]
    ys = [math.sin(x) for x in xs]
    sizes = [20.0 + 8.0 * i for i in range(n)]
    colors = [["C0", "C1", "C2"][i % 3] for i in range(n)]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.scatter(xs, ys, s=sizes, c=colors)
    save(fig, "scatter_varied")


def case_annotate():
    """A label with a leader line, at matplotlib's default 2 point shrink."""
    import math

    n = 61
    xs = [i * 6.0 / (n - 1) for i in range(n)]
    ys = [math.sin(x) for x in xs]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.plot(xs, ys)
    ax.annotate("peak", xy=(1.5707963267948966, 1.0), xytext=(3.0, 0.6),
                arrowprops=dict(arrowstyle="->"))
    save(fig, "annotate")


def case_hist():
    """A histogram at matplotlib's defaults: ten bins over the data's range."""
    import math

    n = 400
    # A deterministic spread, so both sides get bit-identical input without
    # agreeing on a random number generator.
    vals = [math.sin(i * 1.7) + math.sin(i * 0.31) for i in range(n)]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.hist(vals)
    save(fig, "hist")


def case_fixed_ticks():
    """set_xticks / set_yticks, with and without labels of their own."""
    import math

    n = 61
    xs = [i * 6.0 / (n - 1) for i in range(n)]
    ys = [math.sin(x) for x in xs]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.plot(xs, ys)
    ax.set_xticks([0.0, 1.5707963267948966, 3.141592653589793,
                   4.71238898038469, 6.283185307179586],
                  ["0", "pi/2", "pi", "3pi/2", "2pi"])
    ax.set_yticks([-1.0, 0.0, 1.0])
    save(fig, "fixed_ticks")


def case_spines():
    """The top and right spines turned off, as a paper figure usually has."""
    import math

    n = 61
    xs = [i * 6.0 / (n - 1) for i in range(n)]
    ys = [math.sin(x) for x in xs]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.plot(xs, ys)
    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)
    save(fig, "spines")


def case_multiline():
    """Newlines in a title and in free text, left and centre aligned.

    Lines stack by the lower line ascent plus the upper line descent, each
    padded by half the font typographic line gap, and the whole block is
    anchored on the LAST line baseline.
    """
    import math

    n = 61
    xs = [i * 6.0 / (n - 1) for i in range(n)]
    ys = [math.sin(x) for x in xs]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.plot(xs, ys)
    ax.set_title("two lines\nin a title")
    ax.text(0.5, 0.5, "left\naligned lines")
    ax.text(4.0, -0.4, "centred\nlines here", ha="center")
    save(fig, "multiline")


def case_barh():
    """Bars along x, whose sticky baseline is the x axis rather than the y."""
    n = 7
    ys = [float(i) for i in range(n)]
    ws = [1.0 + 0.5 * i for i in range(n)]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.barh(ys, ws)
    save(fig, "barh")


def case_spans():
    """axhspan and axvspan: bands across the axes in the other direction."""
    import math

    n = 61
    xs = [i * 6.0 / (n - 1) for i in range(n)]
    ys = [math.sin(x) for x in xs]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.plot(xs, ys)
    ax.axhspan(0.25, 0.75)
    ax.axvspan(1.0, 2.0)
    save(fig, "spans")


def case_step():
    """A staircase at the default steps-pre."""
    import math

    n = 15
    xs = [i * 6.0 / (n - 1) for i in range(n)]
    ys = [math.sin(x) for x in xs]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.step(xs, ys)
    save(fig, "step")


def case_fill_betweenx():
    """The band between two curves with x as the varying pair."""
    import math

    n = 61
    ys = [i * 6.0 / (n - 1) for i in range(n)]
    x1 = [math.sin(y) - 0.3 for y in ys]
    x2 = [math.sin(y) + 0.3 for y in ys]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.fill_betweenx(ys, x1, x2)
    save(fig, "fill_betweenx")


def case_twiny():
    """Two x scales against one y: the second axis along the top."""
    import math

    n = 61
    xs = [i * 6.0 / (n - 1) for i in range(n)]
    ys = [math.sin(x) for x in xs]
    xs2 = [100.0 * x for x in xs]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax1 = fig.add_subplot(1, 1, 1)
    ax1.plot(xs, ys, color="C0")
    ax2 = ax1.twiny()
    ax2.plot(xs2, ys, color="C1")
    save(fig, "twiny")


def case_legend_locs():
    """Four of the positions that are not corners, on a 2x2 grid."""
    import math

    n = 61
    xs = [i * 6.0 / (n - 1) for i in range(n)]
    ys = [math.sin(x) for x in xs]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    for k, loc in enumerate(["upper center", "center left", "lower center", "center"]):
        ax = fig.add_subplot(2, 2, k + 1)
        ax.plot(xs, ys, label="sin")
        ax.legend(loc=loc)
    save(fig, "legend_locs")


def case_set_bound():
    """set_xbound / set_ybound: move one end, leave the other autoscaling.

    Left panel: set_xbound(None, 5) -- the upper end clips to 5, the lower
    end is still whatever autoscale put it at. Right panel: set_ybound(-0.5,
    None) -- the mirror, None in the OTHER position. Together they rule out
    "None always means the first/second argument" as an accidental match.
    """
    import math

    n = 61
    xs = [i * 8.0 / (n - 1) for i in range(n)]
    ys = [math.sin(x) for x in xs]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)

    ax1 = fig.add_subplot(1, 2, 1)
    ax1.plot(xs, ys)
    ax1.set_xbound(None, 5.0)

    ax2 = fig.add_subplot(1, 2, 2)
    ax2.plot(xs, ys)
    ax2.set_ybound(-0.5, None)

    save(fig, "set_bound")


def case_legend_ncols():
    """legend(ncols=2): 5 entries split 3/2, not evenly, and unevenly wide.

    np.array_split gives the FIRST columns the extra entry when the count
    does not divide evenly -- 5 into 2 columns is 3 then 2, not 2 then 3.
    Each column is also a different width (one has "a very long label" in
    it, the others do not), which is what actually shows a bug in "shrink
    every column to the legend-wide max" instead of each column's own.
    """
    import math

    n = 41
    xs = [i * 4.0 / (n - 1) for i in range(n)]
    labels = ["sin", "a very long cosine label", "x", "y squared", "z"]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    for k, label in enumerate(labels):
        ys = [math.sin(x + k) for x in xs]
        ax.plot(xs, ys, label=label)
    ax.legend(ncols=2)
    save(fig, "legend_ncols")


def case_legend_title():
    """legend(title=...): a heading row above the entries.

    Left panel's title is shorter than the widest entry label, so the entries
    decide the box width and the title sits centred over them. Right panel's
    title is the WIDER of the two, so the title decides the box width instead
    and the entries centre under it -- the two panels together are what tells
    "always widen to the title" apart from "widen to whichever is wider".
    """
    import math

    n = 61
    xs = [i * 6.0 / (n - 1) for i in range(n)]
    ys = [math.sin(x) for x in xs]
    zs = [math.cos(x) for x in xs]
    # Scaled, not identical to ys/zs: two panels with the exact same y-range
    # give tight_layout's margins on both sides the exact same demand, which
    # can land the shared spine on the sub-ULP tie tight_layout's own comment
    # warns about (see Figure::tight_layout) -- a coin flip between two
    # adjacent pixel columns that has nothing to do with the legend title
    # this case exists to test.
    ys2 = [1.4 * y for y in ys]
    zs2 = [1.4 * z for z in zs]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)

    ax1 = fig.add_subplot(1, 2, 1)
    ax1.plot(xs, ys, label="sin")
    ax1.plot(xs, zs, label="cos")
    ax1.legend(title="Signals")

    ax2 = fig.add_subplot(1, 2, 2)
    ax2.plot(xs, ys2, label="a")
    ax2.plot(xs, zs2, label="b")
    ax2.legend(title="A rather longer legend title")

    fig.tight_layout()
    save(fig, "legend_title")


def case_tick_params():
    """tick_params: direction, length, width and label size, set per axis.

    The two axes are given DIFFERENT settings on purpose. Sharing one number
    between them passes a symmetric case and fails this one.
    """
    import math

    n = 61
    xs = [i * 6.0 / (n - 1) for i in range(n)]
    ys = [math.sin(x) for x in xs]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.plot(xs, ys)
    ax.tick_params(axis="x", direction="in", length=6.0, width=1.6, labelsize=8.0)
    ax.tick_params(axis="y", direction="inout", length=8.0, width=0.8, labelsize=13.0)
    save(fig, "tick_params")


def case_tick_colors():
    """tick_params(colors=, labelcolor=): two independent colours, per axis.

    colors= is the tick MARK colour ({x,y}tick.color); labelcolor= is a
    separate rcParam ({x,y}tick.labelcolor) matplotlib only inherits from
    colors= when it is not given explicitly here. Neither is the spine's own
    edgecolor, which stays black -- three independent colours in one panel
    is what tells a shared "recolour everything" bug from the real, narrower
    effect.
    """
    import math

    n = 61
    xs = [i * 6.0 / (n - 1) for i in range(n)]
    ys = [math.sin(x) for x in xs]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.plot(xs, ys)
    ax.tick_params(axis="x", colors="tab:red", labelcolor="tab:green")
    ax.tick_params(axis="y", colors="tab:blue")
    save(fig, "tick_colors")


def case_bar_stacked():
    """Two series stacked: the second rises from the first rather than from 0."""
    n = 7
    xs = [float(i) for i in range(n)]
    lower = [1.0 + 0.5 * i for i in range(n)]
    upper = [2.0 - 0.1 * i for i in range(n)]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.bar(xs, lower, width=0.8)
    ax.bar(xs, upper, width=0.8, bottom=lower)
    save(fig, "bar_stacked")


def case_bar_label_full():
    """bar_label(fmt=, label_type=, padding=): both together, one test.

    Top: fmt='%.1f' and padding=6 -- both the custom format string and the
    outward shift on an ordinary edge label, so a bug in either shows up
    without the other masking it. Bottom: a stacked bar labelled at each
    SEGMENT's own centre rather than at its tip -- the value shown is the
    segment's own height, not its cumulative position, which is the whole
    point of a stacked bar's labels.
    """
    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)

    ax = fig.add_subplot(2, 1, 1)
    bars = ax.bar([0.0, 1.0, 2.0, 3.0], [2.0, 3.5, 1.0, 2.75], width=0.7)
    ax.bar_label(bars, fmt="%.1f", padding=6)

    ax = fig.add_subplot(2, 1, 2)
    xs = [0.0, 1.0, 2.0, 3.0]
    lower = [1.0, 1.5, 0.8, 1.2]
    upper = [1.6, 1.1, 1.4, 0.9]
    b1 = ax.bar(xs, lower, width=0.7)
    b2 = ax.bar(xs, upper, width=0.7, bottom=lower)
    ax.bar_label(b1, label_type="center")
    ax.bar_label(b2, label_type="center")
    save(fig, "bar_label_full")


def case_invert_axes():
    """The four ways an axis ends up running backwards, or stops.

    Panel 4 is the one worth having: set_ylim CLEARS an inversion, because
    matplotlib reads the argument order as the orientation. A library that
    treats invert_yaxis as a flag and set_ylim as two numbers gets the first
    three panels right and this one backwards.
    """
    import math

    n = 61
    xs = [i * 6.0 / (n - 1) for i in range(n)]
    ys = [math.sin(x) for x in xs]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)

    ax = fig.add_subplot(2, 2, 1)  # inverted, autoscaled
    ax.plot(xs, ys)
    ax.invert_yaxis()

    ax = fig.add_subplot(2, 2, 2)  # inverted by the order of set_ylim
    ax.plot(xs, ys)
    ax.set_ylim(1.5, -1.5)

    ax = fig.add_subplot(2, 2, 3)  # the x axis this time
    ax.plot(xs, ys)
    ax.invert_xaxis()

    ax = fig.add_subplot(2, 2, 4)  # inverted, then set_ylim puts it back
    ax.plot(xs, ys)
    ax.invert_yaxis()
    ax.set_ylim(-1.5, 1.5)

    save(fig, "invert_axes")


def case_hlines_vlines():
    """Rules at many places at once. Note the colour: hlines takes lines.color,
    a fixed C0, and does NOT advance the property cycle -- so the second call
    comes out blue as well."""
    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.hlines([1.0, 2.0, 3.0], [0.0, 0.5, 1.0], [3.0, 2.5, 2.0])
    ax.vlines([0.5, 1.5, 2.5], [0.5, 0.0, 1.0], [3.5, 3.0, 4.0])
    save(fig, "hlines_vlines")


def case_eventplot():
    """A rug of event marks, both orientations."""
    a = [0.2, 0.5, 0.9, 1.4, 2.2, 2.25, 3.1]
    b = [0.1, 0.7, 1.1, 1.15, 2.6, 3.4]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(2, 1, 1)
    ax.eventplot(a)
    ax = fig.add_subplot(2, 1, 2)
    ax.eventplot(b, orientation="vertical")
    save(fig, "eventplot")


def case_stem():
    """Three artists in one call: stalks, heads, and a C3 baseline."""
    import math

    n = 13
    xs = [i * 0.5 for i in range(n)]
    ys = [math.sin(x) * math.exp(-x / 4.0) for x in xs]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.stem(xs, ys)
    save(fig, "stem")


def case_stairs():
    """stairs is an OUTLINE, not a filled step: StepPatch takes fill=False."""
    values = [1.0, 2.5, 2.0, 3.5, 3.0, 1.5]
    edges = [0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.stairs(values, edges)
    save(fig, "stairs")


def case_fill_poly():
    """An arbitrary polygon, placed AWAY FROM THE ORIGIN on purpose.

    A closed path carries a CLOSEPOLY vertex that is a placeholder of (0, 0). A
    renderer that counts it into the data limits stretches the axes back to the
    origin, and a polygon straddling zero -- which is what a bar chart is --
    never shows it.
    """
    import math

    n = 24
    xs = [10.0 + math.cos(2.0 * math.pi * i / n) for i in range(n)]
    ys = [20.0 + 0.5 * math.sin(2.0 * math.pi * i / n) for i in range(n)]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.fill(xs, ys)
    save(fig, "fill_poly")


def case_broken_barh():
    """A timeline of intervals. Fixed at C0 -- it does not use the cycle."""
    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.broken_barh([(0.0, 1.5), (2.0, 0.75), (3.5, 2.0)], (10.0, 4.0))
    ax.broken_barh([(0.5, 1.0), (3.0, 2.5)], (16.0, 4.0))
    save(fig, "broken_barh")


def case_stackplot():
    """Series stacked on the running total below them, each its own colour."""
    import math

    n = 41
    xs = [i * 6.0 / (n - 1) for i in range(n)]
    a = [1.0 + 0.5 * math.sin(x) for x in xs]
    b = [1.5 + 0.5 * math.cos(x) for x in xs]
    c = [0.8 + 0.3 * math.sin(2.0 * x) for x in xs]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.stackplot(xs, a, b, c)
    save(fig, "stackplot")


def _stackplot_series(n):
    import math

    xs = [i * 6.0 / (n - 1) for i in range(n)]
    a = [1.0 + 0.5 * math.sin(x) for x in xs]
    b = [1.5 + 0.5 * math.cos(x) for x in xs]
    c = [0.8 + 0.3 * math.sin(2.0 * x) for x in xs]
    return xs, a, b, c


def case_stackplot_sym():
    """stackplot(baseline='sym'): the stack centred on zero."""
    xs, a, b, c = _stackplot_series(41)
    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.stackplot(xs, a, b, c, baseline="sym")
    save(fig, "stackplot_sym")


def case_stackplot_stream():
    """stackplot(baseline='weighted_wiggle'): a streamgraph."""
    xs, a, b, c = _stackplot_series(41)
    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.stackplot(xs, a, b, c, baseline="weighted_wiggle")
    save(fig, "stackplot_stream")


def case_boxplot():
    """Quartiles, whiskers at the furthest datum inside 1.5 IQR, and fliers.

    Group 1 has an outlier so a flier is drawn; group 3 has none, so its
    whiskers reach the extremes and no marker appears. Every number here --
    box width, cap width, the quartile interpolation -- is a chance to be
    almost right.
    """
    a = [1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 20.0]
    b = [2.0, 3.0, 4.0, 5.0, 6.0]
    c = [0.0, 1.0, 2.0, 3.0, 10.0]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.boxplot([a, b, c])
    save(fig, "boxplot")


def case_bxp():
    """The same boxes from statistics rather than from data.

    Deliberately NOT the summary of any sample set: the whiskers sit where
    they are told to rather than on a datum, and one group carries fliers on
    both sides while another carries none. That is the whole point of bxp --
    nothing is recomputed, so nothing can quietly correct a number the caller
    meant.
    """
    stats = [
        dict(med=5.0, q1=3.0, q3=7.0, whislo=1.0, whishi=9.0, fliers=[-1.0, 11.5]),
        dict(med=4.25, q1=3.5, q3=6.0, whislo=2.75, whishi=8.0, fliers=[]),
        dict(med=2.0, q1=1.0, q3=3.0, whislo=0.5, whishi=10.0, fliers=[12.0]),
    ]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.bxp(stats)
    save(fig, "bxp")


def case_quiver():
    """A field of arrows, with no scale given so the auto-scaling runs.

    That is the part worth pinning. An arrow's width is a fraction of the axes
    width and its length is its magnitude over a scale derived from the MEAN
    magnitude, so both ends depend on the axes size on screen -- and the two
    crude constants matplotlib uses are not the same clamp on sqrt(N).
    """
    import math

    g = 7
    xs, ys, us, vs = [], [], [], []
    for i in range(g):
        for j in range(g):
            x = -2.0 + 4.0 * j / (g - 1)
            y = -2.0 + 4.0 * i / (g - 1)
            xs.append(x)
            ys.append(y)
            us.append(-y * 0.5)
            vs.append(x * 0.5 + 0.2 * math.sin(x))

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.quiver(xs, ys, us, vs)
    save(fig, "quiver")


# The spectral cases all share one signal: two tones plus a deterministic
# "noise" term. It has to be deterministic and closed-form, because the C++
# side computes the same samples from the same expression -- a PRNG would have
# to agree bit for bit across two languages, which is not worth arranging.
def _spectral_signal(n=1024, fs=100.0):
    import math

    return [
        math.sin(2.0 * math.pi * 12.0 * i / fs)
        + 0.5 * math.sin(2.0 * math.pi * 31.0 * i / fs)
        + 0.2 * math.sin(2.0 * math.pi * 7.3 * i / fs + 1.1)
        for i in range(n)
    ]


def _spectral_signal_b(n=1024, fs=100.0):
    import math

    return [
        math.sin(2.0 * math.pi * 12.0 * i / fs + 0.7)
        + 0.4 * math.sin(2.0 * math.pi * 22.0 * i / fs)
        for i in range(n)
    ]


def case_grid_over_image():
    """Grid drawn on top of an image, not swallowed by it.

    AxesImage.zorder is 0, below even axisbelow=True's 0.5, so the grid must
    come out on top of the image whatever axisbelow says. It is a separate
    case from axisbelow's bar-plus-line one because an opaque image, unlike a
    bar, would hide a grid drawn underneath it completely rather than just
    losing a segment -- the two failure modes are not the same shape.
    """
    rows, cols = 6, 8
    z = [[float(r * cols + c) for c in range(cols)] for r in range(rows)]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.imshow(z)
    ax.grid(True)
    save(fig, "grid_over_image")


def case_subplots_shared():
    """subplots(sharex='col', sharey='row') on a 2x2 grid.

    Column 0's two panels are forced to agree on x; column 1's two panels
    agree on a DIFFERENT x. Row 0's two panels are forced to agree on y; row
    1's on a different y.

    matplotlib's sharing is a symmetric group: the shown range is the UNION
    of every member's own data, so it can move on the row-0/col-0 axes too if
    a follower's data is bigger. vplot's own sharing is one-directional (see
    vplAxesShareX's note) and only ever shows that leading axes' own range.
    The two agree exactly when the leader's own range already contains every
    follower's -- which is why the leader in each group below carries the
    largest data, not an arbitrary choice.
    """
    import math

    x_wide = [i for i in range(11)]
    y_00 = [math.sin(x * 0.5) for x in x_wide]

    x_01 = [i * 0.5 for i in range(7)]  # 0..3, column 1's leader
    y_01 = [0.05 * x * x for x in x_01]  # small: dominated by a00's y

    y_10 = [5.0 * x for x in x_wide]

    x_11 = [i * 0.3 for i in range(7)]  # 0..1.8, smaller than x_01
    y_11 = [x * x * x for x in x_11]  # small: dominated by a10's y

    fig, axs = plt.subplots(2, 2, sharex="col", sharey="row", figsize=(6.4, 4.8), dpi=100)
    axs[0, 0].plot(x_wide, y_00)
    axs[0, 1].plot(x_01, y_01)
    axs[1, 0].plot(x_wide, y_10)
    axs[1, 1].plot(x_11, y_11)

    fig.tight_layout()
    save(fig, "subplots_shared")


def case_sharex():
    """Two panels forced to agree on x range, even though their data does not.

    Top panel's data spans 0..20; bottom panel's spans only 0..5. Left alone,
    autoscale would give each its own x range. ax2.sharex(ax1) locks the
    bottom panel's x range to the top one's, so the bottom curve visibly
    squeezes into the left quarter of its panel instead of filling it.
    """
    import math

    x_top = [float(i) for i in range(21)]
    y_top = [math.sin(x * 0.5) for x in x_top]

    x_bottom = [i * 0.5 for i in range(11)]
    y_bottom = [x * x for x in x_bottom]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)

    ax1 = fig.add_subplot(2, 1, 1)
    ax1.plot(x_top, y_top)
    ax1.label_outer()

    ax2 = fig.add_subplot(2, 1, 2)
    ax2.plot(x_bottom, y_bottom)
    ax2.sharex(ax1)
    ax2.label_outer()

    fig.tight_layout()
    save(fig, "sharex")


def case_axisbelow():
    """The grid crosses bars in one panel and hides under them in the other.

    Left: the default 'line' -- grid above patches/bars, below the data line.
    Right: True -- grid below EVERYTHING, invisible wherever a bar covers it.

    This also exercises a real ordering bug that no other case caught: an
    image is zorder 0, below even axisbelow=True's 0.5, so the grid must
    never be drawn before the image is -- an opaque image would hide it
    completely otherwise.
    """
    n = 7
    xs = [float(i) for i in range(n)]
    hs = [1.0 + 0.5 * i for i in range(n)]
    line_ys = [0.5 + 0.4 * i for i in range(n)]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)

    ax1 = fig.add_subplot(1, 2, 1)
    ax1.bar(xs, hs)
    ax1.plot(xs, line_ys, color="C1", linewidth=2)
    ax1.grid(True)
    ax1.set_axisbelow("line")
    ax1.set_title("line (default)")

    ax2 = fig.add_subplot(1, 2, 2)
    ax2.bar(xs, hs)
    ax2.plot(xs, line_ys, color="C1", linewidth=2)
    ax2.grid(True)
    ax2.set_axisbelow(True)
    ax2.set_title("True")

    fig.tight_layout()
    save(fig, "axisbelow")


def case_axis_off():
    """A heatmap with all the furniture stripped -- the commonest use of
    set_axis_off, and the one that shows it keeps the DATA and the background
    while dropping the frame, ticks and labels."""
    import math

    rows, cols = 12, 16
    z = [[math.sin(0.6 * c) * math.cos(0.5 * r) for c in range(cols)]
         for r in range(rows)]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.imshow(z)
    ax.set_xlabel("stripped")   # set, then hidden by axis off
    ax.set_title("but the title stays")   # a child Text, not part of the axis
    ax.set_axis_off()
    save(fig, "axis_off")


def case_suplabels():
    """One title and one x/y label for the whole figure, over a shared grid.

    label_outer strips the per-panel labels; supxlabel and supylabel say the
    thing once. Each is anchored by an edge -- the x label's bottom on the
    figure's y=0.01, the y label's left on x=0.02 -- so it grows inward.
    """
    import math

    n = 61
    xs = [i * 6.0 / (n - 1) for i in range(n)]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    for k in range(4):
        ax = fig.add_subplot(2, 2, k + 1)
        ax.plot(xs, [math.sin((k + 1) * x) for x in xs])
        ax.label_outer()
    fig.suptitle("shared grid")
    fig.supxlabel("time")
    fig.supylabel("amplitude")
    save(fig, "suplabels")


def case_label_outer():
    """A wall of panels with labels only on the outer edges.

    Note the tick MARKS survive on the inner panels -- matplotlib removes only
    the labels unless asked otherwise, and the marks are what still tie an
    inner panel to the scale it shares.
    """
    import math

    n = 61
    xs = [i * 6.0 / (n - 1) for i in range(n)]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    for k in range(4):
        ax = fig.add_subplot(2, 2, k + 1)
        ax.plot(xs, [math.sin((k + 1) * x) for x in xs])
        ax.set_xlabel("t")
        ax.set_ylabel("value")
        ax.label_outer()
    save(fig, "label_outer")


def case_margins():
    """The autoscale padding, which is 5% of the data range by default.

    Set to zero on x and a fat 0.3 on y, so both directions can be seen to
    have moved and in opposite ways.
    """
    import math

    n = 61
    xs = [i * 6.0 / (n - 1) for i in range(n)]
    ys = [math.sin(x) for x in xs]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.plot(xs, ys)
    ax.margins(x=0.0, y=0.3)
    save(fig, "margins")


def case_inset_axes():
    """An axes inside another's box, positioned in the PARENT's fractions."""
    import math

    n = 201
    xs = [i * 6.0 / (n - 1) for i in range(n)]
    ys = [math.sin(x) for x in xs]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.plot(xs, ys)

    ins = ax.inset_axes([0.6, 0.6, 0.35, 0.3])
    ins.plot(xs, ys)
    ins.set_xlim(1.2, 2.0)
    ins.set_ylim(0.85, 1.02)
    save(fig, "inset_axes")


def case_indicate_inset_zoom():
    """The magnifier: a rectangle over what the inset shows, and the TWO of
    four corner lines that do not cross each other.

    Which two is decided by comparing the rectangle's box with the inset's edge
    by edge -- four boolean tests, not a case per relative position.
    """
    import math

    n = 201
    xs = [i * 6.0 / (n - 1) for i in range(n)]
    ys = [math.sin(x) for x in xs]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.plot(xs, ys)

    ins = ax.inset_axes([0.55, 0.08, 0.4, 0.35])
    ins.plot(xs, ys)
    ins.set_xlim(1.2, 2.0)
    ins.set_ylim(0.85, 1.02)
    ax.indicate_inset_zoom(ins)
    save(fig, "indicate_inset_zoom")


def case_secondary_xaxis():
    """A second x scale that is an affine function of the first."""
    import math

    n = 101
    xs = [i * 100.0 / (n - 1) for i in range(n)]
    ys = [math.sin(x * 0.1) for x in xs]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.plot(xs, ys)
    ax.set_xlabel("samples")
    # samples at 20 Hz -> seconds
    ax.secondary_xaxis("top", functions=(lambda s: s / 20.0, lambda t: t * 20.0))
    save(fig, "secondary_xaxis")


def case_secondary_yaxis():
    """The same on the other axis: Celsius against Fahrenheit."""
    import math

    n = 101
    xs = [i * 10.0 / (n - 1) for i in range(n)]
    ys = [10.0 + 15.0 * math.sin(x) for x in xs]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.plot(xs, ys)
    ax.set_ylabel("degrees C")
    ax.secondary_yaxis("right", functions=(lambda c: c * 1.8 + 32.0,
                                           lambda f: (f - 32.0) / 1.8))
    save(fig, "secondary_yaxis")


def case_psd():
    """Welch's method: segment, window, transform, average.

    The scaling is the part worth pinning. A one-sided density doubles every
    bin except DC and Nyquist, then divides by Fs and by sum(window^2) -- the
    windowing-loss correction. Get any one of those wrong and the curve has
    the right shape at the wrong height.
    """
    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.psd(_spectral_signal(), NFFT=256, Fs=100.0)
    save(fig, "psd")


def case_psd_detrend_window():
    """psd with a linear detrend per segment and a Hamming window."""
    import numpy as np

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.psd(_spectral_signal(), NFFT=256, Fs=100.0, detrend="linear",
           window=np.hamming(256))
    save(fig, "psd_detrend_window")


def case_psd_twosided():
    """psd(sides='twosided'): the negative frequencies shown too."""
    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.psd(_spectral_signal(), NFFT=256, Fs=100.0, sides="twosided")
    save(fig, "psd_twosided")


def case_csd():
    """The cross-spectrum of two signals sharing one tone."""
    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.csd(_spectral_signal(), _spectral_signal_b(), NFFT=256, Fs=100.0)
    save(fig, "csd")


def case_cohere():
    """Coherence, which is three passes of the same helper combined."""
    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.cohere(_spectral_signal(), _spectral_signal_b(), NFFT=256, Fs=100.0)
    save(fig, "cohere")


def case_magnitude_spectrum():
    """One segment over the whole signal, and NO scaling by frequency."""
    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.magnitude_spectrum(_spectral_signal(256), Fs=100.0)
    save(fig, "magnitude_spectrum")


def case_magnitude_spectrum_db():
    """The same spectrum in decibels -- magnitude_spectrum(scale='dB')."""
    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.magnitude_spectrum(_spectral_signal(256), Fs=100.0, scale="dB")
    save(fig, "magnitude_spectrum_db")


def case_angle_spectrum():
    """The phase, wrapped -- so it sawtooths between -pi and pi."""
    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.angle_spectrum(_spectral_signal(256), Fs=100.0)
    save(fig, "angle_spectrum")


def case_phase_spectrum():
    """The same thing unwrapped, which is the only difference between them."""
    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.phase_spectrum(_spectral_signal(256), Fs=100.0)
    save(fig, "phase_spectrum")


def case_specgram():
    """Power against time and frequency, as an image.

    The array is flipped vertically before imshow sees it, so the lowest
    frequency lands at the bottom. Note also the half-segment of padding at
    each end of the time axis: a segment's value belongs to its whole span,
    not to its centre.
    """
    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.specgram(_spectral_signal(), NFFT=256, Fs=100.0, noverlap=128)
    save(fig, "specgram")


def case_specgram_magnitude():
    """specgram(mode='magnitude', scale='dB'): an amplitude spectrogram."""
    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.specgram(_spectral_signal(), NFFT=256, Fs=100.0, noverlap=128, mode="magnitude",
                scale="dB")
    save(fig, "specgram_magnitude")


def case_streamplot():
    """Streamlines through a vector field.

    The whole picture is decided by three things agreeing exactly: the spiral
    order the mask cells are seeded in, the adaptive RK12 step, and the rule
    that a line stops when it enters a cell another line already claimed. Get
    any of them wrong and the lines are individually plausible and collectively
    different.
    """
    # streamplot checks u.shape against the grid, so these have to be arrays
    # rather than the nested lists the rest of this file uses.
    import numpy as np

    g = 25
    xs = np.array([-3.0 + 6.0 * i / (g - 1) for i in range(g)])
    ys = np.array([-3.0 + 6.0 * i / (g - 1) for i in range(g)])
    us = np.array([[-yy for xx in xs] for yy in ys])
    vs = np.array([[xx for xx in xs] for yy in ys])

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.streamplot(xs, ys, us, vs)
    save(fig, "streamplot")


def case_pie_label():
    """Labels at each wedge's middle angle, inside the pie.

    distance=0.6 is matplotlib's default and keeps the labels within the
    wedges, which is why they are centred both ways -- 'auto' alignment only
    switches to outer past a distance of 1.
    """
    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    c = ax.pie([35.0, 25.0, 20.0, 12.0, 8.0])
    ax.pie_label(c, ["alpha", "beta", "gamma", "delta", "eps"])
    save(fig, "pie_label")


def case_barbs():
    """Wind barbs, chosen so every feature appears at least once.

    The magnitudes run 0 to 65 so the field covers a calm circle, a lone half
    barb (which is offset down the staff so it cannot be mistaken for a full
    one), full barbs, and a flag. The rounding to the nearest half barb is part
    of the convention, not an approximation.
    """
    import math

    xs, ys, us, vs = [], [], [], []
    for i, mag in enumerate([0.0, 4.0, 7.0, 12.0, 23.0, 27.0, 45.0, 55.0, 65.0]):
        ang = math.radians(20.0 * i)
        xs.append(float(i % 3))
        ys.append(float(i // 3))
        us.append(mag * math.cos(ang))
        vs.append(mag * math.sin(ang))

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.barbs(xs, ys, us, vs)
    save(fig, "barbs")


def case_grouped_bar():
    """Bars side by side per group, with the width DERIVED from the spacing.

    Three datasets over four groups. Nothing here says how wide a bar is: it
    falls out of fitting three bars plus their gaps plus a group gap of one and
    a half bar widths into a group distance of one.
    """
    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.grouped_bar(
        [[3.0, 5.0, 2.0, 4.0], [4.5, 2.5, 3.5, 1.5], [1.0, 3.0, 4.0, 2.5]],
        tick_labels=["north", "south", "east", "west"],
    )
    save(fig, "grouped_bar")


def case_acorr():
    """Autocorrelation as stalks, with the rule along zero.

    Normed, so the zero lag is exactly 1 -- which is the cheapest possible
    check that the normalisation is the one matplotlib uses.
    """
    import math

    n = 60
    xs = [math.sin(i * 0.3) + 0.4 * math.cos(i * 0.11) for i in range(n)]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.acorr(xs, maxlags=15)
    save(fig, "acorr")


def case_xcorr():
    """Two different series, so the peak is off zero and can be seen to be."""
    import math

    n = 60
    a = [math.sin(i * 0.3) for i in range(n)]
    b = [math.sin((i - 4) * 0.3) for i in range(n)]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.xcorr(a, b, maxlags=15)
    save(fig, "xcorr")


def case_quiverkey():
    """The reference arrow, which is only meaningful if it is scaled exactly
    like the field it labels.

    Its scale is the field's RESOLVED one -- with no explicit scale, the field
    does not know its own until it has been laid out, so the key cannot be
    built before the field. It also pivots on its middle rather than its tail,
    which is what labelpos='N' selects.
    """
    import math

    g = 7
    xs, ys, us, vs = [], [], [], []
    for i in range(g):
        for j in range(g):
            x = -2.0 + 4.0 * j / (g - 1)
            y = -2.0 + 4.0 * i / (g - 1)
            xs.append(x)
            ys.append(y)
            us.append(-y * 0.5)
            vs.append(x * 0.5 + 0.2 * math.sin(x))

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    q = ax.quiver(xs, ys, us, vs)
    ax.quiverkey(q, 0.8, 0.9, 1.0, "1 unit")
    save(fig, "quiverkey")


def case_table():
    """A table under the axes: cell grid, header row, row labels.

    The row-label column is the only part whose width is MEASURED rather than
    assumed, and it is deliberately excluded from the block that gets
    positioned -- so it hangs off the left of the centred grid. Both are easy
    to get almost right.
    """
    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.plot([0.0, 1.0, 2.0, 3.0], [1.0, 3.0, 2.0, 4.0])

    ax.table(
        cellText=[["1.0", "20", "300"], ["4.5", "60", "700"], ["8.25", "90", "1000"]],
        colLabels=["alpha", "beta", "gamma"],
        rowLabels=["first", "second", "third"],
        loc="bottom",
    )
    save(fig, "table")


def case_contourf():
    """Filled contours, including a saddle cell and levels that do not reach
    the data's own maximum -- so the region above the top level must stay
    UNPAINTED (extend='neither')."""
    import math

    n = 21
    values = []
    for r in range(n):
        y = -2.0 + 4.0 * r / (n - 1)
        for c in range(n):
            x = -2.0 + 4.0 * c / (n - 1)
            # Two peaks and a saddle between them.
            values.append(math.exp(-((x - 0.8) ** 2 + y * y)) +
                          math.exp(-((x + 0.8) ** 2 + y * y)))

    import numpy as np

    z = np.array(values).reshape(n, n)
    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.contourf(z, levels=[0.1, 0.3, 0.5, 0.7, 0.9])
    save(fig, "contourf")


def case_contourf_saddle():
    """Cells that are genuinely ambiguous, which the smooth case has none of.

    Each cell here has its high corners on a DIAGONAL, so the region above the
    level is either two separate corners or one connected band, and nothing in
    the cell's edges says which. matplotlib decides on the mean of the four
    corners; these four cells sit either side of that decision, and one lands
    exactly on it.
    """
    import numpy as np

    z = np.array([[1.0, 0.0, 1.0],
                  [0.0, 0.9, 0.2],
                  [1.0, 0.1, 1.0]])
    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.contourf(z, levels=[0.5, 2.0])
    save(fig, "contourf_saddle")


def case_violinplot():
    """Gaussian KDE at Scott's bandwidth, mirrored, with the extreme bars.

    Three groups in ONE call, which is the thing to get right: they all share a
    single colour taken from the line cycle, rather than one colour each.
    """
    a = [1.0, 2.0, 2.5, 3.0, 3.2, 4.0, 5.0, 5.5, 6.0, 9.0]
    b = [2.0, 3.0, 3.5, 4.0, 4.1, 4.2, 5.0, 6.0]
    c = [0.0, 1.0, 1.5, 5.0, 5.2, 5.4, 8.0, 9.0, 9.5]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.violinplot([a, b, c])
    save(fig, "violinplot")


def case_color_cycles():
    """The two property cycles, and which artists turn them.

    matplotlib keeps a line cycle and a patch cycle. plot draws from the first;
    bar, fill_between and -- unexpectedly -- scatter draw from the second;
    stackplot draws from the FIRST, once per series; and stem, boxplot and
    hlines draw from neither. Counting artists rather than cycle draws gets this
    wrong in both directions at once.
    """
    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)

    ax.plot([0.0, 1.0], [1.0, 1.2])                     # line cycle 0
    ax.stem([0.2, 0.4], [2.0, 2.2])                     # neither
    ax.scatter([0.0, 1.0], [3.0, 3.2])                  # patch cycle 0
    ax.bar([0.3], [0.4], width=0.1, bottom=3.6)         # patch cycle 1
    ax.hlines([4.4], [0.0], [1.0])                      # neither
    ax.plot([0.0, 1.0], [5.0, 5.2])                     # line cycle 1
    ax.fill_between([0.0, 1.0], [5.8, 5.8], [6.2, 6.4])  # patch cycle 2
    ax.plot([0.0, 1.0], [6.8, 7.0])                     # line cycle 2
    ax.scatter([0.0, 1.0], [7.4, 7.6])                  # patch cycle 3
    save(fig, "color_cycles")


def case_pcolormesh():
    """A field on an UNEVEN grid, which is the whole point of pcolormesh: the
    cells are given by their boundaries, so they need not be the same size."""
    import numpy as np

    xedges = [0.0, 0.5, 1.5, 3.0, 3.5, 5.0]
    yedges = [0.0, 1.0, 1.2, 2.5, 4.0]
    z = np.array([[float((r * 5 + c) % 7) for c in range(5)] for r in range(4)])

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.pcolormesh(xedges, yedges, z)
    save(fig, "pcolormesh")


def case_matshow():
    """imshow with a matrix's conventions: column indices along the top."""
    import numpy as np

    z = np.array([[1.0, 2.0, 3.0, 4.0],
                  [5.0, 6.0, 7.0, 8.0],
                  [9.0, 10.0, 11.0, 12.0]])
    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.matshow(z)
    save(fig, "matshow")


def case_pie():
    """Wedges as cubic Beziers, and an axes stripped of its furniture.

    The segment count matters: matplotlib's Path.arc uses 2**ceil(sweep / 90
    degrees) curves, so a 108 degree wedge is four of them and not two.
    """
    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.pie([3.0, 1.0, 2.0, 4.0, 1.5])
    save(fig, "pie")


def case_hist_density():
    """The same histogram normalised to unit area -- hist(density=True).

    Same data and bins as `hist`, so the only thing that can move is the bar
    heights: density divides each count by the total and the bin width, which
    turns the y axis from counts into a probability density and shrinks every
    bar by the same factor.
    """
    import math

    n = 400
    vals = [math.sin(i * 1.7) + math.sin(i * 0.31) for i in range(n)]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.hist(vals, density=True)
    save(fig, "hist_density")


def case_hist2d():
    """Counts over a 2D grid, drawn as a mesh."""
    x = [0.1, 0.5, 0.9, 1.2, 1.8, 2.1, 2.4, 0.3, 1.1, 2.9, 1.4, 2.0, 0.7, 2.6]
    y = [0.2, 0.4, 1.1, 1.5, 0.8, 2.2, 0.5, 1.9, 1.0, 2.5, 1.3, 0.3, 2.4, 1.7]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.hist2d(x, y, bins=4)
    save(fig, "hist2d")


def case_hexbin():
    """Hexagonal binning: the two interleaved lattices and the dropped empties.

    The point set is deliberately clustered so some bins hold several points and
    the neighbouring ones hold none -- a uniform spread would colour every
    hexagon the same and hide both the binning rule and the dropping.
    """
    import math

    xs, ys = [], []
    for i in range(120):
        t = i * 0.37
        xs.append(2.0 + math.cos(t) * (0.4 + 0.02 * i))
        ys.append(2.0 + math.sin(t * 1.3) * (0.4 + 0.02 * i))

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.hexbin(xs, ys, gridsize=8)
    save(fig, "hexbin")


def case_triplot():
    """A Delaunay grid drawn as its edges.

    The points are on an irregular lattice, deliberately off any circle, so the
    triangulation is unique and vplot's own Bowyer-Watson has to agree with
    Qhull's answer rather than merely produce a valid one.
    """
    import math

    xs, ys = [], []
    for i in range(5):
        for j in range(5):
            xs.append(i + 0.13 * math.sin(2.7 * (i + j)))
            ys.append(j + 0.11 * math.cos(1.9 * (i - j)))

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.triplot(xs, ys)
    save(fig, "triplot")


def case_tricontour():
    """Iso-lines over an unstructured grid.

    Same perturbed lattice as case_triplot, so if the triangulation itself ever
    diverges from Qhull's the two cases fail together and say so.
    """
    import math

    xs, ys, zs = [], [], []
    for i in range(6):
        for j in range(6):
            px = i + 0.13 * math.sin(2.7 * (i + j))
            py = j + 0.11 * math.cos(1.9 * (i - j))
            xs.append(px)
            ys.append(py)
            zs.append(math.sin(0.8 * px) * math.cos(0.6 * py))

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.tricontour(xs, ys, zs, levels=[-0.5, -0.2, 0.1, 0.4, 0.7])
    save(fig, "tricontour")


def case_tricontourf():
    """Filled bands over an unstructured grid.

    The same lattice, field and levels as case_tricontour, so the two differ in
    exactly one thing: whether the band between two levels is drawn as its
    boundary or as its interior. A discrepancy that shows here but not there is
    about the filling, not about the triangulation or the level extraction.
    """
    import math

    xs, ys, zs = [], [], []
    for i in range(6):
        for j in range(6):
            px = i + 0.13 * math.sin(2.7 * (i + j))
            py = j + 0.11 * math.cos(1.9 * (i - j))
            xs.append(px)
            ys.append(py)
            zs.append(math.sin(0.8 * px) * math.cos(0.6 * py))

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.tricontourf(xs, ys, zs, levels=[-0.5, -0.2, 0.1, 0.4, 0.7])
    save(fig, "tricontourf")


def case_tripcolor():
    """A flat-shaded triangular mesh.

    Same perturbed lattice as case_triplot and case_tricontour, so a divergence
    in the triangulation shows up across all three rather than in one.
    """
    import math

    xs, ys, zs = [], [], []
    for i in range(6):
        for j in range(6):
            px = i + 0.13 * math.sin(2.7 * (i + j))
            py = j + 0.11 * math.cos(1.9 * (i - j))
            xs.append(px)
            ys.append(py)
            zs.append(math.sin(0.8 * px) * math.cos(0.6 * py))

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.tripcolor(xs, ys, zs)
    save(fig, "tripcolor")


def case_spy():
    """A sparsity pattern: white where the matrix is zero."""
    import numpy as np

    z = np.array([[1.0, 0.0, 0.0, 2.0, 0.0],
                  [0.0, 3.0, 0.0, 0.0, 0.0],
                  [0.0, 0.0, 0.0, 4.0, 5.0],
                  [6.0, 0.0, 7.0, 0.0, 0.0]])
    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.spy(z)
    save(fig, "spy")


def case_axline_barlabel():
    """An infinite line clipped to the view, and bars labelled by their height.

    The axline is the interesting half: it is re-derived from the view rather
    than carrying endpoints, so it must reach the frame on both sides whatever
    the limits turn out to be -- and it must not drag the limits out to meet it.
    """
    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)

    ax = fig.add_subplot(2, 1, 1)
    ax.plot([0.0, 4.0], [1.0, 3.0])
    ax.axline((0.0, 0.0), slope=1.0)
    ax.axline((1.0, 4.0), slope=-0.75)
    ax.axline((3.0, 0.0), slope=float("inf"))

    ax = fig.add_subplot(2, 1, 2)
    bars = ax.bar([0.0, 1.0, 2.0, 3.0], [2.0, 3.5, 1.0, 2.75], width=0.7)
    ax.bar_label(bars)
    save(fig, "axline_barlabel")


def case_figure_api():
    """The figure-level furniture: a suptitle, adjusted subplot bounds, and an
    axes placed by hand.

    subplots_adjust must move the panels ALREADY added, not merely set a
    default for the next one -- and the hand-placed axes must stay where it was
    put, because it was never in the grid.
    """
    import math

    n = 41
    xs = [i * 6.0 / (n - 1) for i in range(n)]
    ys = [math.sin(x) for x in xs]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax1 = fig.add_subplot(2, 1, 1)
    ax1.plot(xs, ys)
    ax2 = fig.add_subplot(2, 1, 2)
    ax2.plot(xs, [y * y for y in ys])

    fig.subplots_adjust(left=0.2, right=0.85, bottom=0.15, top=0.82, hspace=0.4)
    fig.suptitle("figure title")

    inset = fig.add_axes([0.55, 0.55, 0.2, 0.15])
    inset.plot(xs, [-y for y in ys])
    save(fig, "figure_api")


def case_ecdf_arrow_xerr():
    """Three small things at once.

    ecdf rises by 1/n at each observation and is right-continuous, so it is a
    POST-step -- the mirror of the pre-step `step` draws. Its y axis sticks to
    0 and 1, so neither end gets a margin.

    arrow is a patch, not a line: with length_includes_head false, which is the
    default, the head is added BEYOND the endpoint and the arrow comes out
    longer than the vector it was given.

    xerr had been a field on the line since the beginning with nothing drawing
    it.
    """
    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)

    ax = fig.add_subplot(2, 1, 1)
    ax.ecdf([3.0, 1.0, 4.0, 1.0, 5.0, 9.0, 2.0, 6.0, 5.0, 3.0])

    ax = fig.add_subplot(2, 1, 2)
    ax.errorbar([1.0, 2.0, 3.0], [1.0, 2.0, 1.5], xerr=[0.3, 0.5, 0.2],
                yerr=[0.2, 0.3, 0.25], capsize=3.0)
    ax.arrow(1.0, 2.5, 1.5, 0.6, width=0.06)
    save(fig, "ecdf_arrow_xerr")


def case_styling_knobs():
    """Every styling knob at a NON-DEFAULT value.

    Written after error bar caps turned out to have been half size since the
    beginning: capsize defaults to zero, so no case had ever drawn one, and the
    code path was running only with the value that hides the bug. These are the
    other fields the harness had only ever seen at their defaults -- line and
    marker widths, hollow markers, elinewidth, the grid's style, and the two
    label pads.
    """
    import math

    n = 9
    xs = [i * 4.0 / (n - 1) for i in range(n)]
    ys = [math.sin(x) for x in xs]
    es = [0.15] * n

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)

    ax.plot(xs, ys, linewidth=3.5, marker="o", markersize=12.0,
            markeredgewidth=2.5, markerfacecolor="white", markeredgecolor="C3")
    ax.errorbar(xs, [y - 1.5 for y in ys], yerr=es, elinewidth=0.5, capsize=5.0)

    ax.grid(True, linestyle=":", linewidth=1.2)
    ax.set_xlabel("x label", labelpad=15.0)
    ax.set_title("title", pad=20.0)
    save(fig, "styling_knobs")


def case_share_both():
    """One axes following another in BOTH directions at once.

    The follower's data is SMALLER than the leader's in both directions, so its
    curve visibly fails to fill its panel in x and in y. Following only one
    direction leaves the other filling, which is the difference this measures.
    Sharing x and y was an if/else for as long as only twinx and twiny set them
    -- each sets exactly one -- and the second branch never ran once both could
    be set on one axes.

    Smaller in both directions on purpose. matplotlib's sharing is a symmetric
    N-way group, so a follower with a LARGER range drags the leader out to meet
    it; vplot's is a one-directional follow (see the note on vplAxesShareX).
    Keeping the follower inside the leader is the `sharex=ax1` pattern where the
    two models agree, and pinning a case on the disagreement would only measure
    the difference the header already documents.
    """
    import math

    x1 = [i * 0.5 for i in range(21)]
    y1 = [4.0 * math.sin(v) for v in x1]

    x2 = [i * 0.125 for i in range(21)]
    y2 = [math.cos(v * 4.0) for v in x2]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax1 = fig.add_subplot(2, 1, 1)
    ax1.plot(x1, y1)

    ax2 = fig.add_subplot(2, 1, 2, sharex=ax1, sharey=ax1)
    ax2.plot(x2, y2)
    save(fig, "share_both")


def case_sci_offset():
    """The two things ScalarFormatter does besides printing the number.

    Top: values around 1e6, where the ORDER OF MAGNITUDE is pulled out and
    written as "1e6" -- axes.formatter.limits is [-5, 6], so 1e5 would stay
    written out in full and 1e6 does not.

    Middle: values around 1e-6, the other end of the same rule.

    Bottom: values that share leading digits (1000000 to 1000005), where an
    OFFSET is subtracted instead and written as "+1e6". A different mechanism
    with a different trigger -- it must save at least four digits.
    """
    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)

    ax = fig.add_subplot(3, 1, 1)
    ax.plot([0.0, 1.0], [1.0e6, 2.0e6])

    ax = fig.add_subplot(3, 1, 2)
    ax.plot([0.0, 1.0], [1.0e-6, 2.0e-6])

    ax = fig.add_subplot(3, 1, 3)
    ax.plot([0.0, 1.0], [1000000.0, 1000005.0])

    save(fig, "sci_offset")


def case_box_aspect_cycle():
    """A panel of a given SHAPE, an anchor other than centre, a coarser tick
    locator, and a replaced property cycle.

    set_box_aspect is not set_aspect: it asks for a square PANEL rather than
    square DATA units, so it does not depend on the view at all. The anchor only
    shows once something shrinks the box, which is exactly what box_aspect does.
    """
    import math

    n = 61
    xs = [i * 6.0 / (n - 1) for i in range(n)]
    ys = [math.sin(x) for x in xs]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)

    ax = fig.add_subplot(2, 1, 1)
    ax.plot(xs, ys)
    ax.set_box_aspect(0.35)
    ax.set_anchor("SW")
    ax.locator_params(nbins=4)

    ax = fig.add_subplot(2, 1, 2)
    ax.set_prop_cycle(color=["#d62728", "#2ca02c", "#9467bd"])
    ax.plot(xs, ys)
    ax.plot(xs, [y * 0.6 for y in ys])
    ax.plot(xs, [y * 0.3 for y in ys])
    save(fig, "box_aspect_cycle")


def case_prop_cycle_full():
    """set_prop_cycle(color=, linestyle=): zipped together, not independent.

    Five lines through a three-entry cycle -- the fourth and fifth wrap back
    to the first and second (colour, linestyle) PAIR, not to colour position
    0 and linestyle position 0 independently, which would only look
    different from this if the two counters could ever disagree.

    Straight lines rather than curves on purpose: dash phase along a CURVED
    polyline has its own separate, already-measured gap (see styles_markers,
    rms 2.12) that has nothing to do with cycling and would just add noise to
    this case. A
    straight segment's dash phase has no curvature to approximate, so this
    stays a clean test of the cycle order alone.
    """
    n = 21
    xs = [i * 6.0 / (n - 1) for i in range(n)]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.set_prop_cycle(
        color=["tab:blue", "tab:orange", "tab:green"],
        linestyle=["-", "--", ":"],
    )
    for k in range(5):
        ys = [0.15 * k * x for x in xs]
        ax.plot(xs, ys)
    save(fig, "prop_cycle_full")


def case_locator_params_full():
    """locator_params(steps=, tight=): both together, one test.

    steps=[1, 5, 10] forbids the 2 and 2.5 multiples AutoLocator's own
    default would otherwise be free to pick, so the tick spacing this data
    gets is visibly coarser than box_aspect_cycle's nbins=4 case (which
    still allows any step).

    tight=True is included deliberately even though it changes nothing:
    verified directly against matplotlib that get_xlim()/get_ylim() are
    identical for tight=True, tight=False and the unset default, because
    autoscale_view's tight only gates a second expansion through the
    locator's own view_limits(), and MaxNLocator does not override it. This
    case exists specifically so vplot's C++ mirror is checked against that
    (surprising) real behaviour rather than the more intuitive "tight zeroes
    the margin" reading, which is what Axes.autoscale()'s OWN tight argument
    does instead.
    """
    import math

    n = 61
    xs = [i * 6.0 / (n - 1) for i in range(n)]
    ys = [math.sin(x) for x in xs]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    ax = fig.add_subplot(1, 1, 1)
    ax.plot(xs, ys)
    ax.locator_params(axis="both", tight=True, steps=[1, 5, 10])
    save(fig, "locator_params_full")


def case_figure_patch():
    """The figure's own patch: a background and an edge.

    The edge defaults to WHITE at zero width, so it is invisible twice over --
    a colour alone changes nothing until the width is raised too.
    """
    import math

    n = 41
    xs = [i * 6.0 / (n - 1) for i in range(n)]
    ys = [math.sin(x) for x in xs]

    fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
    fig.set_facecolor("#f4f0e6")
    fig.set_edgecolor("C3")
    fig.set_linewidth(4.0)
    ax = fig.add_subplot(1, 1, 1)
    ax.plot(xs, ys)
    save(fig, "figure_patch")


def case_text_probe():
    """A string at a known origin, with no axes anywhere near it.

    Every other case goes through the figure machinery, so a text difference
    there could be the layout deciding where the label belongs or the renderer
    putting the glyphs somewhere else. This one pins the renderer on its own:
    RendererAgg.draw_text takes the string's origin -- the left end of its
    baseline -- and nothing else is on the canvas to argue about.

    Note the origin's y counts DOWN from the top at this level of the Agg
    backend, which is not the convention the rest of matplotlib's display
    coordinates use.
    """
    from matplotlib.font_manager import FontProperties

    width, height, dpi = 320, 120, 100
    fig = plt.figure(figsize=(width / dpi, height / dpi), dpi=dpi)
    fig.canvas.draw()  # paints the white figure patch; the text goes on top

    renderer = fig.canvas.get_renderer()
    gc = renderer.new_gc()
    gc.set_foreground("black")

    # "Apg" covers an ascender, a descender and a cap; the digits are what tick
    # labels are actually made of.
    renderer.draw_text(gc, 30.0, 90.0, "Apg 1.00", FontProperties(size=10), 0)
    renderer.draw_text(gc, 30.0, 40.0, "Apg 1.00", FontProperties(size=12), 0)
    # A FRACTIONAL origin, which is where every real label lands: a tick is
    # almost never on a whole pixel. Whole-pixel origins agree trivially.
    renderer.draw_text(gc, 30.37, 65.0, "Apg 1.00", FontProperties(size=10), 0)

    save(fig, "text_probe", redraw=False)


CASES = [
    case_simple_line,
    case_labels,
    case_styles_markers,
    case_loglog,
    case_fill_between,
    case_errorbar,
    case_scatter,
    case_bar,
    case_twinx,
    case_colorbar,
    case_colorbar_horizontal,
    case_text,
    case_contour,
    case_clabel,
    case_imshow,
    case_reference_lines,
    case_minor_ticks,
    case_scatter_varied,
    case_annotate,
    case_hist,
    case_hist_density,
    case_fixed_ticks,
    case_spines,
    case_multiline,
    case_barh,
    case_spans,
    case_step,
    case_fill_betweenx,
    case_twiny,
    case_legend_locs,
    case_legend_ncols,
    case_set_bound,
    case_legend_title,
    case_tick_params,
    case_tick_colors,
    case_bar_stacked,
    case_invert_axes,
    case_hlines_vlines,
    case_eventplot,
    case_stem,
    case_stairs,
    case_fill_poly,
    case_broken_barh,
    case_stackplot,
    case_stackplot_sym,
    case_stackplot_stream,
    case_boxplot,
    case_bxp,
    case_table,
    case_quiver,
    case_quiverkey,
    case_axis_off,
    case_suplabels,
    case_label_outer,
    case_margins,
    case_inset_axes,
    case_indicate_inset_zoom,
    case_secondary_xaxis,
    case_secondary_yaxis,
    case_grid_over_image,
    case_subplots_shared,
    case_sharex,
    case_share_both,
    case_sci_offset,
    case_box_aspect_cycle,
    case_prop_cycle_full,
    case_locator_params_full,
    case_figure_patch,
    case_axisbelow,
    case_psd,
    case_csd,
    case_cohere,
    case_psd_detrend_window,
    case_psd_twosided,
    case_magnitude_spectrum,
    case_magnitude_spectrum_db,
    case_angle_spectrum,
    case_phase_spectrum,
    case_specgram,
    case_specgram_magnitude,
    case_streamplot,
    case_pie_label,
    case_barbs,
    case_grouped_bar,
    case_acorr,
    case_xcorr,
    case_contourf,
    case_contourf_saddle,
    case_violinplot,
    case_color_cycles,
    case_pcolormesh,
    case_matshow,
    case_pie,
    case_hist2d,
    case_hexbin,
    case_triplot,
    case_tricontour,
    case_tricontourf,
    case_tripcolor,
    case_spy,
    case_axline_barlabel,
    case_bar_label_full,
    case_figure_api,
    case_ecdf_arrow_xerr,
    case_styling_knobs,
    case_tight_layout,
    case_align_ylabels,
    case_ecdf_complementary,
    case_ecdf_weighted,
    case_ecdf_horizontal,
    case_fill_where,
    case_fill_step,
    case_stairs_fill,
    case_stairs_horizontal,
    case_stem_styled,
    case_label_outer_ticks,
    case_no_sticky_edges,
    case_autoscale_tight,
    case_grid_minor,
    case_sup_positioned,
    case_figimage,
    case_text_probe,
]


def main():
    print(f"matplotlib {matplotlib.__version__}")

    # rcParams must be the stock defaults: vplot's own defaults are transcribed
    # from mpl-data/matplotlibrc, so a user style file here would make every
    # comparison fail for the wrong reason.
    matplotlib.rcdefaults()

    os.makedirs(OUT_DIR, exist_ok=True)
    for case in CASES:
        case()

    print(f"\n{len(CASES)} reference(s) written to {os.path.normpath(OUT_DIR)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
