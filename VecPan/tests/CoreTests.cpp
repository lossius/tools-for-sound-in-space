// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file CoreTests.cpp
    @brief Minimal dependency-free test runner for VecPanCore (no JUCE): the 2D VBAP gain law and the layout it works on. */

#include "vecpan/Vbap.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <numbers>
#include <vector>

using namespace vecpan;
using tlos::Vec2;

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
static bool isNear (double a, double b, double tol = 1e-5) { return std::abs (a - b) <= tol; }

/** Sum of the squared gains. */
static double sumSq (const tlos::SourceGains& g)
{
    double s = 0.0;
    for (int i = 0; i < g.count; ++i)
        s += (double) g.gain[(size_t) i] * (double) g.gain[(size_t) i];
    return s;
}

/** The gain of speaker `index` in a result (0 if it has none). */
static double gainOf (const tlos::SourceGains& g, int index)
{
    for (int i = 0; i < g.count; ++i)
        if (g.speaker[(size_t) i] == index)
            return g.gain[(size_t) i];
    return 0.0;
}

/** A point at the given azimuth (degrees, counter-clockwise from the front) and distance. */
static Vec2 at (double azimuthDegrees, double distance = 5.0)
{
    const double a = azimuthDegrees * std::numbers::pi / 180.0;
    return { -distance * std::sin (a), distance * std::cos (a) };
}

/** Speakers at the given azimuths (degrees) on a ring of radius 5 m. */
static std::vector<Vec2> speakersAt (std::initializer_list<double> azimuths)
{
    std::vector<Vec2> out;
    for (double a : azimuths)
        out.push_back (at (a));
    return out;
}

/** Azimuth convention: 0 front, +90 left, -90 right, back +-180. */
static void testAzimuth()
{
    CHECK (isNear (azimuthOf ({ 0.0, 2.0 }), 0.0));
    CHECK (isNear (azimuthOf ({ -2.0, 0.0 }), std::numbers::pi / 2.0));
    CHECK (isNear (azimuthOf ({ 2.0, 0.0 }), -std::numbers::pi / 2.0));
    CHECK (isNear (std::abs (azimuthOf ({ 0.0, -2.0 })), std::numbers::pi));
}

/** Stereo pair at +-30 degrees: the classic tangent law, constant power, a source on a speaker uses only that one. */
static void testStereoPair()
{
    const auto spk = speakersAt ({ 30.0, -30.0 });   // 0 = left, 1 = right
    const Layout layout (spk);
    CHECK (layout.size() == 2);

    auto g = computeGains (layout, at (30.0), 0.0);
    CHECK (isNear (gainOf (g, 0), 1.0) && isNear (gainOf (g, 1), 0.0));
    g = computeGains (layout, at (-30.0), 0.0);
    CHECK (isNear (gainOf (g, 1), 1.0) && isNear (gainOf (g, 0), 0.0));

    g = computeGains (layout, at (0.0), 0.0);
    CHECK (isNear (gainOf (g, 0), std::sqrt (0.5)) && isNear (gainOf (g, 1), std::sqrt (0.5)));

    // The tangent law: (gL - gR) / (gL + gR) = tan(theta) / tan(theta0).
    g = computeGains (layout, at (10.0), 0.0);
    CHECK (isNear ((gainOf (g, 0) - gainOf (g, 1)) / (gainOf (g, 0) + gainOf (g, 1)), std::tan (10.0 * std::numbers::pi / 180.0) / std::tan (30.0 * std::numbers::pi / 180.0), 1e-4));

    for (double a = -180.0; a <= 180.0; a += 7.5)
        CHECK (isNear (sumSq (computeGains (layout, at (a), 0.0)), 1.0));
}

