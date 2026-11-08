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
- Log axes spanning less than a decade: matplotlib labels the minor ticks
  (`2×10¹`, `3×10¹`); vplot instead places major ticks at fractional powers
  (`10^1.30`).

## Axis sharing and layout

- `sharex` / `sharey`: matplotlib's symmetric N-way sharing group (currently a
  one-directional follow).
- `add_axes` / `add_subplot`: projections, gridspec, and row/column spans.
- `inset_axes`: a locator that tracks the parent as it moves.
- `indicate_inset` / `indicate_inset_zoom`: styling arguments and access to all
  four connectors.

## Output

- `savefig`: EPS and PS backends (PNG/SVG/PDF, and a PNG `dpi` override, are
  done).
- `imshow`: an optional resampled raster path for large photographic arrays
  (the default vector quad-per-cell path stays for heatmaps).
- Mathtext: fractions, radicals, and the full symbol table. Superscripts, which
  a log axis needs, already work; `loglog` decade labels wait on the rest.

## Plotting-command options

The long tail — each command works at its defaults and is missing some keyword
arguments. Notable ones:

- `contourf`: hatching.
- `pie`: `hatch` (with the other hatched patches: no hatching yet).
- `tripcolor`: `shading='gouraud'` (needs Gouraud triangles in every
  backend) and edge colours.
- `annotate`: arrow styles beyond the default.

## Ticks, labels, and other accessors

- `set_xticklabels` / `set_yticklabels`: `minor=True` and `fontdict`.

## Infrastructure

- Continuous integration on Linux and macOS in addition to Windows.
- Tagged releases and API stability once the surface settles.

See `docs/API-COVERAGE.md` for the complete, generated status of every method.
