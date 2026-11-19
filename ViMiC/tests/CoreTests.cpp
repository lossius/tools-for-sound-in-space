// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file CoreTests.cpp
    @brief Minimal dependency-free test runner for ViMiCCore (no JUCE): the microphone pattern, the distance laws, the direct path and the
    interpolated delay line (accuracy, wrap-around, ramps, Doppler). */

#include "vimic/Absorption.h"
#include "vimic/DelayLine.h"
#include "vimic/Directivity.h"
#include "vimic/Filter.h"
#include "vimic/Path.h"
#include "vimic/Reflections.h"
#include "vimic/Room.h"

#include <cmath>
#include <cstdio>
#include <algorithm>
#include <limits>
#include <numbers>
#include <random>
#include <set>
#include <vector>

using namespace vimic;
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
static bool isNear (double a, double b, double tol = 1e-9) { return std::abs (a - b) <= tol; }

/** Degrees to radians. */
static double rad (double degrees) { return degrees * std::numbers::pi / 180.0; }

/** The pattern: the table of the manual, the exponent, the polarity. */
static void testDirectivity()
{
    // The six presets on the axis, at 90 degrees and behind.
    const PatternPreset presets[] { PatternPreset::Omni, PatternPreset::Subcardioid, PatternPreset::Cardioid, PatternPreset::Supercardioid, PatternPreset::Hypercardioid, PatternPreset::Figure8 };
    const double a[] { 1.0, 0.7, 0.5, 1.0 / 3.0, 0.3, 0.0 };
    for (int i = 0; i < 6; ++i)
    {
        CHECK (isNear (presetRatio (presets[i]), a[i]));
        CHECK (isNear (directivity (a[i], 1, 1.0, false), 1.0));                      // on the axis: 1 for every pattern
        CHECK (isNear (directivity (a[i], 1, 0.0, false), a[i]));                     // at 90 degrees: a
        CHECK (isNear (directivity (a[i], 1, -1.0, false), 2.0 * a[i] - 1.0));        // behind: 2a - 1
    }

    // A cardioid is silent behind; a figure-8 has an anti-phase rear lobe unless the polarity is restricted.
    CHECK (isNear (directivity (0.5, 1, -1.0, true), 0.0));
    CHECK (isNear (directivity (0.0, 1, -1.0, false), -1.0) && isNear (directivity (0.0, 1, -1.0, true), 0.0));
    CHECK (isNear (directivity (0.0, 1, std::cos (rad (60.0)), true), 0.5));

    // The exponent sharpens the pattern; an even one never gives a negative value.
    CHECK (isNear (directivity (0.5, 2, 0.0, true), 0.25) && isNear (directivity (0.5, 0, 0.3, true), 1.0));
    CHECK (isNear (directivity (0.0, 2, -0.5, false), 0.25) && isNear (directivity (0.0, 3, -0.5, false), -0.125));

    // Out-of-range input is limited; rubbish gives 0.
    CHECK (isNear (directivity (3.0, 1, 5.0, true), 1.0) && isNear (directivity (-1.0, 1, 1.0, true), 1.0) && isNear (directivity (0.5, 20, 0.0, true), std::pow (0.5, 9.0)));
    CHECK (directivity (std::numeric_limits<double>::quiet_NaN(), 1, 0.0, true) == 0.0);
    CHECK (directivity (0.5, 1, std::numeric_limits<double>::infinity(), true) == 0.0);
}

/** The distance laws and the speed of sound. */
static void testDistanceAndSpeed()
{
    CHECK (isNear (speedOfSound (20.0), 343.42) && isNear (speedOfSound (0.0), 331.3) && isNear (speedOfSound (-1000.0), 331.3 + 0.606 * -40.0));

    PathSettings s;
    CHECK (isNear (distanceGain (2.0, s), 0.5) && isNear (distanceGain (4.0, s), 0.25));       // q = 1: 1 / d
    CHECK (isNear (distanceGain (0.2, s), 1.0));                                               // never above 1: limited at minDistance (1 m)
    s.power = 2.0;
    CHECK (isNear (distanceGain (4.0, s), 1.0 / 16.0));
    s.power = 0.0;
    CHECK (isNear (distanceGain (50.0, s), 1.0));                                              // q = 0: no attenuation
    s.minDistance = 0.1;
    s.power = 1.0;
    CHECK (isNear (distanceGain (0.2, s), 5.0));                                              // not limited to 1 any more (computeDirectPath() clamps the total)

    s.law = DistanceLaw::Exponential;
    s.minDistance = 1.0;
    s.dbPerMetre = 6.0;
    CHECK (isNear (distanceGain (1.0, s), std::pow (10.0, -6.0 / 20.0)) && isNear (distanceGain (10.0, s), std::pow (10.0, -3.0)));
    s.dbPerMetre = 0.0;
    CHECK (isNear (distanceGain (30.0, s), 1.0));
}

