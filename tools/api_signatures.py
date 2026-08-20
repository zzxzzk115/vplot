#!/usr/bin/env python3
"""Check that vplot's entry points take their arguments in matplotlib's order.

    pip install matplotlib==3.11.1
    python tools/api_signatures.py

This is not a 1:1 signature check. matplotlib overloads by argument count and
type -- plot(y), plot(x, y), plot(x, y, fmt), plot(x1, y1, fmt1, x2, y2, fmt2)
are all one function -- takes keyword arguments with defaults, and lets an
argument's type decide its meaning. C has none of that: no overloading, no
keywords, no defaults.

What a C caller can be given, and what this checks, is that the arguments vplot
and matplotlib SHARE appear in the SAME ORDER, so a developer who knows
`bar(x, height, width, bottom)` need not relearn it as
`bar(x, height, bottom, width)`.

Arguments only one side has are skipped rather than counted against either:

  * vplot's own    the axes or figure handle, the array lengths C needs and
                   Python reads off the array, the styling descriptor, and the
                   out-parameter that stands in for a return value;
  * matplotlib's   everything it takes as a keyword and vplot takes through a
                   descriptor, or does not take at all.

Deliberate differences go in JUSTIFIED with their reason.
"""

import inspect
import io
import os
import re
import sys

import matplotlib

matplotlib.use("Agg")
from matplotlib.axes import Axes  # noqa: E402
from matplotlib.figure import Figure  # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")

# vplot entry -> (owning class, matplotlib method)
MAPPING = {
    "vplAxesPlot": (Axes, "plot"),
    "vplAxesScatter": (Axes, "scatter"),
    "vplAxesStep": (Axes, "step"),
    "vplAxesBar": (Axes, "bar"),
    "vplAxesBarH": (Axes, "barh"),
    "vplAxesHist": (Axes, "hist"),
    "vplAxesHist2D": (Axes, "hist2d"),
    "vplAxesFillBetween": (Axes, "fill_between"),
    "vplAxesFillBetweenX": (Axes, "fill_betweenx"),
    "vplAxesFill": (Axes, "fill"),
    "vplAxesStairs": (Axes, "stairs"),
    "vplAxesStem": (Axes, "stem"),
    "vplAxesEcdf": (Axes, "ecdf"),
    "vplAxesEventPlot": (Axes, "eventplot"),
    "vplAxesStackPlot": (Axes, "stackplot"),
    "vplAxesBrokenBarH": (Axes, "broken_barh"),
    "vplAxesErrorBar": (Axes, "errorbar"),
    "vplAxesArrow": (Axes, "arrow"),
    "vplAxesHLines": (Axes, "hlines"),
    "vplAxesVLines": (Axes, "vlines"),
    "vplAxesAxHSpan": (Axes, "axhspan"),
    "vplAxesAxVSpan": (Axes, "axvspan"),
    "vplAxesImshow": (Axes, "imshow"),
    "vplAxesPcolorMesh": (Axes, "pcolormesh"),
    "vplAxesMatShow": (Axes, "matshow"),
    "vplAxesSpy": (Axes, "spy"),
    "vplAxesPie": (Axes, "pie"),
    "vplAxesText": (Axes, "text"),
    "vplAxesSetXLim": (Axes, "set_xlim"),
    "vplAxesSetYLim": (Axes, "set_ylim"),
    "vplAxesSetAnchor": (Axes, "set_anchor"),
    "vplAxesXCorr": (Axes, "xcorr"),
    "vplAxesACorr": (Axes, "acorr"),
    "vplAxesCsd": (Axes, "csd"),
    "vplAxesCohere": (Axes, "cohere"),
    "vplFigureAddAxes": (Figure, "add_axes"),
    "vplFigureAddSubplot": (Figure, "add_subplot"),
    "vplFigureSubplots": (Figure, "subplots"),
    "vplFigureSetSizeInches": (Figure, "set_size_inches"),
    "vplFigureAlignXLabels": (Figure, "align_xlabels"),
    "vplFigureAlignYLabels": (Figure, "align_ylabels"),
    "vplFigureAlignLabels": (Figure, "align_labels"),
    "vplFigureAlignTitles": (Figure, "align_titles"),
    "vplFigureFigImage": (Figure, "figimage"),
}

# Names that exist on only one side and are not evidence of a mismatch.
VPLOT_ONLY = {
    "axes", "figure",             # the receiver, which Python spells `self`
    "desc", "outline", "outpatch", "outaxes", "outimage", "outcontour",
    "outsegments", "outtext", "outspan",
    "count", "rows", "cols", "n", "nrows", "ncols",
    "levelcount", "seriescount", "groupcount", "labelcount", "buffersize",
    "outcount",
}

