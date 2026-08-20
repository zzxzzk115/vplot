/*
 * Shared base for the vector backends (SVG, PDF). Turns draw_text into
 * draw_path, so each concrete backend only emits filled/stroked paths. Glyph
 * geometry is written out rather than embedding a font program, so exported
 * figures carry no font dependency.
 */

#ifndef VPL_RENDER_VECTOR_RENDERER_H
#define VPL_RENDER_VECTOR_RENDERER_H

#include <cmath>

#include "font.h"
#include "renderer.h"

namespace vpl
{

class VectorRenderer : public Renderer
{
  public:
    VectorRenderer(double width, double height, double dpi, FontEngine *fonts)
        : m_width(width), m_height(height), m_dpi(dpi), m_fonts(fonts)
    {
    }

    double width() const override { return m_width; }
    double height() const override { return m_height; }
    double dpi() const override { return m_dpi; }

    void set_font_engine(FontEngine *fonts) { m_fonts = fonts; }

    TextExtent measure_text(const std::string &text, const FontProps &font) const override
    {
        if (m_fonts == nullptr || !m_fonts->ok()) {
            return TextExtent{};
        }
        return m_fonts->measure(text, font, m_dpi);
    }

    LineMetrics text_line_metrics(const std::string &text,
                                  const FontProps &font) const override
    {
        if (m_fonts == nullptr || !m_fonts->ok()) {
            return LineMetrics{};
        }
        const TextLayout laid = m_fonts->layout(text, font, m_dpi);
        return LineMetrics{laid.ascent, laid.descent};
    }

    void draw_text(const GraphicsContext &gc,
                   double x,
                   double y,
                   const std::string &text,
                   const FontProps &font,
                   double angle_deg,
                   HAlign halign,
                   VAlign valign,
                   RotationMode rotation_mode) override
    {
        if (m_fonts == nullptr || !m_fonts->ok() || text.empty()) {
            return;
        }

        Path glyphs;
        TextExtent extent;
        if (!m_fonts->outline(text, font, m_dpi, glyphs, extent)) {
            return;
        }

        /* Glyph outlines start at the baseline origin, so placing the run is
           placing that origin -- the same calculation the raster backend does,
           so labels land in the same place. */
        const TextLayout laid = m_fonts->layout(text, font, m_dpi, halign);
        const LineMetrics line{laid.ascent, laid.descent};

        double offset_x = 0.0;
        double offset_y = 0.0;
        text_origin_offset(halign, valign, laid.advance, line, angle_deg, rotation_mode,
                           offset_x, offset_y);

        const double theta = angle_deg * 3.14159265358979323846 / 180.0;
        const Affine2D transform =
            Affine2D::rotate(theta).then(Affine2D::translate(x - offset_x, y - offset_y));

        /* Glyphs are filled, never stroked: an outline stroke would fatten the
           letterforms relative to what the raster backend produces. */
        GraphicsContext text_gc = gc;
        text_gc.linewidth = 0.0;

        draw_path(text_gc, glyphs, transform, &gc.color);
    }

  protected:
    double m_width;
    double m_height;
    double m_dpi;
    FontEngine *m_fonts;
};

} // namespace vpl

#endif /* VPL_RENDER_VECTOR_RENDERER_H */
