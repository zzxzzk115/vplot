/*
 * Vector export, driven through the C ABI. Uses only include/vpl/vpl.h, so it
 * also proves the public surface is self-sufficient with no C++ header leaking
 * out of the library.
 */

#include "vpl/vpl.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static void check(VplResult r, const char *what)
{
    if (r != VplResult_Success) {
        fprintf(stderr, "FAIL: %s -> %s (%s)\n", what, vplResultToString(r),
                vplGetLastError());
        ++failures;
    }
}

static long file_size(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return -1;
    }
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fclose(f);
    return n;
}

/* Confirms the file really is what its extension claims, rather than trusting
   that a nonzero size means success. */
static int starts_with(const char *path, const char *prefix)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return 0;
    }
    char buf[16] = {0};
    const size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    if (n == 0) {
        return 0;
    }
    return strncmp(buf, prefix, strlen(prefix)) == 0;
}

/* Reads a whole file, so a test can look at what was actually written rather
   than at how many bytes it was. */
static char *slurp(const char *path, long *out_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    const size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = 0;
    if (out_size) {
        *out_size = (long)got;
    }
    return buf;
}

static void expect_contains(const char *path, const char *needle, const char *what)
{
    long size = 0;
    char *text = slurp(path, &size);
    if (!text) {
        fprintf(stderr, "FAIL: %s: could not read %s\n", what, path);
        ++failures;
        return;
    }
    if (strstr(text, needle) == NULL) {
        fprintf(stderr, "FAIL: %s: %s does not contain \"%s\"\n", what, path, needle);
        ++failures;
    }
    free(text);
}

/*
 * Checks the exact page coordinates the vector backends emit, worked out by
 * hand. The figure is pinned so its arithmetic is fully determined:
 *
 *   canvas    6.4 x 4.8 inches at 72 dpi  ->  460.8 x 345.6 points
 *   axes box  figure.subplot 0.125 .. 0.9 and 0.11 .. 0.88
 *             -> x 57.6 .. 414.72,  y 38.016 .. 304.128
 *   data      (0, 0) and (10, 1) pinned to the corners of that box
 *
 * SVG counts y downwards (canvas height minus those); PDF counts y upwards
 * like vplot, so its numbers are those exactly.
 */
static void check_vector_geometry(void)
{
    VplFigureDesc fd;
    memset(&fd, 0, sizeof(fd));
    fd.structSize = sizeof(fd);
    fd.widthInches = 6.4;
    fd.heightInches = 4.8;
    fd.dpi = 100.0; /* irrelevant: the vector backends draw at 72 */

    VplFigure *fig = NULL;
    check(vplCreateFigure(&fd, &fig), "geometry: vplCreateFigure");
    if (!fig) {
        return;
    }

    VplAxes *ax = NULL;
    check(vplFigureAddSubplot(fig, 1, 1, 1, &ax), "geometry: vplFigureAddSubplot");

    static const double gx[2] = {0.0, 10.0};
    static const double gy[2] = {0.0, 1.0};
    check(vplAxesPlot(ax, gx, gy, 2, NULL, NULL), "geometry: vplAxesPlot");
    check(vplAxesSetXLim(ax, 0.0, 10.0), "geometry: vplAxesSetXLim");
    check(vplAxesSetYLim(ax, 0.0, 1.0), "geometry: vplAxesSetYLim");

    /* No ticks and no frame, so the line is the only path in the file. A
       non-NULL pointer with a count of zero means "no ticks". */
    static const double no_ticks[1] = {0.0};
    check(vplAxesSetXTicks(ax, no_ticks, NULL, 0), "geometry: vplAxesSetXTicks");
    check(vplAxesSetYTicks(ax, no_ticks, NULL, 0), "geometry: vplAxesSetYTicks");
    check(vplAxesSetSpineVisible(ax, VplSpine_Left, 0), "geometry: spine left");
    check(vplAxesSetSpineVisible(ax, VplSpine_Right, 0), "geometry: spine right");
    check(vplAxesSetSpineVisible(ax, VplSpine_Top, 0), "geometry: spine top");
    check(vplAxesSetSpineVisible(ax, VplSpine_Bottom, 0), "geometry: spine bottom");

    check(vplFigureSaveFig(fig, "test_geometry.svg", NULL), "geometry: save svg");
    check(vplFigureSaveFig(fig, "test_geometry.pdf", NULL), "geometry: save pdf");
    vplDestroyFigure(fig);

    expect_contains("test_geometry.svg", "M 57.6 307.584 L 414.72 41.472",
                    "SVG line geometry");
    expect_contains("test_geometry.pdf", "57.6 38.016 m", "PDF line start");
    expect_contains("test_geometry.pdf", "414.72 304.128 l", "PDF line end");
}

