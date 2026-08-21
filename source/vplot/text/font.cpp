#include "font.h"

#include "dejavu_sans_ttf.h"

#include <algorithm>
#include <cmath>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include FT_TRUETYPE_TABLES_H

namespace vpl
{
namespace
{

FT_Library as_library(void *p) { return static_cast<FT_Library>(p); }
FT_Face as_face(void *p) { return static_cast<FT_Face>(p); }

/* 26.6 fixed point to whole pixels. */
inline double from_26_6(long v) { return static_cast<double>(v) / 64.0; }

} // namespace

std::vector<uint32_t> utf8_decode(const std::string &s)
{
    std::vector<uint32_t> out;
    out.reserve(s.size());

    std::size_t i = 0;
    while (i < s.size()) {
        const auto byte = static_cast<unsigned char>(s[i]);
        uint32_t cp = 0;
        int extra = 0;

        if (byte < 0x80) {
            cp = byte;
        } else if ((byte & 0xE0) == 0xC0) {
            cp = byte & 0x1Fu;
            extra = 1;
        } else if ((byte & 0xF0) == 0xE0) {
            cp = byte & 0x0Fu;
            extra = 2;
        } else if ((byte & 0xF8) == 0xF0) {
            cp = byte & 0x07u;
            extra = 3;
        } else {
            out.push_back(0xFFFD);
            ++i;
            continue;
        }

        if (i + extra >= s.size()) {
            out.push_back(0xFFFD);
            break;
        }

        bool ok = true;
        for (int k = 1; k <= extra; ++k) {
            const auto cont = static_cast<unsigned char>(s[i + k]);
            if ((cont & 0xC0) != 0x80) {
                ok = false;
                break;
            }
            cp = (cp << 6) | (cont & 0x3Fu);
        }

        out.push_back(ok ? cp : 0xFFFD);
        i += ok ? static_cast<std::size_t>(extra) + 1 : 1;
    }

    return out;
}

FontEngine::FontEngine()
{
    FT_Library lib = nullptr;
    if (FT_Init_FreeType(&lib) == 0) {
        m_library = lib;
    }
}

FontEngine::~FontEngine()
{
    if (m_face != nullptr) {
        FT_Done_Face(as_face(m_face));
    }
    if (m_library != nullptr) {
        FT_Done_FreeType(as_library(m_library));
    }
}

bool FontEngine::load_face(const std::string &path)
{
    if (m_library == nullptr) {
        return false;
    }
    if (m_face != nullptr) {
        FT_Done_Face(as_face(m_face));
        m_face = nullptr;
        m_face_path.clear();
    }

    FT_Face face = nullptr;
    if (FT_New_Face(as_library(m_library), path.c_str(), 0, &face) != 0) {
        return false;
    }

    m_face = face;
    m_face_path = path;
    return true;
}

bool FontEngine::load_default_face()
{
    if (m_library == nullptr) {
        return false;
    }
    if (m_face != nullptr) {
        FT_Done_Face(as_face(m_face));
        m_face = nullptr;
        m_face_path.clear();
    }

    /* The default face is compiled in rather than searched for. It used to be a
       walk up from the working directory to assets/fonts, which works in this
       repo and nowhere else -- a consumer linking vplot as a package got a
       library that drew axes correctly and every label not at all, because text
       is skipped, not failed, when there is no face. Embedding also pins the
       metrics: matplotlib lays out against this exact face, so a figure is the
       same on a machine that has never heard of DejaVu.

       FT_New_Memory_Face does not copy, so the buffer has to outlive the face.
       kDejaVuSans has static storage duration, which satisfies that. */
    FT_Face face = nullptr;
    if (FT_New_Memory_Face(as_library(m_library),
                           reinterpret_cast<const FT_Byte *>(kDejaVuSans),
                           static_cast<FT_Long>(kDejaVuSansSize),
                           0,
                           &face) != 0) {
        return false;
    }

    m_face = face;
    m_face_path = "<embedded DejaVuSans>";
    return true;
}

bool FontEngine::set_size(const FontProps &font, double dpi) const
{
    if (m_face == nullptr) {
        return false;
    }
    /* FreeType takes the size in 26.6 points and applies the dpi itself, which
       is the same points -> pixels scaling matplotlib does. */
    const FT_F26Dot6 size = static_cast<FT_F26Dot6>(font.size * 64.0 + 0.5);
    const auto res = static_cast<FT_UInt>(dpi + 0.5);
    return FT_Set_Char_Size(as_face(m_face), 0, size, res, res) == 0;
}

namespace
{

/* mathtext's SHRINK_FACTOR: each nesting level is drawn at 0.7 of the size. */
constexpr double kShrinkFactor = 0.7;

/*
 * DejaVuSansFontConstants, as fractions of the x-height (design value 1120
 * units). These come from the font's TeX table and cannot be recovered at
 * runtime.
 */
constexpr double kSupDrop = 790.527 / 1120.0;
constexpr double kSup1 = 845.824 / 1120.0;
constexpr double kDelta = 0.025;       /* FontConstantsBase.delta */
constexpr double kScriptSpace = 0.05;  /* FontConstantsBase.script_space */

/* Strips one layer of "\name{...}" wrappers, which is all \mathdefault is. */
bool strip_command(std::string &body, const char *name)
{
    const std::string open = std::string("\\") + name + "{";
    if (body.compare(0, open.size(), open) != 0 || body.back() != '}') {
        return false;
    }
    body = body.substr(open.size(), body.size() - open.size() - 1);
    return true;
}

/* In math mode a hyphen is the minus sign: matplotlib maps '-' to U+2212. */
std::string math_symbols(const std::string &in)
{
    static const char *kMinusSign = "\xE2\x88\x92";
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
        if (c == '-') {
            out += kMinusSign;
        } else {
            out += c;
        }
    }
    return out;
}

} // namespace