/** The direct path: geometry, delay, pattern, heights, and the gain limits. */
static void testDirectPath()
{
    PathSettings s;
    s.sourceHeight = s.micHeight = 1.5;
    Microphone mic;
    mic.position = { 0.0, 0.0 };
    mic.yaw = 0.0;
    mic.pattern = 0.5;      // cardioid pointing to the front

    // A source 4 m in front: on axis, gain 1/4, delay 4 / c.
    auto r = computeDirectPath ({ 0.0, 4.0 }, mic, s);
    CHECK (isNear (r.distance, 4.0) && isNear (r.gain, 0.25) && isNear (r.delaySeconds, 4.0 / speedOfSound (20.0)));

    // The same distance at 90 degrees (left): a cardioid gives half; behind: nothing.
    CHECK (isNear (computeDirectPath ({ -4.0, 0.0 }, mic, s).gain, 0.25 * 0.5));
    CHECK (isNear (computeDirectPath ({ 4.0, 0.0 }, mic, s).gain, 0.25 * 0.5));
    CHECK (isNear (computeDirectPath ({ 0.0, -4.0 }, mic, s).gain, 0.0));

    // Yaw is counter-clockwise: a microphone turned 90 degrees to the left hears a source on the left on axis.
    mic.yaw = rad (90.0);
    CHECK (isNear (computeDirectPath ({ -4.0, 0.0 }, mic, s).gain, 0.25));
    CHECK (isNear (computeDirectPath ({ 4.0, 0.0 }, mic, s).gain, 0.0));
    CHECK (isNear (computeDirectPath ({ 0.0, 4.0 }, mic, s).gain, 0.125));

    // The microphone's own position counts: it is the direction from the microphone, not from the origin.
    mic.position = { 3.0, 3.0 };
    mic.yaw = rad (-90.0);                                   // pointing right (+x)
    CHECK (isNear (computeDirectPath ({ 7.0, 3.0 }, mic, s).gain, 0.25));

    // A height difference makes the path longer and turns the source off the axis.
    mic.position = { 0.0, 0.0 };
    mic.yaw = 0.0;
    s.sourceHeight = 4.5;                                    // 3 m above the microphone
    r = computeDirectPath ({ 0.0, 4.0 }, mic, s);
    CHECK (isNear (r.distance, 5.0) && isNear (r.gain, 0.2 * 0.5 * (1.0 + 4.0 / 5.0)));
    s.sourceHeight = 1.5;

    // Omni microphones: a gain from the distance alone, whatever the direction; power 2 of the pattern changes nothing for omni.
    mic.pattern = 1.0;
    s.micPower = 2;
    CHECK (isNear (computeDirectPath ({ 0.0, -4.0 }, mic, s).gain, 0.25));

    // The gain is limited to 1 and, with the polarity free, to -1; a source on the microphone gives gain 1 at minDistance.
    s.micPower = 1;
    mic.pattern = 0.0;                                       // figure-8
    CHECK (isNear (computeDirectPath ({ 0.0, -2.0 }, mic, s).gain, 0.0));       // restricted: no anti-phase lobe
    s.restrictPolarity = false;
    CHECK (isNear (computeDirectPath ({ 0.0, -2.0 }, mic, s).gain, -0.5));
    s.minDistance = 0.1;
    s.restrictPolarity = true;
    mic.pattern = 1.0;
    CHECK (isNear (computeDirectPath ({ 0.0, 0.0 }, mic, s).gain, 1.0));        // on the microphone: capped at 1

    // Not finite: nothing.
    CHECK (computeDirectPath ({ std::numeric_limits<double>::quiet_NaN(), 0.0 }, mic, s).gain == 0.0);
    Microphone bad = mic;
    bad.position.y = std::numeric_limits<double>::infinity();
    CHECK (computeDirectPath ({ 0.0, 1.0 }, bad, s).gain == 0.0);
}

/** The source's radiation pattern: omni changes nothing, a cardioid source radiates in its direction only, yaw is counter-clockwise, the exponent sharpens. */
static void testSourceDirectivity()
{
    CHECK (isNear (sourceDirectivity (1.0, 1, -1.0), 1.0) && isNear (sourceDirectivity (0.5, 1, 1.0), 1.0) && isNear (sourceDirectivity (0.5, 1, -1.0), 0.0));
    CHECK (isNear (sourceDirectivity (0.0, 1, -1.0), 0.0));                      // never negative: no anti-phase radiation
    CHECK (isNear (sourceDirectivity (0.5, 2, 0.0), 0.25));

    PathSettings s;
    Microphone mic;
    mic.position = { 0.0, 0.0 };
    mic.pattern = 1.0;                                                           // an omni microphone: only the source's pattern shows
    Source cardioid { { 0.0, 4.0 }, 0.0, 0.5 };                                  // 4 m in front of the microphone, facing the front: away from it

    CHECK (isNear (computeDirectPath (cardioid, mic, s).gain, 0.0));             // the microphone is behind the source
    cardioid.yaw = std::numbers::pi;                                             // turned round: facing the microphone
    CHECK (isNear (computeDirectPath (cardioid, mic, s).gain, 0.25));
    cardioid.yaw = rad (90.0);                                                   // turned to the left: the microphone is at its right side
    CHECK (isNear (computeDirectPath (cardioid, mic, s).gain, 0.25 * 0.5));
    cardioid.yaw = rad (-90.0);                                                  // to the right: the microphone at its left side
    CHECK (isNear (computeDirectPath (cardioid, mic, s).gain, 0.25 * 0.5));

    // A source to the left of the microphone, facing the right (yaw -90 degrees): faces the microphone.
    Source onLeft { { -4.0, 0.0 }, rad (-90.0), 0.5 };
    CHECK (isNear (computeDirectPath (onLeft, mic, s).gain, 0.25));
    onLeft.yaw = rad (90.0);
    CHECK (isNear (computeDirectPath (onLeft, mic, s).gain, 0.0));

    // The exponent, the heights, and the omni overload.
    s.sourcePower = 2;
    cardioid.yaw = rad (90.0);
    CHECK (isNear (computeDirectPath (cardioid, mic, s).gain, 0.25 * 0.25));
    s.sourcePower = 1;
    s.sourceHeight = 4.5;                                                        // 3 m above the microphone: the path is 5 m, the microphone is below and behind
    cardioid.yaw = std::numbers::pi;                                             // facing the microphone horizontally
    CHECK (isNear (computeDirectPath (cardioid, mic, s).gain, 0.2 * (0.5 + 0.5 * 4.0 / 5.0)));
    CHECK (isNear (computeDirectPath (Vec2 { 0.0, 4.0 }, mic, PathSettings {}).gain, computeDirectPath (Source { { 0.0, 4.0 }, 1.234, 1.0 }, mic, PathSettings {}).gain));

    // A source on the microphone (no direction) is not turned down.
    CHECK (isNear (computeDirectPath (Source { { 0.0, 0.0 }, 1.0, 0.0 }, mic, PathSettings { .minDistance = 0.1 }).gain, 1.0));
}

