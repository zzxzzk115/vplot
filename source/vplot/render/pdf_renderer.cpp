#include "pdf_renderer.h"

#include <cmath>
#include <cstdio>
#include <fstream>

#include "mplutils.h" /* MOVETO / LINETO / CURVE3 / CURVE4 / CLOSEPOLY */

namespace vpl
{
namespace
{

std::string num(double v)
{
    if (v == 0.0) {
        return "0";
    }
    char buf[40];
    /* PDF readers are not required to accept exponent notation in content
       streams, so this stays in fixed notation. */
    std::snprintf(buf, sizeof(buf), "%.4f", v);

    std::string s = buf;
    const std::size_t dot = s.find('.');
    if (dot != std::string::npos) {
        std::size_t last = s.find_last_not_of('0');
        if (last == dot) {
            last = dot - 1;
        }
        s.erase(last + 1);
    }
    return s;
}

std::string rgb(const Color &c, const char *op)
{
    return num(c.r) + " " + num(c.g) + " " + num(c.b) + " " + op + "\n";
}

} // namespace

PdfRenderer::PdfRenderer(double width_pt, double height_pt, FontEngine *fonts)
    : VectorRenderer(width_pt, height_pt, 72.0, fonts)
{
}

void PdfRenderer::begin(const Color &facecolor)
{
    m_content.clear();
    m_document.clear();
    m_alpha_states.clear();

    if (facecolor.visible()) {
        m_content += rgb(facecolor, "rg");
        m_content += "0 0 " + num(m_width) + " " + num(m_height) + " re f\n";
    }
}

std::size_t PdfRenderer::alpha_state(double stroke_alpha, double fill_alpha)
{
    for (std::size_t i = 0; i < m_alpha_states.size(); ++i) {
        if (m_alpha_states[i].first == stroke_alpha &&
            m_alpha_states[i].second == fill_alpha) {
            return i;
        }
    }
    m_alpha_states.emplace_back(stroke_alpha, fill_alpha);
    return m_alpha_states.size() - 1;
}

void PdfRenderer::emit_path_data(const Path &path, const Affine2D &transform)
{
    const std::size_t n = path.size();
    const bool has_codes = !path.codes.empty();

    /* PDF user space matches display space: origin bottom left, y up. */
    auto at = [&](std::size_t i) {
        return transform(Point{path.vertices[2 * i], path.vertices[2 * i + 1]});
    };
    auto put = [&](Point p) { return num(p.x) + " " + num(p.y) + " "; };

    Point current{};

    for (std::size_t i = 0; i < n;) {
        const uint8_t code = has_codes ? path.codes[i] : (i == 0 ? MOVETO : LINETO);

        switch (code) {
            case MOVETO:
                current = at(i);
                m_content += put(current) + "m\n";
                i += 1;
                break;
            case LINETO:
                current = at(i);
                m_content += put(current) + "l\n";
                i += 1;
                break;
            case CURVE3: {
                /* PDF has no quadratic operator, so the quadratic is elevated
                   to the equivalent cubic:
                       c1 = p0 + 2/3 (ctrl - p0)
                       c2 = p2 + 2/3 (ctrl - p2)   */
                const Point ctrl = at(i);
                const Point end = at(i + 1);
                const Point c1{current.x + 2.0 / 3.0 * (ctrl.x - current.x),
                               current.y + 2.0 / 3.0 * (ctrl.y - current.y)};
                const Point c2{end.x + 2.0 / 3.0 * (ctrl.x - end.x),
                               end.y + 2.0 / 3.0 * (ctrl.y - end.y)};
                m_content += put(c1) + put(c2) + put(end) + "c\n";
                current = end;
                i += 2;
                break;
            }
            case CURVE4: {
                const Point c1 = at(i);
                const Point c2 = at(i + 1);
                const Point end = at(i + 2);
                m_content += put(c1) + put(c2) + put(end) + "c\n";
                current = end;
                i += 3;
                break;
            }
            case CLOSEPOLY:
                m_content += "h\n";
                i += 1;
                break;
            default:
                i = n;
                break;
        }
    }
}

void PdfRenderer::draw_path(const GraphicsContext &gc,
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

    const double stroke_alpha = has_stroke ? gc.color.a * gc.alpha : 1.0;
    const double fill_alpha = has_fill ? face->a * gc.alpha : 1.0;

    m_content += "q\n";

    if (stroke_alpha < 1.0 || fill_alpha < 1.0) {
        m_content += "/GS" + std::to_string(alpha_state(stroke_alpha, fill_alpha)) + " gs\n";
    }

    if (has_stroke) {
        m_content += rgb(gc.color, "RG");
        m_content += num(gc.linewidth) + " w\n";
        m_content += std::to_string(gc.cap == CapStyle::Round      ? 1
                                    : gc.cap == CapStyle::Projecting ? 2
                                                                     : 0) +
                     " J\n";
        m_content += std::to_string(gc.join == JoinStyle::Round  ? 1
                                    : gc.join == JoinStyle::Bevel ? 2
                                                                  : 0) +
                     " j\n";
        if (!gc.dashes.empty()) {
            m_content += "[";
            for (std::size_t i = 0; i < gc.dashes.size(); ++i) {
                if (i != 0) {
                    m_content += " ";
                }
                m_content += num(gc.dashes[i].first) + " " + num(gc.dashes[i].second);
            }
            m_content += "] " + num(gc.dash_offset) + " d\n";
        }
    }

    if (has_fill) {
        m_content += rgb(*face, "rg");
    }

    emit_path_data(path, transform);

    /* B fills then strokes; f and S do one each. */
    m_content += has_fill && has_stroke ? "B\n" : (has_fill ? "f\n" : "S\n");
    m_content += "Q\n";
}

void PdfRenderer::finish()
{
    /* Objects: 1 catalog, 2 pages, 3 page, 4 content stream. Content is left
       uncompressed for readability. */
    std::string ext_gstate;
    if (!m_alpha_states.empty()) {
        ext_gstate = " /ExtGState <<";
        for (std::size_t i = 0; i < m_alpha_states.size(); ++i) {
            ext_gstate += " /GS" + std::to_string(i) + " << /Type /ExtGState /CA " +
                          num(m_alpha_states[i].first) + " /ca " +
                          num(m_alpha_states[i].second) + " >>";
        }
        ext_gstate += " >>";
    }

    std::vector<std::string> objects;
    objects.push_back("<< /Type /Catalog /Pages 2 0 R >>");
    objects.push_back("<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    objects.push_back("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + num(m_width) + " " +
                      num(m_height) + "] /Contents 4 0 R /Resources <<" +
                      (ext_gstate.empty() ? " " : ext_gstate) + " >> >>");
    objects.push_back("<< /Length " + std::to_string(m_content.size()) + " >>\nstream\n" +
                      m_content + "endstream");

    m_document = "%PDF-1.4\n";
    /* A binary comment marks the file as containing binary data, so tools do
       not mangle it in text mode. */
    m_document += "%\xE2\xE3\xCF\xD3\n";

    std::vector<std::size_t> offsets;
    offsets.reserve(objects.size());

    for (std::size_t i = 0; i < objects.size(); ++i) {
        offsets.push_back(m_document.size());
        m_document += std::to_string(i + 1) + " 0 obj\n" + objects[i] + "\nendobj\n";
    }

    const std::size_t xref_offset = m_document.size();
    m_document += "xref\n0 " + std::to_string(objects.size() + 1) + "\n";
    m_document += "0000000000 65535 f \n";
    for (std::size_t off : offsets) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%010zu 00000 n \n", off);
        m_document += buf;
    }

    m_document += "trailer\n<< /Size " + std::to_string(objects.size() + 1) +
                  " /Root 1 0 R >>\nstartxref\n" + std::to_string(xref_offset) + "\n%%EOF\n";
}

bool PdfRenderer::save(const std::string &path) const
{
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        return false;
    }
    f.write(m_document.data(), static_cast<std::streamsize>(m_document.size()));
    return static_cast<bool>(f);
}

} // namespace vpl