void FontEngine::string_metrics(const std::string &utf8,
                               double size,
                               double dpi,
                               double &advance,
                               double &ink_top,
                               double &ink_bottom) const
{
    advance = 0.0;
    ink_top = 0.0;
    ink_bottom = 0.0;

    FontProps props;
    props.size = size;
    if (!set_size(props, dpi)) {
        return;
    }

    FT_Face face = as_face(m_face);
    const std::vector<uint32_t> codepoints = utf8_decode(utf8);
    const bool has_kerning = FT_HAS_KERNING(face) != 0;
    FT_UInt previous = 0;

    /*
     * The vertical extent is the string's ink bounding box, not the font's
     * ascender and descender: "0.04" measures 10 pixels tall with zero descent,
     * while "Apg" measures 13 with a descent of 3. Glyphs with no outline (the
     * space) are part of the union, pinning yMin <= 0 <= yMax.
     */
    long ink_min = 0;
    long ink_max = 0;
    bool have_ink = false;

    for (uint32_t cp : codepoints) {
        const FT_UInt index = FT_Get_Char_Index(face, cp);
        if (has_kerning && previous != 0 && index != 0) {
            FT_Vector delta;
            if (FT_Get_Kerning(face, previous, index, FT_KERNING_UNFITTED, &delta) == 0) {
                advance += from_26_6(delta.x);
            }
        }
        if (FT_Load_Glyph(face, index, FT_LOAD_DEFAULT) == 0) {
            advance += from_26_6(face->glyph->advance.x);

            /* The grid-fitted glyph box, which is what FT_Glyph_Get_CBox
               reports for a hinted glyph. Horizontal text keeps the pen on the
               baseline, so no pen offset enters the vertical union. */
            const FT_Glyph_Metrics &m = face->glyph->metrics;
            const long y_max = m.horiBearingY;
            const long y_min = m.horiBearingY - m.height;
            if (have_ink) {
                ink_min = std::min(ink_min, y_min);
                ink_max = std::max(ink_max, y_max);
            } else {
                ink_min = y_min;
                ink_max = y_max;
                have_ink = true;
            }
        }
        previous = index;
    }

    if (have_ink) {
        ink_top = from_26_6(ink_max);
        ink_bottom = from_26_6(ink_min);
    }
}

