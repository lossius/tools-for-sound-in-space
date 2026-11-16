// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file CoreTests.cpp
    @brief Minimal dependency-free test runner for TlosCore (no JUCE): channel limits, source groups and the hull falloff. */

#include "tlos/Groups.h"
#include "tlos/Hull.h"
#include "tlos/Limits.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

using namespace tlos;

/** Number of failed checks so far; the exit code of main(). */
static int failures = 0;

/** Counts and prints a failed condition, and carries on with the next check. */
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (! (cond)) {                                                      \
            std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);     \
            ++failures;                                                      \
        }                                                                    \
    } while (false)

/** True if `a` and `b` differ by at most `tol`. */
static bool isNear (double a, double b, double tol = 1e-6) { return std::abs (a - b) <= tol; }

/** The channel count limits (1 to 64). */
static void testLimits()
{
    CHECK (isValidChannelCount (1));
    CHECK (isValidChannelCount (64));
    CHECK (! isValidChannelCount (0));
    CHECK (! isValidChannelCount (65));
}

/** Group bookkeeping: grouping, merging, ungrouping, dissolving single members, text round trip. */
static void testSourceGroups()
{
    SourceGroups g;
    CHECK (! g.any());
    CHECK (g.group ({ 3 }) == 0);          // one source is no group
    CHECK (g.group ({ 3, 3 }) == 0);       // neither is the same source twice
    CHECK (g.group ({ -1, 99 }) == 0);

    CHECK (g.group ({ 0, 1, 2 }) == 1);
    CHECK (g.group ({ 5, 6 }) == 2);
    CHECK (g.groupOf (1) == 1 && g.groupOf (6) == 2 && g.groupOf (4) == 0);
    CHECK ((g.membersOf (1) == std::vector<int> { 0, 1, 2 }));
    CHECK ((g.membersOf (4) == std::vector<int> { 4 }));
    CHECK (g.membersOf (1, 2).size() == 2);               // only the active ones
    CHECK ((g.membersOf (2, 2) == std::vector<int> { 2 })); // inactive source alone
    CHECK (g.sizeOf (1) == 3 && g.sizeOf (1, 2) == 2 && g.sizeOf (0) == 0);

    // Merging: a source from group 1 and one from group 2 pulls both groups in whole, keeping the smaller id.
    CHECK (g.group ({ 2, 5, 9 }) == 1);
    CHECK ((g.membersOf (6) == std::vector<int> { 0, 1, 2, 5, 6, 9 }));
    CHECK (g.sizeOf (2) == 0);

    // The freed id is reused for the next new group.
    CHECK (g.group ({ 20, 21 }) == 2);

    // Ungrouping any member dissolves the whole group.
    CHECK (g.ungroup ({ 9, 20 }) == 2);
    CHECK (! g.any());

    // Groups of one are dissolved: from ids, and from text.
    SourceGroups::Ids ids {};
    ids[0] = 4; ids[1] = 4; ids[2] = 7; ids[3] = 99; ids[4] = -2;
    const SourceGroups h (ids);
    CHECK (h.groupOf (0) == 4 && h.groupOf (1) == 4);
    CHECK (h.groupOf (2) == 0 && h.groupOf (3) == 0 && h.groupOf (4) == 0);

    SourceGroups k;
    k.group ({ 0, 63 });
    k.group ({ 10, 11, 12 });
    CHECK (SourceGroups::fromString (k.toString()) == k);
    CHECK (! SourceGroups::fromString ("").any());
    CHECK (! SourceGroups::fromString ("garbage 1 1").any());
    CHECK (SourceGroups::fromString ("1 1 0 x 2 2").groupOf (1) == 1);
}

