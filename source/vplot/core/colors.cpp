#include "colors.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

#include "figure.h" /* tab10_palette */

namespace vpl
{
namespace
{

struct NamedColor
{
    const char *name;
    double r, g, b;
};

/* _color_data.BASE_COLORS. Deliberately NOT the CSS values -- matplotlib's 'g'
   is a dark green and 'c'/'m'/'y' are three-quarter saturated. */
const NamedColor kBaseColors[] = {
    {"b", 0.0, 0.0, 1.0},   {"g", 0.0, 0.5, 0.0},     {"r", 1.0, 0.0, 0.0},
    {"c", 0.0, 0.75, 0.75}, {"m", 0.75, 0.0, 0.75},   {"y", 0.75, 0.75, 0.0},
    {"k", 0.0, 0.0, 0.0},   {"w", 1.0, 1.0, 1.0},
};

/* _color_data.TABLEAU_COLORS, in cycle order so "C<n>" can index it. */
const NamedColor kTableau[] = {
    {"tab:blue", 0.121569, 0.466667, 0.705882},
    {"tab:orange", 1.0, 0.498039, 0.054902},
    {"tab:green", 0.172549, 0.627451, 0.172549},
    {"tab:red", 0.839216, 0.152941, 0.156863},
    {"tab:purple", 0.580392, 0.403922, 0.741176},
    {"tab:brown", 0.549020, 0.337255, 0.294118},
    {"tab:pink", 0.890196, 0.466667, 0.760784},
    {"tab:gray", 0.498039, 0.498039, 0.498039},
    {"tab:olive", 0.737255, 0.741176, 0.133333},
    {"tab:cyan", 0.090196, 0.745098, 0.811765},
};

/* A working subset of CSS4_COLORS. The full table is 148 entries in
   _color_data.py and can be transcribed wholesale when something needs it. */
const NamedColor kCssColors[] = {
    {"red", 1.0, 0.0, 0.0},           {"green", 0.0, 0.501961, 0.0},
    {"blue", 0.0, 0.0, 1.0},          {"black", 0.0, 0.0, 0.0},
    {"white", 1.0, 1.0, 1.0},         {"gray", 0.501961, 0.501961, 0.501961},
    {"grey", 0.501961, 0.501961, 0.501961}, {"orange", 1.0, 0.647059, 0.0},
    {"purple", 0.501961, 0.0, 0.501961}, {"brown", 0.647059, 0.164706, 0.164706},
    {"pink", 1.0, 0.752941, 0.796078}, {"olive", 0.501961, 0.501961, 0.0},
    {"cyan", 0.0, 1.0, 1.0},          {"magenta", 1.0, 0.0, 1.0},
    {"yellow", 1.0, 1.0, 0.0},        {"navy", 0.0, 0.0, 0.501961},
    {"teal", 0.0, 0.501961, 0.501961}, {"lime", 0.0, 1.0, 0.0},
    {"maroon", 0.501961, 0.0, 0.0},   {"silver", 0.752941, 0.752941, 0.752941},
    {"gold", 1.0, 0.843137, 0.0},     {"indigo", 0.294118, 0.0, 0.509804},
};

int hex_value(char c)
{
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
    return -1;
}

bool parse_hex(const std::string &s, Color &out)
{
    const std::size_t n = s.size();
    if (n != 4 && n != 7 && n != 9) {
        return false;
    }

    std::vector<int> nibbles;
    nibbles.reserve(n - 1);
    for (std::size_t i = 1; i < n; ++i) {
        const int v = hex_value(s[i]);
        if (v < 0) {
            return false;
        }
        nibbles.push_back(v);
    }

    auto channel = [&](std::size_t index) {
        /* #rgb is shorthand: each nibble is doubled, so 'f' means 0xff. */
        if (nibbles.size() == 3) {
            return (nibbles[index] * 16 + nibbles[index]) / 255.0;
        }
        return (nibbles[2 * index] * 16 + nibbles[2 * index + 1]) / 255.0;
    };

    out.r = channel(0);
    out.g = channel(1);
    out.b = channel(2);
    out.a = nibbles.size() == 8 ? channel(3) : 1.0;
    return true;
}

std::string to_lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

} // namespace

bool parse_color(const std::string &spec, Color &out)
{
    if (spec.empty()) {
        return false;
    }

    if (spec[0] == '#') {
        return parse_hex(spec, out);
    }

    const std::string lower = to_lower(spec);

    if (lower == "none") {
        out = Color::none();
        return true;
    }

    /* "C0".."C9" index the property cycle. Case-sensitive in matplotlib; either
       case is accepted here. */
    if (lower.size() == 2 && lower[0] == 'c' && lower[1] >= '0' && lower[1] <= '9') {
        const NamedColor &c = kTableau[lower[1] - '0'];
        out = Color{c.r, c.g, c.b, 1.0};
        return true;
    }

    for (const NamedColor &c : kBaseColors) {
        if (lower == c.name) {
            out = Color{c.r, c.g, c.b, 1.0};
            return true;
        }
    }
    for (const NamedColor &c : kTableau) {
        if (lower == c.name) {
            out = Color{c.r, c.g, c.b, 1.0};
            return true;
        }
    }
    for (const NamedColor &c : kCssColors) {
        if (lower == c.name) {
            out = Color{c.r, c.g, c.b, 1.0};
            return true;
        }
    }

    /* A bare number in [0, 1] is a grayscale level. */
    char *end = nullptr;
    const double gray = std::strtod(spec.c_str(), &end);
    if (end != nullptr && *end == '\0' && end != spec.c_str() && gray >= 0.0 && gray <= 1.0) {
        out = Color{gray, gray, gray, 1.0};
        return true;
    }

    return false;
}

bool parse_format(const std::string &fmt,
                  Color &color,
                  bool &has_color,
                  LineStyle &linestyle,
                  bool &has_linestyle,
                  MarkerStyle &marker,
                  bool &has_marker)
{
    has_color = false;
    has_linestyle = false;
    has_marker = false;

    std::size_t i = 0;
    while (i < fmt.size()) {
        const char c = fmt[i];

        /* Two-character line styles must be tried before the one-character
           ones, or "--" parses as two solid lines and "-." as a line plus a
           point marker. */
        if (i + 1 < fmt.size()) {
            const std::string pair = fmt.substr(i, 2);
            if (pair == "--") {
                linestyle = LineStyle::Dashed;
                has_linestyle = true;
                i += 2;
                continue;
            }
            if (pair == "-.") {
                linestyle = LineStyle::DashDot;
                has_linestyle = true;
                i += 2;
                continue;
            }
        }

        if (c == '-') {
            linestyle = LineStyle::Solid;
            has_linestyle = true;
            ++i;
            continue;
        }
        if (c == ':') {
            linestyle = LineStyle::Dotted;
            has_linestyle = true;
            ++i;
            continue;
        }

        MarkerStyle m = MarkerStyle::None;
        switch (c) {
            case 'o': m = MarkerStyle::Circle; break;
            case 's': m = MarkerStyle::Square; break;
            case '^': m = MarkerStyle::TriangleUp; break;
            case 'v': m = MarkerStyle::TriangleDown; break;
            case 'D': m = MarkerStyle::Diamond; break;
            case '+': m = MarkerStyle::Plus; break;
            case 'x': m = MarkerStyle::Cross; break;
            case '*': m = MarkerStyle::Star; break;
            default: break;
        }
        if (m != MarkerStyle::None) {
            marker = m;
            has_marker = true;
            ++i;
            continue;
        }

        /* Anything left has to be a colour: a single letter here, since the
           long forms are not legal inside a format string. */
        Color parsed;
        if (parse_color(std::string(1, c), parsed)) {
            color = parsed;
            has_color = true;
            ++i;
            continue;
        }

        return false;
    }

    /* matplotlib's rule: a format that names a marker but no line style draws
       markers only. Without this, "o" would draw a connected line as well. */
    if (has_marker && !has_linestyle) {
        linestyle = LineStyle::None;
        has_linestyle = true;
    }

    return true;
}

} // namespace vpl