/** A reference for the delay line: the same Hermite interpolation of a full history. */
static double hermiteAt (const std::vector<double>& x, double position)
{
    const double base = std::floor (position);
    const long i = (long) base;
    const double mu = position - base, mu2 = mu * mu, mu3 = mu2 * mu;
    const auto at = [&] (long k) { return k >= 0 && k < (long) x.size() ? x[(size_t) k] : 0.0; };
    return (-0.5 * mu3 + mu2 - 0.5 * mu) * at (i - 1) + (1.5 * mu3 - 2.5 * mu2 + 1.0) * at (i)
         + (-1.5 * mu3 + 2.0 * mu2 + 0.5 * mu) * at (i + 1) + (0.5 * mu3 - 0.5 * mu2) * at (i + 2);
}

/** Integer delays: the signal comes out exactly `delay` samples later, also after the buffer has wrapped round many times; gain ramps are linear. */
static void testDelayLineIntegerAndRamp()
{
    DelayLine line;
    line.prepare (300, 64);
    const int blockSize = 48;                 // odd size on purpose: blocks never line up with the buffer
    std::vector<double> signal;
    std::vector<float> out (blockSize), in (blockSize);

    for (int block = 0; block < 400; ++block)   // 19200 samples: the buffer (512) wraps round about 37 times
    {
        for (int i = 0; i < blockSize; ++i)
        {
            in[(size_t) i] = (float) std::sin (0.37 * (double) signal.size() + 0.1 * std::cos (0.011 * (double) signal.size()));
            signal.push_back (in[(size_t) i]);
        }

        line.write (in.data(), blockSize);
        std::fill (out.begin(), out.end(), 0.0f);
        line.addTap (out.data(), blockSize, 100.0, 100.0, 1.0f, 1.0f);
        for (int i = 0; i < blockSize; ++i)
        {
            const long n = (long) signal.size() - blockSize + i;
            const double expected = n >= 100 ? signal[(size_t) (n - 100)] : 0.0;
            CHECK (std::abs (out[(size_t) i] - expected) < 1e-6);
        }
    }

    // A tap adds to what is there, a gain ramp is linear (g0 at the first sample, g1 one block later) and a delay below 2 samples is raised to 2.
    DelayLine ones;
    ones.prepare (100, 32);
    const std::vector<float> one (32, 1.0f);
    for (int block = 0; block < 5; ++block)
        ones.write (one.data(), 32);
    std::vector<float> acc (32, 0.5f);
    ones.addTap (acc.data(), 32, 10.0, 10.0, 0.0f, 1.0f);
    for (int j = 0; j < 32; ++j)
        CHECK (std::abs (acc[(size_t) j] - (0.5f + (float) j / 32.0f)) < 1e-5f);
    std::vector<float> clamped (32, 0.0f), reference (32, 0.0f);
    ones.addTap (clamped.data(), 32, 0.0, 0.0, 1.0f, 1.0f);
    ones.addTap (reference.data(), 32, 2.0, 2.0, 1.0f, 1.0f);
    CHECK (clamped == reference);

    // A block of a different size than the last one written is ignored; before prepare() nothing happens.
    std::vector<float> ignored (16, 0.0f);
    ones.addTap (ignored.data(), 16, 10.0, 10.0, 1.0f, 1.0f);
    CHECK (ignored == std::vector<float> (16, 0.0f));
    DelayLine empty;
    empty.write (one.data(), 32);
    empty.addTap (acc.data(), 32, 10.0, 10.0, 1.0f, 1.0f);
}

/** Fractional delays: the same as the reference interpolation, and close to the ideal delayed sine. */
static void testDelayLineFractional()
{
    DelayLine line;
    line.prepare (2000, 100);
    std::vector<double> signal;
    std::vector<float> in (100), out (100);

    for (int block = 0; block < 100; ++block)
    {
        for (int i = 0; i < 100; ++i)
        {
            const double v = std::sin (0.9 * (double) signal.size()) * 0.5 + std::sin (0.05 * (double) signal.size());
            in[(size_t) i] = (float) v;
            signal.push_back ((double) (float) v);
        }

        line.write (in.data(), 100);
        std::fill (out.begin(), out.end(), 0.0f);
        line.addTap (out.data(), 100, 123.37, 123.37, 1.0f, 1.0f);
        for (int i = 0; i < 100; ++i)
        {
            const long n = (long) signal.size() - 100 + i;
            CHECK (std::abs ((double) out[(size_t) i] - hermiteAt (signal, (double) n - 123.37)) < 1e-5);
        }
    }

    // A pure tone, delayed by a fractional amount, against the ideal delay: the error of the interpolation (small for tones well below the Nyquist
    // frequency), for fractions of every size.
    for (const double frequency : { 0.01, 0.05, 0.1 })                       // as fractions of the sample rate
    {
        double worst = 0.0;
        for (const double delay : { 50.0, 50.1, 50.25, 50.5, 50.75, 50.9 })
        {
            DelayLine tone;
            tone.prepare (200, 256);
            std::vector<float> a (256), b (256, 0.0f);
            for (int i = 0; i < 256; ++i)
                a[(size_t) i] = (float) std::sin (2.0 * std::numbers::pi * frequency * i);
            tone.write (a.data(), 256);
            tone.addTap (b.data(), 256, delay, delay, 1.0f, 1.0f);
            for (int i = 100; i < 256; ++i)
                worst = std::max (worst, std::abs ((double) b[(size_t) i] - std::sin (2.0 * std::numbers::pi * frequency * ((double) i - delay))));
        }
        const double bound = frequency < 0.02 ? 1e-4 : frequency < 0.06 ? 2e-3 : 1.5e-2;
        CHECK (worst < bound);
    }
}