/** Rigid movement clamping, common spread change clamping and convex hull. */
static void testGroupGeometry()
{
    const std::vector<Vec2> pts { { 0.0, 0.0 }, { 4.0, 1.0 }, { 2.0, -3.0 } };

    // Free movement is passed on unchanged.
    auto d = clampTranslation (pts, { 1.0, 2.0 }, 10.0);
    CHECK (isNear (d.x, 1.0) && isNear (d.y, 2.0));

    // Against the right wall: the rightmost point (x = 4) can move by 6.
    d = clampTranslation (pts, { 50.0, 0.5 }, 10.0);
    CHECK (isNear (d.x, 6.0) && isNear (d.y, 0.5));

    // Pushed diagonally into a corner: each axis stops separately, so the group slides along the wall.
    d = clampTranslation (pts, { -50.0, 50.0 }, 10.0);
    CHECK (isNear (d.x, -10.0) && isNear (d.y, 9.0));

    // Shape is preserved: applying the clamped movement keeps all distances.
    std::vector<Vec2> moved;
    for (const auto& p : pts) moved.push_back ({ p.x + d.x, p.y + d.y });
    for (const auto& p : moved) CHECK (std::abs (p.x) <= 10.0 + 1e-9 && std::abs (p.y) <= 10.0 + 1e-9);
    CHECK (isNear (moved[1].x - moved[0].x, 4.0) && isNear (moved[2].y - moved[0].y, -3.0));

    // Degenerate input.
    d = clampTranslation ({}, { 1.0, 1.0 }, 10.0);
    CHECK (d.x == 0.0 && d.y == 0.0);
    d = clampTranslation (pts, { std::nan (""), 1.0 }, 10.0);
    CHECK (d.x == 0.0 && d.y == 0.0);

    // Spread: the largest may rise by 10 - 3 = 7, the smallest may fall by 1.
    const std::vector<double> sp { 1.0, 2.5, 3.0 };
    CHECK (isNear (clampSpreadChange (sp, 0.5, 10.0), 0.5));
    CHECK (isNear (clampSpreadChange (sp, 99.0, 10.0), 7.0));
    CHECK (isNear (clampSpreadChange (sp, -99.0, 10.0), -1.0));
    CHECK (clampSpreadChange ({}, 1.0, 10.0) == 0.0);

    // Centre of gravity.
    auto c = centroid (pts);
    CHECK (isNear (c.x, 2.0) && isNear (c.y, -2.0 / 3.0));
    CHECK (centroid ({}).x == 0.0);

    // Largest scale factor about a centre: here the y offset -5 limits it to 2.
    CHECK (isNear (maxScaleFactor ({ { -2.0, 1.0 }, { 4.0, 1.0 }, { 0.0, -5.0 } }, { 0.0, 0.0 }, 10.0), 2.0));
    CHECK (isNear (maxScaleFactor ({ { 1.0, 1.0 }, { 3.0, 1.0 } }, { 2.0, 1.0 }, 10.0), 8.0));   // (10 - 2) / 1
    CHECK (maxScaleFactor ({ { 2.0, 2.0 } }, { 2.0, 2.0 }, 10.0) > 1e6);                       // no offsets: unlimited

    // Rotation: a quarter turn counter-clockwise about the origin takes (1, 0) to (0, 1).
    auto r = rotateAbout ({ 1.0, 0.0 }, { 0.0, 0.0 }, 1.5707963267948966);
    CHECK (isNear (r.x, 0.0) && isNear (r.y, 1.0));
    const std::vector<Vec2> ring { { 6.0, 0.0 }, { -6.0, 0.0 } };       // radius 6 about the origin: always inside +-10
    CHECK (isNear (clampRotation (ring, { 0.0, 0.0 }, 0.0, 5.0, 10.0), 5.0));
    // A wall: points at radius 6 about (5, 0) leave the square (x up to 11) when turned to the right.
    const std::vector<Vec2> beside { { 5.0, 6.0 }, { 5.0, -6.0 } };
    const double stop = clampRotation (beside, { 5.0, 0.0 }, 0.0, -1.5, 10.0);   // wanted -1.5 rad (clockwise)
    CHECK (stop < 0.0 && stop > -1.5);
    for (const auto& q : beside)
    {
        const auto m = rotateAbout (q, { 5.0, 0.0 }, stop);
        CHECK (std::abs (m.x) <= 10.0 + 1e-6 && std::abs (m.y) <= 10.0 + 1e-6);
    }
    CHECK (isNear (clampRotation (beside, { 5.0, 0.0 }, stop, 0.0, 10.0), 0.0));   // and back again

    // Hull: the inner point and the point on an edge are dropped, order is counter-clockwise.
    auto hull = convexHull ({ { 0, 0 }, { 4, 0 }, { 4, 4 }, { 0, 4 }, { 2, 2 }, { 2, 0 } });
    CHECK (hull.size() == 4);
    double area = 0.0;
    for (size_t i = 0; i < hull.size(); ++i)
    {
        const auto a = hull[i], b = hull[(i + 1) % hull.size()];
        area += a.x * b.y - b.x * a.y;
    }
    CHECK (isNear (area / 2.0, 16.0));
    CHECK (convexHull ({ { 1, 1 }, { 1, 1 } }).size() == 1);
    CHECK (convexHull ({ { 0, 0 }, { 1, 1 }, { 2, 2 } }).size() == 2);
}

