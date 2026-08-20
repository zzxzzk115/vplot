/*
 * MaxNLocator / AutoLocator against known matplotlib output. Expected values
 * are what matplotlib's AutoLocator produces for these ranges (nbins='auto'
 * with no axis, i.e. 9; steps [1, 2, 2.5, 5, 10]).
 */

#include "ticker.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{

int failures = 0;

void expect_ticks(const char *name, double vmin, double vmax,
                  const std::vector<double> &expected)
{
    const vpl::MaxNLocator locator;
    const std::vector<double> got = locator.tick_values(vmin, vmax);

    /* matplotlib's _raw_ticks returns edge ticks just outside the range, which
       the Axis trims when drawing; compare only what falls inside. */
    std::vector<double> visible;
    for (double t : got) {
        if (t >= vmin - 1e-9 && t <= vmax + 1e-9) {
            visible.push_back(t);
        }
    }

    bool ok = visible.size() == expected.size();
    if (ok) {
        for (std::size_t i = 0; i < expected.size(); ++i) {
            if (std::fabs(visible[i] - expected[i]) > 1e-9) {
                ok = false;
                break;
            }
        }
    }

    std::printf("%-18s [%g, %g] -> ", name, vmin, vmax);
    for (double t : visible) {
        std::printf("%g ", t);
    }

    if (ok) {
        std::printf("  OK\n");
    } else {
        std::printf("  FAIL (expected: ");
        for (double t : expected) {
            std::printf("%g ", t);
        }
        std::printf(")\n");
        ++failures;
    }
}

void expect_labels(const char *name, const std::vector<double> &locs,
                   const std::vector<std::string> &expected)
{
    const std::vector<std::string> got = vpl::format_ticks(locs);
    bool ok = got == expected;

    std::printf("%-18s -> ", name);
    for (const std::string &s : got) {
        std::printf("'%s' ", s.c_str());
    }
    if (ok) {
        std::printf("  OK\n");
    } else {
        std::printf("  FAIL\n");
        ++failures;
    }
}

} // namespace

int main()
{
    expect_ticks("unit decade", 0.0, 10.0, {0, 2, 4, 6, 8, 10});
    expect_ticks("fractional", 0.0, 1.0, {0, 0.2, 0.4, 0.6, 0.8, 1.0});
    expect_ticks("symmetric", -1.0, 1.0,
                 {-1, -0.75, -0.5, -0.25, 0, 0.25, 0.5, 0.75, 1});
    expect_ticks("negative range", -50.0, -10.0, {-50, -45, -40, -35, -30, -25, -20, -15, -10});

    /* Degenerate input must not divide by zero; nonsingular() widens it. */
    const vpl::MaxNLocator locator;
    const std::vector<double> degenerate = locator.tick_values(5.0, 5.0);
    std::printf("%-18s [5, 5] -> %zu ticks  %s\n", "degenerate",
                degenerate.size(), degenerate.empty() ? "FAIL" : "OK");
    if (degenerate.empty()) {
        ++failures;
    }

    expect_labels("integer labels", {0, 2, 4, 6, 8, 10},
                  {"0", "2", "4", "6", "8", "10"});
    expect_labels("decimal labels", {0, 0.2, 0.4, 0.6, 0.8, 1.0},
                  {"0.0", "0.2", "0.4", "0.6", "0.8", "1.0"});

    if (failures != 0) {
        std::fprintf(stderr, "\n%d ticker check(s) failed\n", failures);
        return 1;
    }
    std::printf("\nall ticker checks passed\n");
    return 0;
}
