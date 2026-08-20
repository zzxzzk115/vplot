/*
 * Renders a complete figure end to end: background, frame, ticks and data
 * lines, exercising geometry, the tick locator, the path pipeline and the Agg
 * rasterizer together.
 */

#include "agg_renderer.h"
#include "figure.h"
#include "font.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{

bool write_ppm(const char *path, const uint8_t *rgba, unsigned w, unsigned h)
{
    FILE *f = std::fopen(path, "wb");
    if (!f) {
        return false;
    }
    std::fprintf(f, "P6\n%u %u\n255\n", w, h);
    for (unsigned i = 0; i < w * h; ++i) {
        const uint8_t *px = rgba + 4 * static_cast<size_t>(i);
        const double a = px[3] / 255.0;
        for (int c = 0; c < 3; ++c) {
            std::fputc(static_cast<uint8_t>(px[c] * a + 255.0 * (1.0 - a) + 0.5), f);
        }
    }
    std::fclose(f);
    return true;
}

} // namespace

int main()
{
    vpl::Figure fig;
    vpl::Axes &ax = fig.add_subplot();

    std::vector<double> x, y1, y2;
    for (int i = 0; i <= 200; ++i) {
        const double t = i * (2.0 * 3.14159265358979323846) / 200.0;
        x.push_back(t);
        y1.push_back(std::sin(t));
        y2.push_back(std::cos(t) * 0.5);
    }

    ax.plot(x.data(), y1.data(), x.size());
    ax.plot(x.data(), y2.data(), x.size());
    ax.title = "vplot stage 0";
    ax.xlabel = "x";
    ax.ylabel = "amplitude";

    vpl::FontEngine fonts;
    if (!fonts.load_default_face()) {
        std::fprintf(stderr, "FAIL: could not find assets/fonts/DejaVuSans.ttf\n");
        return 1;
    }
    std::printf("font: %s\n", fonts.face_path().c_str());

    vpl::AggRenderer renderer(fig.pixel_width(), fig.pixel_height(), fig.dpi);
    renderer.set_font_engine(&fonts);
    renderer.clear(fig.facecolor);
    fig.draw(renderer);

    const unsigned w = fig.pixel_width();
    const unsigned h = fig.pixel_height();
    std::printf("figure %ux%u at %g dpi\n", w, h, fig.dpi);

    /* The canvas should be opaque everywhere: figure.facecolor covers it. */
    const uint8_t *px = renderer.pixels();
    size_t opaque = 0;
    size_t coloured = 0;
    for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
        if (px[4 * i + 3] == 255) {
            ++opaque;
        }
        /* Anything that isn't the white background is ink: frame, ticks, data. */
        if (px[4 * i] != 255 || px[4 * i + 1] != 255 || px[4 * i + 2] != 255) {
            ++coloured;
        }
    }

    std::printf("opaque pixels: %zu / %zu\n", opaque, static_cast<size_t>(w) * h);
    std::printf("non-background pixels: %zu\n", coloured);

    if (opaque != static_cast<size_t>(w) * h) {
        std::fprintf(stderr, "FAIL: figure background did not cover the canvas\n");
        return 1;
    }
    if (coloured < 2000) {
        std::fprintf(stderr, "FAIL: too little was drawn (%zu px); "
                             "expected a frame, ticks and two curves\n", coloured);
        return 1;
    }

    if (!write_ppm("test_figure.ppm", px, w, h)) {
        std::fprintf(stderr, "FAIL: could not write test_figure.ppm\n");
        return 1;
    }

    std::printf("OK: wrote test_figure.ppm\n");
    return 0;
}
