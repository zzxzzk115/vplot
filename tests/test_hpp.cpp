/*
 * Exercises the C++ ergonomic layer the way a matplotlib user would write it;
 * each line's matplotlib counterpart is in the comment beside it. Builds
 * against include/vpl/vpl.hpp only, so it also proves the header stands on its
 * own over the public ABI.
 */

#include "vpl/vpl.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

int main()
{
    try {
        std::vector<double> x(120), y(120), y2(120);
        for (std::size_t i = 0; i < x.size(); ++i) {
            x[i] = static_cast<double>(i) * 6.28 / 119.0;
            y[i] = std::sin(x[i]);
            y2[i] = std::cos(x[i]);
        }

        vplot::Figure fig;                       // fig = plt.figure()
        auto ax = fig.subplots();              // ax = fig.subplots()

        ax.plot(x, y);                         // ax.plot(x, y)
        ax.plot(x, y2, "r--");                 // ax.plot(x, y2, "r--")
        ax.plot(y, {.label = "sin(index)"});   // ax.plot(y, label="sin(index)")
        ax.plot(x, y, {.color = "C2",          // ax.plot(x, y, color="C2",
                       .linewidth = 2.5,       //         linewidth=2.5,
                       .label = "thick"});     //         label="thick")

        ax.scatter({0.0, 1.0, 2.0}, {0.0, 1.0, 4.0});  // ax.scatter([0,1,2], [0,1,4])

        ax.set_xlabel("t");                    // ax.set_xlabel("t")
        ax.set_ylabel("value");                // ax.set_ylabel("value")
        ax.set_title("the c++ layer");         // ax.set_title(...)
        ax.set_xlim(0.0, 6.28);                // ax.set_xlim(0, 6.28)
        ax.grid();                             // ax.grid()
        ax.legend();                           // ax.legend()

        // A grid of panels, as `fig, axs = plt.subplots(2, 2)` returns them.
        vplot::Figure grid;
        auto axs = grid.subplots(2, 2);        // axs = fig.subplots(2, 2)
        for (std::size_t k = 0; k < axs.size(); ++k) {
            std::vector<double> yk(x.size());
            for (std::size_t i = 0; i < x.size(); ++i) {
                yk[i] = std::sin(x[i] * (k + 1));
            }
            axs[k].plot(x, yk);
            axs[k].bar({0.0, 1.0, 2.0}, {1.0, 2.0, 1.5}, 0.5,
                       {.facecolor = "C1", .alpha = 0.5});
        }
        grid.suptitle("a grid");               // fig.suptitle("a grid")
        grid.tight_layout();                   // fig.tight_layout()

        // A broad sweep of the wrapped surface: each compiles and runs through
        // the wrapper. Pixel fidelity is test-baseline's job.
        {
            vplot::Figure f2;
            auto a = f2.subplots();
            std::vector<double> s{1.0, 2.0, 2.5, 3.0, 4.0, 9.0};
            a.hist(s, 5);                                  // ax.hist(s, bins=5)
            a.hist(s, {.bins = 4, .histtype = VplHistType_Step});  // histtype="step"
            a.errorbar({0, 1, 2}, {1, 2, 1},                  // ax.errorbar(x, y,
                       {.yerr = {{0.1, 0.2, 0.1}, {0.3, 0.1, 0.2}}, //   yerr=[lo, hi],
                        .fmt = "o", .capsize = 2, .errorevery = 2});  //   fmt="o", ...)
            a.errorbar({0, 1, 2}, {1, 2, 1}, {.xerr = {{0.2, 0.2, 0.2}}, .fmt = "none"});
            a.errorbar({0, 1, 2}, {1, 2, 1},                  // errorbar(..., lolims=, elinestyle='--')
                       {.yerr = {{0.3, 0.3, 0.3}},
                        .elinestyle = VplLineStyle_Dashed,
                        .lolims = {1, 0, 1}});
            {
                vplot::Figure pf;
                auto pa = pf.subplots();
                pa.pie({1, 2, 3}, {.explode = {0, 0.1, 0}, .labels = {"a", "b", "c"},
                                   .autopct = "%1.0f%%", .startangle = 90});
                pa.pie_label({"x", "y", "z"}, 1.2, 9.0, VplPieLabelAlign_Outer, true);
                vplot::Figure df;
                auto da = df.subplots();
                da.pie({3, 2, 5}, {.wedge_width = 0.4, .wedge_edgecolor = "w", .shadow = true});
                da.pie_label_format("{absval:d} ({frac:.0%})", 0.8);  // pie_label(pc, "{absval:d}...")
            }
            a.set_anchor("SW");                            // ax.set_anchor("SW")
            {
                const std::vector<double> tx{0, 1, 1, 0}, ty{0, 0, 1, 1}, tz{0, 1, 2, 1};
                vplot::Triangulation tri{{{0, 1, 2}, {0, 2, 3}}, {0, 1}};
                a.triplot(tx, ty, tri, "go-");               // triplot(x, y, triangles, "go-", mask=)
                a.tricontour(tx, ty, tz, tri);               // tricontour(tri, z)
                a.tricontourf(tx, ty, tz, tri, {}, 4, VplContourExtend_Both);
                a.tripcolor(tx, ty, {0.2, 0.8}, tri, VplColormap_Plasma, true);
                if (vplot::triangulate(tx, ty).size() != 2) {
                    throw std::runtime_error("triangulate: a square is two triangles");
                }
            }
            a.quiver({0, 1}, {0, 1}, {1, 0}, {0, 1}, {0.2, 0.9}, VplColormap_Plasma);  // quiver(X, Y, U, V, C)
            {
                VplQuiverDesc qd{};
                qd.set = VplQuiverField_Angles | VplQuiverField_ScaleUnits | VplQuiverField_Scale;
                qd.angles = VplQuiverAngles_XY;
                qd.scaleUnits = VplQuiverUnits_XY;
                qd.scale = 1.0;
                a.quiver({0, 1}, {0, 1}, {1, 0}, {0, 1}, qd);  // quiver(..., angles='xy', scale_units='xy', scale=1)
            }
            {
                VplBoxDesc bd{};
                bd.showMeans = 1;
                bd.showNotches = 1;
                a.boxplot({{1, 2, 3, 4, 9}, {2, 3, 3, 4}}, bd);  // boxplot(d, notch=True, showmeans=True)
                VplBoxStats st{};
                st.structSize = sizeof(st);
                st.q1 = 1;
                st.median = 2;
                st.q3 = 3;
                st.whisLo = 0;
                st.whisHi = 4;
                VplBoxDesc bd2{};
                bd2.showMeans = 1;
                a.bxp({st}, bd2, {2.5});                     // bxp(stats, showmeans=True)
            }
            a.acorr({1, 3, 2, 5, 4, 6, 5, 8}, 0, true, VplSpectralDetrend_Linear, false);  // acorr(x, detrend=..., usevlines=False)
            {
                VplViolinDesc vd{};
                vd.showMedians = 1;
                vd.side = VplViolinSide_High;
                a.violinplot({{1, 2, 2, 3}, {2, 3, 3, 5}}, vd);  // violinplot(d, showmedians=True, side="high")
            }
            {
                VplHexbinDesc hd{};
                hd.gridsizeX = 5;
                hd.mincnt = 1;
                hd.reduce = VplHexReduce_Sum;
                a.hexbin({0, 1, 2, 3}, {0, 1, 0, 1}, hd, {1, 2, 3, 4});  // hexbin(x, y, C, ...)
                hd.marginals = 1;
                a.hexbin({0, 1, 2, 3}, {0, 1, 0, 1}, hd, {1, 2, 3, 4});  // hexbin(..., marginals=True)
            }
            {
                std::vector<double> zz;
                for (int i = 0; i < 25; ++i) {
                    zz.push_back(static_cast<double>(i % 5) * 0.5 + i / 5);
                }
                auto cs = a.contourf(zz, 5, 5, 6, VplContourExtend_Both);  // contourf(Z, 6, extend="both")
                if (cs.levels().empty()) {                     // cs.levels
                    std::fprintf(stderr, "contourf(Z, n) chose no levels\n");
                    return 1;
                }
                a.contour(zz, 5, 5);                           // ax.contour(Z)
            }
            vplot::HistResult hr;
            auto hp = a.hist({s, {2.0, 3.0}}, {.bins = 3, .stacked = true},
                             {{.label = "a"}, {.label = "b"}}, {}, &hr);
            if (hp.size() != 2 || hr.tops.size() != 6 || hr.edges.size() != 4 ||
                hr.tops[3] != 6.0) {                       // n, bins = ax.hist([s, t], ...)
                std::fprintf(stderr, "hist([a, b]) returned the wrong n or bins\n");
                return 1;
            }
            auto bars = a.bar_patch({0, 1, 2}, {1, 2, 3}, 0.6);  // returns a handle
            a.bar_label(bars);                             // ax.bar_label(bars)
            a.step({0, 1, 2}, {1, 3, 2}, "post");          // ax.step(..., where="post")
            a.stem({0, 1, 2}, {1, 2, 1});                  // ax.stem(x, y)
            a.stairs({1, 2, 3}, {0, 1, 2, 3});             // ax.stairs(v, e)
            a.ecdf(s);                                     // ax.ecdf(s)
            a.set_xscale("linear");                        // ax.set_xscale("linear")
            a.set_yscale("linear");                        // ax.set_yscale("linear")
            a.set_xticks({0, 1, 2});                       // ax.set_xticks([0,1,2])
            a.invert_yaxis(false);                         // ax.yaxis_inverted stays put
            a.set_spine_visible("top", false);             // ax.spines["top"].set_visible(False)
            a.grid(true);
            a.tick_params(VplAxis_X, {.direction = VplTickDirection_In,
                                      .labelsize = 8.0});   // ax.tick_params(axis="x", ...)
            a.margins(0.1, 0.1);                           // ax.margins(0.1)
            a.autoscale();                                 // ax.autoscale()
        }
        {
            vplot::Figure f3;
            auto a = f3.subplots();
            std::vector<double> field(9, 0.5);
            a.imshow(field, 3, 3, "magma");                // ax.imshow(Z, cmap="magma")
            auto cb = f3.colorbar(a, 0.0, 1.0, "magma");   // fig.colorbar(...)
            (void)cb;
        }
        {
            vplot::Figure f4;
            auto a = f4.subplots();
            std::vector<double> boxdata{1, 2, 3, 4, 9};
            a.boxplot(boxdata, {5});                       // ax.boxplot([data])
            a.axhline(2.0);                                // ax.axhline(2)
            a.axvspan(0.5, 1.5);                           // ax.axvspan(0.5, 1.5)
            a.text(1.0, 3.0, "hi");                        // ax.text(1, 3, "hi")
            auto tw = f4.twinx(a);                         // ax2 = ax.twinx()
            tw.plot({0.0, 1.0}, {0.0, 100.0});
        }

        // Rendering into a buffer, to prove the whole thing draws.
        unsigned w = 0, h = 0;
        vplot::detail::check(vplFigureGetPixelSize(fig.raw(), &w, &h));
        std::vector<unsigned char> pixels(static_cast<std::size_t>(w) * h * 4);
        vplot::detail::check(
            vplFigureRenderRGBA(fig.raw(), pixels.data(), pixels.size(), nullptr));

        // Move semantics: a figure can be handed off without copying.
        vplot::Figure moved = std::move(grid);
        vplot::detail::check(vplFigureGetPixelSize(moved.raw(), &w, &h));

        // An error becomes an exception, the way it would in Python.
        bool threw = false;
        try {
            ax.set_xlim(5.0, 5.0);  // empty limits: matplotlib would raise
        } catch (const vplot::Error &) {
            threw = true;
        }
        if (!threw) {
            std::fprintf(stderr, "empty limits did not throw\n");
            return 1;
        }
    } catch (const vplot::Error &e) {
        std::fprintf(stderr, "unexpected vplot::Error: %s\n", e.what());
        return 1;
    }

    std::printf("vpl.hpp: every matplotlib-shaped call built and ran\n");
    return 0;
}
