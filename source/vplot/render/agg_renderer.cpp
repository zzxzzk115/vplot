#include "agg_renderer.h"

#include <cmath>

#include "_backend_agg.h"
#include "font.h"

namespace vpl
{
namespace
{

agg::line_cap_e to_agg(CapStyle cap)
{
    switch (cap) {
        case CapStyle::Round: return agg::round_cap;
        case CapStyle::Projecting: return agg::square_cap;
        case CapStyle::Butt:
        default: return agg::butt_cap;
    }
}

agg::line_join_e to_agg(JoinStyle join)
{
    switch (join) {
        case JoinStyle::Miter: return agg::miter_join_revert;
        case JoinStyle::Bevel: return agg::bevel_join;
        case JoinStyle::Round:
        default: return agg::round_join;
    }
}

agg::rgba to_agg(const Color &c, double alpha)
{
    return agg::rgba(c.r, c.g, c.b, c.a * alpha);
}

/* GCAgg is non-copyable, so it is filled in place per draw call. */
void fill_gc(GCAgg &out, const GraphicsContext &gc)
{
    out.linewidth = gc.linewidth;
    out.alpha = gc.alpha;
    out.forced_alpha = false;
    out.isaa = gc.antialiased;
    out.color = to_agg(gc.color, gc.alpha);
    out.cap = to_agg(gc.cap);
    out.join = to_agg(gc.join);
    out.snap_mode = SNAP_AUTO;

    /* An all-zero rect means "no clipping" to RendererAgg::set_clipbox. */
    out.cliprect = gc.has_cliprect
                       ? agg::rect_d(gc.cliprect.x0, gc.cliprect.y0,
                                     gc.cliprect.x1, gc.cliprect.y1)
                       : agg::rect_d(0.0, 0.0, 0.0, 0.0);

    for (const auto &[on, off] : gc.dashes) {
        out.dashes.add_dash_pair(on, off);
    }
    out.dashes.set_dash_offset(gc.dash_offset);

    out.hatch_linewidth = 1.0;
    out.sketch.scale = 0.0;
    out.sketch.length = 0.0;
    out.sketch.randomness = 0.0;
}

} // namespace

AggRenderer::AggRenderer(unsigned width, unsigned height, double dpi)
    : m_agg(std::make_unique<RendererAgg>(width, height, dpi)),
      m_width(width),
      m_height(height),
      m_dpi(dpi)
{
}

AggRenderer::~AggRenderer() = default;

const uint8_t *AggRenderer::pixels() const
{
    return m_agg->pixBuffer;
}

void AggRenderer::clear(const Color &fill)
{
    m_agg->clear();
    if (!fill.visible()) {
        return;
    }

    /* RendererAgg::clear() always clears to transparent, so an opaque figure
       background is painted as a full-canvas rectangle rather than by changing
       the clear colour. */
    GraphicsContext gc;
    gc.color = Color::none();
    gc.linewidth = 0.0;
    gc.antialiased = false;

    const Path canvas = Path::rectangle(Bbox(0.0, 0.0, m_width, m_height));
    draw_path(gc, canvas, Affine2D(), &fill);
}

void AggRenderer::draw_path(const GraphicsContext &gc,
                            const Path &path,
                            const Affine2D &transform,
                            const Color *face)
{
    if (path.size() == 0) {
        return;
    }

    GCAgg agg_gc;
    fill_gc(agg_gc, gc);

    mpl::PathIterator iterator = path.iterator();

    /* RendererAgg::draw_path mutates the transform it is handed (it appends the
       y-flip), so it gets a fresh copy every call. */
    agg::trans_affine trans = transform.to_agg();

    /* draw_path fills only when the face colour has non-zero alpha. */
    agg::rgba face_color = face != nullptr ? to_agg(*face, gc.alpha)
                                           : agg::rgba(0.0, 0.0, 0.0, 0.0);

    m_agg->draw_path(agg_gc, iterator, trans, face_color);
}

void AggRenderer::draw_markers(const GraphicsContext &gc,
                               const Path &marker,
                               const Affine2D &marker_transform,
                               const Path &points,
                               const Affine2D &transform,
                               const Color *face)
{
    if (marker.size() == 0 || points.size() == 0) {
        return;
    }

    GCAgg agg_gc;
    fill_gc(agg_gc, gc);

    mpl::PathIterator marker_iterator = marker.iterator();
    mpl::PathIterator points_iterator = points.iterator();

    /* Both transforms are mutated in place by draw_markers (it appends the
       y-flip to one and the marker's own centring to the other), so each gets a
       fresh copy. */
    agg::trans_affine marker_trans = marker_transform.to_agg();
    agg::trans_affine trans = transform.to_agg();

    const agg::rgba face_color =
        face != nullptr ? to_agg(*face, gc.alpha) : agg::rgba(0.0, 0.0, 0.0, 0.0);

    m_agg->draw_markers(agg_gc, marker_iterator, marker_trans, points_iterator, trans,
                        face_color);
}

void AggRenderer::draw_text(const GraphicsContext &gc,
                            double x,
                            double y,
                            const std::string &text,
                            const FontProps &font,
                            double angle_deg,
                            HAlign halign,
                            VAlign valign,
                            RotationMode rotation_mode)
{
    /* Without a font engine, figures render without labels rather than
       failing. */
    if (m_fonts == nullptr || !m_fonts->ok() || text.empty()) {
        return;
    }

    /* Horizontal alignment doubles as the multialignment (matplotlib's
       default), controlling how multiple lines align to each other. */
    const TextLayout laid = m_fonts->layout(text, font, m_dpi, halign);
    if (laid.advance <= 0.0) {
        return;
    }

    /* Align against the layout box, not the ink, so a label without a descender
       sits where one with a descender would. */
    const LineMetrics line{laid.ascent, laid.descent};

    /* Resolve the alignment to the string's origin (left end of the baseline),
       which the glyph rasterizer needs. */
    double offset_x = 0.0;
    double offset_y = 0.0;
    text_origin_offset(halign, valign, laid.advance, line, angle_deg, rotation_mode,
                       offset_x, offset_y);

    const double origin_x = x - offset_x;
    const double origin_y = y - offset_y;

    GCAgg agg_gc;
    fill_gc(agg_gc, gc);

    /*
     * Each glyph is blitted separately at angle zero; the rotation is already
     * baked into the rasterized outline. Unlike draw_path, draw_text_image does
     * not apply the y-flip -- it writes into the top-down pixel buffer, so the
     * row coordinate is converted below.
     */
    std::vector<GlyphImage> glyphs =
        m_fonts->render_glyphs(text, font, m_dpi, angle_deg, origin_x, origin_y, halign);

    for (GlyphImage &glyph : glyphs) {
        const int py = static_cast<int>(m_height) - glyph.top +
                       static_cast<int>(glyph.image.shape(0));
        m_agg->draw_text_image(agg_gc, glyph.image, glyph.left, py, 0.0);
    }
}

TextExtent AggRenderer::measure_text(const std::string &text, const FontProps &font) const
{
    if (m_fonts == nullptr || !m_fonts->ok()) {
        return TextExtent{};
    }
    return m_fonts->measure(text, font, m_dpi);
}

LineMetrics AggRenderer::text_line_metrics(const std::string &text, const FontProps &font) const
{
    if (m_fonts == nullptr || !m_fonts->ok()) {
        return LineMetrics{};
    }
    const TextLayout laid = m_fonts->layout(text, font, m_dpi);
    return LineMetrics{laid.ascent, laid.descent};
}

} // namespace vpl
