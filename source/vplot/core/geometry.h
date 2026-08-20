/*
 * Bounding boxes and affine transforms.
 *
 * Ported from matplotlib's lib/matplotlib/transforms.py.
 * Copyright (c) 2012- Matplotlib Development Team; All Rights Reserved.
 * See THIRD-PARTY-NOTICES.
 *
 * matplotlib builds a full lazily-invalidated transform graph (TransformNode).
 * vplot uses plain values instead: figures are small, and every transform
 * needed is a composition of two bbox mappings. The graph can return if
 * interactive invalidation makes it worthwhile.
 *
 * Coordinates follow matplotlib's display space: y points UP, origin at the
 * bottom-left of the canvas. RendererAgg flips to the top-down pixel buffer
 * itself.
 */

#ifndef VPL_CORE_GEOMETRY_H
#define VPL_CORE_GEOMETRY_H

#include <algorithm>
#include <cmath>
#include <limits>

#include "agg_trans_affine.h"

namespace vpl
{

struct Point
{
    double x = 0.0;
    double y = 0.0;
};

class Bbox
{
  public:
    double x0 = 0.0;
    double y0 = 0.0;
    double x1 = 1.0;
    double y1 = 1.0;

    Bbox() = default;
    Bbox(double x0_, double y0_, double x1_, double y1_) : x0(x0_), y0(y0_), x1(x1_), y1(y1_) {}

    static Bbox from_bounds(double x, double y, double w, double h)
    {
        return Bbox(x, y, x + w, y + h);
    }

    double width() const { return x1 - x0; }
    double height() const { return y1 - y0; }

    bool is_degenerate() const
    {
        return !(std::isfinite(width()) && std::isfinite(height())) ||
               width() == 0.0 || height() == 0.0;
    }

    /* Grow a degenerate interval into a usable one, mirroring
       transforms.nonsingular(). Data limits like a single point, or a constant
       series, would otherwise divide by zero in the data->axes transform. */
    static void nonsingular(double &vmin, double &vmax,
                            double expander = 0.001,
                            double tiny = 1e-15)
    {
        if (!std::isfinite(vmin) || !std::isfinite(vmax)) {
            vmin = 0.0;
            vmax = 1.0;
            return;
        }
        if (vmax < vmin) {
            std::swap(vmin, vmax);
        }

        const double maxabs = std::max(std::fabs(vmin), std::fabs(vmax));
        if (maxabs < (1.0 / tiny) * std::numeric_limits<double>::min()) {
            vmin = -expander;
            vmax = expander;
        } else if (vmax - vmin <= maxabs * tiny) {
            if (vmax == 0.0 && vmin == 0.0) {
                vmin = -expander;
                vmax = expander;
            } else {
                vmin -= expander * std::fabs(vmin);
                vmax += expander * std::fabs(vmax);
            }
        }
    }
};

/*
 * A 2D affine transform, stored the way matplotlib writes it:
 *
 *     x' = a*x + b*y + c
 *     y' = d*x + e*y + f
 */
class Affine2D
{
  public:
    double a = 1.0, b = 0.0, c = 0.0;
    double d = 0.0, e = 1.0, f = 0.0;

    Affine2D() = default;
    Affine2D(double a_, double b_, double c_, double d_, double e_, double f_)
        : a(a_), b(b_), c(c_), d(d_), e(e_), f(f_)
    {
    }

    static Affine2D translate(double tx, double ty)
    {
        return Affine2D(1.0, 0.0, tx, 0.0, 1.0, ty);
    }

    static Affine2D scale(double sx, double sy)
    {
        return Affine2D(sx, 0.0, 0.0, 0.0, sy, 0.0);
    }

    /* Counterclockwise in display space (y up). */
    static Affine2D rotate(double radians)
    {
        const double c = std::cos(radians);
        const double s = std::sin(radians);
        return Affine2D(c, -s, 0.0, s, c, 0.0);
    }

    /* Map `from` onto `to` -- matplotlib's BboxTransform. data->axes and
       axes->figure are both instances of it. */
    static Affine2D from_bbox_to_bbox(const Bbox &from, const Bbox &to)
    {
        const double sx = to.width() / from.width();
        const double sy = to.height() / from.height();
        return Affine2D(sx, 0.0, to.x0 - from.x0 * sx,
                        0.0, sy, to.y0 - from.y0 * sy);
    }

    /* Apply this transform, then `next`. */
    Affine2D then(const Affine2D &next) const
    {
        return Affine2D(next.a * a + next.b * d,
                        next.a * b + next.b * e,
                        next.a * c + next.b * f + next.c,
                        next.d * a + next.e * d,
                        next.d * b + next.e * e,
                        next.d * c + next.e * f + next.f);
    }

    Point operator()(Point p) const
    {
        return Point{a * p.x + b * p.y + c, d * p.x + e * p.y + f};
    }

    /* Agg orders the same six numbers as (sx, shy, shx, sy, tx, ty), i.e.
       x' = x*sx + y*shx + tx. */
    agg::trans_affine to_agg() const
    {
        return agg::trans_affine(a, d, b, e, c, f);
    }
};

} // namespace vpl

#endif /* VPL_CORE_GEOMETRY_H */