/* A run's metrics: measured at metrics_size, then scaled -- see TextRun. */
void FontEngine::run_metrics(const TextRun &run,
                             double dpi,
                             double &advance,
                             double &ink_top,
                             double &ink_bottom) const
{
    string_metrics(run.text, run.metrics_size, dpi, advance, ink_top, ink_bottom);
    advance *= run.metrics_scale;
    ink_top *= run.metrics_scale;
    ink_bottom *= run.metrics_scale;
}

double FontEngine::string_advance(const std::string &utf8, double size, double dpi) const
{
    double advance = 0.0;
    double top = 0.0;
    double bottom = 0.0;
    string_metrics(utf8, size, dpi, advance, top, bottom);
    return advance;
}

double FontEngine::x_height(double size, double dpi) const
{
    double advance = 0.0;
    double top = 0.0;
    double bottom = 0.0;
    string_metrics("x", size, dpi, advance, top, bottom);
    return top;
}

bool is_mathtext(const std::string &utf8)
{
    return utf8.size() >= 2 && utf8.front() == '$' && utf8.back() == '$';
}

void FontEngine::typo_metrics(double size,
                              double dpi,
                              double &ascent,
                              double &descent,
                              double &line_gap) const
{
    ascent = 0.0;
    descent = 0.0;
    line_gap = 0.0;
    if (m_face == nullptr) {
        return;
    }

    FT_Face face = as_face(m_face);
    if (face->units_per_EM == 0) {
        return;
    }

    /* matplotlib reads sTypoAscender/sTypoDescender/sTypoLineGap from OS/2 and
       falls back to hhea, rescaling them itself rather than asking FreeType --
       so these are NOT the rounded values in size->metrics. */
    const double scale = size * dpi / 72.0 / static_cast<double>(face->units_per_EM);

    const auto *os2 = static_cast<const TT_OS2 *>(FT_Get_Sfnt_Table(face, FT_SFNT_OS2));
    if (os2 != nullptr && os2->version != 0xFFFFu) {
        ascent = os2->sTypoAscender * scale;
        descent = -os2->sTypoDescender * scale;
        line_gap = os2->sTypoLineGap * scale;
    } else {
        ascent = face->ascender * scale;
        descent = -face->descender * scale;
    }
}

void FontEngine::layout_line(const std::string &utf8,
                             const FontProps &font,
                             double dpi,
                             std::vector<TextRun> &runs,
                             double &advance,
                             double &ink_top,
                             double &ink_bottom) const
{
    runs.clear();
    advance = 0.0;
    ink_top = 0.0;
    ink_bottom = 0.0;

    auto plain = [&](const std::string &text) {
        runs.push_back(TextRun{text, font.size, 0.0, 0.0, font.size, 1.0});
        string_metrics(text, font.size, dpi, advance, ink_top, ink_bottom);
    };

    if (!is_mathtext(utf8)) {
        plain(utf8);
        return;
    }

    std::string body = utf8.substr(1, utf8.size() - 2);
    strip_command(body, "mathdefault");

    /* The subset: a nucleus, then one superscript, either braced or a single
       character. Anything else falls through as literal text. */
    const std::size_t caret = body.find('^');
    if (caret == std::string::npos || caret == 0 || caret + 1 >= body.size()) {
        plain(math_symbols(body));
        return;
    }

    std::string script = body.substr(caret + 1);
    if (script.front() == '{') {
        const std::size_t close = script.find('}');
        script = close == std::string::npos ? script.substr(1) : script.substr(1, close - 1);
    } else {
        script = script.substr(0, 1);
    }

    const std::string nucleus = math_symbols(body.substr(0, caret));
    script = math_symbols(script);

    const double script_size = font.size * kShrinkFactor;

    /*
     * Parser.subsuperscript, for the one case a log axis produces: a nucleus
     * with a superscript and no subscript.
     *
     *     shift_up = max(nucleus_ink_top - supdrop * shrunk_x_height,
     *                    sup1 * x_height,
     *                    script_depth + x_height / 4)
     *
     * The kern before the script shrinks with the sublist; the trailing space
     * is added afterwards and does not.
     */
    const double x_height = this->x_height(font.size, dpi);
    const double shrunk_x_height = this->x_height(script_size, dpi);

    double nucleus_advance = 0.0;
    double nucleus_top = 0.0;
    double nucleus_bottom = 0.0;
    string_metrics(nucleus, font.size, dpi, nucleus_advance, nucleus_top, nucleus_bottom);

    /* Measured at the FULL size and scaled, which is what Char.shrink does. */
    double script_advance = 0.0;
    double script_top = 0.0;
    double script_bottom = 0.0;
    string_metrics(script, font.size, dpi, script_advance, script_top, script_bottom);
    script_advance *= kShrinkFactor;
    script_top *= kShrinkFactor;
    script_bottom *= kShrinkFactor;

    const double script_depth = std::max(0.0, -script_bottom);
    const double shift_up = std::max(std::max(nucleus_top - kSupDrop * shrunk_x_height,
                                              kSup1 * x_height),
                                     script_depth + x_height / 4.0);

    const double kern = kDelta * x_height * kShrinkFactor;

    runs.push_back(TextRun{nucleus, font.size, 0.0, 0.0, font.size, 1.0});
    runs.push_back(TextRun{script, script_size, nucleus_advance + kern, shift_up,
                           font.size, kShrinkFactor});

    advance = nucleus_advance + kern + script_advance + kScriptSpace * x_height;
    ink_top = std::max(nucleus_top, shift_up + script_top);
    ink_bottom = std::min(nucleus_bottom, shift_up + script_bottom);
}