/** A ring: only the two speakers around the source are used, with constant power, at any distance. */
static void testRingLocality()
{
    std::vector<Vec2> spk;
    for (int i = 0; i < 8; ++i)
        spk.push_back (at (-45.0 * i));          // clockwise from the front, as the plugin's default ring
    const Layout layout (spk);

    for (double a = -180.0; a < 180.0; a += 3.0)
        for (double d : { 0.5, 5.0, 30.0 })
        {
            const auto g = computeGains (layout, at (a, d), 0.0);
            CHECK (g.count <= 2 && g.count >= 1);
            CHECK (isNear (sumSq (g), 1.0));
        }

    // Halfway between two neighbours: equal shares.
    const auto g = computeGains (layout, at (-22.5), 0.0);
    CHECK (g.count == 2 && isNear (gainOf (g, 0), gainOf (g, 1)));
    // Exactly on the speaker: only that speaker.
    CHECK (isNear (gainOf (computeGains (layout, at (-135.0), 0.0), 3), 1.0));

    // Continuity: a small move never changes a gain by much.
    tlos::SourceGains previous = computeGains (layout, at (-180.0), 0.0);
    for (double a = -179.9; a <= 180.0; a += 0.1)
    {
        const auto now = computeGains (layout, at (a), 0.0);
        for (int s = 0; s < 8; ++s)
            CHECK (std::abs (gainOf (now, s) - gainOf (previous, s)) < 0.03);
        previous = now;
    }
}

/** The seam at +-180 degrees: a speaker pair across the back is panned like any other. */
static void testSeam()
{
    const Layout layout (speakersAt ({ 150.0, -150.0, 0.0 }));    // 0 = back-left, 1 = back-right, 2 = front
    const auto g = computeGains (layout, at (180.0), 0.0);
    CHECK (g.count == 2 && isNear (gainOf (g, 0), gainOf (g, 1)) && isNear (sumSq (g), 1.0));
    const auto g2 = computeGains (layout, at (-170.0), 0.0);
    CHECK (gainOf (g2, 1) > gainOf (g2, 0) && gainOf (g2, 2) == 0.0);
}

/** Speakers that do not surround the listener: a cross-fade over the wide gap, continuous, constant power. */
static void testWideGap()
{
    const Layout layout (speakersAt ({ 90.0, 30.0 }));            // both on the left side: the gap to the right is wide
    CHECK (layout.size() == 2);

    for (double a = -180.0; a <= 180.0; a += 5.0)
        CHECK (isNear (sumSq (computeGains (layout, at (a), 0.0)), 1.0));

    // Continuity through the wide gap and its ends.
    auto previous = computeGains (layout, at (-180.0), 0.0);
    for (double a = -179.9; a <= 180.0; a += 0.1)
    {
        const auto now = computeGains (layout, at (a), 0.0);
        CHECK (std::abs (gainOf (now, 0) - gainOf (previous, 0)) < 0.03 && std::abs (gainOf (now, 1) - gainOf (previous, 1)) < 0.03);
        previous = now;
    }

    // Opposite the speakers the sound is shared equally (the middle of the wide gap).
    const auto g = computeGains (layout, at (-120.0), 0.0);     // 90 + 150 degrees
    CHECK (isNear (gainOf (g, 0), gainOf (g, 1), 1e-3));
}

/** One speaker gets everything; none gives silence; unusable speakers are ignored; a source at the listener is shared. */
static void testEdgeCases()
{
    const std::vector<Vec2> one { at (40.0) };
    CHECK (isNear (gainOf (computeGains (Layout (one), at (-100.0), 0.0), 0), 1.0));

    CHECK (computeGains (Layout (std::vector<Vec2> {}), at (0.0), 0.0).count == 0);

    const double nan = std::numeric_limits<double>::quiet_NaN();
    const std::vector<Vec2> odd { at (30.0), { nan, 1.0 }, { 0.0, 0.0 }, at (-30.0) };    // indices 1 and 2 do not take part
    const Layout layout (odd);
    CHECK (layout.size() == 2);
    const auto g = computeGains (layout, at (0.0), 0.0);
    CHECK (gainOf (g, 1) == 0.0 && gainOf (g, 2) == 0.0 && isNear (sumSq (g), 1.0));

    // Not finite: nothing.
    CHECK (computeGains (layout, { nan, 0.0 }, 0.0).count == 0);

    // At the listener: every speaker the same share.
    const auto centre = computeGains (Layout (speakersAt ({ 0.0, 90.0, 180.0, -90.0 })), { 0.0, 0.0 }, 0.0);
    CHECK (centre.count == 4 && isNear (gainOf (centre, 0), 0.5) && isNear (gainOf (centre, 3), 0.5));

    // Two speakers in the same direction count as one (the first takes the sound).
    const Layout same (std::vector<Vec2> { at (30.0, 3.0), at (30.0, 8.0), at (-30.0) });
    CHECK (same.size() == 2);
    CHECK (isNear (gainOf (computeGains (same, at (30.0), 0.0), 0), 1.0) && gainOf (computeGains (same, at (30.0), 0.0), 1) == 0.0);
}

