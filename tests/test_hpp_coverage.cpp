/*
 * Guards that the C++ wrapper keeps up with the C ABI. Reads include/vpl/vpl.h
 * and include/vpl/vpl.hpp and fails unless every C entry point is either called
 * by the wrapper or listed below with a reason. Checks call sites (not
 * comments), so the only way past is to actually wrap the call.
 *
 * The exemptions are entries a C++ caller reaches through `.raw()` instead:
 * getters, interaction hooks, and descriptor-heavy calls.
 */

#include <cctype>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace
{

struct Exemption
{
    const char *entry;
    const char *why;
};

/* Reached through .raw() for now, each with the reason it is not yet wrapped. */
const Exemption kNotWrapped[] = {
    /* Lifecycle and rendering, called by Figure's ctor/dtor and savefig. */
    {"vplCreateFigure", "Figure's constructor calls it"},
    {"vplDestroyFigure", "Figure's destructor calls it"},
    {"vplFigureRenderRGBA", "reached through Figure::raw() to fill a caller's own buffer"},
    {"vplFigureGetPixelSize", "reached through raw(); most callers save a file instead"},
    {"vplGetLastError", "the Error exception already carries its message"},
    {"vplResultToString", "same"},

    /* Getters, reached through raw() until the wrapper grows read-side types. */
    {"vplAxesGetViewLimits", "getter, reached through raw()"},
    {"vplAxesGetXScale", "getter"},
    {"vplAxesGetYScale", "getter"},
    {"vplAxesGetXLabel", "getter"},
    {"vplAxesGetYLabel", "getter"},
    {"vplAxesGetTitle", "getter"},
    {"vplAxesGetXTicks", "getter"},
    {"vplAxesGetYTicks", "getter"},
    {"vplAxesGetXTickLabels", "getter"},
    {"vplAxesGetYTickLabels", "getter"},
    {"vplAxesGetTickParams", "getter"},
    {"vplAxesGetAxisBelow", "getter"},
    {"vplAxesGetAxisOn", "getter"},
    {"vplAxesGetFrameOn", "getter"},
    {"vplAxesGetAspect", "getter"},
    {"vplAxesGetBoxAspect", "getter"},
    {"vplAxesGetAnchor", "getter"},
    {"vplAxesGetMargins", "getter"},
    {"vplAxesGetXMargin", "getter"},
    {"vplAxesGetYMargin", "getter"},
    {"vplAxesGetPosition", "getter"},
    {"vplAxesGetFaceColor", "getter"},
    {"vplAxesGetDataRatio", "getter"},
    {"vplAxesGetAutoscale", "getter"},
    {"vplAxesGetAutoscaleX", "getter"},
    {"vplAxesGetAutoscaleY", "getter"},
    {"vplAxesGetUseStickyEdges", "getter"},
    {"vplAxesGetLegendLabel", "getter"},
    {"vplAxesGetLegendLabelCount", "getter"},
    {"vplAxesHasData", "getter"},
    {"vplFigureGetSubplotParams", "getter"},
    {"vplFigureGetSupTitle", "getter"},
    {"vplFigureGetSupXLabel", "getter"},
    {"vplFigureGetSupYLabel", "getter"},
    {"vplFigureGetFaceColor", "getter"},
    {"vplFigureGetEdgeColor", "getter"},
    {"vplAxesGetAdjustable", "getter"},
    {"vplFigureGetFrameOn", "getter"},
    {"vplFigureGetLineWidth", "getter"},
    {"vplFigureGetDpi", "getter"},
    {"vplFigureGetSizeInches", "getter"},
    {"vplFigureGetAxes", "getter"},
    {"vplFigureGetAxesCount", "getter"},

    /* Interaction, driven by an event loop the wrapper does not model. */
    {"vplAxesPan", "interactive navigation, reached through raw() by a GUI host"},
    {"vplAxesZoomAt", "interactive navigation"},

    /* Implicit-state helpers: the wrapper returns Axes objects, so there is no
       current-axes to track. */
    {"vplFigureGca", "the wrapper returns Axes objects; there is no implicit current axes"},
    {"vplFigureSca", "same"},

    /* Font path: the free function vplot::set_font_path calls it. */
    {"vplSetFontPath", "the free function vplot::set_font_path calls it"},
};

bool ident_char(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool read_file(const char *relative, std::string &out)
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

/* Every `vplSomething` in `text` that is immediately followed by '(' -- a call,
   not a mention. */
std::set<std::string> called(const std::string &text)
{
    std::set<std::string> out;
    std::size_t at = 0;
    while ((at = text.find("vpl", at)) != std::string::npos) {
        if (at != 0 && ident_char(text[at - 1])) {
            at += 3;
            continue;
        }
        std::size_t i = at;
        while (i < text.size() && ident_char(text[i])) {
            ++i;
        }
        std::size_t j = i;
        while (j < text.size() && std::isspace(static_cast<unsigned char>(text[j]))) {
            ++j;
        }
        const std::string name = text.substr(at, i - at);
        if (name.size() > 3 && j < text.size() && text[j] == '(') {
            out.insert(name);
        }
        at = i;
    }
    return out;
}

} // namespace

int main()
{
    std::string header;
    std::string wrapper;
    if (!read_file("include/vpl/vpl.h", header) || !read_file("include/vpl/vpl.hpp", wrapper)) {
        std::fprintf(stderr, "could not find vpl.h and vpl.hpp\n");
        return 1;
    }

    /* Declared entry points. */
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
    if (declared.size() < 100) {
        std::fprintf(stderr, "only %zu entries parsed from vpl.h -- the parse broke\n",
                     declared.size());
        return 1;
    }

    const std::set<std::string> wrapped = called(wrapper);

    std::set<std::string> exempt;
    int failures = 0;
    for (const Exemption &e : kNotWrapped) {
        exempt.insert(e.entry);
        if (declared.find(e.entry) == declared.end()) {
            std::printf("  STALE   %s is exempted but is no longer in vpl.h\n", e.entry);
            ++failures;
        }
    }

    int covered = 0;
    for (const std::string &name : declared) {
        if (wrapped.count(name) != 0) {
            ++covered;
            continue;
        }
        if (exempt.count(name) != 0) {
            continue;
        }
        std::printf("  MISSING %s is in the C ABI and vpl.hpp neither wraps nor exempts it\n",
                    name.c_str());
        ++failures;
    }

    std::printf("vpl.hpp coverage: %zu ABI entries, %d wrapped, %zu reached through raw()\n",
                declared.size(), covered, exempt.size());

    if (failures != 0) {
        std::fprintf(stderr,
                     "\n%d problem(s). Add a method to include/vpl/vpl.hpp that calls the\n"
                     "entry, or exempt it in kNotWrapped with the reason it is reached\n"
                     "through raw() instead.\n",
                     failures);
        return 1;
    }

    std::printf("every C ABI entry is either wrapped by vpl.hpp or exempted with a reason\n");
    return 0;
}