TextLayout FontEngine::layout(const std::string &utf8,
                              const FontProps &font,
                              double dpi,
                              HAlign multialignment) const
{
    TextLayout out;

    /* Split on newlines first; everything below stacks the pieces. */
    std::vector<std::string> lines;
    std::size_t from = 0;
    for (;;) {
        const std::size_t nl = utf8.find('\n', from);
        if (nl == std::string::npos) {
            lines.push_back(utf8.substr(from));
            break;
        }
        lines.push_back(utf8.substr(from, nl - from));
        from = nl + 1;
    }

    double typo_ascent = 0.0;
    double typo_descent = 0.0;
    double line_gap = 0.0;
    typo_metrics(font.size, dpi, typo_ascent, typo_descent, line_gap);

    /* One line gets no gap: matplotlib zeroes it rather than letting a single
       line take the room two would need. */
    if (lines.size() == 1) {
        line_gap = 0.0;
    }

    struct Line
    {
        std::vector<TextRun> runs;
        double advance = 0.0;
        double ascent = 0.0; /* padded: max(ink, typographic) + gap/2 */
        double descent = 0.0;
    };

    std::vector<Line> laid;
    laid.reserve(lines.size());
    for (const std::string &text : lines) {
        Line line;
        double ink_top = 0.0;
        double ink_bottom = 0.0;
        layout_line(text, font, dpi, line.runs, line.advance, ink_top, ink_bottom);

        /* Text._get_layout, at the default linespacing of "normal": a line is
           never counted shorter than the font's own typographic ascent and
           descent, so "0.04" still aligns as though it had a descender. */
        line.ascent = std::max(ink_top, typo_ascent) + line_gap / 2.0;
        line.descent = std::max(-ink_bottom, typo_descent) + line_gap / 2.0;
        out.advance = std::max(out.advance, line.advance);
        laid.push_back(std::move(line));
    }

    /*
     * Stack upwards from the last line's baseline (the string's origin):
     * consecutive baselines sit (upper descent + lower ascent) apart, and the
     * box top is the first line's ascent above its own baseline.
     */
    double y = 0.0;
    for (std::size_t i = laid.size(); i-- > 0;) {
        Line &line = laid[i];

        /* multialignment defaults to the horizontal alignment, so a centred
           label centres its lines against each other too. */
        double dx = 0.0;
        switch (multialignment) {
            case HAlign::Left: break;
            case HAlign::Right: dx = out.advance - line.advance; break;
            case HAlign::Center: dx = (out.advance - line.advance) / 2.0; break;
        }

        for (TextRun &run : line.runs) {
            run.dx += dx;
            run.dy += y;
            out.runs.push_back(run);
        }

        if (i > 0) {
            y += line.ascent + laid[i - 1].descent;
        } else {
            out.ascent = y + line.ascent;
        }
    }
    out.descent = laid.back().descent;

    return out;
}

