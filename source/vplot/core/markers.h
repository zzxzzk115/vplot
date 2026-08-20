/*
 * Marker shapes and dash patterns.
 *
 * Ported from matplotlib's lib/matplotlib/markers.py and lines.py.
 * Copyright (c) 2012- Matplotlib Development Team; All Rights Reserved.
 * See THIRD-PARTY-NOTICES.
 *
 * Marker paths live in a unit box of radius 1 and are scaled by
 * 0.5 * markersize at draw time, which is what makes a markersize of 6 mean
 * "6 points across" -- matplotlib's convention.
 */

#ifndef VPL_CORE_MARKERS_H
#define VPL_CORE_MARKERS_H

#include <cmath>
#include <utility>
#include <vector>

#include "path.h"

namespace vpl
{

enum class LineStyle
{
    Solid,
    Dashed,
    DashDot,
    Dotted,
    None,
};

enum class MarkerStyle
{
    None,
    Circle,     /* 'o' */
    Square,     /* 's' */
    TriangleUp, /* '^' */
    TriangleDown,
    Diamond, /* 'D' */
    Plus,    /* '+' */
    Cross,   /* 'x' */
    Star,    /* '*' */
};

/* lines.{dashed,dashdot,dotted}_pattern, in units of linewidth. matplotlib
   scales dash patterns by the line width (lines.scale_dashes, default True), so
   a thick dashed line gets proportionally longer dashes. */
inline std::vector<std::pair<double, double>> dash_pattern(LineStyle style, double linewidth)
{
    const double w = linewidth > 0.0 ? linewidth : 1.0;
    switch (style) {
        case LineStyle::Dashed:
            return {{3.7 * w, 1.6 * w}};
        case LineStyle::DashDot:
            return {{6.4 * w, 1.6 * w}, {1.0 * w, 1.6 * w}};
        case LineStyle::Dotted:
            return {{1.0 * w, 1.65 * w}};
        case LineStyle::Solid:
        case LineStyle::None:
        default:
            return {};
    }
}

/* Whether the marker is an outline to be filled, or strokes only ('+' and 'x'
   have no interior). */
inline bool marker_is_filled(MarkerStyle marker)
{
    return marker != MarkerStyle::Plus && marker != MarkerStyle::Cross &&
           marker != MarkerStyle::None;
}

inline Path marker_path(MarkerStyle marker)
{
    Path p;

    /* Bezier circle constant: the control-point offset that approximates a
       quarter arc to within about 0.02%. */
    constexpr double k = 0.5522847498307936;

    switch (marker) {
        case MarkerStyle::Circle:
            p.move_to(1.0, 0.0);
            p.curve4(1.0, k, k, 1.0, 0.0, 1.0);
            p.curve4(-k, 1.0, -1.0, k, -1.0, 0.0);
            p.curve4(-1.0, -k, -k, -1.0, 0.0, -1.0);
            p.curve4(k, -1.0, 1.0, -k, 1.0, 0.0);
            p.close_poly();
            break;

        case MarkerStyle::Square:
            p.move_to(-1.0, -1.0);
            p.line_to(1.0, -1.0);
            p.line_to(1.0, 1.0);
            p.line_to(-1.0, 1.0);
            p.close_poly();
            break;

        case MarkerStyle::TriangleUp:
            p.move_to(0.0, 1.0);
            p.line_to(-1.0, -1.0);
            p.line_to(1.0, -1.0);
            p.close_poly();
            break;

        case MarkerStyle::TriangleDown:
            p.move_to(0.0, -1.0);
            p.line_to(1.0, 1.0);
            p.line_to(-1.0, 1.0);
            p.close_poly();
            break;

        case MarkerStyle::Diamond:
            p.move_to(0.0, 1.0);
            p.line_to(-1.0, 0.0);
            p.line_to(0.0, -1.0);
            p.line_to(1.0, 0.0);
            p.close_poly();
            break;

        case MarkerStyle::Plus:
            p.move_to(-1.0, 0.0);
            p.line_to(1.0, 0.0);
            p.move_to(0.0, -1.0);
            p.line_to(0.0, 1.0);
            break;

        case MarkerStyle::Cross:
            p.move_to(-1.0, -1.0);
            p.line_to(1.0, 1.0);
            p.move_to(-1.0, 1.0);
            p.line_to(1.0, -1.0);
            break;

        case MarkerStyle::Star: {
            /* Five-pointed star, outer radius 1 and inner radius 0.5, first
               point straight up -- matplotlib's '*'. */
            constexpr double pi = 3.14159265358979323846;
            for (int i = 0; i < 10; ++i) {
                const double r = (i % 2 == 0) ? 1.0 : 0.5;
                const double a = pi / 2.0 + i * pi / 5.0;
                const double x = r * std::cos(a);
                const double y = r * std::sin(a);
                if (i == 0) {
                    p.move_to(x, y);
                } else {
                    p.line_to(x, y);
                }
            }
            p.close_poly();
            break;
        }

        case MarkerStyle::None:
        default:
            break;
    }

    return p;
}

} // namespace vpl

#endif /* VPL_CORE_MARKERS_H */
