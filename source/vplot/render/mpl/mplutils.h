/* -*- mode: c++; c-basic-offset: 4 -*- */

/*
 * Derived from matplotlib's src/mplutils.h.
 * Copyright (c) 2012- Matplotlib Development Team; All Rights Reserved.
 * Licensed under the matplotlib license; see THIRD-PARTY-NOTICES.
 *
 * Changes from the original: the <Python.h> include and the _POSIX_C_SOURCE /
 * _XOPEN_SOURCE / _XPG undef blocks are gone (those only existed to work around
 * Python.h redefining them), and the shape checks now throw
 * std::invalid_argument instead of py::value_error. The checks stay templated
 * on the array type, so they still accept anything exposing ndim()/shape()/
 * size() -- array::scalar, array::empty and vpl::ArrayView alike.
 */

#ifndef MPLUTILS_H
#define MPLUTILS_H

inline int mpl_round_to_int(double v)
{
    return (int)(v + ((v >= 0.0) ? 0.5 : -0.5));
}

inline double mpl_round(double v)
{
    return (double)mpl_round_to_int(v);
}

// 'kind' codes for paths.
enum {
    STOP = 0,
    MOVETO = 1,
    LINETO = 2,
    CURVE3 = 3,
    CURVE4 = 4,
    CLOSEPOLY = 0x4f
};

#ifdef __cplusplus

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>

// Helper for std::visit.
template<typename... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<typename... Ts> overloaded(Ts...) -> overloaded<Ts...>;

// Check that array has shape (N, d1) or (N, d1, d2).
template<typename T>
inline void check_trailing_shape(T array, char const* name, long d1)
{
    if (array.ndim() != 2) {
        throw std::invalid_argument(
            "Expected 2-dimensional array, got " + std::to_string(array.ndim()));
    }
    if (array.size() == 0) {
        // Sometimes things come through as atleast_2d, etc., but they're empty, so
        // don't bother enforcing the trailing shape.
        return;
    }
    if (array.shape(1) != d1) {
        throw std::invalid_argument(
            std::string(name) + " must have shape (N, " + std::to_string(d1) +
            "), got (" + std::to_string(array.shape(0)) + ", " +
            std::to_string(array.shape(1)) + ")");
    }
}

template<typename T>
inline void check_trailing_shape(T array, char const* name, long d1, long d2)
{
    if (array.ndim() != 3) {
        throw std::invalid_argument(
            "Expected 3-dimensional array, got " + std::to_string(array.ndim()));
    }
    if (array.size() == 0) {
        // Sometimes things come through as atleast_3d, etc., but they're empty, so
        // don't bother enforcing the trailing shape.
        return;
    }
    if (array.shape(1) != d1 || array.shape(2) != d2) {
        throw std::invalid_argument(
            std::string(name) + " must have shape (N, " + std::to_string(d1) + ", " +
            std::to_string(d2) + "), got (" + std::to_string(array.shape(0)) + ", " +
            std::to_string(array.shape(1)) + ", " + std::to_string(array.shape(2)) + ")");
    }
}

// In most cases, code should use safe_first_shape(obj) instead of obj.shape(0),
// since safe_first_shape(obj) == 0 when any dimension is 0. Overloads live next
// to each array type: array::scalar and array::empty in array.h,
// vpl::ArrayView in vpl_array.h.
template <typename T, std::size_t N>
constexpr std::size_t
safe_first_shape(const std::array<T, N> &)
{
    return N;
}

#endif

#endif