TextExtent FontEngine::measure(const std::string &utf8, const FontProps &font, double dpi) const
{
    TextExtent extent;

    /* Runs, so that a math string measures as the box its parts actually
       cover -- a raised superscript makes the whole label taller. */
    const TextLayout laid = layout(utf8, font, dpi);
    extent.width = laid.advance;

    bool have_ink = false;
    double top = 0.0;
    double bottom = 0.0;
    for (const TextRun &run : laid.runs) {
        double advance = 0.0;
        double run_top = 0.0;
        double run_bottom = 0.0;
        run_metrics(run, dpi, advance, run_top, run_bottom);
        if (run_top == 0.0 && run_bottom == 0.0) {
            continue; /* no ink of its own */
        }
        run_top += run.dy;
        run_bottom += run.dy;
        if (have_ink) {
            top = std::max(top, run_top);
            bottom = std::min(bottom, run_bottom);
        } else {
            top = run_top;
            bottom = run_bottom;
            have_ink = true;
        }
    }

    extent.descent = have_ink ? -bottom : 0.0;
    extent.height = have_ink ? top - bottom : 0.0;

    return extent;
}

/*
 * matplotlib's Text._get_layout, at the default linespacing of "normal":
 *
 *     a = max(ink_ascent,  typo_ascent)
 *     d = max(ink_descent, typo_descent)
 *
 * so "0.04", which has no descender at all, still aligns as though it did.
 */
LineMetrics FontEngine::layout_metrics(const TextExtent &ink, const FontProps &font,
                                       double dpi) const
{
    LineMetrics out;
    out.ascent = ink.height - ink.descent;
    out.descent = ink.descent;

    if (m_face == nullptr) {
        return out;
    }
    FT_Face face = as_face(m_face);
    if (face->units_per_EM == 0) {
        return out;
    }

    /* matplotlib reads sTypoAscender/sTypoDescender from OS/2 and falls back to
       hhea, rescaling both to the size and dpi itself rather than asking
       FreeType -- so these are NOT the rounded values in size->metrics. */
    const double scale = font.size * dpi / 72.0 / static_cast<double>(face->units_per_EM);
    double typo_ascent = 0.0;
    double typo_descent = 0.0;

    const auto *os2 = static_cast<const TT_OS2 *>(FT_Get_Sfnt_Table(face, FT_SFNT_OS2));
    if (os2 != nullptr && os2->version != 0xFFFFu) {
        typo_ascent = os2->sTypoAscender * scale;
        typo_descent = -os2->sTypoDescender * scale;
    } else {
        typo_ascent = face->ascender * scale;
        typo_descent = -face->descender * scale;
    }

    out.ascent = std::max(out.ascent, typo_ascent);
    out.descent = std::max(out.descent, typo_descent);
    return out;
}