/** A changing delay against the exact interpolation of every sample (the fast version reads in runs): slow and fast drifts, in both directions, across the buffer end. */
static void testDelayLineVariableAgainstReference()
{
    for (const double drift : { 0.0003, -0.0003, 0.02, -0.02, 0.9, -0.9 })           // delay change per sample
        for (const double start : { 20.0, 333.3, 1000.7 })
        {
            const int blockSize = 100;
            DelayLine line;
            line.prepare (1500, blockSize);
            std::vector<double> signal;
            std::vector<float> in (blockSize), out (blockSize);
            double delay = start;
            double worst = 0.0;

            for (int block = 0; block < 700; ++block)           // 70000 samples: the buffer wraps round about 40 times
            {
                for (int i = 0; i < blockSize; ++i)
                {
                    const double v = std::sin (0.21 * (double) signal.size()) + 0.5 * std::sin (0.013 * (double) signal.size() + 1.0);
                    in[(size_t) i] = (float) v;
                    signal.push_back ((double) (float) v);
                }

                const double next = std::clamp (delay + drift * blockSize, 3.0, 1400.0);
                line.write (in.data(), blockSize);
                std::fill (out.begin(), out.end(), 0.0f);
                line.addTap (out.data(), blockSize, delay, next, 1.0f, 1.0f);

                for (int i = 0; i < blockSize; ++i)
                {
                    const long n = (long) signal.size() - blockSize + i;
                    const double d = delay + (next - delay) * i / blockSize;
                    worst = std::max (worst, std::abs ((double) out[(size_t) i] - hermiteAt (signal, (double) n - d)));
                }

                delay = next;
            }

            CHECK (worst < 2e-3);
        }
}

/** A moving source: the delay changes across a block (and carries on in the next one), giving a Doppler shift without discontinuities. */
static void testDelayLineDoppler()
{
    const double frequency = 0.05;                   // of the sample rate
    const double slope = 0.01;                       // the delay grows by 0.01 samples per sample: the source moves away at 0.01 c
    const int blockSize = 128, blocks = 80;
    DelayLine line;
    line.prepare (2000, blockSize);
    std::vector<float> in (blockSize), out ((size_t) (blockSize * blocks), 0.0f);

    double delay = 100.0;
    for (int block = 0; block < blocks; ++block)
    {
        for (int i = 0; i < blockSize; ++i)
            in[(size_t) i] = (float) std::sin (2.0 * std::numbers::pi * frequency * (block * blockSize + i));
        line.write (in.data(), blockSize);
        line.addTap (out.data() + block * blockSize, blockSize, delay, delay + slope * blockSize, 1.0f, 1.0f);
        delay += slope * blockSize;
    }

    // The output is the input delayed by delay(t) = 100 + slope t, i.e. sin(2 pi f ((1 - slope) t - 100)): a frequency lower by the factor
    // (1 - slope). Compare with that, from sample 1000 on.
    double worst = 0.0;
    for (int n = 1000; n < blockSize * blocks; ++n)
        worst = std::max (worst, std::abs ((double) out[(size_t) n] - std::sin (2.0 * std::numbers::pi * frequency * ((1.0 - slope) * n - 100.0))));
    CHECK (worst < 4e-3);

    // The pitch measured from zero crossings: lower by 1 - slope.
    int crossings = 0;
    long first = -1, last = -1;
    for (int n = 1001; n < blockSize * blocks; ++n)
        if (out[(size_t) n - 1] < 0.0f && out[(size_t) n] >= 0.0f)
        {
            ++crossings;
            if (first < 0) first = n;
            last = n;
        }
    const double measured = (double) (crossings - 1) / (double) (last - first);
    CHECK (std::abs (measured / frequency - (1.0 - slope)) < 0.005);

    // No discontinuity at the block boundaries: the largest step is as large as in the (smooth) sine itself.
    double largestStep = 0.0;
    for (int n = 1001; n < blockSize * blocks; ++n)
        largestStep = std::max (largestStep, (double) std::abs (out[(size_t) n] - out[(size_t) n - 1]));
    CHECK (largestStep < 2.0 * std::numbers::pi * frequency * 1.01);

    // Moving towards the listener raises the pitch.
    DelayLine towards;
    towards.prepare (2000, blockSize);
    std::vector<float> out2 ((size_t) (blockSize * blocks), 0.0f);
    delay = 800.0;
    for (int block = 0; block < blocks; ++block)
    {
        for (int i = 0; i < blockSize; ++i)
            in[(size_t) i] = (float) std::sin (2.0 * std::numbers::pi * frequency * (block * blockSize + i));
        towards.write (in.data(), blockSize);
        towards.addTap (out2.data() + block * blockSize, blockSize, delay, delay - slope * blockSize, 1.0f, 1.0f);
        delay -= slope * blockSize;
    }
    crossings = 0;
    first = last = -1;
    for (int n = 1001; n < blockSize * blocks; ++n)
        if (out2[(size_t) n - 1] < 0.0f && out2[(size_t) n] >= 0.0f)
        {
            ++crossings;
            if (first < 0) first = n;
            last = n;
        }
    CHECK (std::abs ((double) (crossings - 1) / (double) (last - first) / frequency - (1.0 + slope)) < 0.005);
}

