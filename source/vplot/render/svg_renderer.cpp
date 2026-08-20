#include "svg_renderer.h"

#include <cmath>
#include <cstdio>
#include <fstream>

#include "mplutils.h" /* MOVETO / LINETO / CURVE3 / CURVE4 / CLOSEPOLY */

namespace vpl
{
namespace
{

/* matplotlib's _short_float_fmt: a short representation that keeps files
   readable and smaller. */
std::string num(double v)
{
    if (v == 0.0) {
        return "0";
    }
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.6g", v);
    return buf;
}

std::string color_hex(const Color &c)
{
    auto ch = [](double v) {
        const int i = static_cast<int>(std::lround(v * 255.0));
        return i < 0 ? 0 : (i > 255 ? 255 : i);
    };
    char buf[16];
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", ch(c.r), ch(c.g), ch(c.b));
    return buf;
}

} // namespace

SvgRenderer::SvgRenderer(double width_pt, double height_pt, FontEngine *fonts)
    /* 72 dpi is what makes a user unit equal a point. */
    : VectorRenderer(width_pt, height_pt, 72.0, fonts)
{
}

void SvgRenderer::begin(const Color &facecolor)
{
    m_out.clear();
    m_out += "<?xml version=\"1.0\" encoding=\"utf-8\" standalone=\"no\"?>\n";
    m_out += "<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.1//EN\" "
             "\"http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd\">\n";
    m_out += "<!-- Created with vplot (https://github.com/zzxzzk115/vplot) -->\n";
    m_out += "<svg xmlns=\"http://www.w3.org/2000/svg\" "
             "xmlns:xlink=\"http://www.w3.org/1999/xlink\" version=\"1.1\" ";
    m_out += "width=\"" + num(m_width) + "pt\" height=\"" + num(m_height) + "pt\" ";
    m_out += "viewBox=\"0 0 " + num(m_width) + " " + num(m_height) + "\">\n";

    /* Same defaults matplotlib writes, so paths that omit these attributes
       stroke identically. */
    m_out += " <defs><style type=\"text/css\">*{stroke-linejoin: round; "
             "stroke-linecap: butt}</style></defs>\n";

    if (facecolor.visible()) {
        m_out += " <rect x=\"0\" y=\"0\" width=\"" + num(m_width) + "\" height=\"" +
                 num(m_height) + "\" style=\"fill: " + color_hex(facecolor) + "\"/>\n";
    }
}

void SvgRenderer::emit_path_data(const Path &path, const Affine2D &transform)
{
    const std::size_t n = path.size();
    const bool has_codes = !path.codes.empty();

    /* Transform into display space, then flip: SVG's y axis points down, ours
       points up. matplotlib composes exactly this in _make_flip_transform. */
    auto put = [&](std::size_t i) {
        const Point p = transform(Point{path.vertices[2 * i], path.vertices[2 * i + 1]});
        m_out += num(p.x);
        m_out += " ";
        m_out += num(m_height - p.y);
    };

    for (std::size_t i = 0; i < n;) {
        const uint8_t code = has_codes ? path.codes[i] : (i == 0 ? MOVETO : LINETO);

        switch (code) {
            case MOVETO:
                m_out += "M ";
                put(i);
                m_out += " ";
                i += 1;
                break;
            case LINETO:
                m_out += "L ";
                put(i);
                m_out += " ";
                i += 1;
                break;
            case CURVE3:
                m_out += "Q ";
                put(i);
                m_out += " ";
                put(i + 1);
                m_out += " ";
                i += 2;
                break;
            case CURVE4:
                m_out += "C ";
                put(i);
                m_out += " ";
                put(i + 1);
                m_out += " ";
                put(i + 2);
                m_out += " ";
                i += 3;
                break;
            case CLOSEPOLY:
                m_out += "z ";
                i += 1;
                break;
            default:
                /* STOP, or anything unrecognized: nothing further to emit. */
                i = n;
                break;
        }
    }
}

void SvgRenderer::draw_path(const GraphicsContext &gc,
                            const Path &path,
                            const Affine2D &transform,
                            const Color *face)
{
    if (path.size() == 0) {
        return;
    }

    const bool has_fill = face != nullptr && face->visible();
    const bool has_stroke = gc.color.visible() && gc.linewidth > 0.0;
    if (!has_fill && !has_stroke) {
        return;
    }

    m_out += " <path d=\"";
    emit_path_data(path, transform);
    m_out += "\" style=\"";

    if (has_fill) {
        m_out += "fill: " + color_hex(*face) + "; ";
        if (face->a * gc.alpha < 1.0) {
            m_out += "fill-opacity: " + num(face->a * gc.alpha) + "; ";
        }
    } else {
        m_out += "fill: none; ";
    }

    if (has_stroke) {
        m_out += "stroke: " + color_hex(gc.color) + "; ";
        m_out += "stroke-width: " + num(gc.linewidth) + "; ";
        if (gc.color.a * gc.alpha < 1.0) {
            m_out += "stroke-opacity: " + num(gc.color.a * gc.alpha) + "; ";
        }
        switch (gc.cap) {
            case CapStyle::Round: m_out += "stroke-linecap: round; "; break;
            case CapStyle::Projecting: m_out += "stroke-linecap: square; "; break;
            case CapStyle::Butt: m_out += "stroke-linecap: butt; "; break;
        }
        switch (gc.join) {
            case JoinStyle::Miter: m_out += "stroke-linejoin: miter; "; break;
            case JoinStyle::Bevel: m_out += "stroke-linejoin: bevel; "; break;
            case JoinStyle::Round: m_out += "stroke-linejoin: round; "; break;
        }
        if (!gc.dashes.empty()) {
            m_out += "stroke-dasharray: ";
            for (std::size_t i = 0; i < gc.dashes.size(); ++i) {
                if (i != 0) {
                    m_out += ",";
                }
                m_out += num(gc.dashes[i].first) + "," + num(gc.dashes[i].second);
            }
            m_out += "; stroke-dashoffset: " + num(gc.dash_offset) + "; ";
        }
    } else {
        m_out += "stroke: none; ";
    }

    m_out += "\"/>\n";
}

void SvgRenderer::finish()
{
    m_out += "</svg>\n";
}

bool SvgRenderer::save(const std::string &path) const
{
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        return false;
    }
    f.write(m_out.data(), static_cast<std::streamsize>(m_out.size()));
    return static_cast<bool>(f);
}

} // namespace vpl