std::vector<GlyphImage> FontEngine::render_glyphs(const std::string &utf8,
                                                  const FontProps &font,
                                                  double dpi,
                                                  double angle_deg,
                                                  double origin_x,
                                                  double origin_y,
                                                  HAlign multialignment) const
{
    std::vector<GlyphImage> out;
    if (m_face == nullptr) {
        return out;
    }

    FT_Face face = as_face(m_face);
    const TextLayout laid = layout(utf8, font, dpi, multialignment);

    for (const TextRun &run : laid.runs) {
    FontProps metrics_font = font;
    metrics_font.size = run.metrics_size;
    if (!set_size(metrics_font, dpi)) {
        continue;
    }

    const std::vector<uint32_t> codepoints = utf8_decode(run.text);
    const bool has_kerning = FT_HAS_KERNING(face) != 0;

    /* The layout runs unrotated and pen positions are rotated afterwards:
       kerning and advances are properties of the string, not the angle. It runs
       at the metrics size, scaled -- a shrunken superscript walks on full-size
       advances multiplied by 0.7. */
    std::vector<double> pen_offsets;
    pen_offsets.reserve(codepoints.size());
    {
        double pen = 0.0;
        FT_UInt previous = 0;
        for (uint32_t cp : codepoints) {
            const FT_UInt index = FT_Get_Char_Index(face, cp);
            if (has_kerning && previous != 0 && index != 0) {
                FT_Vector kern;
                if (FT_Get_Kerning(face, previous, index, FT_KERNING_UNFITTED, &kern) == 0) {
                    pen += from_26_6(kern.x);
                }
            }
            pen_offsets.push_back(pen * run.metrics_scale);
            if (FT_Load_Glyph(face, index, FT_LOAD_DEFAULT) == 0) {
                pen += from_26_6(face->glyph->advance.x);
            }
            previous = index;
        }
    }

    /* Now to the size the glyphs are actually drawn at. */
    FontProps run_font = font;
    run_font.size = run.size;
    if (!set_size(run_font, dpi)) {
        continue;
    }

    constexpr double kPi = 3.14159265358979323846;
    const double theta = angle_deg * kPi / 180.0;
    const double c = std::cos(theta);
    const double s = std::sin(theta);

    /* 16.16 fixed point, rounded -- the same matrix matplotlib builds. */
    FT_Matrix matrix;
    matrix.xx = static_cast<FT_Fixed>(std::lround(c * 65536.0));
    matrix.xy = static_cast<FT_Fixed>(std::lround(-s * 65536.0));
    matrix.yx = static_cast<FT_Fixed>(std::lround(s * 65536.0));
    matrix.yy = static_cast<FT_Fixed>(std::lround(c * 65536.0));

    /* A run's own offset rides along with the rotation, so a superscript stays
       above the baseline it belongs to however the label is turned. */
    const double run_x = origin_x + run.dx * c - run.dy * s;
    const double run_y = origin_y + run.dx * s + run.dy * c;

    for (std::size_t i = 0; i < codepoints.size(); ++i) {
        const FT_UInt index = FT_Get_Char_Index(face, codepoints[i]);
        const double dx = pen_offsets[i];

        /* The pen's ABSOLUTE position on the canvas in 26.6, so the glyph is
           rasterized at the sub-pixel offset it will occupy. FreeType's y
           points up, which is already vplot's display convention. */
        FT_Vector delta;
        delta.x = std::lround(64.0 * (run_x + dx * c));
        delta.y = std::lround(64.0 * (run_y + dx * s));
        FT_Set_Transform(face, &matrix, &delta);

        const FT_Error err = FT_Load_Glyph(face, index, FT_LOAD_RENDER);

        /* Leave the face untransformed for whoever renders next -- measure()
           and outline() both assume no transform. */
        FT_Set_Transform(face, nullptr, nullptr);

        if (err != 0) {
            continue;
        }

        const FT_GlyphSlot slot = face->glyph;
        const FT_Bitmap &bitmap = slot->bitmap;
        if (bitmap.rows == 0 || bitmap.width == 0) {
            continue; /* a space carries no ink */
        }

        GlyphImage glyph;
        glyph.image = GrayImage(bitmap.rows, bitmap.width);
        glyph.left = slot->bitmap_left;
        glyph.top = slot->bitmap_top;
        for (unsigned r = 0; r < bitmap.rows; ++r) {
            for (unsigned col = 0; col < bitmap.width; ++col) {
                glyph.image(static_cast<int>(r), static_cast<int>(col)) =
                    bitmap.buffer[r * static_cast<unsigned>(bitmap.pitch) + col];
            }
        }
        out.push_back(std::move(glyph));
    }
    }

    return out;
}