/** The image table: the counts of the orders, the hits of every image, and the filter classes. */
static void testImageTable()
{
    CHECK (images (0).size() == 1 && images (1).size() == 7 && images (2).size() == 25);
    CHECK (images (-3).size() == 1 && images (9).size() == 25);
    CHECK (filterClassCount (0) == 1 && filterClassCount (1) == 7 && filterClassCount (2) == 22);

    int perOrder[3] {};
    for (const auto& image : images (2))
    {
        ++perOrder[image.order];
        int total = 0;
        for (int h : image.hits)
            total += h;
        CHECK (total == image.order);
        CHECK (image.filterClass >= 0 && image.filterClass < 22 && classHits (image.filterClass) == image.hits);
    }
    CHECK (perOrder[0] == 1 && perOrder[1] == 6 && perOrder[2] == 18);

    // The direct sound is first, then the first order, then the second; class 0 is the direct sound alone.
    CHECK (images (2)[0].order == 0 && images (2)[0].filterClass == 0 && images (2)[6].order == 1 && images (2)[7].order == 2);
    std::set<int> classes;
    for (const auto& image : images (2))
        classes.insert (image.filterClass);
    CHECK (classes.size() == 22);
}

/** The reflection of a point in one of the six walls of a room (for the brute-force enumeration). */
static Vec3 mirrorIn (Vec3 p, const Room& room, int wall)
{
    switch (wall)
    {
        case Left:    p.x = -room.width - p.x; break;           // the plane x = -width / 2
        case Right:   p.x =  room.width - p.x; break;
        case Front:   p.y =  room.depth - p.y; break;
        case Rear:    p.y = -room.depth - p.y; break;
        case Floor:   p.z = -p.z; break;
        case Ceiling: p.z =  2.0 * room.height - p.z; break;
        default: break;
    }
    return p;
}

/** The images of a random source, found by reflecting it in the walls one after the other (no wall twice in a row), against the table. */
static void testImagesAgainstBruteForce()
{
    std::mt19937 rng (7);
    std::uniform_real_distribution<double> u (0.1, 0.9);

    for (int trial = 0; trial < 20; ++trial)
    {
        const Room room { 4.0 + 20.0 * u (rng), 3.0 + 20.0 * u (rng), 2.0 + 10.0 * u (rng) };
        const Vec3 source { (u (rng) - 0.5) * room.width, (u (rng) - 0.5) * room.depth, u (rng) * room.height };
        const Vec3 mic { (u (rng) - 0.5) * room.width, (u (rng) - 0.5) * room.depth, u (rng) * room.height };

        struct Found { Vec3 image, micImage; int w1, w2; };
        std::vector<Found> found;
        found.push_back ({ source, mic, -1, -1 });
        for (int a = 0; a < WallCount; ++a)
        {
            found.push_back ({ mirrorIn (source, room, a), mirrorIn (mic, room, a), a, -1 });
            for (int b = 0; b < WallCount; ++b)
                if (b != a)
                    // Source reflected in a, then in b; the microphone in the reverse order (b, then a).
                    found.push_back ({ mirrorIn (mirrorIn (source, room, a), room, b), mirrorIn (mirrorIn (mic, room, b), room, a), a, b });
        }

        // Several sequences give the same image (a then b on different axes is the same as b then a): keep the different ones.
        std::vector<Found> distinct;
        for (const auto& f : found)
            if (std::none_of (distinct.begin(), distinct.end(), [&] (const Found& d) { return std::abs (d.image.x - f.image.x) + std::abs (d.image.y - f.image.y) + std::abs (d.image.z - f.image.z) < 1e-9; }))
                distinct.push_back (f);

        CHECK (distinct.size() == 25);                                     // 1 + 6 + 18

        for (const auto& image : images (2))
        {
            const auto p = imagePosition (source, room, image);
            const auto match = std::find_if (distinct.begin(), distinct.end(), [&] (const Found& d) { return std::abs (d.image.x - p.x) + std::abs (d.image.y - p.y) + std::abs (d.image.z - p.z) < 1e-9; });
            CHECK (match != distinct.end());
            if (match == distinct.end())
                continue;

            // The unfolded path from the image has the length of the path from the source to the image of the microphone.
            const auto m2 = micImagePosition (mic, room, image);
            const double a = std::sqrt ((p.x - mic.x) * (p.x - mic.x) + (p.y - mic.y) * (p.y - mic.y) + (p.z - mic.z) * (p.z - mic.z));
            const double b = std::sqrt ((m2.x - source.x) * (m2.x - source.x) + (m2.y - source.y) * (m2.y - source.y) + (m2.z - source.z) * (m2.z - source.z));
            CHECK (isNear (a, b, 1e-9));

            // And the table's microphone image is the one of the reverse wall sequence (for images made by a sequence of walls with a distinct order).
            CHECK (isNear (m2.x, match->micImage.x, 1e-9) && isNear (m2.y, match->micImage.y, 1e-9) && isNear (m2.z, match->micImage.z, 1e-9));
        }
    }

    // The direct sound is the source itself; the first-order images are the mirror images in the six walls (a simple room).
    const Room room { 10.0, 8.0, 3.0 };
    const Vec3 s { 1.0, 2.0, 1.5 };
    const auto left = imagePosition (s, room, images (1)[1 + 0]);
    int firstOrder = 0;
    for (size_t i = 1; i < 7; ++i)
    {
        const auto p = imagePosition (s, room, images (1)[i]);
        for (int w = 0; w < WallCount; ++w)
            if (images (1)[i].hits[(size_t) w] == 1)
            {
                const auto expected = mirrorIn (s, room, w);
                CHECK (isNear (p.x, expected.x) && isNear (p.y, expected.y) && isNear (p.z, expected.z));
                ++firstOrder;
            }
    }
    CHECK (firstOrder == 6 && isNear (left.z, imagePosition (s, room, images (1)[1]).z));
    CHECK (imagePosition (s, room, images (0)[0]).x == 1.0);

    // Inside the room nothing is faded; outside the walls it fades within half a metre, and the fade is continuous.
    CHECK (isNear (insideFactor ({ 0.0, 0.0, 1.0 }, room), 1.0) && isNear (insideFactor ({ 5.0, 0.0, 1.0 }, room), 1.0));
    CHECK (isNear (insideFactor ({ 5.25, 0.0, 1.0 }, room), 0.5) && isNear (insideFactor ({ 6.0, 0.0, 1.0 }, room), 0.0) && isNear (insideFactor ({ 0.0, 0.0, 3.25 }, room), 0.5));
}