# entry -> reason a remaining difference is intended
JUSTIFIED = {
    "vplAxesImshow": "matplotlib takes one array argument named X; vplot needs "
                     "its shape as well, and rows/cols are skipped as vplot-only",
    "vplAxesMatShow": "as vplAxesImshow",
    "vplAxesSpy": "as vplAxesImshow",
    "vplAxesPie": "matplotlib's first argument is `x`; vplot calls it `values` "
                  "because `x` in this ABI otherwise always means a coordinate",
    "vplAxesStackPlot": "matplotlib takes *args of varying series; vplot takes "
                        "one packed array, so the series parameter has no "
                        "matplotlib counterpart to align with",
    "vplFigureSubplots": "matplotlib returns the axes array; vplot writes it "
                         "through outAxes, which is skipped as vplot-only",
    "vplAxesText": "matplotlib's third positional is `s`; vplot spells it "
                   "`text`, which is the name matplotlib's own documentation "
                   "uses for it throughout",
    "vplAxesXCorr": "matplotlib takes usevlines/maxlags/normed as keywords in a "
                    "different order than a C caller can default them",
    "vplAxesACorr": "as vplAxesXCorr",
}


def vplot_params(header, entry):
    m = re.search(r"VPL_CALL\s+" + re.escape(entry) + r"\s*\((.*?)\)\s*;", header, re.S)
    if m is None:
        return None
    out = []
    for part in m.group(1).split(","):
        part = part.strip()
        if not part:
            continue
        name = re.split(r"[\s*]+", part)[-1]
        out.append(name)
    return out


def mpl_params(cls, method):
    try:
        sig = inspect.signature(getattr(cls, method))
    except (TypeError, ValueError):
        return None
    out = []
    for name, p in sig.parameters.items():
        if name in ("self", "data"):
            continue
        if p.kind in (p.VAR_KEYWORD,):
            continue
        out.append(name)
    return out


def normalise(name):
    """camelCase and snake_case spellings of the same idea compare equal."""
    return re.sub(r"[^a-z0-9]", "", name.lower())


def main():
    with io.open(os.path.join(ROOT, "include", "vpl", "vpl.h"), encoding="utf-8") as f:
        header = f.read()

    problems = []
    checked = 0
    aligned = 0

    for entry, (cls, method) in sorted(MAPPING.items()):
        vp = vplot_params(header, entry)
        if vp is None:
            problems.append("%s is in the mapping and not in vpl.h" % entry)
            continue
        mp = mpl_params(cls, method)
        if mp is None:
            problems.append("%s maps to %s.%s, which has no signature"
                            % (entry, cls.__name__, method))
            continue

        checked += 1

        mp_norm = [normalise(n) for n in mp]
        # vplot's own bookkeeping is not evidence either way
        vp_kept = [n for n in vp if normalise(n) not in VPLOT_ONLY]
        # ...and only the names the two SHARE can be compared at all
        shared_v = [n for n in vp_kept if normalise(n) in mp_norm]
        shared_m = [n for n in mp if normalise(n) in {normalise(x) for x in shared_v}]

        if [normalise(n) for n in shared_v] == [normalise(n) for n in shared_m]:
            aligned += 1
            continue

        if entry in JUSTIFIED:
            aligned += 1
            continue

        problems.append(
            "%s takes %s where %s.%s takes %s"
            % (entry, shared_v, cls.__name__, method, shared_m)
        )

    for entry in sorted(set(JUSTIFIED) - set(MAPPING)):
        problems.append("JUSTIFIED excuses %s, which the mapping does not cover" % entry)

    print("signature order: %d entries checked, %d aligned with matplotlib"
          % (checked, aligned))

    # And the names that could not be compared at all.
    #
    # A vplot parameter with no matplotlib counterpart is invisible to the
    # order check above -- it is simply skipped -- so a near miss like
    # `lineOffset` against matplotlib's `lineoffsets` passes silently. Printed
    # rather than failed, since some parameters genuinely have no counterpart,
    # but printed every time as a reminder when adding a new entry.
    print("")
    print("vplot parameters with no matplotlib counterpart:")
    any_unmatched = False
    for entry, (cls, method) in sorted(MAPPING.items()):
        vp = vplot_params(header, entry)
        mp = mpl_params(cls, method)
        if vp is None or mp is None:
            continue
        mp_norm = set(normalise(n) for n in mp)
        odd = [n for n in vp
               if normalise(n) not in VPLOT_ONLY and normalise(n) not in mp_norm]
        if odd:
            any_unmatched = True
            print("  %-24s %s" % (entry, " ".join(odd)))
    if not any_unmatched:
        print("  (none)")

    if problems:
        print("\nout of order:", file=sys.stderr)
        for p in problems:
            print("  " + p, file=sys.stderr)
        print(
            "\nReorder the C parameters to match matplotlib's, or add the entry to\n"
            "JUSTIFIED with the reason the difference is intended.",
            file=sys.stderr,
        )
        return 1

    print("every mapped entry takes its shared arguments in matplotlib's order")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
