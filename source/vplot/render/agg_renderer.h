/*
 * Agg raster backend. Wraps matplotlib's RendererAgg (source/vplot/render/mpl/)
 * and renders to an RGBA buffer, suitable for a PNG encoder or a GPU texture
 * upload.
 */

#ifndef VPL_RENDER_AGG_RENDERER_H
#define VPL_RENDER_AGG_RENDERER_H

#include <cstdint>
#include <memory>

#include "renderer.h"

class RendererAgg; /* source/vplot/render/mpl/_backend_agg.h */

namespace vpl
{

class FontEngine;

class AggRenderer : public Renderer
{
  public:
    AggRenderer(unsigned width, unsigned height, double dpi);
    ~AggRenderer() override;

    AggRenderer(const AggRenderer &) = delete;
    AggRenderer &operator=(const AggRenderer &) = delete;

    double width() const override { return m_width; }
    double height() const override { return m_height; }
    double dpi() const override { return m_dpi; }

    void clear(const Color &fill);

    void draw_path(const GraphicsContext &gc,
                   const Path &path,
                   const Affine2D &transform,
                   const Color *face) override;

    /* Overridden to use RendererAgg::draw_markers, which rasterizes the marker
       once and stamps the cached scanlines at each point. */
    void draw_markers(const GraphicsContext &gc,
                      const Path &marker,
                      const Affine2D &marker_transform,
                      const Path &points,
                      const Affine2D &transform,
                      const Color *face) override;

    void draw_text(const GraphicsContext &gc,
                   double x,
                   double y,
                   const std::string &text,
                   const FontProps &font,
                   double angle_deg,
                   HAlign halign,
                   VAlign valign,
                   RotationMode rotation_mode) override;

    TextExtent measure_text(const std::string &text, const FontProps &font) const override;

    LineMetrics text_line_metrics(const std::string &text,
                                  const FontProps &font) const override;

    /* Text rendering is inert until a font engine is attached. */
    void set_font_engine(FontEngine *fonts) { m_fonts = fonts; }

    /* RGBA8888, width*height*4 bytes, row 0 is the image's top row -- ready for
       a PNG encoder or a GPU texture upload with no flip. */
    const uint8_t *pixels() const;
    std::size_t pixels_size() const { return static_cast<std::size_t>(m_width) * m_height * 4; }

  private:
    std::unique_ptr<RendererAgg> m_agg;
    unsigned m_width;
    unsigned m_height;
    double m_dpi;
    FontEngine *m_fonts = nullptr;
};

} // namespace vpl

#endif /* VPL_RENDER_AGG_RENDERER_H */