/** The absorption: the reflection factor, the band gains of wall sequences, the presets. */
static void testAbsorption()
{
    CHECK (isNear (reflectionFactor (0.0), 1.0) && isNear (reflectionFactor (75.0), 0.5) && isNear (reflectionFactor (99.9), std::sqrt (0.001)));
    CHECK (isNear (reflectionFactor (-5.0), 1.0) && isNear (reflectionFactor (500.0), std::sqrt (0.001)));

    Walls walls;
    walls.surface[Floor] = { 75.0, 0.1, 99.9 };
    walls.surface[Left] = { 0.1, 36.0, 51.0 };
    const auto floorOnly = bandGains ({ 0, 0, 0, 0, 1, 0 }, walls);
    CHECK (isNear (floorOnly.low, 0.5, 1e-3) && isNear (floorOnly.mid, std::sqrt (0.999)) && isNear (floorOnly.high, std::sqrt (0.001)));
    const auto both = bandGains ({ 1, 0, 0, 0, 1, 0 }, walls);                      // a left wall then the floor: the product
    CHECK (isNear (both.mid, std::sqrt (0.999) * std::sqrt (0.64)) && isNear (both.high, std::sqrt (0.001) * std::sqrt (0.49)));
    const auto twice = bandGains ({ 0, 0, 0, 0, 2, 0 }, walls);                      // the floor twice: squared
    CHECK (isNear (twice.low, 0.25, 1e-3));
    const auto direct = bandGains ({}, walls);
    CHECK (isNear (direct.low, 1.0) && isNear (direct.mid, 1.0) && isNear (direct.high, 1.0));

    CHECK (surfacePresets().size() >= 12);
    const auto* carpet = findSurfacePreset ("carpet");
    CHECK (carpet != nullptr && isNear (carpet->surface.low, 6.0) && isNear (carpet->surface.mid, 37.0) && isNear (carpet->surface.high, 63.0));
    CHECK (findSurfacePreset ("no such surface") == nullptr && findSurfacePreset ("zero") != nullptr && findSurfacePreset ("glass") != nullptr);
    for (const auto& preset : surfacePresets())
        CHECK (preset.surface.low >= 0.1 && preset.surface.high <= 99.9 && preset.surface.mid >= 0.1 && preset.surface.mid <= 99.9);
}

