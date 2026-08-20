/*
 * Colour specs and Matlab-style format strings.
 *
 * Ported from matplotlib's lib/matplotlib/colors.py, _color_data.py and
 * axes/_base.py (_process_plot_format).
 * Copyright (c) 2012- Matplotlib Development Team; All Rights Reserved.
 * See THIRD-PARTY-NOTICES.
 */

#ifndef VPL_CORE_COLORS_H
#define VPL_CORE_COLORS_H

#include <string>

#include "markers.h"
#include "renderer.h"

namespace vpl
{

/*
 * Accepts what matplotlib accepts, in rough order of how often it shows up:
 *
 *   "C0" .. "C9"      the property cycle
 *   "tab:blue" etc.   the tableau palette
 *   "#rgb", "#rrggbb", "#rrggbbaa"
 *   "r", "g", "b", "c", "m", "y", "k", "w"
 *   "0.5"             a grayscale level
 *   "red", "green", ... a subset of the CSS names
 *   "none"            fully transparent
 *
 * Returns false and leaves `out` untouched when the spec is not recognized,
 * so callers can report the bad input instead of silently drawing black.
 */
bool parse_color(const std::string &spec, Color &out);

/*
 * A Matlab-style format string such as "r--o", "g:", "ko".
 *
 * matplotlib's _process_plot_format accepts colour, line style and marker in
 * any order, which is why this cannot simply scan left to right: "1" is a valid
 * marker in matplotlib and a valid grayscale prefix, and "--" must be matched
 * before "-". Two-character line styles are therefore tried first.
 *
 * A field the string does not mention is left untouched in the out parameters,
 * so the caller's defaults survive. `has_marker` and `has_linestyle` report
 * whether the string actually specified them, which matters because "o" alone
 * means markers with NO connecting line -- matplotlib's rule.
 */
bool parse_format(const std::string &fmt,
                  Color &color,
                  bool &has_color,
                  LineStyle &linestyle,
                  bool &has_linestyle,
                  MarkerStyle &marker,
                  bool &has_marker);

} // namespace vpl

#endif /* VPL_CORE_COLORS_H */
