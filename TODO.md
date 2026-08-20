# Roadmap

vplot implements every plotting command in matplotlib's headless-applicable API
surface (204 of 204 methods, 0 missing). About two thirds of those are complete
against matplotlib's keyword arguments; the rest are usable at their defaults but
do not yet accept every option. `docs/API-COVERAGE.md` is the authoritative,
generated per-method status. This file groups the remaining work.

Every change is verified the same way: a figure is rendered by real matplotlib,
committed as a reference image, and compared pixel for pixel by the differential
test harness (`tests/test_baseline.cpp`).

## Scales and transforms

- `set_xscale` / `set_yscale`: add `symlog`, `logit`, and arbitrary function
  scales (linear and log are done).
- `secondary_xaxis` / `secondary_yaxis`: arbitrary forward/inverse function
  pairs (only affine conversions are supported so far).
- `set_aspect`: the `datalim` adjustable mode (only `box` is drawn).

## Axis sharing and layout

- `sharex` / `sharey`: matplotlib's symmetric N-way sharing group (currently a
  one-directional follow).
- `add_axes` / `add_subplot`: projections, gridspec, and row/column spans.
- `inset_axes`: a locator that tracks the parent as it moves.
- `indicate_inset` / `indicate_inset_zoom`: styling arguments and access to all
  four connectors.

## Output

- `savefig`: EPS and PS backends, and a `dpi` override (PNG/SVG/PDF are done).
- `imshow`: an optional resampled raster path for large photographic arrays
  (the default vector quad-per-cell path stays for heatmaps).
- Mathtext: fractions, radicals, and the full symbol table. Superscripts, which
  a log axis needs, already work; `loglog` decade labels wait on the rest.

## Plotting-command options

The long tail — each command works at its defaults and is missing some keyword
arguments. Notable ones:

- `contour` / `contourf`: automatic level selection, `extend`, hatching.
- `hist`: weights, explicit bin edges, `histtype`, stacking.
- `errorbar`: asymmetric error pairs, `errorevery`, format strings.
- `pie`: labels, `autopct`, `explode`, `startangle`.
- `violinplot`, `boxplot` / `bxp`: medians/means, custom positions, notches,
  bandwidth selection.
- `hexbin`: `C`/`reduce_C_function`, `mincnt`, log scales, chosen colormap.
- `quiver` / `barbs`: per-arrow colour and custom sizes.
- The `tripcolor` / `tricontour` / `tricontourf` / `triplot` family: explicit
  triangles, masks, Gouraud shading.
- `annotate`: arrow styles beyond the default.
- `acorr` / `xcorr`: `detrend` and the non-vlines line form.

## Ticks, labels, and other accessors

- `set_xticklabels` / `set_yticklabels`: `minor=True` and `fontdict`.
- `get_anchor`: return the string code form as well as the pair.

## Infrastructure

- Continuous integration on Linux and macOS in addition to Windows.
- Font embedding, for a truly zero-runtime-dependency build.
- Tagged releases and API stability once the surface settles.

See `docs/API-COVERAGE.md` for the complete, generated status of every method.