/** Spread: the sound widens over more speakers, power stays 1, and it is continuous from 0. */
static void testSpread()
{
    std::vector<Vec2> spk;
    for (int i = 0; i < 8; ++i)
        spk.push_back (at (-45.0 * i));
    const Layout layout (spk);

    const auto point = computeGains (layout, at (0.0), 0.0);
    CHECK (point.count == 1);

    const auto wide = computeGains (layout, at (0.0), 3.0);        // atan(3/5) = 31 degrees each side
    CHECK (wide.count == 3 && isNear (sumSq (wide), 1.0) && gainOf (wide, 0) > gainOf (wide, 1));

    const auto tiny = computeGains (layout, at (0.0), 1e-6);
    CHECK (isNear (gainOf (tiny, 0), 1.0, 1e-3));

    // A source further away with the same spread is narrower.
    CHECK (computeGains (layout, at (0.0, 50.0), 3.0).count < wide.count || gainOf (computeGains (layout, at (0.0, 50.0), 3.0), 0) > gainOf (wide, 0));

    // A spread at the listener: shared equally.
    CHECK (isNear (gainOf (computeGains (layout, { 0.0, 0.0 }, 3.0), 0), std::sqrt (1.0 / 8.0)));
}

/** The level outside the layout falls off like the other plugins'; inside, the distance changes nothing. */
static void testOutside()
{
    const Layout layout (speakersAt ({ 45.0, 135.0, -135.0, -45.0 }));    // a square, ring radius 5 m
    const VbapSettings off {}, on { 6.0 };

    CHECK (isNear (sumSq (computeGains (layout, at (10.0, 2.0), 0.0, on)), 1.0));            // inside: unchanged
    CHECK (isNear (sumSq (computeGains (layout, at (10.0, 30.0), 0.0, off)), 1.0));          // off: constant at any distance

    const double outside = std::sqrt (sumSq (computeGains (layout, at (10.0, 30.0), 0.0, on)));
    CHECK (outside < 0.2 && outside > 0.0);
    CHECK (outside < std::sqrt (sumSq (computeGains (layout, at (10.0, 8.0), 0.0, on))));    // further is quieter
}

/** The layout's order and pairs. */
static void testLayoutOrder()
{
    const Layout layout (speakersAt ({ 90.0, -90.0, 0.0, 180.0 }));
    const auto order = layout.order();
    // By increasing azimuth: -90 (1), 0 (2), 90 (0), 180 (3).
    CHECK (order.size() == 4 && order[0] == 1 && order[1] == 2 && order[2] == 0 && order[3] == 3);
    CHECK (layout.pairs().size() == 4);
    for (const auto& pair : layout.pairs())
        CHECK (isNear (pair.gap, std::numbers::pi / 2.0) && ! pair.wide);
}

/** Runs all VecPanCore tests. @return 0 if every check passed, otherwise 1. */
int main()
{
    testAzimuth();
    testStereoPair();
    testRingLocality();
    testSeam();
    testWideGap();
    testEdgeCases();
    testSpread();
    testOutside();
    testLayoutOrder();

    if (failures == 0)
        std::printf ("All tests passed\n");
    return failures == 0 ? 0 : 1;
}
