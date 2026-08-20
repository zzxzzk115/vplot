/*
 * The renderer abstraction all output backends implement. Mirrors matplotlib's
 * RendererBase (lib/matplotlib/backend_bases.py): draw_markers has a default in
 * terms of draw_path, so a backend mainly implements draw_path and draw_text.
 *
 * Everything here is in display space: y up, origin bottom-left.
 */

#ifndef VPL_RENDER_RENDERER_H
#define VPL_RENDER_RENDERER_H

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "geometry.h"
#include "path.h"

namespace vpl
{

struct Color
{
    double r = 0.0, g = 0.0, b = 0.0, a = 1.0;

    static Color rgb(double r, double g, double b) { return Color{r, g, b, 1.0}; }
    static Color none() { return Color{0.0, 0.0, 0.0, 0.0}; }
    bool visible() const { return a != 0.0; }
};

enum class CapStyle
{
    Butt,
    Round,
    Projecting
};

enum class JoinStyle
{
    Miter,
    Round,
    Bevel
};

struct FontProps
{
    /* Points, as matplotlib measures them; the renderer scales by dpi/72. */
    double size = 10.0;
    bool bold = false;
    bool italic = false;
};

struct GraphicsContext
{
    /*
     * In points, not pixels (likewise `dashes` below). Each backend scales:
     * RendererAgg by dpi/72, the vector backends draw at 72 dpi where a point
     * is the unit. Pre-scaling here would double the conversion.
     */
    double linewidth = 1.0;
    Color color = Color::rgb(0.0, 0.0, 0.0);
    double alpha = 1.0;
    bool antialiased = true;
    CapStyle cap = CapStyle::Butt;
    JoinStyle join = JoinStyle::Round;

    /* on/off lengths in points, empty means solid */
    std::vector<std::pair<double, double>> dashes;
    double dash_offset = 0.0;

    bool has_cliprect = false;
    Bbox cliprect;
};

/* Where a string sits relative to the anchor point it is drawn at. */
enum class HAlign
{
    Left,
    Center,
    Right
};

enum class VAlign
{
    Bottom,
    /* Centres the full box, descender included. */
    Center,
    Top,
    Baseline,
    /*
     * Centres between the baseline and the top of the line, ignoring the
     * descender (matplotlib's "center_baseline", the default for y tick
     * labels). Most tick labels have no descender, so centring the full box
     * would push them visibly high.
     */
    CenterBaseline
};

/* width is the sum of glyph advances; height and descent describe the ink the
   string covers (matplotlib's get_text_width_height_descent). "0.04" is shorter
   than "Apg" at the same size. */
struct TextExtent
{
    double width = 0.0;
    double height = 0.0;
    double descent = 0.0;
};

/*
 * The ascent and descent a string is aligned against, not the ink it covers:
 * a line is never counted shorter than the font's typographic ascent and
 * descent, so a label with no descender still sits where one with a descender
 * would.
 */
struct LineMetrics
{
    double ascent = 0.0;
    double descent = 0.0;
};

/*
 * How rotation and alignment combine (matplotlib's Text.rotation_mode).
 * `Bbox` aligns against the text's rotated bounding box (a plain text()).
 * `Anchor` aligns in the text's unrotated frame and rotates the offset (the
 * y axis label). The two agree at 0 degrees and diverge as the angle grows.
 */
enum class RotationMode
{
    Bbox,
    Anchor
};

/* Where a string's origin (the left end of its baseline) goes, as an offset to
   subtract from the anchor the caller asked for. */
inline void text_origin_offset(HAlign halign,
                               VAlign valign,
                               double width,
                               const LineMetrics &line,
                               double angle_deg,
                               RotationMode mode,
                               double &offset_x,
                               double &offset_y)
{
    const double theta = angle_deg * 3.14159265358979323846 / 180.0;
    const double c = std::cos(theta);
    const double s = std::sin(theta);

    if (mode == RotationMode::Anchor) {
        /* Aligned in the text's own frame, then that offset rotated. */
        double local_x = 0.0;
        switch (halign) {
            case HAlign::Left: break;
            case HAlign::Right: local_x = width; break;
            case HAlign::Center: local_x = width / 2.0; break;
        }
        double local_y = 0.0;
        switch (valign) {
            case VAlign::Bottom: local_y = -line.descent; break;
            case VAlign::Top: local_y = line.ascent; break;
            case VAlign::Center: local_y = (line.ascent - line.descent) / 2.0; break;
            case VAlign::Baseline: break;
            case VAlign::CenterBaseline: local_y = line.ascent / 2.0; break;
        }
        offset_x = local_x * c - local_y * s;
        offset_y = local_x * s + local_y * c;
        return;
    }

    /* The unrotated box, relative to the origin: the line's full ascent and
       descent, not the ink's. */
    const double corners[4][2] = {
        {0.0, -line.descent},
        {0.0, line.ascent},
        {width, line.ascent},
        {width, -line.descent},
    };

    double xmin = 0.0;
    double xmax = 0.0;
    double ymin = 0.0;
    double ymax = 0.0;
    for (int i = 0; i < 4; ++i) {
        const double rx = corners[i][0] * c - corners[i][1] * s;
        const double ry = corners[i][0] * s + corners[i][1] * c;
        if (i == 0) {
            xmin = xmax = rx;
            ymin = ymax = ry;
        } else {
            xmin = std::min(xmin, rx);
            xmax = std::max(xmax, rx);
            ymin = std::min(ymin, ry);
            ymax = std::max(ymax, ry);
        }
    }

    switch (halign) {
        case HAlign::Left: offset_x = xmin; break;
        case HAlign::Right: offset_x = xmax; break;
        case HAlign::Center: offset_x = (xmin + xmax) / 2.0; break;
    }

    switch (valign) {
        case VAlign::Bottom: offset_y = ymin; break;
        case VAlign::Top: offset_y = ymax; break;
        case VAlign::Center: offset_y = (ymin + ymax) / 2.0; break;
        case VAlign::Baseline: offset_y = ymin + line.descent; break;
        case VAlign::CenterBaseline:
            offset_y = ymin + (ymax - ymin) - line.ascent / 2.0;
            break;
    }
}

class Renderer
{
  public:
    virtual ~Renderer() = default;