namespace
{

/* FT_Outline_Decompose callbacks. FreeType hands back 26.6 fixed-point points
   in the glyph's own space (y up, origin on the baseline), which is already the
   convention vpl::Path wants -- only the pen offset and the 1/64 scale need
   applying. */
struct OutlineSink
{
    Path *path = nullptr;
    double offset_x = 0.0;
    /* Non-zero for a raised superscript run. */
    double offset_y = 0.0;
    bool contour_open = false;
};

int outline_move_to(const FT_Vector *to, void *user)
{
    auto *sink = static_cast<OutlineSink *>(user);
    if (sink->contour_open) {
        sink->path->close_poly();
    }
    sink->path->move_to(sink->offset_x + from_26_6(to->x),
                        sink->offset_y + from_26_6(to->y));
    sink->contour_open = true;
    return 0;
}

int outline_line_to(const FT_Vector *to, void *user)
{
    auto *sink = static_cast<OutlineSink *>(user);
    sink->path->line_to(sink->offset_x + from_26_6(to->x),
                        sink->offset_y + from_26_6(to->y));
    return 0;
}

int outline_conic_to(const FT_Vector *control, const FT_Vector *to, void *user)
{
    /* TrueType curves are quadratic, and matplotlib's CURVE3 code is quadratic
       too, so this maps across without degree elevation. */
    auto *sink = static_cast<OutlineSink *>(user);
    sink->path->curve3(sink->offset_x + from_26_6(control->x),
                       sink->offset_y + from_26_6(control->y),
                       sink->offset_x + from_26_6(to->x),
                       sink->offset_y + from_26_6(to->y));
    return 0;
}

int outline_cubic_to(const FT_Vector *c1, const FT_Vector *c2, const FT_Vector *to, void *user)
{
    auto *sink = static_cast<OutlineSink *>(user);
    sink->path->curve4(sink->offset_x + from_26_6(c1->x), sink->offset_y + from_26_6(c1->y),
                       sink->offset_x + from_26_6(c2->x), sink->offset_y + from_26_6(c2->y),
                       sink->offset_x + from_26_6(to->x), sink->offset_y + from_26_6(to->y));
    return 0;
}

} // namespace

bool FontEngine::outline(const std::string &utf8,
                         const FontProps &font,
                         double dpi,
                         Path &out,
                         TextExtent &extent) const
{
    extent = measure(utf8, font, dpi);
    if (m_face == nullptr) {
        return false;
    }

    out = Path();

    FT_Face face = as_face(m_face);
    const TextLayout laid = layout(utf8, font, dpi);

    for (const TextRun &run : laid.runs) {
    FontProps run_font = font;
    run_font.size = run.size;
    if (!set_size(run_font, dpi)) {
        continue;
    }

    const std::vector<uint32_t> codepoints = utf8_decode(run.text);
    const bool has_kerning = FT_HAS_KERNING(face) != 0;

    FT_Outline_Funcs funcs {};
    funcs.move_to = outline_move_to;
    funcs.line_to = outline_line_to;
    funcs.conic_to = outline_conic_to;
    funcs.cubic_to = outline_cubic_to;
    funcs.shift = 0;
    funcs.delta = 0;

    OutlineSink sink;
    sink.path = &out;

    double pen_x = 0.0;
    FT_UInt previous = 0;

    for (uint32_t cp : codepoints) {
        const FT_UInt index = FT_Get_Char_Index(face, cp);

        if (has_kerning && previous != 0 && index != 0) {
            FT_Vector delta;
            if (FT_Get_Kerning(face, previous, index, FT_KERNING_UNFITTED, &delta) == 0) {
                pen_x += from_26_6(delta.x);
            }
        }
        previous = index;

        /* NO_BITMAP keeps embedded bitmap strikes from pre-empting the outline. */
        if (FT_Load_Glyph(face, index, FT_LOAD_NO_BITMAP) != 0) {
            continue;
        }

        const FT_GlyphSlot slot = face->glyph;
        if (slot->format == FT_GLYPH_FORMAT_OUTLINE) {
            sink.offset_x = run.dx + pen_x;
            sink.offset_y = run.dy;
            sink.contour_open = false;
            if (FT_Outline_Decompose(&slot->outline, &funcs, &sink) == 0 && sink.contour_open) {
                out.close_poly();
            }
        }

        pen_x += from_26_6(slot->advance.x);
    }
    }

    return out.size() > 0;
}

} // namespace vpl
