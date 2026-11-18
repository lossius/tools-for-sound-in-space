// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file CoreTests.cpp
    @brief Minimal dependency-free test runner for ViMiCCore (no JUCE): the microphone pattern, the distance laws, the direct path and the
    interpolated delay line (accuracy, wrap-around, ramps, Doppler). */

#include "vimic/DelayLine.h"
#include "vimic/Directivity.h"
#include "vimic/Path.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <numbers>
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

/** Runs all ViMiCCore tests. @return 0 if every check passed, otherwise 1. */
int main()
{
    testDirectivity();
    testDistanceAndSpeed();
    testDirectPath();
    testDelayLineIntegerAndRamp();
    testDelayLineFractional();
    testDelayLineDoppler();

    if (failures == 0)
        std::printf ("All tests passed\n");
    return failures == 0 ? 0 : 1;
}
