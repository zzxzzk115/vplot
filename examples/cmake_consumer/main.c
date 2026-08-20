/* Plain C99: no C++ anywhere in the consumer. */
#include <vpl/vpl.h>

#include <math.h>
#include <stdio.h>

static int failed(const char *what, VplResult r)
{
    if (r == VplResult_Success) {
        return 0;
    }
    printf("FAIL: %s: %s (%s)\n", what, vplResultToString(r), vplGetLastError());
    return 1;
}

int main(void)
{
    VplFigure *fig = NULL;
    VplAxes *ax = NULL;
    VplAxes *hax = NULL;
    double xs[201], ys[201], samples[400];
    int i;

    /* Ticks of our own, at the multiples of pi the curve actually turns at. */
    static const double xticks[3] = {0.0, 3.14159265358979323846,
                                     2.0 * 3.14159265358979323846};
    static const char *const xlabels[3] = {"0", "pi", "2pi"};

    if (failed("vplCreateFigure", vplCreateFigure(NULL, &fig))) {
        return 1;
    }
    if (failed("vplFigureAddSubplot", vplFigureAddSubplot(fig, 2, 1, 1, &ax))) {
        return 1;
    }

    for (i = 0; i < 201; ++i) {
        xs[i] = i * (2.0 * 3.14159265358979323846) / 200.0;
        ys[i] = sin(xs[i]);
    }
    vplAxesPlot(ax, xs, ys, 201, NULL, NULL);
    vplAxesSetTitle(ax, "vplot, from C");
    vplAxesSetYLabel(ax, "sin");

    if (failed("vplAxesSetXTicks", vplAxesSetXTicks(ax, xticks, xlabels, 3))) {
        return 1;
    }
    /* The pair a paper figure usually drops. */
    if (failed("vplAxesSetSpineVisible",
               vplAxesSetSpineVisible(ax, VplSpine_Top, 0)) ||
        failed("vplAxesSetSpineVisible",
               vplAxesSetSpineVisible(ax, VplSpine_Right, 0))) {
        return 1;
    }

    if (failed("vplFigureAddSubplot", vplFigureAddSubplot(fig, 2, 1, 2, &hax))) {
        return 1;
    }
    for (i = 0; i < 400; ++i) {
        samples[i] = sin(i * 1.7) + sin(i * 0.31);
    }
    if (failed("vplAxesHist", vplAxesHist(hax, samples, 400, 0, 0, NULL, NULL))) {
        return 1;
    }
    vplAxesSetXLabel(hax, "value");

    if (failed("vplFigureTightLayout", vplFigureTightLayout(fig))) {
        return 1;
    }
    if (failed("vplFigureSaveFig", vplFigureSaveFig(fig, "cmake_consumer.png", NULL))) {
        return 1;
    }
    vplDestroyFigure(fig);

    printf("ok: wrote cmake_consumer.png\n");
    return 0;
}
