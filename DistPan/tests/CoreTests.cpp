// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file CoreTests.cpp
    @brief Minimal dependency-free test runner for DistPanCore (no JUCE): the DBAP gain law. */

#include "distpan/Dbap.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <random>
#include <vector>

using namespace distpan;
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

/** Speakers on a ring of the given radius, first one at the front, clockwise. */
static std::vector<Vec2> ring (int count, double radius = 5.0)
{
    std::vector<Vec2> out;
    for (int i = 0; i < count; ++i)
    {
        const double theta = 2.0 * 3.14159265358979323846 * i / count;
        out.push_back ({ radius * std::sin (theta), radius * std::cos (theta) });
    }
    return out;
}

/** The rolloff in dB per doubling gives the exponent a: 6.02 dB is a = 1. */
static void testRolloffExponent()
{
    CHECK (isNear (rolloffExponent (20.0 * std::log10 (2.0)), 1.0, 1e-12));
    CHECK (isNear (rolloffExponent (12.0412), 2.0, 1e-4));
    CHECK (isNear (rolloffExponent (0.0), 0.0, 1e-12));
}

/** The gains are constant power wherever the source is, for every setting. */
static void testConstantPower()
{
    std::mt19937 rng (3);
    std::uniform_real_distribution<double> pos (-10.0, 10.0), rolloff (1.0, 12.0), blur (0.0, 5.0), spread (0.0, 10.0);

    for (const int m : { 1, 2, 3, 4, 8, 16, 64 })
    {
        const auto speakers = ring (m);
        for (int t = 0; t < 200; ++t)
        {
            const auto g = computeGains (speakers, { pos (rng), pos (rng) }, spread (rng), { rolloff (rng), blur (rng) });
            CHECK (g.count == m);
            CHECK (isNear (sumSq (g), 1.0));
        }
    }
}

/** With no blur, the gain ratio of two speakers follows (d2 / d1)^a. */
static void testRolloffLaw()
{
    const std::vector<Vec2> speakers { { 0.0, 2.0 }, { 0.0, 4.0 } }; // distances 2 and 4 from the origin
    for (const double dB : { 3.0, 6.0, 9.0 })
    {
        const auto g = computeGains (speakers, { 0.0, 0.0 }, 0.0, { dB, 0.0 });
        const double a = rolloffExponent (dB);
        CHECK (isNear ((double) g.gain[0] / (double) g.gain[1], std::pow (2.0, a), 1e-4));
    }

    // Doubling the distance costs exactly the rolloff in dB.
    const auto g = computeGains (speakers, { 0.0, 0.0 }, 0.0, { 6.0, 0.0 });
    CHECK (isNear (20.0 * std::log10 ((double) g.gain[0] / (double) g.gain[1]), 6.0, 1e-3));
}

/** The nearest speaker always gets the most, and equidistant speakers get equal shares. */
static void testNearestAndSymmetry()
{
    const auto speakers = ring (8);

    const auto centre = computeGains (speakers, { 0.0, 0.0 }, 0.0, {});
    for (int i = 1; i < 8; ++i)
        CHECK (isNear ((double) centre.gain[(size_t) i], (double) centre.gain[0], 1e-6));

    const auto near3 = computeGains (speakers, { speakers[3].x * 0.8, speakers[3].y * 0.8 }, 0.0, {});
    for (int i = 0; i < 8; ++i)
        if (i != 3)
            CHECK (near3.gain[3] > near3.gain[(size_t) i]);

    // Mirror symmetry about the y axis: speakers 1 and 7 are mirror images.
    const auto left = computeGains (speakers, { -3.0, 1.0 }, 0.0, {});
    const auto right = computeGains (speakers, { 3.0, 1.0 }, 0.0, {});
    CHECK (isNear ((double) left.gain[1], (double) right.gain[7], 1e-6));
    CHECK (isNear ((double) left.gain[7], (double) right.gain[1], 1e-6));
}

/** A source exactly on a speaker is finite and continuous, with or without blur. */
static void testCoincidentSource()
{
    const auto speakers = ring (4);

    for (const double blur : { 0.0, 0.2, 1.0 })
    {
        const auto g = computeGains (speakers, speakers[0], 0.0, { 6.0, blur });
        CHECK (g.count == 4);
        CHECK (std::isfinite (g.gain[0]) && isNear (sumSq (g), 1.0));
        CHECK (g.gain[0] > g.gain[1]);
    }

    // With no blur the speaker takes (practically) everything; with blur it shares.
    const auto sharp = computeGains (speakers, speakers[0], 0.0, { 6.0, 0.0 });
    const auto soft = computeGains (speakers, speakers[0], 0.0, { 6.0, 1.0 });
    CHECK (sharp.gain[0] > 0.999f);
    CHECK (soft.gain[0] < sharp.gain[0]);

    // No jump while the source passes over the speaker.
    double previous = (double) computeGains (speakers, { speakers[0].x - 0.5, speakers[0].y }, 0.0, { 6.0, 0.2 }).gain[0];
    for (int step = 1; step <= 100; ++step)
    {
        const double x = speakers[0].x - 0.5 + 0.01 * step;
        const double now = (double) computeGains (speakers, { x, speakers[0].y }, 0.0, { 6.0, 0.2 }).gain[0];
        CHECK (std::abs (now - previous) < 0.05);
        previous = now;
    }
}