int main(void)
{
    VplFigureDesc fd;
    memset(&fd, 0, sizeof(fd));
    fd.structSize = sizeof(fd);
    fd.widthInches = 6.4;
    fd.heightInches = 4.8;
    fd.dpi = 100.0;

    VplFigure *fig = NULL;
    check(vplCreateFigure(&fd, &fig), "vplCreateFigure");
    if (!fig) {
        return 1;
    }

    VplAxes *ax = NULL;
    check(vplFigureAddSubplot(fig, 1, 1, 1, &ax), "vplFigureAddSubplot");

    enum { N = 200 };
    static double xs[N], ys[N], zs[N];
    for (int i = 0; i < N; ++i) {
        const double t = i * (4.0 * 3.14159265358979323846) / (N - 1);
        xs[i] = t;
        ys[i] = sin(t);
        zs[i] = sin(t) * exp(-t / 8.0);
    }

    check(vplAxesPlot(ax, xs, ys, N, NULL, NULL), "vplAxesPlot(sine)");

    /* Exercise the desc path too: an explicit colour and width. */
    VplLineDesc ld;
    memset(&ld, 0, sizeof(ld));
    ld.structSize = sizeof(ld);
    ld.set = VplLineField_Color | VplLineField_Width;
    ld.color[0] = 0.84f;
    ld.color[1] = 0.15f;
    ld.color[2] = 0.16f;
    ld.color[3] = 1.0f;
    ld.width = 2.0;
    check(vplAxesPlot(ax, xs, zs, N, &ld, NULL), "vplAxesPlot(damped)");

    check(vplAxesSetTitle(ax, "vplot vector export"), "vplAxesSetTitle");
    check(vplAxesSetXLabel(ax, "t [s]"), "vplAxesSetXLabel");
    check(vplAxesSetYLabel(ax, "amplitude"), "vplAxesSetYLabel");

    check(vplFigureSaveFig(fig, "test_export.svg", NULL), "vplFigureSaveFig(svg)");
    check(vplFigureSaveFig(fig, "test_export.pdf", NULL), "vplFigureSaveFig(pdf)");
    check(vplFigureSaveFig(fig, "test_export.png", NULL), "vplFigureSaveFig(png)");

    /* A higher-dpi raster copy, which is what a slide or a poster wants. */
    VplSaveDesc hidpi;
    memset(&hidpi, 0, sizeof(hidpi));
    hidpi.structSize = sizeof(hidpi);
    hidpi.format = VplFormat_PNG;
    hidpi.dpi = 200.0;
    check(vplFigureSaveFig(fig, "test_export@2x.png", &hidpi), "vplFigureSaveFig(png 200dpi)");

    check_vector_geometry();

    const long svg_bytes = file_size("test_export.svg");
    const long pdf_bytes = file_size("test_export.pdf");
    const long png_bytes = file_size("test_export.png");
    const long png2x_bytes = file_size("test_export@2x.png");
    printf("test_export.svg:    %ld bytes\n", svg_bytes);
    printf("test_export.pdf:    %ld bytes\n", pdf_bytes);
    printf("test_export.png:    %ld bytes\n", png_bytes);
    printf("test_export@2x.png: %ld bytes\n", png2x_bytes);

    if (!starts_with("test_export.png", "\x89PNG")) {
        fprintf(stderr, "FAIL: png does not start with the PNG signature\n");
        ++failures;
    }
    /* Doubling the dpi quadruples the pixel count, so the file must grow. */
    if (png2x_bytes <= png_bytes) {
        fprintf(stderr, "FAIL: the 200dpi png (%ld) is not larger than the 100dpi one (%ld)\n",
                png2x_bytes, png_bytes);
        ++failures;
    }

    if (!starts_with("test_export.svg", "<?xml")) {
        fprintf(stderr, "FAIL: svg does not start with an XML declaration\n");
        ++failures;
    }
    if (!starts_with("test_export.pdf", "%PDF-")) {
        fprintf(stderr, "FAIL: pdf does not start with a %%PDF- header\n");
        ++failures;
    }

    /* Text becomes outlines, so a figure with a title and labels carries a lot
       of path data; a few hundred bytes would mean the glyphs went missing. */
    if (svg_bytes < 20000) {
        fprintf(stderr, "FAIL: svg is suspiciously small (%ld bytes)\n", svg_bytes);
        ++failures;
    }
    if (pdf_bytes < 20000) {
        fprintf(stderr, "FAIL: pdf is suspiciously small (%ld bytes)\n", pdf_bytes);
        ++failures;
    }

    /* An unknown extension must be rejected rather than guessed at. */
    if (vplFigureSaveFig(fig, "test_export.bogus", NULL) != VplResult_InvalidArgument) {
        fprintf(stderr, "FAIL: unknown extension was not rejected\n");
        ++failures;
    }

    vplDestroyFigure(fig);

    if (failures != 0) {
        fprintf(stderr, "\n%d export check(s) failed\n", failures);
        return 1;
    }
    printf("\nall export checks passed\n");
    return 0;
}
