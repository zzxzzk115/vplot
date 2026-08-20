/* -*- mode: c++; c-basic-offset: 4 -*- */

/*
 * Derived from matplotlib's src/py_adaptors.h.
 * Copyright (c) 2012- Matplotlib Development Team; All Rights Reserved.
 * Licensed under the matplotlib license; see THIRD-PARTY-NOTICES.
 *
 * The original adapts NumPy arrays to the interfaces Agg and matplotlib's
 * renderer templates expect. This version adapts plain C arrays instead, which
 * is the whole reason the rest of the rendering core needs no changes: the
 * templates in path_converters.h, _path.h and _backend_agg.h are written
 * against these duck-typed surfaces, not against Python types.
 */

#ifndef VPL_ADAPTORS_H
#define VPL_ADAPTORS_H

#include <cstddef>
#include <cstdint>
#include <stdexcept>

#include "agg_basics.h"

namespace mpl
{

/************************************************************
 * mpl::PathIterator is the bridge between vertex data and Agg. Given a
 * vertices array (n x 2, row-major) and an optional codes array (n), it walks
 * them using Agg's vertex source interface:
 *
 *     unsigned vertex(double* x, double* y)
 *
 * Non-owning: both arrays must outlive the iterator.
 */
class PathIterator
{
    /* n x 2 row-major; m_vertices[2*i] is x, m_vertices[2*i + 1] is y. */
    const double *m_vertices;
    /* One code per vertex, or null for an implicit polyline. */
    const uint8_t *m_codes;

    unsigned m_total_vertices;
    unsigned m_iterator;

    /* This class doesn't simplify anything itself; the value is carried here
       because that is where path_converters.h reads it from. */
    bool m_should_simplify;
    double m_simplify_threshold;

  public:
    inline PathIterator()
        : m_vertices(nullptr),
          m_codes(nullptr),
          m_total_vertices(0),
          m_iterator(0),
          m_should_simplify(false),
          m_simplify_threshold(1.0 / 9.0)
    {
    }

    inline PathIterator(const double *vertices,
                        const uint8_t *codes,
                        unsigned total_vertices,
                        bool should_simplify = false,
                        double simplify_threshold = 1.0 / 9.0)
        : m_iterator(0)
    {
        set(vertices, codes, total_vertices, should_simplify, simplify_threshold);
    }

    inline void set(const double *vertices,
                    const uint8_t *codes,
                    unsigned total_vertices,
                    bool should_simplify = false,
                    double simplify_threshold = 1.0 / 9.0)
    {
        if (total_vertices != 0 && vertices == nullptr) {
            throw std::invalid_argument(
                "PathIterator: null vertices with non-zero vertex count");
        }

        m_vertices = vertices;
        m_codes = codes;
        m_total_vertices = total_vertices;
        m_should_simplify = should_simplify;
        m_simplify_threshold = simplify_threshold;
        m_iterator = 0;
    }

    inline unsigned vertex(double *x, double *y)
    {
        if (m_iterator >= m_total_vertices) {
            *x = 0.0;
            *y = 0.0;
            return agg::path_cmd_stop;
        }

        const std::size_t idx = m_iterator++;

        *x = m_vertices[2 * idx];
        *y = m_vertices[2 * idx + 1];

        if (m_codes) {
            return m_codes[idx];
        }
        return idx == 0 ? agg::path_cmd_move_to : agg::path_cmd_line_to;
    }

    inline void rewind(unsigned path_id)
    {
        m_iterator = path_id;
    }

    inline unsigned total_vertices() const
    {
        return m_total_vertices;
    }

    inline bool should_simplify() const
    {
        return m_should_simplify;
    }

    inline double simplify_threshold() const
    {
        return m_simplify_threshold;
    }

    inline bool has_codes() const
    {
        return m_codes != nullptr;
    }

    /* Identity used by the renderer to cache clip paths; any stable pointer
       distinct per path will do. */
    inline void *get_id()
    {
        return (void *)m_vertices;
    }
};

/************************************************************
 * mpl::PathGenerator yields one PathIterator per index, wrapping around when
 * asked for more paths than it holds -- the cycling behaviour
 * draw_path_collection relies on. Non-owning.
 */
class PathGenerator
{
    const PathIterator *m_paths;
    std::size_t m_npaths;

  public:
    typedef PathIterator path_iterator;

    PathGenerator() : m_paths(nullptr), m_npaths(0) {}

    PathGenerator(const PathIterator *paths, std::size_t npaths)
        : m_paths(paths), m_npaths(npaths)
    {
    }

    void set(const PathIterator *paths, std::size_t npaths)
    {
        m_paths = paths;
        m_npaths = npaths;
    }

    std::size_t num_paths() const
    {
        return m_npaths;
    }

    std::size_t size() const
    {
        return m_npaths;
    }

    path_iterator operator()(std::size_t i) const
    {
        if (m_npaths == 0) {
            return path_iterator();
        }
        return m_paths[i % m_npaths];
    }
};

} // namespace mpl

#endif /* VPL_ADAPTORS_H */
