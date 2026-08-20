/*
 * Colormaps and value normalization.
 *
 * Ported from matplotlib's lib/matplotlib/colors.py (Normalize, ListedColormap).
 * Copyright (c) 2012- Matplotlib Development Team; All Rights Reserved.
 * See THIRD-PARTY-NOTICES.
 */

#ifndef VPL_CORE_COLORMAP_H
#define VPL_CORE_COLORMAP_H

#include <algorithm>
#include <cmath>

#include "colormap_data.h"
#include "renderer.h"

namespace vpl
{

enum class Colormap
{
    Viridis,
    Magma,
    Inferno,
    Plasma,
    Gray,
    /* matplotlib's 'binary', which is 'gray' the other way up: zero is WHITE.
       spy uses it so an empty matrix reads as blank paper. */
    Binary,
};

/* `t` is the normalized position in 0..1; anything outside is clamped, which is
   what Normalize does with clip enabled. NaN yields transparent, so missing
   data leaves a hole rather than being drawn as an extreme value. */
inline Color colormap_lookup(Colormap cm, double t)
{
    if (std::isnan(t)) {
        return Color::none();
    }
    t = std::clamp(t, 0.0, 1.0);

    if (cm == Colormap::Gray) {
        return Color{t, t, t, 1.0};
    }
    if (cm == Colormap::Binary) {
        return Color{1.0 - t, 1.0 - t, 1.0 - t, 1.0};
    }

    const double (*table)[3] = k_viridis_data;
    switch (cm) {
        case Colormap::Magma: table = k_magma_data; break;
        case Colormap::Inferno: table = k_inferno_data; break;
        case Colormap::Plasma: table = k_plasma_data; break;
        case Colormap::Viridis:
        case Colormap::Gray:
        default: break;
    }

    /* matplotlib's ListedColormap quantizes to N entries rather than
       interpolating between them, so the same value gives the same colour in
       both libraries. */
    const int i = std::clamp(static_cast<int>(t * 256.0), 0, 255);
    return Color{table[i][0], table[i][1], table[i][2], 1.0};
}

/* colors.Normalize: map [vmin, vmax] onto [0, 1]. A degenerate range collapses
   to 0, matching Normalize's behaviour when vmin == vmax. */
inline double normalize(double value, double vmin, double vmax)
{
    if (!(vmax > vmin)) {
        return 0.0;
    }
    return (value - vmin) / (vmax - vmin);
}

} // namespace vpl

#endif /* VPL_CORE_COLORMAP_H */