    virtual double width() const = 0;
    virtual double height() const = 0;
    virtual double dpi() const = 0;

    /* face == nullptr (or an invisible colour) strokes without filling. */
    virtual void draw_path(const GraphicsContext &gc,
                           const Path &path,
                           const Affine2D &transform,
                           const Color *face) = 0;

    /*
     * The same marker stamped at every vertex of `points`. This default loops
     * draw_path; the Agg backend overrides it to rasterize the marker once and
     * stamp the cached scanlines, which is faster and makes every marker
     * identical. The vector backends want one path per point, so they use this.
     */
    virtual void draw_markers(const GraphicsContext &gc,
                              const Path &marker,
                              const Affine2D &marker_transform,
                              const Path &points,
                              const Affine2D &transform,
                              const Color *face)
    {
        for (std::size_t i = 0; i < points.size(); ++i) {
            const Point p =
                transform(Point{points.vertices[2 * i], points.vertices[2 * i + 1]});
            if (!std::isfinite(p.x) || !std::isfinite(p.y)) {
                continue;
            }
            draw_path(gc, marker, marker_transform.then(Affine2D::translate(p.x, p.y)), face);
        }
    }

    virtual void draw_text(const GraphicsContext &gc,
                           double x,
                           double y,
                           const std::string &text,
                           const FontProps &font,
                           double angle_deg,
                           HAlign halign,
                           VAlign valign,
                           RotationMode rotation_mode = RotationMode::Bbox) = 0;

    virtual TextExtent measure_text(const std::string &text, const FontProps &font) const = 0;

    /* Layout needs both: measure_text for the ink, this for the line box that
       ink is aligned inside. See LineMetrics. */
    virtual LineMetrics text_line_metrics(const std::string &text,
                                          const FontProps &font) const = 0;

    /* matplotlib's points-to-pixels: linewidths and font sizes are specified in
       points and scaled by dpi/72. */
    double points_to_pixels(double points) const { return points * dpi() / 72.0; }
};

} // namespace vpl

#endif /* VPL_RENDER_RENDERER_H */
