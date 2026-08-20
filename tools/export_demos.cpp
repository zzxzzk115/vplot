/*
 * Renders every demo in examples/demo/demos.h to PNG (and one SVG/PDF pair) so
 * the catalogue can be looked at without launching the interactive browser.
 *
 * Uses the same builders the browser does, so what lands on disk is what the
 * demo actually draws.
 *
 *     xmake run export-demos [output-dir]
 */

#include "../examples/demo/demos.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    const char *out_dir = argc > 1 ? argv[1] : ".";

    VplDemoParams params;
    vplDemoDefaults(&params);

    int failures = 0;

    for (int i = 0; i < VplDemo_Count; ++i) {
        const VplDemoId id = (VplDemoId)i;

        VplFigure *fig = NULL;
        VplResult r = vplBuildDemo(id, &params, &fig);
        if (r != VplResult_Success || fig == NULL) {
            fprintf(stderr, "FAIL: building %s -> %s (%s)\n", kVplDemos[i].name,
                    vplResultToString(r), vplGetLastError());
            ++failures;
            continue;
        }

        char path[512];
        /* Named by slug, not by index: inserting a demo would otherwise renumber
           everything after it and leave stale files from previous runs sitting
           alongside the new ones under the old names. */
        snprintf(path, sizeof(path), "%s/demo_%s.png", out_dir, kVplDemos[i].slug);

        /* 2x for legibility when these are viewed inline rather than printed. */
        VplSaveDesc desc;
        memset(&desc, 0, sizeof(desc));
        desc.structSize = sizeof(desc);
        desc.format = VplFormat_PNG;
        desc.dpi = 200.0;

        r = vplFigureSaveFig(fig, path, &desc);
        if (r != VplResult_Success) {
            fprintf(stderr, "FAIL: writing %s -> %s (%s)\n", path, vplResultToString(r),
                    vplGetLastError());
            ++failures;
        } else {
            printf("%-18s %s\n", kVplDemos[i].name, path);
        }

        /* One demo also goes out as vector, to show the same figure survives
           the other backends. */
        if (id == VplDemo_ErrorBars) {
            char vec[512];
            snprintf(vec, sizeof(vec), "%s/demo_%s.pdf", out_dir, kVplDemos[i].slug);
            if (vplFigureSaveFig(fig, vec, NULL) != VplResult_Success) {
                fprintf(stderr, "FAIL: writing %s\n", vec);
                ++failures;
            }
            snprintf(vec, sizeof(vec), "%s/demo_%s.svg", out_dir, kVplDemos[i].slug);
            if (vplFigureSaveFig(fig, vec, NULL) != VplResult_Success) {
                fprintf(stderr, "FAIL: writing %s\n", vec);
                ++failures;
            }
        }

        vplDestroyFigure(fig);
    }

    if (failures != 0) {
        fprintf(stderr, "\n%d demo(s) failed\n", failures);
        return 1;
    }
    printf("\n%d demos exported to %s\n", (int)VplDemo_Count, out_dir);
    return 0;
}
