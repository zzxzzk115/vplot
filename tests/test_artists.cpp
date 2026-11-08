/*
 * Exercises the common artists through the C ABI: error bars, confidence
 * bands, bars, reference lines, colour specs, format strings, per-axis limits
 * and a 2x2 subplot grid.
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

static void expect_invalid(VplResult r, const char *what)
{
    if (r != VplResult_InvalidArgument) {
        fprintf(stderr, "FAIL: %s should have been rejected (got %s)\n", what,
                vplResultToString(r));
        ++failures;
    }
}

int main(void)
{
    VplFigureDesc fd;
    memset(&fd, 0, sizeof(fd));
    fd.structSize = sizeof(fd);
    fd.widthInches = 9.0;
    fd.heightInches = 7.0;
    fd.dpi = 100.0;

    VplFigure *fig = NULL;
    check(vplCreateFigure(&fd, &fig), "vplCreateFigure");
    if (!fig) {
        return 1;
    }

    VplLineDesc ld;
    VplPatchDesc pd;

    /* ---- 1: error bars ---------------------------------------------------- */
    VplAxes *ax1 = NULL;
    check(vplFigureAddSubplot(fig, 2, 2, 1, &ax1), "addSubplot(1)");
    {
        enum { N = 12 };
        double x[N], y[N], e[N];
        for (int i = 0; i < N; ++i) {
            x[i] = i + 1.0;
            y[i] = 10.0 / x[i];
            e[i] = y[i] * 0.15;
        }
        memset(&ld, 0, sizeof(ld));
        ld.structSize = sizeof(ld);
        ld.set = VplLineField_Format | VplLineField_Label;
        ld.format = "o-";
        ld.label = "measured";
        check(vplAxesErrorBar(ax1, x, y, e, N, 3.0, &ld, NULL), "errorbar");

        memset(&ld, 0, sizeof(ld));
        ld.structSize = sizeof(ld);
        ld.set = VplLineField_ColorSpec | VplLineField_LineStyle;
        ld.colorSpec = "0.4";
        ld.linestyle = VplLineStyle_Dashed;
        check(vplAxesAxHLine(ax1, 1.0, &ld), "axhline");

        check(vplAxesSetTitle(ax1, "error bars"), "title(1)");
        check(vplAxesSetLegend(ax1, 1, VplLegendLoc_UpperRight), "legend(1)");
    }

    /* ---- 2: confidence band ----------------------------------------------- */
    VplAxes *ax2 = NULL;
    check(vplFigureAddSubplot(fig, 2, 2, 2, &ax2), "addSubplot(2)");
    {
        enum { N = 120 };
        double x[N], mid[N], lo[N], hi[N];
        for (int i = 0; i < N; ++i) {
            x[i] = i * 8.0 / (N - 1);
            mid[i] = sin(x[i]);
            const double band = 0.15 + 0.05 * x[i];
            lo[i] = mid[i] - band;
            hi[i] = mid[i] + band;
        }

        memset(&pd, 0, sizeof(pd));
        pd.structSize = sizeof(pd);
        pd.set = VplPatchField_FaceColorSpec | VplPatchField_Alpha;
        pd.faceColorSpec = "tab:blue";
        pd.alpha = 0.25;
        check(vplAxesFillBetween(ax2, x, lo, hi, N, NULL, 0, VplFillStep_None, &pd, NULL), "fillBetween");

        memset(&ld, 0, sizeof(ld));
        ld.structSize = sizeof(ld);
        ld.set = VplLineField_ColorSpec | VplLineField_Label;
        ld.colorSpec = "tab:blue";
        ld.label = "mean";
        check(vplAxesPlot(ax2, x, mid, N, &ld, NULL), "plot(mean)");

        check(vplAxesSetTitle(ax2, "confidence band"), "title(2)");
        check(vplAxesSetGrid(ax2, 1, VplGridWhich_Major, VplGridAxis_Both), "grid(2)");
        /* Pin y only; x keeps autoscaling. */
        check(vplAxesSetYLim(ax2, -2.0, 2.0), "setYLim");
    }

    /* ---- 3: bars ----------------------------------------------------------- */
    VplAxes *ax3 = NULL;
    check(vplFigureAddSubplot(fig, 2, 2, 3, &ax3), "addSubplot(3)");
    {
        enum { N = 7 };
        double x[N], h[N];
        for (int i = 0; i < N; ++i) {
            x[i] = i;
            h[i] = 3.0 + 2.0 * sin(i * 0.9);
        }
        memset(&pd, 0, sizeof(pd));
        pd.structSize = sizeof(pd);
        pd.set = VplPatchField_FaceColorSpec | VplPatchField_EdgeWidth;
        pd.faceColorSpec = "C2";
        pd.edgeWidth = 0.8;
        check(vplAxesBar(ax3, x, h, 0.7, NULL, N, &pd, NULL), "bar");
        check(vplAxesSetTitle(ax3, "bars"), "title(3)");
    }

    /* ---- 4: format strings ------------------------------------------------- */
    VplAxes *ax4 = NULL;
    check(vplFigureAddSubplot(fig, 2, 2, 4, &ax4), "addSubplot(4)");
    {
        enum { N = 20 };
        double x[N], a[N], b[N], c[N];
        for (int i = 0; i < N; ++i) {
            x[i] = i * 5.0 / (N - 1);
            a[i] = sin(x[i]);
            b[i] = cos(x[i]);
            c[i] = sin(x[i]) * cos(x[i]);
        }
        const char *formats[] = {"r--o", "g:s", "k^"};
        const char *labels[] = {"r--o", "g:s", "k^"};
        double *series[] = {a, b, c};
        for (int k = 0; k < 3; ++k) {
            memset(&ld, 0, sizeof(ld));
            ld.structSize = sizeof(ld);
            ld.set = VplLineField_Format | VplLineField_Label;
            ld.format = formats[k];
            ld.label = labels[k];
            check(vplAxesPlot(ax4, x, series[k], N, &ld, NULL), "plot(format)");
        }
        check(vplAxesSetTitle(ax4, "format strings"), "title(4)");
        check(vplAxesSetLegend(ax4, 1, VplLegendLoc_LowerLeft), "legend(4)");
    }

    /* Without this the bottom row's titles overlap the top row's tick labels. */
    check(vplFigureTightLayout(fig), "tightLayout");

    /* ---- render and export -------------------------------------------------- */
    uint32_t w = 0, h = 0;
    check(vplFigureGetPixelSize(fig, &w, &h), "getPixelSize");

    static uint8_t pixels[900 * 700 * 4];
    check(vplFigureRenderRGBA(fig, pixels, sizeof(pixels), NULL), "renderRGBA");

    FILE *f = fopen("test_artists.ppm", "wb");
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
    check(vplFigureSaveFig(fig, "test_artists.pdf", NULL), "saveFig(pdf)");

    /* ---- the newer entries, through the C ABI ------------------------------- */
    /* Verifies the C declarations compile, link and accept valid arguments. */
    {
        VplFigure *f2 = NULL;
        check(vplCreateFigure(NULL, &f2), "createFigure(2)");
        VplAxes *a = NULL;
        check(vplFigureAddSubplot(f2, 1, 1, 1, &a), "addSubplot(abi)");

        const double ry[2] = {1.0, 2.0};
        const double rx0[2] = {0.0, 0.5};
        const double rx1[2] = {2.0, 1.5};
        check(vplAxesHLines(a, ry, rx0, rx1, 2, NULL, NULL), "hlines");
        check(vplAxesVLines(a, rx0, ry, rx1, 2, NULL, NULL), "vlines");

        const double ev[3] = {0.2, 0.5, 1.1};
        check(vplAxesEventPlot(a, ev, 3, 1.0, 1.0, 1, NULL, NULL), "eventplot");
        expect_invalid(vplAxesEventPlot(a, ev, 3, 1.0, 0.0, 1, NULL, NULL),
                       "zero event line length");

        const double sx[3] = {0.0, 1.0, 2.0};
        const double sy[3] = {1.0, 2.0, 0.5};
        check(vplAxesStem(a, sx, sy, 3, NULL, NULL, NULL, 0.0), "stem");

        const double vals[3] = {1.0, 2.0, 1.5};
        const double edges[4] = {0.0, 1.0, 2.0, 3.0};
        check(vplAxesStairs(a, vals, edges, 3, 0, NULL, 0, 0, NULL, NULL), "stairs");

        const double px[4] = {0.0, 1.0, 1.0, 0.0};
        const double py[4] = {0.0, 0.0, 1.0, 1.0};
        check(vplAxesFill(a, px, py, 4, NULL, NULL), "fill");
        expect_invalid(vplAxesFill(a, px, py, 2, NULL, NULL), "polygon of two points");

        const double bs[2] = {0.0, 2.0};
        const double bw[2] = {1.0, 1.0};
        check(vplAxesBrokenBarH(a, bs, bw, 2, 5.0, 1.0, NULL, NULL), "brokenBarH");
        expect_invalid(vplAxesBrokenBarH(a, bs, bw, 2, 5.0, 0.0, NULL, NULL),
                       "zero broken bar height");

        const double stack[6] = {1.0, 2.0, 1.0, 2.0, 1.0, 2.0};
        check(vplAxesStackPlot(a, sx, stack, 2, 3, VplStackBaseline_Zero), "stackPlot");

        const double box[8] = {1.0, 2.0, 3.0, 4.0, 10.0, 2.0, 3.0, 4.0};
        const size_t counts[2] = {5, 3};
        check(vplAxesBoxPlot(a, box, counts, 2), "boxPlot");
        const size_t bad_counts[2] = {5, 0};
        expect_invalid(vplAxesBoxPlot(a, box, bad_counts, 2), "empty box group");
        check(vplAxesViolinPlot(a, box, counts, 2), "violinPlot");
        const size_t thin[2] = {7, 1};
        expect_invalid(vplAxesViolinPlot(a, box, thin, 2), "one-value violin group");

        VplTickParams tp;
        check(vplAxesGetTickParams(a, VplAxis_X, &tp), "getTickParams");
        tp.direction = VplTickDirection_InOut;
        tp.length = 6.0;
        check(vplAxesSetTickParams(a, VplAxis_Both, &tp), "setTickParams");
        expect_invalid(vplAxesGetTickParams(a, VplAxis_Both, &tp), "getTickParams(both)");

        /* tick_params(colors=, labelcolor=): the two are independent. */
        check(vplAxesGetTickParams(a, VplAxis_X, &tp), "getTickParams(for colours)");
        tp.color[0] = 1.0f;
        tp.color[1] = 0.0f;
        tp.color[2] = 0.0f;
        tp.color[3] = 1.0f;
        tp.labelColor[0] = 0.0f;
        tp.labelColor[1] = 1.0f;
        tp.labelColor[2] = 0.0f;
        tp.labelColor[3] = 1.0f;
        check(vplAxesSetTickParams(a, VplAxis_X, &tp), "setTickParams(colours)");
        VplTickParams tp_readback;
        check(vplAxesGetTickParams(a, VplAxis_X, &tp_readback), "getTickParams(readback)");
        if (tp_readback.color[0] != 1.0f || tp_readback.color[1] != 0.0f ||
            tp_readback.labelColor[1] != 1.0f || tp_readback.labelColor[0] != 0.0f) {
            fprintf(stderr, "FAIL: tick colour/labelColor did not round-trip\n");
            ++failures;
        }

        check(vplAxesSetYInverted(a, 1), "setYInverted");

        /* The simple accessors: each set must be visible to its get. */
        int on = -1;
        check(vplAxesSetAutoscaleY(a, 0), "setAutoscaleY(off)");
        check(vplAxesGetAutoscaleY(a, &on), "getAutoscaleY");
        if (on != 0) { fprintf(stderr, "autoscale y did not turn off\n"); ++failures; }
        check(vplAxesSetAutoscale(a, 1), "setAutoscale(on)");
        check(vplAxesGetAutoscale(a, &on), "getAutoscale");
        if (on != 1) { fprintf(stderr, "autoscale did not turn on\n"); ++failures; }
        check(vplAxesGetAutoscaleX(a, &on), "getAutoscaleX");

        double rgba[4] = {0, 0, 0, 0};
        check(vplAxesSetFaceColor(a, "#123456"), "setFaceColor");
        check(vplAxesGetFaceColor(a, rgba), "getFaceColor");
        if (rgba[0] < 0.06 || rgba[0] > 0.08) {
            fprintf(stderr, "face colour read back as %.3f, not 0x12/255\n", rgba[0]);
            ++failures;
        }
        expect_invalid(vplAxesSetFaceColor(a, "not-a-colour"), "bogus axes face colour");

        double ratio = 0.0;
        check(vplAxesGetDataRatio(a, &ratio), "getDataRatio");
        check(vplAxesUpdateDataLim(a, 0.0, 0.0, 5.0, 5.0), "updateDataLim");

        /* The label strings, in the two-pass form the header describes. */
        size_t label_count = 0;
        check(vplAxesGetXTickLabels(a, NULL, 0, 0, &label_count), "getXTickLabels(count)");
        if (label_count != 0) {
            char storage[16][32];
            char *ptrs[16];
            /* Initialise all pointers, since the y axis may want more than the
               x axis did. */
            for (size_t i = 0; i < 16; ++i) { ptrs[i] = storage[i]; }
            const size_t n = label_count < 16 ? label_count : 16;
            check(vplAxesGetXTickLabels(a, ptrs, n, 32, &label_count), "getXTickLabels");
            /* The two axes need not have the same number of ticks, so the y
               count is asked for separately rather than reused. */
            size_t y_count = 0;
            check(vplAxesGetYTickLabels(a, NULL, 0, 0, &y_count), "getYTickLabels(count)");
            if (y_count <= 16) {
                check(vplAxesGetYTickLabels(a, ptrs, y_count, 32, &y_count),
                      "getYTickLabels");
            }
            expect_invalid(vplAxesGetXTickLabels(a, ptrs, 1, 32, &label_count),
                           "fewer label pointers than ticks");
        }

        check(vplFigureGetFaceColor(f2, rgba), "figureGetFaceColor");
        check(vplFigureGetEdgeColor(f2, rgba), "figureGetEdgeColor");

        VplAxes *current = NULL;
        check(vplFigureGca(f2, &current), "gca");
        check(vplFigureSca(f2, a), "sca");
        check(vplFigureGca(f2, &current), "gca(after sca)");
        if (current != a) { fprintf(stderr, "gca did not return what sca set\n"); ++failures; }

        const double field[9] = {0.0, 1.0, 0.0, 1.0, 0.5, 1.0, 0.0, 1.0, 0.0};
        const double flevels[3] = {0.25, 0.6, 0.9};
        check(vplAxesContourF(a, field, 3, 3, flevels, 3, NULL, NULL), "contourF");

        const double xe[4] = {0.0, 1.0, 2.5, 3.0};
        const double ye[3] = {0.0, 1.0, 3.0};
        const double mesh[6] = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
        check(vplAxesPcolorMesh(a, xe, ye, mesh, 2, 3, NULL, NULL), "pcolorMesh");
        check(vplAxesMatShow(a, mesh, 2, 3, NULL, NULL), "matShow");
        check(vplAxesSpy(a, mesh, 2, 3, NULL, NULL), "spy");
        check(vplAxesHist2D(a, sx, sy, 3, 2, 2), "hist2D");
        expect_invalid(vplAxesHist2D(a, sx, sy, 3, 0, 2), "zero x bins");

        VplLine *eline = NULL;
        check(vplAxesEcdf(a, sy, 3, NULL, 0, 0, NULL, &eline), "ecdf");
        check(vplAxesArrow(a, 0.0, 0.0, 1.0, 1.0, 0.05, NULL, NULL), "arrow");
        expect_invalid(vplAxesArrow(a, 0.0, 0.0, 0.0, 0.0, 0.05, NULL, NULL),
                       "arrow with no direction");

        VplLine *ebar = NULL;
        check(vplAxesErrorBar(a, sx, sy, sy, 3, 3.0, NULL, &ebar), "errorbar(for xerr)");
        check(vplAxesSetXErr(a, ebar, sy, 3), "setXErr");
        expect_invalid(vplAxesSetXErr(a, ebar, sy, 2), "xerr of the wrong length");

        check(vplAxesAxLine(a, 0.0, 0.0, 1.0, 0, NULL), "axLine");
        check(vplAxesAxLine(a, 1.0, 0.0, 0.0, 1, NULL), "axLine(vertical)");
        /* Built at run time: 1.0/0.0 as a literal is a compile-time divide by
           zero, which MSVC refuses outright. */
        const double zero = counts[0] * 0.0;
        expect_invalid(vplAxesAxLine(a, 0.0, 0.0, 1.0 / zero, 0, NULL),
                       "infinite slope without the vertical flag");

        VplPatch *labelled = NULL;
        check(vplAxesBar(a, sx, sy, 0.5, NULL, 3, NULL, &labelled), "bar(for label)");
        check(vplAxesBarLabel(a, labelled), "barLabel");

        check(vplAxesBarLabelFull(a, labelled, "%.2f", VplBarLabelType_Edge, 4.0),
              "barLabelFull(edge)");
        check(vplAxesBarLabelFull(a, labelled, "%+g", VplBarLabelType_Center, 0.0),
              "barLabelFull(center, flag)");
        check(vplAxesBarLabelFull(a, labelled, "%6.1f", VplBarLabelType_Edge, 0.0),
              "barLabelFull(width+precision)");
        /* fmt reaches snprintf with one double argument, so any other or
           non-floating-point conversion must be rejected. */
        expect_invalid(vplAxesBarLabelFull(a, labelled, "%s", VplBarLabelType_Edge, 0.0),
                       "barLabelFull(%s)");
        expect_invalid(vplAxesBarLabelFull(a, labelled, "%d", VplBarLabelType_Edge, 0.0),
                       "barLabelFull(%d)");
        expect_invalid(vplAxesBarLabelFull(a, labelled, "%n", VplBarLabelType_Edge, 0.0),
                       "barLabelFull(%n)");
        expect_invalid(
            vplAxesBarLabelFull(a, labelled, "%g %g", VplBarLabelType_Edge, 0.0),
            "barLabelFull(two conversions)");
        expect_invalid(vplAxesBarLabelFull(a, labelled, "no conversion at all",
                                           VplBarLabelType_Edge, 0.0),
                       "barLabelFull(no conversion)");
        expect_invalid(vplAxesBarLabelFull(a, labelled, NULL, VplBarLabelType_Edge, 0.0),
                       "barLabelFull(null fmt)");
        check(vplAxesBarLabelFull(a, labelled, "100%% done: %g", VplBarLabelType_Edge, 0.0),
              "barLabelFull(literal percent)");

        check(vplAxesLocatorParams(a, VplAxis_Both, 4), "locatorParams(nbins)");
        const double steps135[3] = {1.0, 3.5, 10.0};
        check(vplAxesLocatorParamsFull(a, VplAxis_Both, 0, steps135, 3, -1),
              "locatorParamsFull(steps)");
        check(vplAxesLocatorParamsFull(a, VplAxis_X, 6, NULL, 0, 1),
              "locatorParamsFull(nbins+tight, no steps)");
        check(vplAxesLocatorParamsFull(a, VplAxis_Y, 0, NULL, 0, 0),
              "locatorParamsFull(tight off, nothing else touched)");
        expect_invalid(vplAxesLocatorParamsFull(a, VplAxis_Both, -1, NULL, 0, -1),
                       "locatorParamsFull(negative nbins)");
        const double descending[2] = {5.0, 2.0};
        expect_invalid(vplAxesLocatorParamsFull(a, VplAxis_Both, 0, descending, 2, -1),
                       "locatorParamsFull(non-increasing steps)");
        const double outOfRange[2] = {1.0, 20.0};
        expect_invalid(vplAxesLocatorParamsFull(a, VplAxis_Both, 0, outOfRange, 2, -1),
                       "locatorParamsFull(step above 10)");
        expect_invalid(vplAxesLocatorParamsFull(a, VplAxis_Both, 0, NULL, 3, -1),
                       "locatorParamsFull(null steps, nonzero count)");
        expect_invalid(vplAxesLocatorParamsFull(a, VplAxis_Both, 0, steps135, 0, -1),
                       "locatorParamsFull(steps given, zero count)");
        expect_invalid(vplAxesLocatorParamsFull(a, VplAxis_Both, 0, NULL, 0, 2),
                       "locatorParamsFull(tight out of range)");

        const double shares[3] = {1.0, 2.0, 3.0};
        check(vplAxesPie(a, shares, 3), "pie");
        expect_invalid(vplAxesContourF(a, field, 3, 3, flevels, 1, NULL, NULL),
                       "filled contour with one level");

        VplSubplotParams sp;
        check(vplFigureGetSubplotParams(f2, &sp), "getSubplotParams");
        sp.left = 0.2;
        sp.hspace = 0.4;
        check(vplFigureSubplotsAdjust(f2, &sp), "subplotsAdjust");
        sp.right = 0.1; /* now on the wrong side of left */
        expect_invalid(vplFigureSubplotsAdjust(f2, &sp), "right left of left");

        check(vplFigureSuptitle(f2, "a title", 0.5, 0.98, 0.0), "suptitle");

        VplAxes *inset = NULL;
        check(vplFigureAddAxes(f2, 0.6, 0.6, 0.2, 0.2, &inset), "addAxes");
        expect_invalid(vplFigureAddAxes(f2, 0.6, 0.6, 0.0, 0.2, &inset), "zero-width axes");

        unsigned w2 = 0, h2 = 0;
        check(vplFigureGetPixelSize(f2, &w2, &h2), "pixelSize(2)");
        unsigned char *px2 = (unsigned char *)malloc((size_t)w2 * h2 * 4);
        check(vplFigureRenderRGBA(f2, px2, (size_t)w2 * h2 * 4, 0), "render(2)");
        free(px2);
        vplDestroyFigure(f2);
    }

    /* ---- accessors: every get/set pair round-trips what it was given ------- */
    {
        VplFigure *f3 = NULL;
        check(vplCreateFigure(NULL, &f3), "createFigure(3)");
        VplAxes *a3 = NULL;
        check(vplFigureAddSubplot(f3, 1, 1, 1, &a3), "addSubplot(3)");

        /* has_data: false before anything is plotted, true after. */
        int has_data = 1;
        check(vplAxesHasData(a3, &has_data), "hasData(empty)");
        if (has_data) {
            fprintf(stderr, "FAIL: hasData true on an empty axes\n");
            ++failures;
        }
        const double hx[3] = {0.0, 1.0, 2.0};
        const double hy[3] = {0.0, 1.0, 0.0};
        check(vplAxesPlot(a3, hx, hy, 3, NULL, NULL), "plot(for hasData)");
        check(vplAxesHasData(a3, &has_data), "hasData(after plot)");
        if (!has_data) {
            fprintf(stderr, "FAIL: hasData false after a plot\n");
            ++failures;
        }

        /* axis labels. */
        check(vplAxesSetXLabel(a3, "the x axis"), "setXLabel");
        check(vplAxesSetYLabel(a3, "the y axis"), "setYLabel");
        char lbl[64];
        size_t lbl_len = 0;
        check(vplAxesGetXLabel(a3, lbl, sizeof(lbl), &lbl_len), "getXLabel");
        if (strcmp(lbl, "the x axis") != 0) {
            fprintf(stderr, "FAIL: getXLabel returned \"%s\"\n", lbl);
            ++failures;
        }
        check(vplAxesGetYLabel(a3, lbl, sizeof(lbl), &lbl_len), "getYLabel");
        if (strcmp(lbl, "the y axis") != 0) {
            fprintf(stderr, "FAIL: getYLabel returned \"%s\"\n", lbl);
            ++failures;
        }
        /* buffer = NULL queries the size; an undersized buffer is rejected. */
        check(vplAxesGetXLabel(a3, NULL, 0, &lbl_len), "getXLabel(size query)");
        if (lbl_len != strlen("the x axis") + 1) {
            fprintf(stderr, "FAIL: getXLabel size query returned %zu\n", lbl_len);
            ++failures;
        }
        char tiny[2];
        if (vplAxesGetXLabel(a3, tiny, sizeof(tiny), &lbl_len) != VplResult_BufferTooSmall) {
            fprintf(stderr, "FAIL: getXLabel into a too-small buffer should be rejected\n");
            ++failures;
        }

        /* scales. */
        check(vplAxesSetXScale(a3, VplScale_Log), "setXScale(log)");
        VplScale sc = VplScale_Linear;
        check(vplAxesGetXScale(a3, &sc), "getXScale");
        if (sc != VplScale_Log) {
            fprintf(stderr, "FAIL: getXScale did not read back Log\n");
            ++failures;
        }
        check(vplAxesSetXScale(a3, VplScale_Linear), "setXScale(linear, restore)");

        /* margins. */
        check(vplAxesSetMargins(a3, 0.0, 0.3), "setMargins");
        double xm = -1.0, ym = -1.0;
        check(vplAxesGetMargins(a3, &xm, &ym), "getMargins");
        if (xm != 0.0 || ym != 0.3) {
            fprintf(stderr, "FAIL: getMargins returned (%.3f, %.3f)\n", xm, ym);
            ++failures;
        }

        /* aspect. */
        check(vplAxesSetAspect(a3, 1.0), "setAspect(equal)");
        double asp = -1.0;
        check(vplAxesGetAspect(a3, &asp), "getAspect");
        if (asp != 1.0) {
            fprintf(stderr, "FAIL: getAspect returned %.3f\n", asp);
            ++failures;
        }
        check(vplAxesSetAspect(a3, 0.0), "setAspect(auto, restore)");

        /* frame and axis visibility. */
        check(vplAxesSetFrameOn(a3, 0), "setFrameOn(0)");
        int frame_on = 1;
        check(vplAxesGetFrameOn(a3, &frame_on), "getFrameOn");
        if (frame_on) {
            fprintf(stderr, "FAIL: getFrameOn true after setFrameOn(0)\n");
            ++failures;
        }
        check(vplAxesSetFrameOn(a3, 1), "setFrameOn(1, restore)");

        check(vplAxesSetAxisOn(a3, 0), "setAxisOn(0)");
        int axis_on = 1;
        check(vplAxesGetAxisOn(a3, &axis_on), "getAxisOn");
        if (axis_on) {
            fprintf(stderr, "FAIL: getAxisOn true after setAxisOn(0)\n");
            ++failures;
        }
        check(vplAxesSetAxisOn(a3, 1), "setAxisOn(1, restore)");

        /* axisbelow: all three states round-trip. */
        const VplAxisBelow ab_states[3] = {VplAxisBelow_On, VplAxisBelow_Line,
                                           VplAxisBelow_Off};
        for (int k = 0; k < 3; ++k) {
            check(vplAxesSetAxisBelow(a3, ab_states[k]), "setAxisBelow");
            VplAxisBelow ab = (VplAxisBelow)-1;
            check(vplAxesGetAxisBelow(a3, &ab), "getAxisBelow");
            if (ab != ab_states[k]) {
                fprintf(stderr, "FAIL: getAxisBelow returned %d, expected %d\n", (int)ab,
                        (int)ab_states[k]);
                ++failures;
            }
        }

        /* xticks/yticks: fixed positions round-trip exactly. */
        const double fixed_x[4] = {0.0, 0.5, 1.0, 1.5};
        const char *fixed_labels[4] = {"a", "b", "c", "d"};
        check(vplAxesSetXTicks(a3, fixed_x, fixed_labels, 4), "setXTicks");

        size_t tick_count = 0;
        check(vplAxesGetXTicks(a3, NULL, 0, &tick_count), "getXTicks(size query)");
        if (tick_count != 4) {
            fprintf(stderr, "FAIL: getXTicks size query returned %zu, expected 4\n",
                    tick_count);
            ++failures;
        }
        double tick_buf[4];
        check(vplAxesGetXTicks(a3, tick_buf, 4, &tick_count), "getXTicks(fill)");
        for (int k = 0; k < 4; ++k) {
            if (tick_buf[k] != fixed_x[k]) {
                fprintf(stderr, "FAIL: getXTicks[%d] = %.4f, expected %.4f\n", k,
                        tick_buf[k], fixed_x[k]);
                ++failures;
            }
        }
        /* A buffer smaller than the tick count is rejected, not truncated. */
        double small_buf[2];
        if (vplAxesGetXTicks(a3, small_buf, 2, &tick_count) != VplResult_BufferTooSmall) {
            fprintf(stderr, "FAIL: getXTicks into a too-small buffer should be rejected\n");
            ++failures;
        }
        check(vplAxesSetXTicks(a3, NULL, NULL, 0), "setXTicks(hand back to locator)");

        /* set_xticklabels mechanism: read the locator-chosen positions, then
           relabel exactly those. */
        check(vplAxesGetXTicks(a3, NULL, 0, &tick_count), "getXTicks(auto, size query)");
        if (tick_count > 0 && tick_count <= 32) {
            double auto_ticks[32];
            check(vplAxesGetXTicks(a3, auto_ticks, tick_count, &tick_count),
                  "getXTicks(auto, fill)");
            const char *relabel[32];
            for (size_t k = 0; k < tick_count; ++k) {
                relabel[k] = "x";
            }
            check(vplAxesSetXTicks(a3, auto_ticks, relabel, tick_count),
                  "setXTicks(relabel the auto positions)");
        }

        /* figure-level: suptitle/supxlabel/supylabel read back what was set. */
        check(vplFigureSuptitle(f3, "the figure title", 0.5, 0.98, 0.0), "suptitle(3)");
        check(vplFigureSupXLabel(f3, "shared x", 0.5, 0.01, 0.0), "supxlabel(3)");
        check(vplFigureSupYLabel(f3, "shared y", 0.02, 0.5, 0.0), "supylabel(3)");
        char sup[64];
        size_t sup_len = 0;
        check(vplFigureGetSupTitle(f3, sup, sizeof(sup), &sup_len), "getSupTitle");
        if (strcmp(sup, "the figure title") != 0) {
            fprintf(stderr, "FAIL: getSupTitle returned \"%s\"\n", sup);
            ++failures;
        }
        check(vplFigureGetSupXLabel(f3, sup, sizeof(sup), &sup_len), "getSupXLabel");
        if (strcmp(sup, "shared x") != 0) {
            fprintf(stderr, "FAIL: getSupXLabel returned \"%s\"\n", sup);
            ++failures;
        }
        check(vplFigureGetSupYLabel(f3, sup, sizeof(sup), &sup_len), "getSupYLabel");
        if (strcmp(sup, "shared y") != 0) {
            fprintf(stderr, "FAIL: getSupYLabel returned \"%s\"\n", sup);
            ++failures;
        }

        /* figure size and dpi. */
        check(vplFigureSetSizeInches(f3, 8.0, 5.0), "setSizeInches");
        double fw = 0.0, fh = 0.0;
        check(vplFigureGetSizeInches(f3, &fw, &fh), "getSizeInches");
        if (fw != 8.0 || fh != 5.0) {
            fprintf(stderr, "FAIL: getSizeInches returned (%.3f, %.3f)\n", fw, fh);
            ++failures;
        }
        check(vplFigureSetDpi(f3, 150.0), "setDpi");
        double fdpi = 0.0;
        check(vplFigureGetDpi(f3, &fdpi), "getDpi");
        if (fdpi != 150.0) {
            fprintf(stderr, "FAIL: getDpi returned %.3f\n", fdpi);
            ++failures;
        }

        /* axes enumeration: count then index, left to right / top to bottom. */
        VplAxes *grid3[4] = {NULL, NULL, NULL, NULL};
        check(vplFigureSubplots(f3, 2, 2, grid3), "subplots(2x2)");
        size_t axes_count = 0;
        check(vplFigureGetAxesCount(f3, &axes_count), "getAxesCount");
        /* a3 plus the 2x2 grid just added. */
        if (axes_count != 5) {
            fprintf(stderr, "FAIL: getAxesCount returned %zu, expected 5\n", axes_count);
            ++failures;
        }
        VplAxes *by_index = NULL;
        check(vplFigureGetAxes(f3, 0, &by_index), "getAxes(0)");
        if (by_index != a3) {
            fprintf(stderr, "FAIL: getAxes(0) did not return the first axes added\n");
            ++failures;
        }
        if (vplFigureGetAxes(f3, axes_count, &by_index) != VplResult_InvalidArgument) {
            fprintf(stderr, "FAIL: getAxes at the count itself should be out of range\n");
            ++failures;
        }

        /* clf drops everything; the count goes back to zero. */
        check(vplFigureClear(f3), "clear");
        check(vplFigureGetAxesCount(f3, &axes_count), "getAxesCount(after clear)");
        if (axes_count != 0) {
            fprintf(stderr, "FAIL: getAxesCount after clf returned %zu, expected 0\n",
                    axes_count);
            ++failures;
        }

        vplDestroyFigure(f3);
    }

    /* ---- sharex / sharey: a one-directional follow of another axes' view --- */
    {
        VplFigure *f4 = NULL;
        check(vplCreateFigure(NULL, &f4), "createFigure(4)");
        VplAxes *top = NULL, *bottom = NULL;
        check(vplFigureAddSubplot(f4, 2, 1, 1, &top), "addSubplot(top)");
        check(vplFigureAddSubplot(f4, 2, 1, 2, &bottom), "addSubplot(bottom)");

        const double top_x[2] = {0.0, 10.0};
        const double top_y[2] = {0.0, 1.0};
        check(vplAxesPlot(top, top_x, top_y, 2, NULL, NULL), "plot(top)");
        const double bottom_x[2] = {0.0, 3.0};
        const double bottom_y[2] = {5.0, 9.0};
        check(vplAxesPlot(bottom, bottom_x, bottom_y, 2, NULL, NULL), "plot(bottom)");

        double top_xmin, top_ymin, top_xmax, top_ymax;
        check(vplAxesGetViewLimits(top, &top_xmin, &top_ymin, &top_xmax, &top_ymax),
              "getViewLimits(top)");

        double bx0, by0, bx1, by1;
        check(vplAxesGetViewLimits(bottom, &bx0, &by0, &bx1, &by1),
              "getViewLimits(bottom, unshared)");
        if (bx1 == top_xmax) {
            fprintf(stderr,
                    "FAIL: bottom's x range already matched top's before sharing\n");
            ++failures;
        }
        const double bottom_ymin_before = by0, bottom_ymax_before = by1;

        /* After sharing, bottom's x matches top's exactly, but its y is its
           own -- sharex locks one axis, not the whole view. */
        check(vplAxesShareX(bottom, top), "shareX");
        check(vplAxesGetViewLimits(bottom, &bx0, &by0, &bx1, &by1),
              "getViewLimits(bottom, shared)");
        if (bx0 != top_xmin || bx1 != top_xmax) {
            fprintf(stderr,
                    "FAIL: sharex did not lock bottom's x range to top's "
                    "(%.3f..%.3f vs %.3f..%.3f)\n",
                    bx0, bx1, top_xmin, top_xmax);
            ++failures;
        }
        if (by0 != bottom_ymin_before || by1 != bottom_ymax_before) {
            fprintf(stderr, "FAIL: sharex changed bottom's y range too\n");
            ++failures;
        }

        /* other = NULL hands it back to its own view. */
        check(vplAxesShareX(bottom, NULL), "shareX(NULL, restore)");
        check(vplAxesGetViewLimits(bottom, &bx0, &by0, &bx1, &by1),
              "getViewLimits(bottom, unshared again)");
        if (bx1 == top_xmax) {
            fprintf(stderr, "FAIL: shareX(NULL) did not release the x range\n");
            ++failures;
        }

        /* sharey is the same mechanism on the other axis. */
        check(vplAxesShareY(bottom, top), "shareY");
        check(vplAxesGetViewLimits(bottom, &bx0, &by0, &bx1, &by1),
              "getViewLimits(bottom, y-shared)");
        if (by0 != top_ymin || by1 != top_ymax) {
            fprintf(stderr,
                    "FAIL: sharey did not lock bottom's y range to top's "
                    "(%.3f..%.3f vs %.3f..%.3f)\n",
                    by0, by1, top_ymin, top_ymax);
            ++failures;
        }

        vplDestroyFigure(f4);
    }

    /* ---- vplFigureSubplotsShared: grid sharing by row/column ---------------- */
    {
        VplFigure *f5 = NULL;
        check(vplCreateFigure(NULL, &f5), "createFigure(5)");
        VplAxes *grid5[4] = {NULL, NULL, NULL, NULL};
        check(vplFigureSubplotsShared(f5, 2, 2, VplShareMode_Col, VplShareMode_Row, grid5),
              "subplotsShared(col, row)");

        /* Each leader is given the dominating data so the one-directional
           follow matches a symmetric group; checked here on exact values. */
        const double x00[3] = {0.0, 5.0, 10.0};
        const double y00[3] = {-1.0, 0.0, 1.0};
        check(vplAxesPlot(grid5[0], x00, y00, 3, NULL, NULL), "plot(grid5[0])");
        const double x01[3] = {0.0, 1.5, 3.0};
        const double y01[3] = {0.0, 0.1, 0.2};
        check(vplAxesPlot(grid5[1], x01, y01, 3, NULL, NULL), "plot(grid5[1])");
        const double x10[3] = {0.0, 5.0, 10.0};
        const double y10[3] = {0.0, 25.0, 50.0};
        check(vplAxesPlot(grid5[2], x10, y10, 3, NULL, NULL), "plot(grid5[2])");
        const double x11[3] = {0.0, 0.9, 1.8};
        const double y11[3] = {0.0, 3.0, 6.0};
        check(vplAxesPlot(grid5[3], x11, y11, 3, NULL, NULL), "plot(grid5[3])");

        double gx0, gy0, gx1, gy1;
        check(vplAxesGetViewLimits(grid5[0], &gx0, &gy0, &gx1, &gy1), "getViewLimits(0,0)");
        const double leader_x0 = gx0, leader_x1 = gx1;
        const double leader_y0 = gy0, leader_y1 = gy1;

        /* (0,1): x is its own (column 1's leader); y follows (0,0), row 0's
           leader. */
        check(vplAxesGetViewLimits(grid5[1], &gx0, &gy0, &gx1, &gy1), "getViewLimits(0,1)");
        if (gx0 == leader_x0 && gx1 == leader_x1) {
            fprintf(stderr, "FAIL: (0,1)'s x already matched (0,0)'s before checking sharey\n");
            ++failures;
        }
        if (gy0 != leader_y0 || gy1 != leader_y1) {
            fprintf(stderr, "FAIL: (0,1)'s y did not follow (0,0)'s (sharey='row')\n");
            ++failures;
        }
        const double col1_x0 = gx0, col1_x1 = gx1;

        /* (1,0): x follows (0,0), column 0's leader; y is its own (row 1's
           leader). */
        check(vplAxesGetViewLimits(grid5[2], &gx0, &gy0, &gx1, &gy1), "getViewLimits(1,0)");
        if (gx0 != leader_x0 || gx1 != leader_x1) {
            fprintf(stderr, "FAIL: (1,0)'s x did not follow (0,0)'s (sharex='col')\n");
            ++failures;
        }
        const double row1_y0 = gy0, row1_y1 = gy1;

        /* (1,1): x follows (0,1), column 1's leader; y follows (1,0), row
           1's leader -- both at once, the same as make_share_both. */
        check(vplAxesGetViewLimits(grid5[3], &gx0, &gy0, &gx1, &gy1), "getViewLimits(1,1)");
        if (gx0 != col1_x0 || gx1 != col1_x1) {
            fprintf(stderr, "FAIL: (1,1)'s x did not follow (0,1)'s (sharex='col')\n");
            ++failures;
        }
        if (gy0 != row1_y0 || gy1 != row1_y1) {
            fprintf(stderr, "FAIL: (1,1)'s y did not follow (1,0)'s (sharey='row')\n");
            ++failures;
        }

        /* label_outer runs automatically: (0,0) is not in the last row, so
           its x tick labels are gone. */
        VplTickParams tp;
        check(vplAxesGetTickParams(grid5[0], VplAxis_X, &tp), "getTickParams(0,0, x)");
        if (tp.labelsVisible) {
            fprintf(stderr,
                    "FAIL: subplotsShared did not strip (0,0)'s x tick labels\n");
            ++failures;
        }

        vplDestroyFigure(f5);
    }

    /* ---- ax.lines / ax.patches / ax.images: enumerate what was already added */
    {
        VplFigure *f6 = NULL;
        check(vplCreateFigure(NULL, &f6), "createFigure(6)");
        VplAxes *a6 = NULL;
        check(vplFigureAddSubplot(f6, 1, 1, 1, &a6), "addSubplot(6)");

        size_t count = 0;
        check(vplAxesGetLineCount(a6, &count), "getLineCount(empty)");
        if (count != 0) {
            fprintf(stderr, "FAIL: getLineCount on a fresh axes returned %zu\n", count);
            ++failures;
        }

        const double lx[3] = {0.0, 1.0, 2.0};
        const double ly1[3] = {0.0, 1.0, 0.0};
        const double ly2[3] = {1.0, 0.0, 1.0};
        VplLine *plotted1 = NULL, *plotted2 = NULL;
        check(vplAxesPlot(a6, lx, ly1, 3, NULL, &plotted1), "plot(line 1)");
        check(vplAxesPlot(a6, lx, ly2, 3, NULL, &plotted2), "plot(line 2)");

        check(vplAxesGetLineCount(a6, &count), "getLineCount(after 2 plots)");
        if (count != 2) {
            fprintf(stderr, "FAIL: getLineCount returned %zu, expected 2\n", count);
            ++failures;
        }
        VplLine *fetched1 = NULL, *fetched2 = NULL;
        check(vplAxesGetLine(a6, 0, &fetched1), "getLine(0)");
        check(vplAxesGetLine(a6, 1, &fetched2), "getLine(1)");
        if (fetched1 != plotted1 || fetched2 != plotted2) {
            fprintf(stderr,
                    "FAIL: getLine did not return the same handles plot() did\n");
            ++failures;
        }
        if (vplAxesGetLine(a6, 2, &fetched1) != VplResult_InvalidArgument) {
            fprintf(stderr, "FAIL: getLine at the count itself should be out of range\n");
            ++failures;
        }

        const double bx[2] = {0.0, 1.0};
        const double bh[2] = {2.0, 3.0};
        VplPatch *plotted_bar = NULL;
        check(vplAxesBar(a6, bx, bh, 0.8, NULL, 2, NULL, &plotted_bar), "bar");
        check(vplAxesGetPatchCount(a6, &count), "getPatchCount");
        if (count != 1) {
            fprintf(stderr, "FAIL: getPatchCount returned %zu, expected 1\n", count);
            ++failures;
        }
        VplPatch *fetched_bar = NULL;
        check(vplAxesGetPatch(a6, 0, &fetched_bar), "getPatch(0)");
        if (fetched_bar != plotted_bar) {
            fprintf(stderr, "FAIL: getPatch did not return bar()'s own handle\n");
            ++failures;
        }

        VplFieldDesc fd6;
        memset(&fd6, 0, sizeof(fd6));
        fd6.structSize = sizeof(fd6);
        const double img[4] = {0.0, 1.0, 2.0, 3.0};
        VplImage *plotted_img = NULL;
        check(vplAxesImshow(a6, img, 2, 2, &fd6, &plotted_img), "imshow");
        check(vplAxesGetImageCount(a6, &count), "getImageCount");
        if (count != 1) {
            fprintf(stderr, "FAIL: getImageCount returned %zu, expected 1\n", count);
            ++failures;
        }
        VplImage *fetched_img = NULL;
        check(vplAxesGetImage(a6, 0, &fetched_img), "getImage(0)");
        if (fetched_img != plotted_img) {
            fprintf(stderr, "FAIL: getImage did not return imshow()'s own handle\n");
            ++failures;
        }

        vplDestroyFigure(f6);
    }

    /* ---- set_xbound / set_ybound: NAN means "leave this end alone" ---------- */
    {
        VplFigure *f7 = NULL;
        check(vplCreateFigure(NULL, &f7), "createFigure(7)");
        VplAxes *a7 = NULL;
        check(vplFigureAddSubplot(f7, 1, 1, 1, &a7), "addSubplot(7)");

        const double sx[3] = {0.0, 4.0, 8.0};
        const double sy[3] = {0.0, 1.0, -1.0};
        check(vplAxesPlot(a7, sx, sy, 3, NULL, NULL), "plot(for bounds)");

        double xmin, ymin, xmax, ymax;
        check(vplAxesGetViewLimits(a7, &xmin, &ymin, &xmax, &ymax),
              "getViewLimits(autoscaled)");
        const double autoscaled_xmin = xmin;

        /* NAN in the upper position: only the lower end moves. */
        check(vplAxesSetXBound(a7, NAN, 5.0), "setXBound(NAN, 5)");
        check(vplAxesGetViewLimits(a7, &xmin, &ymin, &xmax, &ymax),
              "getViewLimits(after setXBound)");
        if (xmax != 5.0) {
            fprintf(stderr, "FAIL: setXBound(NAN, 5) left xmax at %.3f\n", xmax);
            ++failures;
        }
        if (xmin != autoscaled_xmin) {
            fprintf(stderr,
                    "FAIL: setXBound(NAN, 5) moved xmin from %.3f to %.3f\n",
                    autoscaled_xmin, xmin);
            ++failures;
        }

        /* NAN in the LOWER position this time -- the other half of the pair. */
        check(vplAxesGetViewLimits(a7, &xmin, &ymin, &xmax, &ymax),
              "getViewLimits(before setYBound)");
        const double autoscaled_ymax = ymax;
        check(vplAxesSetYBound(a7, -0.5, NAN), "setYBound(-0.5, NAN)");
        check(vplAxesGetViewLimits(a7, &xmin, &ymin, &xmax, &ymax),
              "getViewLimits(after setYBound)");
        if (ymin != -0.5) {
            fprintf(stderr, "FAIL: setYBound(-0.5, NAN) left ymin at %.3f\n", ymin);
            ++failures;
        }
        if (ymax != autoscaled_ymax) {
            fprintf(stderr,
                    "FAIL: setYBound(-0.5, NAN) moved ymax from %.3f to %.3f\n",
                    autoscaled_ymax, ymax);
            ++failures;
        }

        expect_invalid(vplAxesSetXBound(a7, 5.0, 1.0), "setXBound(5, 1)");

        vplDestroyFigure(f7);
    }

    /* ---- argument validation ------------------------------------------------ */
    memset(&ld, 0, sizeof(ld));
    ld.structSize = sizeof(ld);
    ld.set = VplLineField_ColorSpec;
    ld.colorSpec = "not-a-colour";
    double t[3] = {0, 1, 2};
    expect_invalid(vplAxesPlot(ax1, t, t, 3, &ld, NULL), "bogus colour spec");

    ld.set = VplLineField_Format;
    ld.format = "zz";
    expect_invalid(vplAxesPlot(ax1, t, t, 3, &ld, NULL), "bogus format string");

    expect_invalid(vplAxesSetXLim(ax1, 5.0, 5.0), "empty x limits");
    expect_invalid(vplAxesBar(ax3, t, t, 0.0, NULL, 3, NULL, NULL), "zero bar width");
    expect_invalid(vplAxesErrorBar(ax1, t, t, t, 3, -1.0, NULL, NULL), "negative capsize");
    check(vplAxesSetLegendNCols(ax1, 2), "legendNCols(2)");
    check(vplAxesSetLegendNCols(ax1, 1), "legendNCols(1, restore)");
    expect_invalid(vplAxesSetLegendNCols(ax1, 0), "legendNCols(0)");

    {
        static const char *const colors3[3] = {"C0", "C1", "C2"};
        static const VplLineStyle styles3[3] = {VplLineStyle_Solid, VplLineStyle_Dashed,
                                                VplLineStyle_Dotted};
        static const VplLineStyle styles2[2] = {VplLineStyle_Solid, VplLineStyle_Dashed};
        check(vplAxesSetPropCycleFull(ax1, colors3, 3, styles3, 3, NULL, 0, NULL, 0),
              "propCycleFull(matched lengths)");
        expect_invalid(
            vplAxesSetPropCycleFull(ax1, colors3, 3, styles2, 2, NULL, 0, NULL, 0),
            "propCycleFull(mismatched lengths)");
        check(vplAxesSetPropCycleFull(ax1, NULL, 0, NULL, 0, NULL, 0, NULL, 0),
              "propCycleFull(reset)");
    }

    /* errorbar's limit carets and elinestyle, and the pre-limits desc size. */
    {
        static const double ex[3] = {0, 1, 2}, ey[3] = {1, 2, 1}, ee[3] = {0.3, 0.3, 0.3};
        static const uint8_t elo[3] = {1, 0, 1}, eup[3] = {0, 1, 1};
        VplErrorBarDesc ed;
        memset(&ed, 0, sizeof(ed));
        ed.structSize = sizeof(ed);
        ed.capsize = 2.0;
        ed.eLineStyle = VplLineStyle_DashDot;
        ed.lolims = elo;
        ed.uplims = eup;
        ed.xlolims = eup;
        check(vplAxesErrorBarFull(ax3, ex, ey, 3, ee, NULL, ee, NULL, &ed, NULL, NULL),
              "errorBarFull(limits)");
        ed.eLineStyle = (VplLineStyle)42;
        expect_invalid(vplAxesErrorBarFull(ax3, ex, ey, 3, ee, NULL, NULL, NULL, &ed, NULL, NULL),
                       "errorBarFull(bad eLineStyle)");
        ed.structSize = offsetof(VplErrorBarDesc, eLineStyle); /* an older caller */
        check(vplAxesErrorBarFull(ax3, ex, ey, 3, ee, NULL, NULL, NULL, &ed, NULL, NULL),
              "errorBarFull(original desc size)");
    }

    /* The triangle family over given triangles and a mask. */
    {
        static const double tx[4] = {0, 1, 1, 0}, ty[4] = {0, 0, 1, 1};
        static const double tz[4] = {0.0, 1.0, 2.0, 1.0};
        static const int32_t tt[6] = {0, 1, 2, 0, 2, 3};
        static const uint8_t tm[2] = {0, 1};
        VplTriangulation tri;
        memset(&tri, 0, sizeof(tri));
        tri.structSize = sizeof(tri);
        tri.triangles = tt;
        tri.triangleCount = 2;
        tri.mask = tm;
        check(vplAxesTriplotFull(ax3, tx, ty, 4, &tri, "ro--"), "triplotFull");
        check(vplAxesTriplotFull(ax3, tx, ty, 4, NULL, NULL), "triplotFull(Delaunay)");
        expect_invalid(vplAxesTriplotFull(ax3, tx, ty, 4, &tri, "zz"), "triplotFull(bad fmt)");
        check(vplAxesTriContourFull(ax3, tx, ty, tz, 4, &tri, NULL, 4, NULL, NULL),
              "triContourFull(auto levels)");
        static const double lv[3] = {0.5, 1.0, 1.5};
        check(vplAxesTriContourFFull(ax3, tx, ty, tz, 4, &tri, lv, 3, NULL, NULL),
              "triContourFFull");
        static const double tc[2] = {0.25, 0.75};
        check(vplAxesTriPcolorFull(ax3, tx, ty, 4, &tri, tc, 2, 1, NULL), "triPcolorFull(faces)");
        check(vplAxesTriPcolorFull(ax3, tx, ty, 4, &tri, tz, 4, 0, NULL), "triPcolorFull(points)");
        expect_invalid(vplAxesTriPcolorFull(ax3, tx, ty, 4, &tri, tc, 3, 0, NULL),
                       "triPcolorFull(c length)");
        static const int32_t bad[3] = {0, 1, 7};
        tri.triangles = bad;
        tri.triangleCount = 1;
        tri.mask = NULL;
        expect_invalid(vplAxesTriplotFull(ax3, tx, ty, 4, &tri, NULL), "triplotFull(bad index)");
        tri.triangles = NULL;
        tri.mask = tm;
        expect_invalid(vplAxesTriplotFull(ax3, tx, ty, 4, &tri, NULL), "triplotFull(mask, no triangles)");

        size_t nt = 0;
        check(vplTriangulate(tx, ty, 4, NULL, 0, &nt), "triangulate(count)");
        if (nt != 2) {
            fprintf(stderr, "FAIL: triangulate gave %zu triangles for a square\n", nt);
            ++failures;
        }
        int32_t out[6];
        if (vplTriangulate(tx, ty, 4, out, 1, &nt) != VplResult_BufferTooSmall) {
            fprintf(stderr, "FAIL: triangulate(short buffer) did not report BufferTooSmall\n");
            ++failures;
        }
        check(vplTriangulate(tx, ty, 4, out, 2, &nt), "triangulate");
    }

    /* barbs' later options, their validation, and the original desc size. */
    {
        static const double bx[3] = {0, 1, 2}, by[3] = {0, 0, 0};
        static const double bu[3] = {0, 30, 60}, bv[3] = {0, 10, -5}, bc[3] = {1, 2, NAN};
        static const uint8_t bf[3] = {0, 1, 0};
        VplBarbsDesc bd;
        memset(&bd, 0, sizeof(bd));
        bd.structSize = sizeof(bd);
        bd.set = VplBarbsField_C | VplBarbsField_Sizes | VplBarbsField_Flips |
                 VplBarbsField_PivotValue | VplBarbsField_BarbColor;
        bd.C = bc;
        bd.cmap = VplColormap_Inferno;
        bd.spacing = 0.2;
        bd.height = 0.5;
        bd.width = 0.3;
        bd.emptyBarb = 0.2;
        bd.flips = bf;
        bd.pivotValue = -1.5;
        bd.barbColorSpec = "k";
        check(vplAxesBarbs(ax3, bx, by, bu, bv, 3, &bd), "barbs(C, sizes, flips, pivot)");
        bd.flagColorSpec = "nope";
        bd.set |= VplBarbsField_FlagColor;
        expect_invalid(vplAxesBarbs(ax3, bx, by, bu, bv, 3, &bd), "barbs(bad flag colour)");
        bd.set &= ~(uint32_t)VplBarbsField_FlagColor;
        bd.spacing = -1.0;
        expect_invalid(vplAxesBarbs(ax3, bx, by, bu, bv, 3, &bd), "barbs(negative spacing)");
        bd.spacing = 0.2;
        bd.flips = NULL;
        expect_invalid(vplAxesBarbs(ax3, bx, by, bu, bv, 3, &bd), "barbs(flips flag, no flips)");
        memset(&bd, 0, sizeof(bd));
        bd.structSize = offsetof(VplBarbsDesc, spacing); /* an older caller */
        bd.set = VplBarbsField_Length;
        bd.length = 5.0;
        check(vplAxesBarbs(ax3, bx, by, bu, bv, 3, &bd), "barbs(original desc size)");
    }

    /* quiver's per-arrow colours, and that the original desc size still works. */
    {
        static const double qx[3] = {0, 1, 2}, qy[3] = {0, 1, 0};
        static const double qu[3] = {1, 0, -1}, qv[3] = {0, 1, 0}, qc[3] = {0.0, 0.5, 1.0};
        VplQuiverDesc qd;
        memset(&qd, 0, sizeof(qd));
        qd.structSize = sizeof(qd);
        qd.set = VplQuiverField_C;
        qd.C = qc;
        qd.cmap = VplColormap_Plasma;
        check(vplAxesQuiver(ax3, qx, qy, qu, qv, 3, &qd), "quiver(C)");
        qd.C = NULL;
        expect_invalid(vplAxesQuiver(ax3, qx, qy, qu, qv, 3, &qd), "quiver(C flag, no C)");
        memset(&qd, 0, sizeof(qd));
        qd.structSize = offsetof(VplQuiverDesc, C); /* a caller built before C existed */
        qd.set = VplQuiverField_Width;
        qd.width = 0.01;
        check(vplAxesQuiver(ax3, qx, qy, qu, qv, 3, &qd), "quiver(original desc size)");
        qd.structSize = offsetof(VplQuiverDesc, units); /* a caller from before units */
        check(vplAxesQuiver(ax3, qx, qy, qu, qv, 3, &qd), "quiver(colours-era desc size)");

        static const double qa[3] = {0.0, 45.0, NAN};
        memset(&qd, 0, sizeof(qd));
        qd.structSize = sizeof(qd);
        qd.set = VplQuiverField_Units | VplQuiverField_ScaleUnits | VplQuiverField_Angles |
                 VplQuiverField_Head;
        qd.units = VplQuiverUnits_Inches;
        qd.scaleUnits = VplQuiverUnits_Y;
        qd.angles = VplQuiverAngles_Fixed;
        qd.angleDegrees = qa;
        qd.headWidth = 4.0;
        qd.headLength = 6.0;
        qd.headAxisLength = 5.0;
        qd.minShaft = 2.0;
        qd.minLength = 0.5;
        check(vplAxesQuiver(ax3, qx, qy, qu, qv, 3, &qd), "quiver(units, fixed angles, head)");
        qd.angleDegrees = NULL;
        expect_invalid(vplAxesQuiver(ax3, qx, qy, qu, qv, 3, &qd), "quiver(fixed, no angles)");
        qd.angleDegrees = qa;
        qd.units = (VplQuiverUnits)99;
        expect_invalid(vplAxesQuiver(ax3, qx, qy, qu, qv, 3, &qd), "quiver(unknown units)");
        qd.units = VplQuiverUnits_Dots;
        qd.headLength = 0.0;
        expect_invalid(vplAxesQuiver(ax3, qx, qy, qu, qv, 3, &qd), "quiver(zero headLength)");
        qd.structSize = 4;
        expect_invalid(vplAxesQuiver(ax3, qx, qy, qu, qv, 3, &qd), "quiver(truncated desc)");
    }

    /* boxplot/bxp's full forms and their validation. */
    {
        static const double bv[10] = {1, 2, 3, 4, 9, 2, 3, 3, 4, 5};
        static const size_t bc[2] = {5, 5};
        static const double bpos[2] = {1.5, 4.0};
        static const char *const bl[2] = {"a", "b"};
        VplBoxDesc bd;
        memset(&bd, 0, sizeof(bd));
        bd.structSize = sizeof(bd);
        bd.positions = bpos;
        bd.labels = bl;
        bd.showNotches = 1;
        bd.showMeans = 1;
        bd.patchArtist = 1;
        bd.hasWhisPercentiles = 1;
        bd.whisPercentiles[0] = 10.0;
        bd.whisPercentiles[1] = 90.0;
        check(vplAxesBoxPlotFull(ax3, bv, bc, 2, &bd), "boxPlotFull");
        bd.whisPercentiles[0] = 95.0;
        expect_invalid(vplAxesBoxPlotFull(ax3, bv, bc, 2, &bd), "boxPlotFull(lo pct > hi)");
        bd.hasWhisPercentiles = 0;
        static const double badw[2] = {0.5, -1.0};
        bd.widths = badw;
        expect_invalid(vplAxesBoxPlotFull(ax3, bv, bc, 2, &bd), "boxPlotFull(negative width)");
        bd.widths = NULL;

        VplBoxStats bs;
        memset(&bs, 0, sizeof(bs));
        bs.structSize = sizeof(bs);
        bs.q1 = 1.0;
        bs.median = 2.0;
        bs.q3 = 3.0;
        bs.whisLo = 0.0;
        bs.whisHi = 4.0;
        static const double bmean[1] = {2.2};
        static const double blo[1] = {1.6};
        static const double bhi[1] = {2.4};
        bd.positions = NULL;
        bd.labels = NULL;
        check(vplAxesBxpFull(ax3, &bs, 1, bmean, blo, bhi, &bd), "bxpFull(means, notches)");
        expect_invalid(vplAxesBxpFull(ax3, &bs, 1, NULL, blo, bhi, &bd), "bxpFull(no means)");
        expect_invalid(vplAxesBxpFull(ax3, &bs, 1, bmean, NULL, NULL, &bd), "bxpFull(no notches)");
    }

    /* xcorr/acorr's full form. */
    {
        double cx[16], cy[16];
        for (int i = 0; i < 16; ++i) {
            cx[i] = 0.3 * i + sin(i * 0.9);
            cy[i] = cos(i * 0.9);
        }
        VplLine *cl = NULL;
        check(vplAxesACorrFull(ax3, cx, 16, 0, 1, VplSpectralDetrend_Linear, 0, NULL, &cl),
              "acorrFull(linear, markers, maxlags=None)");
        if (cl == NULL) {
            fprintf(stderr, "FAIL: the marker form should hand back its line\n");
            ++failures;
        }
        check(vplAxesXCorrFull(ax3, cx, cy, 16, 5, 0, VplSpectralDetrend_Mean, 1, NULL, &cl),
              "xcorrFull(mean, stalks)");
        if (cl != NULL) {
            fprintf(stderr, "FAIL: the stalk form has no line to hand back\n");
            ++failures;
        }
        memset(&ld, 0, sizeof(ld));
        ld.structSize = sizeof(ld);
        expect_invalid(vplAxesXCorrFull(ax3, cx, cy, 16, 5, 1, VplSpectralDetrend_None, 1, &ld,
                                        NULL),
                       "xcorrFull(desc with stalks)");
        expect_invalid(vplAxesXCorrFull(ax3, cx, cy, 16, 16, 1, VplSpectralDetrend_None, 0, NULL,
                                        NULL),
                       "xcorrFull(maxlags too large)");
        expect_invalid(vplAxesXCorrFull(ax3, cx, cy, 16, 5, 1, (VplSpectralDetrend)8, 0, NULL,
                                        NULL),
                       "xcorrFull(unknown detrend)");
    }

    /* violinplot's full form and its validation. */
    {
        static const double vv[10] = {1, 2, 2, 3, 4, 2, 3, 3, 4, 5};
        static const size_t vc[2] = {5, 5};
        static const double vq[2] = {0.25, 0.75};
        static const size_t vqc[2] = {2, 0};
        static const double vpos[2] = {1.0, 3.0};
        VplViolinDesc vd;
        memset(&vd, 0, sizeof(vd));
        vd.structSize = sizeof(vd);
        vd.positions = vpos;
        vd.showMedians = 1;
        vd.quantiles = vq;
        vd.quantileCounts = vqc;
        vd.bwMethod = VplViolinBandwidth_Silverman;
        vd.horizontal = 1;
        check(vplAxesViolinPlotFull(ax3, vv, vc, 2, &vd), "violinPlotFull");
        vd.bwMethod = VplViolinBandwidth_Scalar;
        expect_invalid(vplAxesViolinPlotFull(ax3, vv, vc, 2, &vd), "violinPlotFull(no factor)");
        vd.bwMethod = VplViolinBandwidth_Scott;
        static const double badq[2] = {0.25, 1.5};
        vd.quantiles = badq;
        expect_invalid(vplAxesViolinPlotFull(ax3, vv, vc, 2, &vd),
                       "violinPlotFull(quantile above 1)");
        vd.quantiles = vq;
        vd.side = (VplViolinSide)5;
        expect_invalid(vplAxesViolinPlotFull(ax3, vv, vc, 2, &vd), "violinPlotFull(bad side)");
    }

    /* hexbin's full form and its validation. */
    {
        static const double hx[6] = {0.0, 0.1, 0.9, 1.0, 0.5, 0.45};
        static const double hy[6] = {0.0, 0.2, 0.8, 1.0, 0.5, 0.55};
        static const double hc[6] = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
        VplHexbinDesc hd;
        memset(&hd, 0, sizeof(hd));
        hd.structSize = sizeof(hd);
        hd.gridsizeX = 4;
        hd.C = hc;
        hd.reduce = VplHexReduce_Median;
        hd.mincnt = 1;
        check(vplAxesHexbinFull(ax3, hx, hy, 6, &hd), "hexbinFull(C, median)");
        hd.C = NULL;
        hd.logBins = 1;
        hd.hasExtent = 1;
        hd.extent[0] = -1.0;
        hd.extent[1] = 2.0;
        hd.extent[2] = -1.0;
        hd.extent[3] = 2.0;
        check(vplAxesHexbinFull(ax3, hx, hy, 6, &hd), "hexbinFull(log, extent)");
        hd.extent[1] = -2.0;
        expect_invalid(vplAxesHexbinFull(ax3, hx, hy, 6, &hd), "hexbinFull(inverted extent)");
        hd.hasExtent = 0;
        hd.hasVLimits = 1;
        hd.vmin = 0.0;
        hd.vmax = 5.0;
        expect_invalid(vplAxesHexbinFull(ax3, hx, hy, 6, &hd), "hexbinFull(log with vmin 0)");
        hd.hasVLimits = 0;
        hd.reduce = (VplHexReduce)42;
        expect_invalid(vplAxesHexbinFull(ax3, hx, hy, 6, &hd), "hexbinFull(unknown reduce)");
        hd.reduce = VplHexReduce_Mean;
        hd.logBins = 0;
        hd.marginals = 1;
        check(vplAxesHexbinFull(ax3, hx, hy, 6, &hd), "hexbinFull(marginals)");
        hd.marginals = 0;
        hd.xLog = 1; /* hx holds a 0 */
        expect_invalid(vplAxesHexbinFull(ax3, hx, hy, 6, &hd), "hexbinFull(log x, non-positive)");
        hd.xLog = 0;
        hd.structSize = offsetof(VplHexbinDesc, xLog); /* an older caller */
        check(vplAxesHexbinFull(ax3, hx, hy, 6, &hd), "hexbinFull(original desc size)");
    }

    /* Automatic contour levels, read back, and extend's validation. */
    {
        enum { R = 12, C = 16 };
        static double z[R * C];
        for (int r = 0; r < R; ++r) {
            for (int c = 0; c < C; ++c) {
                z[r * C + c] = 0.1 * c + 0.05 * r; /* 0 .. 2.05 */
            }
        }
        VplContour *auto_cs = NULL;
        check(vplAxesContour(ax3, z, R, C, NULL, 0, NULL, &auto_cs), "contour(Z)");
        size_t nlev = 0;
        check(vplContourGetLevels(auto_cs, NULL, 0, &nlev), "contourGetLevels(count)");
        double lev[32];
        check(vplContourGetLevels(auto_cs, lev, 32, &nlev), "contourGetLevels");
        /* MaxNLocator(8) over 0..2.05 steps by 0.3: 0, 0.3, ..., 2.1. */
        if (nlev != 8 || fabs(lev[0]) > 1e-12 || fabs(lev[7] - 2.1) > 1e-9) {
            fprintf(stderr, "FAIL: contour(Z) chose %zu levels from %g to %g\n", nlev,
                    nlev ? lev[0] : 0.0, nlev ? lev[nlev - 1] : 0.0);
            ++failures;
        }
        if (vplContourGetLevels(auto_cs, lev, 2, &nlev) != VplResult_BufferTooSmall) {
            fprintf(stderr, "FAIL: contourGetLevels should refuse a 2-entry buffer\n");
            ++failures;
        }

        VplFieldDesc fdesc;
        memset(&fdesc, 0, sizeof(fdesc));
        fdesc.structSize = sizeof(fdesc);
        fdesc.set = VplFieldField_Extend;
        fdesc.extend = VplContourExtend_Max;
        check(vplAxesContourF(ax3, z, R, C, NULL, 4, &fdesc, NULL), "contourf(Z, 4, max)");
        fdesc.extend = (VplContourExtend)9;
        expect_invalid(vplAxesContourF(ax3, z, R, C, NULL, 4, &fdesc, NULL),
                       "contourf(unknown extend)");
        static const double down[3] = {1.0, 0.5, 0.0};
        expect_invalid(vplAxesContour(ax3, z, R, C, down, 3, NULL, NULL),
                       "contour(decreasing levels)");
    }

    /* get_anchor gives back the form it was given: a name, or a pair. */
    {
        const char *code = "unset";
        check(vplAxesGetAnchorCode(ax1, &code), "getAnchorCode(default)");
        if (code == NULL || strcmp(code, "C") != 0) {
            fprintf(stderr, "FAIL: the default anchor should read back as C\n");
            ++failures;
        }
        check(vplAxesSetAnchorCode(ax1, "NW"), "setAnchorCode(NW)");
        double axx = -1.0, axy = -1.0;
        check(vplAxesGetAnchor(ax1, &axx, &axy), "getAnchor(after NW)");
        check(vplAxesGetAnchorCode(ax1, &code), "getAnchorCode(NW)");
        if (axx != 0.0 || axy != 1.0 || code == NULL || strcmp(code, "NW") != 0) {
            fprintf(stderr, "FAIL: NW should be (0, 1) and read back by name\n");
            ++failures;
        }
        check(vplAxesSetAnchor(ax1, 0.25, 0.75), "setAnchor(pair)");
        check(vplAxesGetAnchorCode(ax1, &code), "getAnchorCode(after a pair)");
        if (code != NULL) {
            fprintf(stderr, "FAIL: a pair-set anchor has no name\n");
            ++failures;
        }
        expect_invalid(vplAxesSetAnchorCode(ax1, "middle"), "setAnchorCode(unknown)");
        check(vplAxesSetAnchorCode(ax1, "C"), "setAnchorCode(restore)");
    }

    /* pie's full form, on a figure of its own since a pie takes over its axes. */
    {
        VplFigure *pf = NULL;
        check(vplCreateFigure(NULL, &pf), "createFigure(pie)");
        VplAxes *pa = NULL;
        check(vplFigureAddSubplot(pf, 1, 1, 1, &pa), "addSubplot(pie)");
        static const double pv[3] = {1.0, 2.0, 3.0};
        static const char *const pl[3] = {"a", "b", "c"};
        static const double ex[3] = {0.0, 0.1, 0.0};
        VplPieDesc pd2;
        memset(&pd2, 0, sizeof(pd2));
        pd2.structSize = sizeof(pd2);
        pd2.labels = pl;
        pd2.explode = ex;
        pd2.autopct = "%1.1f%%";
        pd2.startAngle = 90.0;
        check(vplAxesPieFull(pa, pv, 3, &pd2), "pieFull(labels, autopct, explode)");
        check(vplAxesPieLabelFull(pa, pl, 3, 1.2, 9.0, VplPieLabelAlign_Outer, 1),
              "pieLabelFull(outer, rotated)");
        expect_invalid(vplAxesPieLabelFull(pa, pl, 3, 1.2, 9.0, (VplPieLabelAlign)7, 0),
                       "pieLabelFull(unknown alignment)");
        check(vplAxesPieLabelFormat(pa, "{absval:d} ({frac:.1%}) {{x}}", 0.5, 0.0,
                                    VplPieLabelAlign_Center, 0),
              "pieLabelFormat");
        check(vplAxesPieLabelFormat(pa, "{absval:>+08,.2f}|{frac}", 0.5, 0.0,
                                    VplPieLabelAlign_Auto, 0),
              "pieLabelFormat(spec)");
        expect_invalid(vplAxesPieLabelFormat(pa, "{value}", 0.5, 0.0, VplPieLabelAlign_Auto, 0),
                       "pieLabelFormat(unknown field)");
        expect_invalid(vplAxesPieLabelFormat(pa, "{frac:d}", 0.5, 0.0, VplPieLabelAlign_Auto, 0),
                       "pieLabelFormat(d on a fraction)");
        expect_invalid(vplAxesPieLabelFormat(pa, "{frac", 0.5, 0.0, VplPieLabelAlign_Auto, 0),
                       "pieLabelFormat(unclosed field)");

        pd2.shadow = 1;
        pd2.wedgeWidth = 0.3;
        pd2.wedgeEdgeColorSpec = "white";
        check(vplAxesPieFull(pa, pv, 3, &pd2), "pieFull(shadow, donut)");
        pd2.hasShadowParams = 1;
        pd2.shadowShade = 1.5;
        expect_invalid(vplAxesPieFull(pa, pv, 3, &pd2), "pieFull(shade > 1)");
        pd2.hasShadowParams = 0;
        pd2.wedgeEdgeColorSpec = "not-a-colour";
        expect_invalid(vplAxesPieFull(pa, pv, 3, &pd2), "pieFull(bad edge colour)");
        pd2.wedgeEdgeColorSpec = NULL;
        pd2.shadow = 0;
        pd2.wedgeWidth = 0.0;

        pd2.autopct = "%d";
        expect_invalid(vplAxesPieFull(pa, pv, 3, &pd2), "pieFull(integer autopct)");
        pd2.autopct = NULL;
        pd2.noNormalize = 1;
        expect_invalid(vplAxesPieFull(pa, pv, 3, &pd2), "pieFull(unnormalized sum > 1)");
        static const double fr[3] = {0.2, 0.3, 0.1};
        check(vplAxesPieFull(pa, fr, 3, &pd2), "pieFull(unnormalized, a gap)");
        static const double negv[3] = {1.0, -1.0, 2.0};
        expect_invalid(vplAxesPieFull(pa, negv, 3, NULL), "pieFull(negative wedge)");
        static const double zero3[3] = {0.0, 0.0, 0.0};
        expect_invalid(vplAxesPieFull(pa, zero3, 3, NULL), "pieFull(all zero)");
        static const char *const badc[1] = {"not-a-colour"};
        memset(&pd2, 0, sizeof(pd2));
        pd2.structSize = sizeof(pd2);
        pd2.colorSpecs = badc;
        pd2.colorCount = 1;
        expect_invalid(vplAxesPieFull(pa, pv, 3, &pd2), "pieFull(bogus colour)");
        vplDestroyFigure(pf);
    }

    /* errorbar's full form: what it accepts and what it refuses. */
    {
        static const double ex[4] = {0.0, 1.0, 2.0, 3.0};
        static const double ey[4] = {1.0, 2.0, 1.5, 2.5};
        static const double lo[4] = {0.1, 0.2, 0.3, 0.4};
        static const double hi[4] = {0.4, 0.3, 0.2, 0.1};
        static const double neg[4] = {0.1, -0.2, 0.3, 0.4};
        static const double gap[4] = {0.1, (double)NAN, 0.3, 0.4};

        VplErrorBarDesc eb;
        memset(&eb, 0, sizeof(eb));
        eb.structSize = sizeof(eb);
        eb.capsize = 2.0;
        eb.eColorSpec = "tab:red";
        eb.errorEveryStep = 2;
        VplLine *ebl = NULL;
        check(vplAxesErrorBarFull(ax3, ex, ey, 4, lo, hi, hi, lo, &eb, NULL, &ebl),
              "errorBarFull(asymmetric both ways)");
        if (ebl == NULL) {
            fprintf(stderr, "FAIL: errorBarFull returned no line\n");
            ++failures;
        }
        check(vplAxesErrorBarFull(ax3, ex, ey, 4, gap, NULL, NULL, NULL, NULL, NULL, NULL),
              "errorBarFull(NaN skips a bar)");

        memset(&ld, 0, sizeof(ld));
        ld.structSize = sizeof(ld);
        ld.set = VplLineField_Format;
        ld.format = "NONE";
        check(vplAxesErrorBarFull(ax3, ex, ey, 4, lo, NULL, NULL, NULL, NULL, &ld, NULL),
              "errorBarFull(fmt='none', any case)");
        ld.format = "o--";
        check(vplAxesErrorBarFull(ax3, ex, ey, 4, lo, NULL, NULL, NULL, NULL, &ld, NULL),
              "errorBarFull(fmt)");

        expect_invalid(vplAxesErrorBarFull(ax3, ex, ey, 4, neg, NULL, NULL, NULL, NULL, NULL,
                                           NULL),
                       "errorBarFull(negative extent)");
        expect_invalid(vplAxesErrorBarFull(ax3, ex, ey, 4, NULL, hi, NULL, NULL, NULL, NULL,
                                           NULL),
                       "errorBarFull(upper without lower)");
        eb.eColorSpec = "not-a-colour";
        expect_invalid(vplAxesErrorBarFull(ax3, ex, ey, 4, lo, NULL, NULL, NULL, &eb, NULL,
                                           NULL),
                       "errorBarFull(bogus ecolor)");
        eb.eColorSpec = NULL;
        eb.capthick = -1.0;
        expect_invalid(vplAxesErrorBarFull(ax3, ex, ey, 4, lo, NULL, NULL, NULL, &eb, NULL,
                                           NULL),
                       "errorBarFull(negative capthick)");
        expect_invalid(vplAxesErrorBarFull(ax3, ex, ey, 0, lo, NULL, NULL, NULL, NULL, NULL,
                                           NULL),
                       "errorBarFull(no points)");
    }

    /* hist's full form: the n and bins it hands back, and its validation. */
    {
        static const double a[6] = {0.0, 0.5, 1.0, 1.0, 2.5, 3.0};
        static const double b[4] = {0.2, 1.4, 2.6, 2.9};
        static const double wb[4] = {2.0, 1.0, 0.5, 0.5};
        const double *data[2] = {a, b};
        const size_t counts[2] = {6, 4};
        const double *weights[2] = {NULL, wb};

        VplHistDesc hd;
        memset(&hd, 0, sizeof(hd));
        hd.structSize = sizeof(hd);
        hd.bins = 3;
        hd.stacked = 1;
        double tops[6], edges[4];
        VplPatch *patches[2] = {NULL, NULL};
        check(vplAxesHistFull(ax3, data, counts, weights, 2, &hd, NULL, tops, edges, patches),
              "histFull(stacked, weighted)");
        /* Edges 0, 1, 2, 3 over both datasets; a's counts are 2, 2, 2 (the
           1.0s fall in the middle bin, 3.0 in the last, inclusive), and b
           adds 2, 1, 1 of weight on top. */
        static const double want_edges[4] = {0.0, 1.0, 2.0, 3.0};
        static const double want_tops[6] = {2.0, 2.0, 2.0, 4.0, 3.0, 3.0};
        for (int k = 0; k < 4; ++k) {
            if (fabs(edges[k] - want_edges[k]) > 1e-12) {
                fprintf(stderr, "FAIL: histFull edge %d = %g, want %g\n", k, edges[k],
                        want_edges[k]);
                ++failures;
            }
        }
        for (int k = 0; k < 6; ++k) {
            if (fabs(tops[k] - want_tops[k]) > 1e-12) {
                fprintf(stderr, "FAIL: histFull top %d = %g, want %g\n", k, tops[k],
                        want_tops[k]);
                ++failures;
            }
        }
        if (patches[0] == NULL || patches[1] == NULL || patches[0] == patches[1]) {
            fprintf(stderr, "FAIL: histFull did not return one patch per dataset\n");
            ++failures;
        }

        /* Reverse cumulative over explicit edges: per-bin counts 2, 2, 1 (the
           inclusive last bin keeps 2.5, and 3.0 is past the top edge), summed
           from the right to 5, 3, 1. */
        static const double uneven[4] = {0.0, 0.75, 2.0, 2.5};
        memset(&hd, 0, sizeof(hd));
        hd.structSize = sizeof(hd);
        hd.binEdges = uneven;
        hd.binEdgeCount = 4;
        hd.cumulative = -1;
        hd.histtype = VplHistType_Step;
        check(vplAxesHistFull(ax3, data, counts, NULL, 1, &hd, NULL, tops, NULL, NULL),
              "histFull(reverse cumulative, explicit edges)");
        static const double want_rev[3] = {5.0, 3.0, 1.0};
        for (int k = 0; k < 3; ++k) {
            if (fabs(tops[k] - want_rev[k]) > 1e-12) {
                fprintf(stderr, "FAIL: histFull reverse top %d = %g, want %g\n", k, tops[k],
                        want_rev[k]);
                ++failures;
            }
        }

        static const double backwards[3] = {0.0, 2.0, 1.0};
        hd.binEdges = backwards;
        hd.binEdgeCount = 3;
        expect_invalid(vplAxesHistFull(ax3, data, counts, NULL, 1, &hd, NULL, NULL, NULL, NULL),
                       "histFull(decreasing edges)");
        hd.binEdges = uneven;
        hd.binEdgeCount = 1;
        expect_invalid(vplAxesHistFull(ax3, data, counts, NULL, 1, &hd, NULL, NULL, NULL, NULL),
                       "histFull(one edge)");
        memset(&hd, 0, sizeof(hd));
        hd.structSize = sizeof(hd);
        hd.hasRange = 1;
        hd.rangeMin = 2.0;
        hd.rangeMax = 1.0;
        expect_invalid(vplAxesHistFull(ax3, data, counts, NULL, 1, &hd, NULL, NULL, NULL, NULL),
                       "histFull(inverted range)");
        hd.hasRange = 0;
        hd.histtype = (VplHistType)9;
        expect_invalid(vplAxesHistFull(ax3, data, counts, NULL, 1, &hd, NULL, NULL, NULL, NULL),
                       "histFull(unknown histtype)");
        hd.histtype = VplHistType_Bar;
        hd.rwidth = -0.5;
        expect_invalid(vplAxesHistFull(ax3, data, counts, NULL, 1, &hd, NULL, NULL, NULL, NULL),
                       "histFull(negative rwidth)");
        expect_invalid(vplAxesHistFull(ax3, data, counts, NULL, 0, NULL, NULL, NULL, NULL, NULL),
                       "histFull(no datasets)");
        const double *null_data[1] = {NULL};
        expect_invalid(
            vplAxesHistFull(ax3, null_data, counts, NULL, 1, NULL, NULL, NULL, NULL, NULL),
            "histFull(null dataset)");
    }

    vplDestroyFigure(fig);

    if (failures != 0) {
        fprintf(stderr, "\n%d artist check(s) failed\n", failures);
        return 1;
    }
    printf("figure %ux%u, all artist checks passed\n", w, h);
    return 0;
}