/** The hull: distance 0 inside and on the boundary, the distance to the nearest edge or corner outside, no effect without area. */
static void testHull()
{
    const std::vector<Vec2> square { { -2, -2 }, { 2, -2 }, { 2, 2 }, { -2, 2 }, { 0, 0 }, { 1, 1 } }; // the inner points do not matter
    const Hull hull (square);
    CHECK (hull.hasArea() && hull.getCorners().size() == 4);

    CHECK (isNear (hull.distance ({ 0, 0 }), 0.0));
    CHECK (isNear (hull.distance ({ 1.9, -1.9 }), 0.0));
    CHECK (isNear (hull.distance ({ 2, 0 }), 0.0));       // on an edge
    CHECK (isNear (hull.distance ({ 2, 2 }), 0.0));       // on a corner
    CHECK (isNear (hull.distance ({ 5, 0 }), 3.0));       // beside an edge
    CHECK (isNear (hull.distance ({ 0, -2.5 }), 0.5));
    CHECK (isNear (hull.distance ({ 5, 6 }), 5.0));       // beyond a corner: 3-4-5

    // A triangle: continuous across the boundary.
    const Hull triangle (std::vector<Vec2> { { 0, 0 }, { 4, 0 }, { 0, 4 } });
    CHECK (isNear (triangle.distance ({ 1, 1 }), 0.0));
    CHECK (isNear (triangle.distance ({ 3, 3 }), std::sqrt (2.0)));   // |3 + 3 - 4| / sqrt(2) from the hypotenuse
    double previous = triangle.distance ({ 0.5, 0.5 });
    for (int i = 1; i <= 400; ++i)
    {
        const double x = 0.5 + 0.01 * i;
        const double d = triangle.distance ({ x, x });
        CHECK (d >= previous - 1e-12 && d - previous < 0.02);
        previous = d;
    }

    // No area: nothing to be outside of.
    CHECK (! Hull().hasArea() && isNear (Hull().distance ({ 9, 9 }), 0.0));
    CHECK (! Hull (std::vector<Vec2> { { 1, 1 } }).hasArea());
    CHECK (isNear (Hull (std::vector<Vec2> { { 0, 0 }, { 4, 0 } }).distance ({ 2, 5 }), 0.0));
    CHECK (isNear (Hull (std::vector<Vec2> { { 0, 0 }, { 1, 1 }, { 2, 2 } }).distance ({ 0, 9 }), 0.0));

    // Non-finite points and positions are ignored.
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const Hull withNan (std::vector<Vec2> { { 0, 0 }, { 4, 0 }, { 0, 4 }, { nan, 1 } });
    CHECK (withNan.getCorners().size() == 3);
    CHECK (isNear (withNan.distance ({ nan, 0 }), 0.0));
}

/** The falloff: 1 up to the boundary, then (1 + d)^(-rolloff / 6.02): each doubling of (1 + d) costs `rolloff` dB. */
static void testOutsideFactor()
{
    CHECK (isNear (outsideFactor (0.0, 6.0), 1.0));
    CHECK (isNear (outsideFactor (-1.0, 6.0), 1.0));
    CHECK (isNear (outsideFactor (3.0, 0.0), 1.0));      // off
    CHECK (isNear (outsideFactor (3.0, -6.0), 1.0));
    CHECK (isNear (outsideFactor (std::numeric_limits<double>::quiet_NaN(), 6.0), 1.0));

    // 1 m outside = (1 + 1) = one doubling: 6 dB down; 3 m outside = two doublings: 12 dB down.
    CHECK (isNear (20.0 * std::log10 (outsideFactor (1.0, 6.0206)), -6.0206, 1e-3));
    CHECK (isNear (20.0 * std::log10 (outsideFactor (3.0, 6.0206)), -12.0412, 1e-3));
    CHECK (isNear (20.0 * std::log10 (outsideFactor (1.0, 12.0)), -12.0 * std::log2 (2.0), 1e-3));

    // Falls steadily and smoothly, and a larger rolloff always gives less.
    double previous = 1.0;
    for (int i = 1; i <= 200; ++i)
    {
        const double f = outsideFactor (0.05 * i, 6.0);
        CHECK (f < previous && previous - f < 0.06);
        CHECK (outsideFactor (0.05 * i, 12.0) < f);
        previous = f;
    }
}

/** Runs all shared core tests. @return 0 if every check passed, otherwise 1. */
int main()
{
    testLimits();
    testSourceGroups();
    testGroupGeometry();
    testHull();
    testOutsideFactor();

    if (failures == 0)
        std::printf ("All tests passed\n");
    return failures == 0 ? 0 : 1;
}
