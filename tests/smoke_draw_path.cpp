/*
 * Smoke test: drives the lifted Agg renderer with plain C arrays and confirms
 * it rasterizes a polyline with no Python in the process. The rendering core is
 * matplotlib's own C++ with py_adaptors.h swapped for vpl_adaptors.h.
 */

#include "_backend_agg.h"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace
{

/* Write RGBA8888 as a binary PPM, compositing over white. PPM keeps this test
   dependency-free. */
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
            const double v = px[c] * a + 255.0 * (1.0 - a);
            const uint8_t out = static_cast<uint8_t>(v + 0.5);
            std::fputc(out, f);
        }
    }
    std::fclose(f);
    return true;
}

} // namespace

int main()
{
    const unsigned width = 400;
    const unsigned height = 300;

    RendererAgg renderer(width, height, 100.0);
    renderer.clear();

    /* A zig-zag polyline in device coordinates. Vertices are a plain n x 2
       C array with no codes, which PathIterator walks as MOVETO then LINETOs. */
    const double vertices[] = {
        20.0,  20.0,
        380.0, 280.0,
        20.0,  280.0,
        380.0, 20.0,
    };
    mpl::PathIterator path(vertices, nullptr, 4);

    GCAgg gc;
    gc.linewidth = 3.0;
    gc.alpha = 1.0;
    gc.forced_alpha = false;
    gc.isaa = true;
    gc.color = agg::rgba(0.0, 0.0, 1.0, 1.0);
    gc.cap = agg::round_cap;
    gc.join = agg::round_join;
    gc.snap_mode = SNAP_FALSE;
    /* An empty clip rect means "no clipping" to set_clipbox. */
    gc.cliprect = agg::rect_d(0.0, 0.0, 0.0, 0.0);

    agg::trans_affine trans;              /* identity */
    agg::rgba face(0.0, 0.0, 0.0, 0.0);   /* unfilled: stroke only */

    renderer.draw_path(gc, path, trans, face);

    /* Did anything land on the canvas? clear() leaves every pixel fully
       transparent, so any non-zero alpha is ink from draw_path. */
    size_t inked = 0;
    for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i) {
        if (renderer.pixBuffer[4 * i + 3] != 0) {
            ++inked;
        }
    }

    /* Find which buffer row the horizontal segment (y = 280) landed on, which
       pins the buffer's y orientation. It inks far more pixels in its row than
       the diagonals, so the densest row is it. */
    unsigned densest_row = 0;
    size_t densest_count = 0;
    for (unsigned row = 0; row < height; ++row) {
        size_t count = 0;
        for (unsigned col = 0; col < width; ++col) {
            if (renderer.pixBuffer[4 * (static_cast<size_t>(row) * width + col) + 3] != 0) {
                ++count;
            }
        }
        if (count > densest_count) {
            densest_count = count;
            densest_row = row;
        }
    }

    std::printf("canvas %ux%u, inked pixels: %zu\n", width, height, inked);
    std::printf("horizontal segment at y=280 landed on buffer row %u (%zu px)\n",
                densest_row, densest_count);

    /* draw_path applies scaling(1, -1) then translation(0, height), so input
       coordinates are y-up (origin bottom-left) while the buffer comes out
       top-down. y = 280 on a 300px canvas must land near row 20, not 280. */
    const unsigned expected_row = height - 280;
    if (densest_row + 5 < expected_row || densest_row > expected_row + 5) {
        std::fprintf(stderr,
                     "FAIL: expected the y=280 segment near row %u, got row %u --"
                     " the buffer's y orientation is not what the transform layer assumes\n",
                     expected_row, densest_row);
        return 1;
    }

    if (inked == 0) {
        std::fprintf(stderr, "FAIL: draw_path produced no output\n");
        return 1;
    }

    /* A 3px stroke over ~1250px of polyline should cover a few thousand pixels.
       The bound is loose: this asserts the line was drawn, not pixel fidelity. */
    if (inked < 1000) {
        std::fprintf(stderr, "FAIL: only %zu inked pixels, expected thousands\n", inked);
        return 1;
    }

    if (!write_ppm("smoke_draw_path.ppm", renderer.pixBuffer, width, height)) {
        std::fprintf(stderr, "FAIL: could not write smoke_draw_path.ppm\n");
        return 1;
    }

    std::printf("OK: wrote smoke_draw_path.ppm\n");
    return 0;
}
