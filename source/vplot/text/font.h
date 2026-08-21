/*
 * FreeType-backed text layout and rasterization. Follows matplotlib's model:
 * lay out a string, rasterize glyphs, and hand them to
 * RendererAgg::draw_text_image, which tints the coverage with the graphics
 * context's colour.
 */

#ifndef VPL_TEXT_FONT_H
#define VPL_TEXT_FONT_H

#include <cstdint>
#include <string>
#include <vector>

#include "renderer.h"

namespace vpl
{

/*
 * An 8-bit grayscale image shaped to what RendererAgg::draw_text_image expects:
 * shape(0) is the row count, shape(1) the column count, plus mutable_data() and
 * an operator()(row, col) it takes the address of when blitting spans.
 */
class GrayImage
{
  public:
    GrayImage() = default;
    GrayImage(unsigned rows, unsigned cols)
        : m_rows(rows), m_cols(cols), m_data(static_cast<std::size_t>(rows) * cols, 0)
    {
    }

    unsigned shape(int dim) const { return dim == 0 ? m_rows : m_cols; }
    bool empty() const { return m_data.empty(); }

    uint8_t &operator()(int row, int col)
    {
        return m_data[static_cast<std::size_t>(row) * m_cols + col];
    }
    const uint8_t &operator()(int row, int col) const
    {
        return m_data[static_cast<std::size_t>(row) * m_cols + col];
    }

    uint8_t *mutable_data(int row, int col) { return &(*this)(row, col); }

  private:
    unsigned m_rows = 0;
    unsigned m_cols = 0;
    std::vector<uint8_t> m_data;
};

/*
 * One run of glyphs at a single size and offset. Plain text is one run; a
 * mathtext superscript is two. Everything downstream iterates runs, so it need
 * not know which kind of string it was handed.
 */
struct TextRun
{
    std::string text;
    double size = 0.0; /* the size the glyphs are RASTERIZED at, in points */
    double dx = 0.0;   /* from the string's origin, in pixels */
    double dy = 0.0;   /* upwards from the baseline, in pixels */

    /*
     * Where the advances come from, which is not always the size above. A
     * superscript's pen positions come from full-size advances scaled by 0.7,
     * while the glyph is rasterized at the smaller size; hinting makes the two
     * differ by a fraction of a pixel.
     */
    double metrics_size = 0.0;
    double metrics_scale = 1.0;
};

struct TextLayout
{
    std::vector<TextRun> runs;
    double advance = 0.0; /* the widest line's advance, in pixels */

    /*
     * The layout box, measured from the string's origin -- for multiple lines
     * the last line's baseline. A single line is the max(ink, typographic)
     * pair; multiple lines also carry the line gaps.
     */
    double ascent = 0.0;
    double descent = 0.0;
};

/*
 * Is this one of matplotlib's math strings? A label between dollar signs, as
 * LogFormatterSciNotation writes: "$\mathdefault{10^{2}}$".
 */
bool is_mathtext(const std::string &utf8);

/*
 * One rasterized glyph, positioned on the canvas. `left` and `top` are the
 * absolute display-space coordinates of the bitmap's top-left corner (y up).
 */
struct GlyphImage
{
    GrayImage image;
    int left = 0;
    int top = 0;
};

class FontEngine
{
  public:
    FontEngine();
    ~FontEngine();

    FontEngine(const FontEngine &) = delete;
    FontEngine &operator=(const FontEngine &) = delete;

    /* Loads a TTF from disk. Returns false and leaves the engine unusable if the
       file is missing or unparseable; callers render without text rather than
       failing the whole figure. */
    bool load_face(const std::string &path);

    /* Loads the compiled-in DejaVu Sans. No filesystem access and no search
       path, so a linked-in vplot always has a face -- see the note in the
       definition for why this is not a lookup any more. */
    bool load_default_face();

    bool ok() const { return m_face != nullptr; }
    const std::string &face_path() const { return m_face_path; }

    /* width is the sum of advances; height and descent describe the ink the
       string covers (matplotlib's get_text_width_height_descent). */
    TextExtent measure(const std::string &utf8, const FontProps &font, double dpi) const;

    /* The ascent/descent to align `ink` against -- see LineMetrics. */
    LineMetrics layout_metrics(const TextExtent &ink, const FontProps &font, double dpi) const;

    /*
     * Break a string into the runs it is drawn as. Plain text yields one run;
     * a math string yields the nucleus plus a shrunk, raised superscript. Only
     * superscripts are supported, which is what a log axis needs; anything else
     * is drawn as its literal text minus the markup.
     */
    TextLayout layout(const std::string &utf8,
                      const FontProps &font,
                      double dpi,
                      HAlign multialignment = HAlign::Left) const;

    /*
     * Rasterizes each glyph separately at its place on the canvas. `origin` is
     * the left end of the baseline in display pixels; `angle_deg` rotates the
     * string about that point. Per-glyph rasterization preserves fidelity: each
     * glyph is rasterized at its sub-pixel offset, rotation is applied to the
     * outline rather than a finished bitmap, and overlapping glyphs composite
     * one at a time.
     */
    std::vector<GlyphImage> render_glyphs(const std::string &utf8,
                                          const FontProps &font,
                                          double dpi,
                                          double angle_deg,
                                          double origin_x,
                                          double origin_y,
                                          HAlign multialignment = HAlign::Left) const;

    /* Glyph outlines as a fillable path, for the vector backends. Emitting
     * geometry lets SVG and PDF reuse the raster backend's path pipeline and
     * avoids TrueType subsetting.
     *
     * The path is in text-local coordinates: origin at the start of the
     * baseline, y up, units of pixels at the given dpi.
     */
    bool outline(const std::string &utf8,
                 const FontProps &font,
                 double dpi,
                 Path &out,
                 TextExtent &extent) const;

  private:
    bool set_size(const FontProps &font, double dpi) const;

    /* One run at one size: its advance, and its ink's top and bottom relative
       to the baseline. All in pixels. */
    void string_metrics(const std::string &utf8,
                        double size,
                        double dpi,
                        double &advance,
                        double &ink_top,
                        double &ink_bottom) const;
    double string_advance(const std::string &utf8, double size, double dpi) const;

    /* The same, for a run: measured at its metrics_size and scaled. */
    void run_metrics(const TextRun &run,
                     double dpi,
                     double &advance,
                     double &ink_top,
                     double &ink_bottom) const;

    /* The x-height at a size: the ink top of 'x', which is what matplotlib
       falls back to for a font with no PCLT table -- DejaVu has none. */
    double x_height(double size, double dpi) const;

    /* The font's own typographic metrics at a size, from OS/2 (falling back to
       hhea): what a line is never allowed to be shorter than, and the gap
       stacked between lines. */
    void typo_metrics(double size,
                      double dpi,
                      double &ascent,
                      double &descent,
                      double &line_gap) const;

    /* One line's runs -- this is where mathtext is handled. `advance`,
       `ink_top` and `ink_bottom` describe the line on its own. */
    void layout_line(const std::string &utf8,
                     const FontProps &font,
                     double dpi,
                     std::vector<TextRun> &runs,
                     double &advance,
                     double &ink_top,
                     double &ink_bottom) const;

    void *m_library = nullptr; /* FT_Library */
    void *m_face = nullptr;    /* FT_Face */
    std::string m_face_path;
};

/* Decodes UTF-8 into code points, substituting U+FFFD for malformed input. */
std::vector<uint32_t> utf8_decode(const std::string &s);

} // namespace vpl

#endif /* VPL_TEXT_FONT_H */
