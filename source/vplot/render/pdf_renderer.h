/*
 * PDF output. Draws at 72 dpi since PDF's default user-space unit is the point.
 * PDF's coordinate convention (origin bottom-left, y up) matches vplot's
 * display space, so no y-flip is needed. Text arrives as glyph outlines from
 * VectorRenderer, so no font program is embedded or subset.
 */

#ifndef VPL_RENDER_PDF_RENDERER_H
#define VPL_RENDER_PDF_RENDERER_H

#include <string>
#include <vector>

#include "vector_renderer.h"

namespace vpl
{

class PdfRenderer : public VectorRenderer
{
  public:
    /* Sizes are in points: inches * 72. */
    PdfRenderer(double width_pt, double height_pt, FontEngine *fonts);

    void begin(const Color &facecolor);
    void draw_path(const GraphicsContext &gc,
                   const Path &path,
                   const Affine2D &transform,
                   const Color *face) override;
    void finish();

    const std::string &document() const { return m_document; }
    bool save(const std::string &path) const;

  private:
    void emit_path_data(const Path &path, const Affine2D &transform);

    /* PDF expresses constant alpha through a named graphics state in the page's
       resource dictionary, so each distinct value used needs an entry. */
    std::size_t alpha_state(double stroke_alpha, double fill_alpha);

    std::string m_content;
    std::string m_document;
    std::vector<std::pair<double, double>> m_alpha_states;
};

} // namespace vpl

#endif /* VPL_RENDER_PDF_RENDERER_H */