/** The low-pass biquad against its analytic response, and the band splitter: the bands add up to the input, the path filter follows the band gains. */
static void testFilters()
{
    const double fs = 48000.0;
    const auto db = [] (std::complex<double> z) { return 20.0 * std::log10 (std::abs (z)); };

    // The Butterworth low-pass: -3 dB at the cut-off, 12 dB per octave above (the analytic, bilinear, pre-warped response).
    const auto air = Biquad::lowPass (fs, 5000.0);
    CHECK (std::abs (db (air.response (fs, 5000.0)) + 3.01) < 0.05 && std::abs (db (air.response (fs, 50.0))) < 0.01);
    for (const double f : { 500.0, 2000.0, 5000.0, 9000.0, 15000.0 })
    {
        const double ratio = std::tan (std::numbers::pi * f / fs) / std::tan (std::numbers::pi * 5000.0 / fs);
        CHECK (std::abs (db (air.response (fs, f)) + 10.0 * std::log10 (1.0 + ratio * ratio * ratio * ratio)) < 0.01);
    }
    CHECK (Biquad().isIdentity());

    // The bands add up to the input exactly (also in the time domain), whatever the crossovers.
    BandSplitter splitter;
    splitter.design (fs, 500.0, 3500.0, 0.0);
    std::vector<float> in (2048), low (2048), mid (2048), high (2048);
    std::mt19937 rng (3);
    std::uniform_real_distribution<float> u (-1.0f, 1.0f);
    for (auto& x : in)
        x = u (rng);
    splitter.process (in.data(), low.data(), mid.data(), high.data(), 2048);
    double worst = 0.0;
    for (size_t i = 0; i < in.size(); ++i)
        worst = std::max (worst, (double) std::abs (low[i] + mid[i] + high[i] - in[i]));
    CHECK (worst < 1e-5);

    // The response of path filters from band gains: the low band, the middle band (at the geometric mean of the crossovers) and the high band follow the gains
    // (to within 1.5 dB, the bands overlap a little).
    Walls walls;
    walls.surface[Floor] = { 60.0, 20.0, 80.0 };
    walls.surface[Left] = { 10.0, 50.0, 90.0 };
    for (const auto& hits : { std::array<int, WallCount> { 0, 0, 0, 0, 1, 0 }, std::array<int, WallCount> { 1, 0, 0, 0, 1, 0 }, std::array<int, WallCount> { 0, 0, 0, 0, 2, 0 } })
    {
        const auto gains = bandGains (hits, walls);
        CHECK (std::abs (db (splitter.response (fs, gains, 20.0)) - 20.0 * std::log10 (gains.low)) < 0.5);
        CHECK (std::abs (db (splitter.response (fs, gains, std::sqrt (500.0 * 3500.0))) - 20.0 * std::log10 (gains.mid)) < 3.0);
        CHECK (std::abs (db (splitter.response (fs, gains, 20000.0)) - 20.0 * std::log10 (gains.high)) < 0.5);
    }

    // Gains of 1 leave the sound alone (at every frequency); the air low-pass sits on top: -3 dB at its cut-off.
    for (const double f : { 50.0, 500.0, 1500.0, 3500.0, 12000.0 })
        CHECK (std::abs (splitter.response (fs, BandGains {}, f) - 1.0) < 1e-9);
    BandSplitter withAir;
    withAir.design (fs, 500.0, 3500.0, 6000.0);
    CHECK (std::abs (db (withAir.response (fs, BandGains {}, 6000.0)) + 3.01) < 0.05 && std::abs (db (withAir.response (fs, BandGains {}, 100.0))) < 0.01);

    // In the time domain: a sine comes out of the mix with the size of the response (after the start-up).
    const auto gains = bandGains ({ 0, 1, 0, 0, 1, 0 }, walls);
    for (const double frequency : { 100.0, 1500.0, 9000.0 })
    {
        std::vector<float> tone (4096), l (4096), m (4096), h (4096), mixed (4096);
        for (int i = 0; i < 4096; ++i)
            tone[(size_t) i] = (float) std::sin (2.0 * std::numbers::pi * frequency / fs * i);
        withAir.reset();
        withAir.process (tone.data(), l.data(), m.data(), h.data(), 4096);
        double peak = 0.0;
        for (int i = 2048; i < 4096; ++i)
        {
            const float y = (float) (gains.low * l[(size_t) i] + gains.mid * m[(size_t) i] + gains.high * h[(size_t) i]);
            peak = std::max (peak, (double) std::abs (y));
        }
        CHECK (std::abs (peak - std::abs (withAir.response (fs, gains, frequency))) < 0.01 * std::max (peak, 0.05) + 1e-3);
    }

    // A crossover pair in the wrong order is corrected (the upper one is at least 1.2 times the lower one), and new designs keep working.
    splitter.design (fs, 3000.0, 1000.0, 0.0);
    splitter.process (in.data(), low.data(), mid.data(), high.data(), 2048);
    worst = 0.0;
    for (size_t i = 0; i < in.size(); ++i)
        worst = std::max (worst, (double) std::abs (low[i] + mid[i] + high[i] - in[i]));
    CHECK (worst < 1e-5);
}

/** The mix of three signals written to a delay line: gains that ramp, and the same as writing the mixed signal. */
static void testDelayLineMix()
{
    DelayLine a, b;
    a.prepare (400, 64);
    b.prepare (400, 64);
    std::vector<float> low (64), mid (64), high (64), mixed (64), outA (64, 0.0f), outB (64, 0.0f);
    for (int i = 0; i < 64; ++i)
    {
        low[(size_t) i] = std::sin (0.1f * (float) i);
        mid[(size_t) i] = std::cos (0.3f * (float) i);
        high[(size_t) i] = (float) (i % 3) - 1.0f;
        mixed[(size_t) i] = 0.5f * low[(size_t) i] - 2.0f * mid[(size_t) i] + 0.25f * high[(size_t) i];
    }

    const float gains[3] { 0.5f, -2.0f, 0.25f };
    a.writeMix (low.data(), mid.data(), high.data(), gains, gains, 64);
    b.write (mixed.data(), 64);
    a.addTap (outA.data(), 64, 10.0, 10.0, 1.0f, 1.0f);
    b.addTap (outB.data(), 64, 10.0, 10.0, 1.0f, 1.0f);
    for (int i = 0; i < 64; ++i)
        CHECK (std::abs (outA[(size_t) i] - outB[(size_t) i]) < 1e-5f);

    // Ramped gains: from (1, 0, 0) to (0, 0, 1) across the block.
    DelayLine c;
    c.prepare (400, 64);
    const float from[3] { 1.0f, 0.0f, 0.0f }, to[3] { 0.0f, 0.0f, 1.0f };
    c.writeMix (low.data(), mid.data(), high.data(), from, to, 64);
    std::vector<float> outC (64, 0.0f);
    c.addTap (outC.data(), 64, 2.0, 2.0, 1.0f, 1.0f);
    for (int i = 2; i < 64; ++i)
    {
        const float t = (float) (i - 2) / 64.0f;
        CHECK (std::abs (outC[(size_t) i] - ((1.0f - t) * low[(size_t) (i - 2)] + t * high[(size_t) (i - 2)])) < 1e-5f);
    }
}

