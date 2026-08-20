/*
 * An owning path: vertices plus optional per-vertex codes.
 *
 * The layout mirrors matplotlib's Path (lib/matplotlib/path.py) -- an (n, 2)
 * vertex array and an n-length code array -- because that is exactly what
 * mpl::PathIterator walks, and what a C ABI caller can hand us without any
 * conversion.
 */

#ifndef VPL_CORE_PATH_H
#define VPL_CORE_PATH_H

#include <cstdint>
#include <vector>

#include "geometry.h"
#include "mplutils.h"    /* MOVETO / LINETO / CLOSEPOLY path codes */
#include "vpl_adaptors.h"

namespace vpl
{

class Path
{
  public:
    /* Flat n x 2, row-major: [x0, y0, x1, y1, ...]. */
    std::vector<double> vertices;
    /* Empty means "implicit polyline": MOVETO then LINETOs, which is what
       PathIterator synthesizes when handed a null codes pointer. */
    std::vector<uint8_t> codes;

    bool should_simplify = false;
    double simplify_threshold = 1.0 / 9.0;

    std::size_t size() const { return vertices.size() / 2; }

    void move_to(double x, double y) { push(MOVETO, x, y); }
    void line_to(double x, double y) { push(LINETO, x, y); }

    /* Quadratic and cubic segments, coded the way matplotlib codes them: every
       vertex of the segment carries the segment's code, so CURVE3 appears twice
       (control, end) and CURVE4 three times (control, control, end). Agg's
       conv_curve consumes exactly this. */
    void curve3(double cx, double cy, double x, double y)
    {
        push(CURVE3, cx, cy);
        push(CURVE3, x, y);
    }

    /*
     * A circular arc on the unit circle, as cubic Beziers -- matplotlib's
     * Path.arc, reproduced including its segment count.
     *
     * The number of segments is 2^ceil(sweep / 90 degrees), NOT ceil: a 108
     * degree arc is drawn with FOUR curves, not two. The control-point offset
     * is the standard alpha = sin(d) * (sqrt(4 + 3 tan^2(d/2)) - 1) / 3, which
     * is what makes the approximation exact at both ends and in the middle.
     *
     * Emitted as a fresh path starting with a move_to.
     */
    static Path arc(double theta1_deg, double theta2_deg)
    {
        constexpr double kPi = 3.14159265358979323846;

        double eta1 = theta1_deg;
        double eta2 = theta2_deg - 360.0 * std::floor((theta2_deg - theta1_deg) / 360.0);
        if (theta2_deg != theta1_deg && eta2 <= eta1) {
            eta2 += 360.0;
        }
        eta1 *= kPi / 180.0;
        eta2 *= kPi / 180.0;

        const double halfpi = kPi / 2.0;
        int n = static_cast<int>(std::pow(2.0, std::ceil((eta2 - eta1) / halfpi)));
        if (n < 1) {
            n = 1;
        }

        const double deta = (eta2 - eta1) / n;
        const double t = std::tan(0.5 * deta);
        const double alpha = std::sin(deta) * (std::sqrt(4.0 + 3.0 * t * t) - 1.0) / 3.0;

        Path p;
        p.move_to(std::cos(eta1), std::sin(eta1));
        for (int i = 0; i < n; ++i) {
            const double a = eta1 + deta * i;
            const double b = eta1 + deta * (i + 1);
            const double xa = std::cos(a), ya = std::sin(a);
            const double xb = std::cos(b), yb = std::sin(b);
            /* The tangent at angle e is (-sin e, cos e), i.e. the point rotated
               a quarter turn -- hence the swap and the sign. */
            p.curve4(xa + alpha * -ya, ya + alpha * xa, xb - alpha * -yb, yb - alpha * xb,
                     xb, yb);
        }
        return p;
    }

    void curve4(double c1x, double c1y, double c2x, double c2y, double x, double y)
    {
        push(CURVE4, c1x, c1y);
        push(CURVE4, c2x, c2y);
        push(CURVE4, x, y);
    }

    void close_poly()
    {
        /* CLOSEPOLY's vertex is ignored, but one must be present to keep the
           vertices and codes arrays the same length. */
        push(CLOSEPOLY, 0.0, 0.0);
    }

    static Path polyline(const double *xs, const double *ys, std::size_t n)
    {
        Path p;
        p.vertices.reserve(2 * n);
        for (std::size_t i = 0; i < n; ++i) {
            p.vertices.push_back(xs[i]);
            p.vertices.push_back(ys[i]);
        }
        /* No codes: PathIterator reads this as MOVETO followed by LINETOs. */
        return p;
    }

    /* An unclosed rectangle, used for the axes frame. Left unclosed so that a
       stroke does not double-draw the starting corner. */
    static Path rectangle(const Bbox &box)
    {
        Path p;
        p.move_to(box.x0, box.y0);
        p.line_to(box.x1, box.y0);
        p.line_to(box.x1, box.y1);
        p.line_to(box.x0, box.y1);
        p.line_to(box.x0, box.y0);
        return p;
    }

    static Path segment(double x0, double y0, double x1, double y1)
    {
        Path p;
        p.move_to(x0, y0);
        p.line_to(x1, y1);
        return p;
    }

    /* A non-owning iterator over this path. The path must outlive it. */
    mpl::PathIterator iterator() const
    {
        return mpl::PathIterator(vertices.data(),
                                 codes.empty() ? nullptr : codes.data(),
                                 static_cast<unsigned>(size()),
                                 should_simplify,
                                 simplify_threshold);
    }

  private:
    void push(uint8_t code, double x, double y)
    {
        /* Switching from implicit to explicit codes means backfilling the ones
           the implicit form was standing in for. */
        if (codes.empty() && !vertices.empty()) {
            codes.assign(size(), LINETO);
            codes[0] = MOVETO;
        }
        vertices.push_back(x);
        vertices.push_back(y);
        codes.push_back(code);
    }
};

} // namespace vpl

#endif /* VPL_CORE_PATH_H */
