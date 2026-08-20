/*
 * Guards that the demo catalogue keeps up with the library. Reads vpl.h and
 * demos.h at run time and checks two things:
 *
 *   1. every entry point in vpl.h is called somewhere in demos.h, or is listed
 *      below with a reason;
 *   2. every call named in a panel's `api` field is one the catalogue makes.
 *
 * The first check looks at call sites, not `api` fields, so the only way past
 * the guard is to write the demo.
 */

#include "demos.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace
{

int failures = 0;

/*
 * Entries no demo panel can sensibly exercise, each with the reason: host
 * plumbing and the read half of settable pairs, whose value a picture cannot
 * show.
 */
struct Exemption
{
    const char *entry;
    const char *why;
};

const Exemption kNotDemoed[] = {
    /* Host plumbing, called around the catalogue rather than inside it. */
    {"vplCreateFigure", "lifecycle, called by the host around every panel"},
    {"vplDestroyFigure", "lifecycle"},
    {"vplFigureClear", "lifecycle"},
    {"vplFigureRenderRGBA", "the host draws every panel with it"},
    {"vplFigureSaveFig", "output; tools/export_demos writes every panel with it"},
    {"vplFigureGetPixelSize", "the host queries it to size its window"},
    {"vplSetFontPath", "process setup, done once before any panel is built"},
    {"vplGetLastError", "error reporting, and a panel that fails is a bug"},
    {"vplResultToString", "error reporting"},

    /* Interaction, driven by mouse input; an exported PNG cannot show a drag. */
    {"vplAxesPan", "interactive navigation, driven by the browser's mouse handling"},
    {"vplAxesZoomAt", "interactive navigation"},

    /* Read-back. A panel is a picture; it cannot show a number it read. */
    {"vplAxesGetViewLimits", "read-back: a picture cannot show the value returned"},
    {"vplAxesGetTickParams", "read-back"},
    {"vplAxesGetXScale", "read-back"},
    {"vplAxesGetYScale", "read-back"},
    {"vplAxesGetXLabel", "read-back"},
    {"vplAxesGetYLabel", "read-back"},
    {"vplAxesGetTitle", "read-back"},
    {"vplAxesGetXTicks", "read-back"},
    {"vplAxesGetYTicks", "read-back"},
    {"vplAxesGetAxisBelow", "read-back"},
    {"vplAxesGetAxisOn", "read-back"},
    {"vplAxesGetAspect", "read-back"},
    {"vplAxesGetMargins", "read-back"},
    {"vplAxesGetXMargin", "read-back"},
    {"vplAxesGetYMargin", "read-back"},
    {"vplAxesGetPosition", "read-back"},
    {"vplAxesGetFrameOn", "read-back"},
    {"vplAxesGetLegendLabel", "read-back"},
    {"vplAxesGetLegendLabelCount", "read-back"},
    {"vplAxesHasData", "read-back"},
    {"vplFigureGetSubplotParams", "read-back"},
    {"vplFigureGetSupTitle", "read-back"},
    {"vplFigureGetSupXLabel", "read-back"},
    {"vplFigureGetSupYLabel", "read-back"},
    {"vplFigureGetAxes", "read-back"},
    {"vplFigureGetAxesCount", "read-back"},
    {"vplFigureGetDpi", "read-back"},
    {"vplFigureGetSizeInches", "read-back"},
    {"vplAxesGetBoxAspect", "read-back"},
    {"vplAxesGetAnchor", "read-back"},
    {"vplAxesGetAdjustable", "read-back"},
    {"vplFigureGetFrameOn", "read-back"},
    {"vplFigureGetLineWidth", "read-back"},
    {"vplAxesGetAutoscale", "read-back"},
    {"vplAxesGetAutoscaleX", "read-back"},
    {"vplAxesGetAutoscaleY", "read-back"},
    {"vplAxesGetDataRatio", "read-back"},
    {"vplAxesGetFaceColor", "read-back"},
    {"vplAxesGetXTickLabels", "read-back"},
    {"vplAxesGetYTickLabels", "read-back"},
    {"vplFigureGetFaceColor", "read-back"},
    {"vplFigureGetEdgeColor", "read-back"},

    /* Figure bookkeeping: gca/sca serve matplotlib's implicit current-axes
       state, which a panel holding its own axes has no use for; delaxes removes
       what a panel exists to show. */
    {"vplFigureGca", "implicit-state helper; a panel already holds its axes"},
    {"vplFigureSca", "implicit-state helper"},
    {"vplFigureDelAxes", "removes what a panel exists to show"},

    /* align_labels is align_xlabels plus align_ylabels, both demoed directly. */
    {"vplFigureAlignLabels", "the union of align_xlabels and align_ylabels, both demoed in the align panel"},

    /* Single-axis / single-dimension forms of a pair a panel already sets. */
    {"vplAxesSetXMargin", "the single-axis form of set_margins, shown by the aspect & margins panel"},
    {"vplAxesSetYMargin", "the single-axis form of set_margins"},
    {"vplFigureSetFigWidth", "the single-dimension form of set_size_inches, shown by the page-size panel"},
    {"vplFigureSetFigHeight", "the single-dimension form of set_size_inches"},
    {"vplAxesSetPosition", "moves an axes to an explicit rect, the placement the inset-indicator panel shows via add_axes"},
    {"vplAxesGetUseStickyEdges", "read-back"},
    {"vplAxesAddStickyEdgeX", "the caller-facing half of the sticky machinery; artists add most, and its effect is a margin clamp verified by the baseline suite"},
    {"vplAxesAddStickyEdgeY", "the caller-facing half of the sticky machinery"},
};

/* Reads a project file, walking up from the test binary's directory. */
bool read_project_file(const char *relative, std::string &out)
{
    static const char *prefixes[] = {"", "../", "../../", "../../../", "../../../../"};

    for (const char *prefix : prefixes) {
        const std::string path = std::string(prefix) + relative;
        FILE *f = std::fopen(path.c_str(), "rb");
        if (f == nullptr) {
            continue;
        }
        std::fseek(f, 0, SEEK_END);
        const long size = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (size <= 0) {
            std::fclose(f);
            continue;
        }
        out.resize(static_cast<std::size_t>(size));
        const std::size_t got = std::fread(&out[0], 1, out.size(), f);
        std::fclose(f);
        out.resize(got);
        return true;
    }
    return false;
}

bool ident_char(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

/* Every `vplSomething` in `text`; with must_be_called, only where a '(' follows
   (i.e. an actual call rather than a mention). */
std::set<std::string> collect(const std::string &text, bool must_be_called)
{
    std::set<std::string> out;
    const std::string prefix = "vpl";

    std::size_t at = 0;
    while ((at = text.find(prefix, at)) != std::string::npos) {
        if (at != 0 && ident_char(text[at - 1])) {
            at += prefix.size();
            continue;
        }
        std::size_t i = at;
        while (i < text.size() && ident_char(text[i])) {
            ++i;
        }
        const std::string name = text.substr(at, i - at);
        at = i;

        if (name.size() <= prefix.size()) {
            continue;
        }
        if (must_be_called) {
            std::size_t j = i;
            while (j < text.size() && std::isspace(static_cast<unsigned char>(text[j]))) {
                ++j;
            }
            if (j >= text.size() || text[j] != '(') {
                continue;
            }
        }
        out.insert(name);
    }
    return out;
}

} // namespace

int main()
{
    std::string header;
    std::string catalogue;
    if (!read_project_file("include/vpl/vpl.h", header) ||
        !read_project_file("examples/demo/demos.h", catalogue)) {
        std::fprintf(stderr, "could not find vpl.h and demos.h from the build directory\n");
        return 1;
    }

    /* Declared entry points: `VPL_API VplResult VPL_CALL vplX(...)`. */
    std::set<std::string> declared;
    {
        const std::string marker = "VPL_CALL ";
        std::size_t at = 0;
        while ((at = header.find(marker, at)) != std::string::npos) {
            std::size_t i = at + marker.size();
            const std::size_t start = i;
            while (i < header.size() && ident_char(header[i])) {
                ++i;
            }
            if (i > start) {
                declared.insert(header.substr(start, i - start));
            }
            at = i;
        }
    }

    if (declared.size() < 50) {
        /* A floor well under the real count catches a broken parse without
           pinning a number that must be edited on every addition. */
        std::fprintf(stderr, "only %zu entry points parsed out of vpl.h -- the parse broke\n",
                     declared.size());
        return 1;
    }

    const std::set<std::string> called = collect(catalogue, /*must_be_called=*/true);

    std::set<std::string> exempt;
    for (const Exemption &e : kNotDemoed) {
        exempt.insert(e.entry);
        /* An exemption for an entry that no longer exists is stale. */
        if (declared.find(e.entry) == declared.end()) {
            std::printf("  STALE   %s is excused but is no longer in vpl.h\n", e.entry);
            ++failures;
        }
    }

    int shown = 0;
    for (const std::string &name : declared) {
        if (exempt.count(name) != 0) {
            continue;
        }
        if (called.count(name) != 0) {
            ++shown;
            continue;
        }
        std::printf("  MISSING %s is in the ABI and no demo calls it\n", name.c_str());
        ++failures;
    }

    /* The other direction: a panel's `api` field must not name a call the
       catalogue never makes. */
    for (int i = 0; i < VplDemo_Count; ++i) {
        const std::set<std::string> claimed = collect(kVplDemos[i].api, /*must_be_called=*/false);
        for (const std::string &name : claimed) {
            if (called.count(name) == 0) {
                std::printf("  CLAIMED %s is named by the \"%s\" panel and called nowhere\n",
                            name.c_str(), kVplDemos[i].name);
                ++failures;
            }
        }
    }

    std::printf("demo catalogue: %d panels, %zu ABI entries, %d demonstrated, %zu excused\n",
                VplDemo_Count, declared.size(), shown, exempt.size());

    if (failures != 0) {
        std::fprintf(stderr,
                     "\n%d problem(s). Add a panel to examples/demo/demos.h that CALLS the\n"
                     "entry, or excuse it in kNotDemoed with a reason. A panel's `api` field\n"
                     "may only name calls the catalogue really makes.\n",
                     failures);
        return 1;
    }

    std::printf("every ABI entry is either demonstrated or excused\n");
    return 0;
}