/** Spread acts like blur: a larger spread distributes the sound more evenly. */
static void testSpreadEvensOut()
{
    const auto speakers = ring (8);
    const Vec2 source { speakers[2].x * 0.9, speakers[2].y * 0.9 };

    const auto point = computeGains (speakers, source, 0.0, {});
    const auto wide = computeGains (speakers, source, 5.0, {});
    CHECK (wide.gain[2] < point.gain[2]);
    CHECK (wide.gain[6] > point.gain[6]);
    CHECK (isNear (sumSq (wide), 1.0));
}

/** Degenerate input: no speakers, one speaker, non-finite values, more than 64 speakers. */
static void testDegenerate()
{
    const double nan = std::numeric_limits<double>::quiet_NaN();

    CHECK (computeGains ({}, { 0.0, 0.0 }, 0.0, {}).count == 0);

    const std::vector<Vec2> one { { 1.0, 1.0 } };
    const auto g1 = computeGains (one, { -3.0, 2.0 }, 0.0, {});
    CHECK (g1.count == 1 && isNear ((double) g1.gain[0], 1.0));

    CHECK (computeGains (ring (4), { nan, 0.0 }, 0.0, {}).count == 0);

    // A speaker with a non-finite position gets nothing; the rest is still constant power.
    auto speakers = ring (4);
    speakers[1] = { nan, 0.0 };
    const auto g = computeGains (speakers, { 0.0, 0.0 }, 0.0, {});
    CHECK (g.count == 4 && g.gain[1] == 0.0f && isNear (sumSq (g), 1.0));

    const std::vector<Vec2> allBad { { nan, nan }, { nan, 0.0 } };
    CHECK (computeGains (allBad, { 0.0, 0.0 }, 0.0, {}).count == 0);

    CHECK (computeGains (ring (100), { 0.0, 1.0 }, 0.0, {}).count == tlos::kMaxGainEntries);

    // Negative or non-finite settings are treated as 0.
    const auto h = computeGains (ring (4), { 1.0, 1.0 }, -2.0, { -3.0, nan });
    CHECK (isNear (sumSq (h), 1.0));
}

/** Falloff outside the speakers: the plain gains inside, on the boundary and when off; scaled by the factor outside. */
static void testOutsideFalloff()
{
    const auto speakers = ring (8); // radius 5, an octagon
    const tlos::Hull hull (speakers);
    CHECK (hull.hasArea());

    const auto same = [] (const tlos::SourceGains& a, const tlos::SourceGains& b)
    {
        if (a.count != b.count)
            return false;
        for (int i = 0; i < a.count; ++i)
            if (a.gain[(size_t) i] != b.gain[(size_t) i])
                return false;
        return true;
    };

    const DbapSettings on { 6.0, 0.2, 6.0206 }, off { 6.0, 0.2, 0.0 };

    // Inside, on the boundary, and with the falloff off: exactly the plain gains.
    for (const Vec2 p : { Vec2 { 0.0, 0.0 }, Vec2 { 1.0, 2.0 }, Vec2 { 0.0, 5.0 } })
        CHECK (same (computeGains (speakers, hull, p, 0.0, on), computeGains (speakers, p, 0.0, on)));
    for (const Vec2 p : { Vec2 { 0.0, 6.5 }, Vec2 { -9.0, 3.0 } })
        CHECK (same (computeGains (speakers, hull, p, 1.0, off), computeGains (speakers, p, 1.0, off)));

    // 1 m beyond the speaker at the front: (1 + 1)^-1 = 0.5, the same distribution, half the amplitude.
    const Vec2 outside { 0.0, 6.0 };
    const auto plain = computeGains (speakers, outside, 0.0, on);
    const auto fallen = computeGains (speakers, hull, outside, 0.0, on);
    CHECK (fallen.count == plain.count);
    for (int i = 0; i < plain.count; ++i)
        CHECK (isNear ((double) fallen.gain[(size_t) i], 0.5 * (double) plain.gain[(size_t) i], 1e-4));
    CHECK (isNear (sumSq (fallen), 0.25, 1e-4));

    // Steadily and smoothly lower while the source goes outwards, with no step at the boundary.
    double previous = 2.0;
    for (int i = 0; i <= 160; ++i)
    {
        const double power = sumSq (computeGains (speakers, hull, { 0.0, 3.0 + 0.05 * i }, 0.5, on));
        CHECK (power <= previous + 1e-6);
        CHECK (previous > 1.5 || previous - power < 0.12); // a slope of about 2 per metre at the boundary, not a step
        previous = power;
    }

    // No area: no falloff, so a stereo pair keeps its power wherever the source is.
    const std::vector<Vec2> pair { { -2.0, 3.0 }, { 2.0, 3.0 } };
    const tlos::Hull pairHull (pair);
    CHECK (! pairHull.hasArea());
    CHECK (isNear (sumSq (computeGains (pair, pairHull, { 8.0, -7.0 }, 0.0, on)), 1.0));
}