/** The path of an image: the direct sound is the direct path, the floor reflection has the length, direction and gain of the unfolded path. */
static void testImagePaths()
{
    PathSettings s;
    s.sourceHeight = 1.5;
    s.micHeight = 1.5;
    RoomSettings r;
    r.room = { 10.0, 8.0, 3.0 };
    r.order = 2;
    Microphone mic;
    mic.position = { 0.0, 0.0 };
    mic.yaw = 0.0;
    mic.pattern = 0.5;                       // a cardioid facing the front
    Source source;
    source.position = { 0.0, 4.0 };
    const double c = speedOfSound (20.0);

    // The direct sound: the same as the direct path.
    CHECK (computeImagePath (source, mic, images (2)[0], s, r).gain == computeDirectPath (source, mic, s).gain);

    // The floor: the image is 1.5 m below the floor; the path is 5 m long (4 forward, 3 down), the cardioid hears it at cos = 4/5.
    const Image* floorImage = nullptr;
    const Image* leftImage = nullptr;
    for (const auto& image : images (2))
    {
        if (image.order == 1 && image.hits[Floor] == 1)
            floorImage = &image;
        if (image.order == 1 && image.hits[Left] == 1)
            leftImage = &image;
    }
    const auto floorPath = computeImagePath (source, mic, *floorImage, s, r);
    CHECK (isNear (floorPath.distance, 5.0) && isNear (floorPath.delaySeconds, 5.0 / c));
    CHECK (isNear (floorPath.gain, 0.2 * (0.5 + 0.5 * 0.8)));

    // The order gains, and an omni source: the same sound leaves in every direction. A source facing the front: its sound towards the floor image leaves at cos = 4/5.
    r.gainFirst = 0.5;
    CHECK (isNear (computeImagePath (source, mic, *floorImage, s, r).gain, 0.5 * 0.2 * 0.9));
    source.pattern = 0.5;
    source.yaw = std::numbers::pi;                                     // facing the microphone (at the back of the source)
    CHECK (isNear (computeImagePath (source, mic, *floorImage, s, r).gain, 0.5 * 0.2 * 0.9 * 0.9));
    source.pattern = 1.0;
    r.gainFirst = 1.0;

    // The left wall (x = -5): a source at x = -3 mirrored to x = -7; the path to a microphone at the origin is 8 m (7 and 4 horizontally: sqrt(65)).
    source.position = { -3.0, 4.0 };
    const auto leftPath = computeImagePath (source, mic, *leftImage, s, r);
    CHECK (isNear (leftPath.distance, std::sqrt (7.0 * 7.0 + 4.0 * 4.0)));
    CHECK (isNear (leftPath.gain, (1.0 / leftPath.distance) * (0.5 + 0.5 * 4.0 / leftPath.distance)));

    // A second-order image (left wall, then front wall): checked against the explicit mirror of the source.
    for (const auto& image : images (2))
        if (image.order == 2 && image.hits[Left] == 1 && image.hits[Front] == 1)
        {
            const double x = -10.0 - source.position.x, y = 8.0 - source.position.y;   // mirror in x = -5, then in y = 4
            const double d = std::sqrt (x * x + y * y);
            const auto path = computeImagePath (source, mic, image, s, r);
            CHECK (isNear (path.distance, d) && isNear (path.gain, std::min (1.0, (1.0 / d) * (0.5 + 0.5 * y / d))));
        }

    // The second-order gain applies to the second order only; the threshold removes weak paths.
    source.position = { 0.0, 4.0 };
    mic.pattern = 1.0;                                                 // omni, so that every direction is heard
    r.gainSecond = 0.1;
    for (const auto& image : images (2))
        if (image.order == 2 && image.hits[Front] == 1 && image.hits[Rear] == 1)
        {
            r.gainSecond = 1.0;
            const double full = computeImagePath (source, mic, image, s, r).gain;
            r.gainSecond = 0.1;
            CHECK (isNear (computeImagePath (source, mic, image, s, r).gain, 0.1 * full) && std::abs (full) > 0.0);
            r.threshold = std::abs (full) * 0.5;
            CHECK (computeImagePath (source, mic, image, s, r).gain == 0.0);
            r.threshold = 0.0;
            break;
        }

    // A source outside the room fades its reflections (half a metre outside a wall: nothing), the direct sound is not touched.
    source.position = { 5.25, 0.0 };
    CHECK (isNear (computeImagePath (source, mic, *floorImage, s, r).gain, 0.5 * computeImagePath (Source { { 4.9, 0.0 }, 0.0, 1.0 }, mic, *floorImage, s, r).gain * 1.0, 0.2));
    source.position = { 6.0, 0.0 };
    CHECK (computeImagePath (source, mic, *floorImage, s, r).gain == 0.0 && computeImagePath (source, mic, images (2)[0], s, r).gain > 0.0);

    // Not finite: nothing.
    source.position = { std::numeric_limits<double>::quiet_NaN(), 0.0 };
    CHECK (computeImagePath (source, mic, *floorImage, s, r).gain == 0.0);
}

/** Runs all ViMiCCore tests. @return 0 if every check passed, otherwise 1. */
int main()
{
    testDirectivity();
    testDistanceAndSpeed();
    testDirectPath();
    testSourceDirectivity();
    testDelayLineIntegerAndRamp();
    testDelayLineFractional();
    testDelayLineVariableAgainstReference();
    testDelayLineDoppler();
    testImageTable();
    testImagesAgainstBruteForce();
    testAbsorption();
    testFilters();
    testDelayLineMix();
    testImagePaths();

    if (failures == 0)
        std::printf ("All tests passed\n");
    return failures == 0 ? 0 : 1;
}
