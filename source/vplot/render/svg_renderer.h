/*
 * SVG output. Follows matplotlib's backend_svg.py: the canvas is measured in
 * points and drawn at 72 dpi so one user unit is one point. The y flip is baked
 * into each emitted path rather than wrapped around a root group.
 */

#ifndef VPL_RENDER_SVG_RENDERER_H
#define VPL_RENDER_SVG_RENDERER_H

#include <string>

#include "vector_renderer.h"

namespace vpl
{

class SvgRenderer : public VectorRenderer
{
  public:
    /* Sizes are in points: inches * 72. */
    SvgRenderer(double width_pt, double height_pt, FontEngine *fonts);

    void begin(const Color &facecolor);
    void draw_path(const GraphicsContext &gc,
                   const Path &path,
                   const Affine2D &transform,
                   const Color *face) override;
    void finish();

    const std::string &document() const { return m_out; }
    bool save(const std::string &path) const;

  private:
    void emit_path_data(const Path &path, const Affine2D &transform);

    std::string m_out;
};

} // namespace vpl

#endif /* VPL_RENDER_SVG_RENDERER_H */
