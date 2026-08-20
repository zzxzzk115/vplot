/*
 * Exercises the artist features added on top of the Stage 0 slice: line styles,
 * markers, grid, legend and log scales -- all through the C ABI, and out to all
 * three backends.
 */

#include "vpl/vpl.h"

#include <math.h>
#include <stdio.h>
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

static void line_desc(VplLineDesc *d,
                      const char *label,
                      VplLineStyle style,
                      VplMarker marker)
{
    memset(d, 0, sizeof(*d));
    d->structSize = sizeof(*d);
    d->set = VplLineField_Label | VplLineField_LineStyle | VplLineField_Marker;
    d->label = label;
    d->linestyle = style;
    d->marker = marker;
}

int main(void)
{
    /* ---- Linear axes: styles, markers, grid, legend ---------------------- */

    VplFigure *fig = NULL;
    check(vplCreateFigure(NULL, &fig), "vplCreateFigure");
    if (!fig) {
        return 1;
    }

    VplAxes *ax = NULL;
    check(vplFigureAddSubplot(fig, 1, 1, 1, &ax), "vplFigureAddSubplot");

    enum { N = 24 };
    static double xs[N], a[N], b[N], c[N];
    for (int i = 0; i < N; ++i) {
        const double t = i * 6.0 / (N - 1);
        xs[i] = t;
        a[i] = sin(t);
        b[i] = cos(t) * 0.7;
        c[i] = sin(t * 0.5) * 0.4;
    }

    VplLineDesc d;
    line_desc(&d, "sin", VplLineStyle_Solid, VplMarker_Circle);
    check(vplAxesPlot(ax, xs, a, N, &d, NULL), "plot(sin)");

    line_desc(&d, "cos", VplLineStyle_Dashed, VplMarker_Square);
    check(vplAxesPlot(ax, xs, b, N, &d, NULL), "plot(cos)");

    line_desc(&d, "half", VplLineStyle_DashDot, VplMarker_TriangleUp);
    check(vplAxesPlot(ax, xs, c, N, &d, NULL), "plot(half)");

    check(vplAxesSetGrid(ax, 1, VplGridWhich_Major, VplGridAxis_Both), "vplAxesSetGrid");
    check(vplAxesSetLegend(ax, 1, VplLegendLoc_UpperRight), "vplAxesSetLegend");
    check(vplAxesSetTitle(ax, "styles, markers, grid, legend"), "setTitle");
    check(vplAxesSetXLabel(ax, "t"), "setXLabel");
    check(vplAxesSetYLabel(ax, "value"), "setYLabel");

    uint32_t w = 0, h = 0;
    check(vplFigureGetPixelSize(fig, &w, &h), "getPixelSize");

    static uint8_t pixels[640 * 480 * 4];
    if ((size_t)w * h * 4 > sizeof(pixels)) {
        fprintf(stderr, "FAIL: default figure larger than the scratch buffer\n");
        return 1;
    }
    check(vplFigureRenderRGBA(fig, pixels, sizeof(pixels), NULL), "renderRGBA");

    FILE *f = fopen("test_showcase.ppm", "wb");
    if (f) {
        fprintf(f, "P6\n%u %u\n255\n", w, h);
        for (unsigned i = 0; i < w * h; ++i) {
            const uint8_t *px = pixels + 4 * (size_t)i;
            const double al = px[3] / 255.0;
            for (int k = 0; k < 3; ++k) {
                fputc((int)(px[k] * al + 255.0 * (1.0 - al) + 0.5), f);
            }
        }
        fclose(f);
    } else {
        fprintf(stderr, "FAIL: could not write test_showcase.ppm\n");
        ++failures;
    }

    check(vplFigureSaveFig(fig, "test_showcase.svg", NULL), "saveFig(svg)");
    check(vplFigureSaveFig(fig, "test_showcase.pdf", NULL), "saveFig(pdf)");
    vplDestroyFigure(fig);

    /* ---- Log axes -------------------------------------------------------- */

    VplFigure *logfig = NULL;
    check(vplCreateFigure(NULL, &logfig), "vplCreateFigure(log)");
    VplAxes *lax = NULL;
    check(vplFigureAddSubplot(logfig, 1, 1, 1, &lax), "addSubplot(log)");

    enum { M = 60 };
    static double lx[M], ly[M], lz[M];
    for (int i = 0; i < M; ++i) {
        lx[i] = pow(10.0, i * 4.0 / (M - 1));   /* 1 .. 1e4 */
        ly[i] = lx[i] * lx[i];                  /* quadratic: slope 2 on log-log */
        lz[i] = 1000.0 * sqrt(lx[i]);           /* slope 0.5 */
    }

    line_desc(&d, "O(n^2)", VplLineStyle_Solid, VplMarker_None);
    check(vplAxesPlot(lax, lx, ly, M, &d, NULL), "plot(quadratic)");
    line_desc(&d, "O(sqrt n)", VplLineStyle_Dotted, VplMarker_None);
    check(vplAxesPlot(lax, lx, lz, M, &d, NULL), "plot(sqrt)");

    check(vplAxesSetXScale(lax, VplScale_Log), "setXScale");
    check(vplAxesSetYScale(lax, VplScale_Log), "setYScale");
    check(vplAxesSetGrid(lax, 1, VplGridWhich_Major, VplGridAxis_Both), "setGrid(log)");
    check(vplAxesSetLegend(lax, 1, VplLegendLoc_UpperLeft), "setLegend(log)");
    check(vplAxesSetTitle(lax, "log-log scaling"), "setTitle(log)");

    check(vplFigureRenderRGBA(logfig, pixels, sizeof(pixels), NULL), "renderRGBA(log)");

    f = fopen("test_showcase_log.ppm", "wb");
    if (f) {
        fprintf(f, "P6\n%u %u\n255\n", w, h);
        for (unsigned i = 0; i < w * h; ++i) {
            const uint8_t *px = pixels + 4 * (size_t)i;
            const double al = px[3] / 255.0;
            for (int k = 0; k < 3; ++k) {
                fputc((int)(px[k] * al + 255.0 * (1.0 - al) + 0.5), f);
            }
        }
        fclose(f);
    }

    check(vplFigureSaveFig(logfig, "test_showcase_log.svg", NULL), "saveFig(log svg)");

    /* A log axis must reject non-positive data by excluding it, not by
       producing NaNs that poison the view. */
    double xmin = 0.0, ymin = 0.0, xmax = 0.0, ymax = 0.0;
    check(vplAxesGetViewLimits(lax, &xmin, &ymin, &xmax, &ymax), "getViewLimits(log)");
    if (!(xmin > 0.0) || !(ymin > 0.0) || !(xmax > xmin) || !(ymax > ymin)) {
        fprintf(stderr, "FAIL: log view limits are not positive and ordered: "
                        "x [%g, %g] y [%g, %g]\n", xmin, xmax, ymin, ymax);
        ++failures;
    }
    printf("log view: x [%g, %g]  y [%g, %g]\n", xmin, xmax, ymin, ymax);

    vplDestroyFigure(logfig);

    /* Bad enum values must be rejected rather than silently defaulted. */
    VplFigure *tmp = NULL;
    vplCreateFigure(NULL, &tmp);
    VplAxes *tax = NULL;
    vplFigureAddSubplot(tmp, 1, 1, 1, &tax);
    if (vplAxesSetXScale(tax, (VplScale)99) != VplResult_InvalidArgument) {
        fprintf(stderr, "FAIL: bogus scale was accepted\n");
        ++failures;
    }
    if (vplAxesSetLegend(tax, 1, (VplLegendLoc)99) != VplResult_InvalidArgument) {
        fprintf(stderr, "FAIL: bogus legend location was accepted\n");
        ++failures;
    }
    vplDestroyFigure(tmp);

    if (failures != 0) {
        fprintf(stderr, "\n%d showcase check(s) failed\n", failures);
        return 1;
    }
    printf("\nall showcase checks passed\n");
    return 0;
}