/** Speaker weights: 1 (or none) is the plain law; 0 takes a speaker out; a smaller weight gives a smaller share; power stays 1. */
static void testWeights()
{
    const auto speakers = ring (6);
    const DbapSettings settings { 6.0, 0.2, 0.0 };
    const Vec2 source { 1.0, 2.0 };

    const auto plain = computeGains (speakers, source, 0.0, settings);
    const std::vector<float> ones (6, 1.0f);
    const auto withOnes = computeGains (speakers, source, 0.0, settings, ones);
    for (int i = 0; i < 6; ++i)
        CHECK ((double) plain.gain[(size_t) i] == (double) withOnes.gain[(size_t) i]);        // exactly the same

    // Weight 0: silent, and the others share everything (constant power) in their old proportions.
    std::vector<float> w (6, 1.0f);
    w[2] = 0.0f;
    const auto without = computeGains (speakers, source, 0.0, settings, w);
    CHECK (without.gain[2] == 0.0f && isNear (sumSq (without), 1.0));
    for (int i : { 0, 1, 3, 4, 5 })
        CHECK (isNear ((double) without.gain[(size_t) i] / (double) without.gain[0], (double) plain.gain[(size_t) i] / (double) plain.gain[0], 1e-4));

    // Weight 0.5: before the normalisation the speaker's share is half; the others' proportions are unchanged.
    w[2] = 0.5f;
    const auto half = computeGains (speakers, source, 0.0, settings, w);
    CHECK (isNear ((double) half.gain[2] / (double) half.gain[0], 0.5 * (double) plain.gain[2] / (double) plain.gain[0], 1e-4));
    CHECK (isNear (sumSq (half), 1.0));
    CHECK (half.gain[2] < plain.gain[2] && half.gain[0] > plain.gain[0]);

    // A continuous change of the weight gives a continuous change of the gains.
    double previous = (double) computeGains (speakers, source, 0.0, settings, std::vector<float> { 1, 1, 0.0f, 1, 1, 1 }).gain[0];
    for (int i = 1; i <= 100; ++i)
    {
        const float weight = 0.01f * (float) i;
        const double now = (double) computeGains (speakers, source, 0.0, settings, std::vector<float> { 1, 1, weight, 1, 1, 1 }).gain[0];
        CHECK (std::abs (now - previous) < 0.02);
        previous = now;
    }

    // Short, negative and non-finite weights: missing ones count as 1, negative as 0, non-finite as 1.
    const auto shortList = computeGains (speakers, source, 0.0, settings, std::vector<float> { 1.0f, 1.0f });
    CHECK ((double) shortList.gain[3] == (double) plain.gain[3]);
    const auto odd = computeGains (speakers, source, 0.0, settings, std::vector<float> { -1.0f, std::numeric_limits<float>::quiet_NaN(), 1, 1, 1, 1 });
    CHECK (odd.gain[0] == 0.0f && isNear (sumSq (odd), 1.0));

    // All weights 0: nothing to pan to.
    CHECK (computeGains (speakers, source, 0.0, settings, std::vector<float> (6, 0.0f)).count == 0);

    // With the falloff outside the layout the weights still apply.
    const tlos::Hull hull (speakers);
    const DbapSettings outside { 6.0, 0.2, 6.0206 };
    const auto a = computeGains (speakers, hull, { 0.0, 7.0 }, 0.0, outside, w);
    const auto b = computeGains (speakers, { 0.0, 7.0 }, 0.0, outside, w);
    CHECK (a.count == b.count && a.gain[2] < b.gain[2] + 1e-9f);
}

/** Runs all core tests. @return 0 if every check passed, otherwise 1. */
int main()
{
    testRolloffExponent();
    testConstantPower();
    testRolloffLaw();
    testNearestAndSymmetry();
    testCoincidentSource();
    testSpreadEvensOut();
    testDegenerate();
    testOutsideFalloff();
    testWeights();

    if (failures == 0)
        std::printf ("All tests passed\n");
    return failures == 0 ? 0 : 1;
}
