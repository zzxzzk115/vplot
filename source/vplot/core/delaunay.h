/*
 * Delaunay triangulation, by Bowyer-Watson.
 *
 * matplotlib triangulates through Qhull; this is a separate implementation
 * rather than a binding, since Qhull's C API is a large surface for one family
 * of commands. The answer is independent of the method: for points in general
 * position the Delaunay triangulation is unique, so both agree. Exactly
 * co-circular points are the exception, where the diagonal is a real choice.
 */

#ifndef VPL_CORE_DELAUNAY_H
#define VPL_CORE_DELAUNAY_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

namespace vpl
{

struct Triangle
{
    /* Indices into the input points. */
    int a = 0;
    int b = 0;
    int c = 0;
};

/*
 * Triangulates `n` points. Degenerate input -- fewer than three points, or all
 * of them coincident -- yields no triangles.
 */
inline std::vector<Triangle> delaunay(const double *xs, const double *ys, std::size_t n)
{
    std::vector<Triangle> result;
    if (n < 3) {
        return result;
    }

    /* Working copy, with room for the three super-triangle corners at the end. */
    std::vector<double> px(xs, xs + n);
    std::vector<double> py(ys, ys + n);

    double xmin = px[0], xmax = px[0], ymin = py[0], ymax = py[0];
    for (std::size_t i = 1; i < n; ++i) {
        xmin = std::min(xmin, px[i]);
        xmax = std::max(xmax, px[i]);
        ymin = std::min(ymin, py[i]);
        ymax = std::max(ymax, py[i]);
    }
    const double dmax = std::max(xmax - xmin, ymax - ymin);
    if (!(dmax > 0.0)) {
        return result;
    }
    const double midx = 0.5 * (xmin + xmax);
    const double midy = 0.5 * (ymin + ymax);

    /* One triangle large enough to contain every point, so each insertion
       always has a cavity to work in. Its corners, and anything still attached
       to them, are dropped at the end. */
    const int s0 = static_cast<int>(n);
    px.push_back(midx - 20.0 * dmax);
    py.push_back(midy - dmax);
    px.push_back(midx);
    py.push_back(midy + 20.0 * dmax);
    px.push_back(midx + 20.0 * dmax);
    py.push_back(midy - dmax);

    std::vector<Triangle> tris{Triangle{s0, s0 + 1, s0 + 2}};

    auto in_circumcircle = [&](int p, const Triangle &t) {
        const double ax = px[t.a] - px[p], ay = py[t.a] - py[p];
        const double bx = px[t.b] - px[p], by = py[t.b] - py[p];
        const double cx = px[t.c] - px[p], cy = py[t.c] - py[p];

        const double det = (ax * ax + ay * ay) * (bx * cy - cx * by) -
                           (bx * bx + by * by) * (ax * cy - cx * ay) +
                           (cx * cx + cy * cy) * (ax * by - bx * ay);

        /* The determinant's sign follows the winding of (a, b, c), so it is
           compared against that rather than against zero. */
        const double area = (px[t.b] - px[t.a]) * (py[t.c] - py[t.a]) -
                            (px[t.c] - px[t.a]) * (py[t.b] - py[t.a]);
        return area > 0.0 ? det > 0.0 : det < 0.0;
    };

    struct Edge
    {
        int u, v;
    };

    for (int i = 0; i < static_cast<int>(n); ++i) {
        std::vector<Edge> boundary;
        std::vector<Triangle> kept;
        kept.reserve(tris.size());

        for (const Triangle &t : tris) {
            if (in_circumcircle(i, t)) {
                boundary.push_back({t.a, t.b});
                boundary.push_back({t.b, t.c});
                boundary.push_back({t.c, t.a});
            } else {
                kept.push_back(t);
            }
        }

        /* An edge shared by two removed triangles is interior to the cavity and
           must not be relinked; only edges seen once bound it. */
        for (std::size_t e = 0; e < boundary.size(); ++e) {
            bool shared = false;
            for (std::size_t f = 0; f < boundary.size(); ++f) {
                if (e == f) {
                    continue;
                }
                if ((boundary[e].u == boundary[f].v && boundary[e].v == boundary[f].u) ||
                    (boundary[e].u == boundary[f].u && boundary[e].v == boundary[f].v)) {
                    shared = true;
                    break;
                }
            }
            if (!shared) {
                kept.push_back(Triangle{boundary[e].u, boundary[e].v, i});
            }
        }

        tris.swap(kept);
    }

    for (const Triangle &t : tris) {
        if (t.a >= s0 || t.b >= s0 || t.c >= s0) {
            continue;
        }
        result.push_back(t);
    }
    return result;
}

/*
 * The unique undirected edges, each with the smaller index first -- matching
 * matplotlib's Triangulation.edges, which is what triplot draws.
 */
inline std::vector<std::pair<int, int>> triangulation_edges(const std::vector<Triangle> &tris)
{
    std::vector<std::pair<int, int>> edges;
    edges.reserve(tris.size() * 3);
    for (const Triangle &t : tris) {
        const int e[3][2] = {{t.a, t.b}, {t.b, t.c}, {t.c, t.a}};
        for (const auto &pair : e) {
            edges.emplace_back(std::min(pair[0], pair[1]), std::max(pair[0], pair[1]));
        }
    }
    std::sort(edges.begin(), edges.end());
    edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
    return edges;
}

} // namespace vpl

#endif /* VPL_CORE_DELAUNAY_H */
