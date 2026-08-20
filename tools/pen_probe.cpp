/*
 * Print vplot's own text metrics, to hold against matplotlib's.
 *
 * This is the instrument that found the kerning bug: matplotlib's
 * FT2Font._layout reports each glyph's x, and comparing that list against
 * these numbers showed one pair 0.25 pixels apart -- which is what a quarter
 * of a pixel of drift looks like before it becomes an RMS you cannot explain.
 *
 *     python -c "...font._layout('left baseline')..."   # matplotlib's side
 *     xmake run pen-probe                               # ours
 */
#include "font.h"

#include <cstdio>
#include <string>

int main(int argc, char **argv)
{
    vpl::FontEngine fonts;
    if (!fonts.load_default_face()) {
        std::printf("could not load assets/fonts/DejaVuSans.ttf\n");
        return 1;
    }

    const std::string text = argc > 1 ? argv[1] : "left baseline";
    const double size = argc > 2 ? std::atof(argv[2]) : 10.0;

    vpl::FontProps props;
    props.size = size;

    const vpl::TextExtent e = fonts.measure(text, props, 100.0);
    std::printf("\"%s\" at %gpt: width %.4f  height %.4f  descent %.4f\n",
                text.c_str(), size, e.width, e.height, e.descent);

    /* Cumulative advance after each glyph, which is where the next one starts
       before its kern is applied. */
    std::printf("pen: ");
    for (std::size_t n = 1; n <= text.size(); ++n) {
        const std::string prefix = text.substr(0, n);
        std::printf("%.2f ", fonts.measure(prefix, props, 100.0).width);
    }
    std::printf("\n");
    return 0;
}
