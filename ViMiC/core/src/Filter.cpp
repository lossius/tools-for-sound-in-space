// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file Filter.cpp
    @brief Biquad designs after the well-known audio EQ cookbook formulas, and the filter of a wall sequence. */

#include "vimic/Filter.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace vimic
{
namespace
{
/** Limits a frequency to a range that is safe for the design at this sample rate. */
double safeFrequency (double sampleRate, double frequency) noexcept
{
    return std::clamp (std::isfinite (frequency) ? frequency : 1000.0, 10.0, 0.45 * sampleRate);
}
}

void Biquad::set (double b0In, double b1In, double b2In, double a0, double a1In, double a2In) noexcept
{
    b0 = b0In / a0;
    b1 = b1In / a0;
    b2 = b2In / a0;
    a1 = a1In / a0;
    a2 = a2In / a0;
    identity = false;
}

Biquad Biquad::lowPass (double sampleRate, double frequency) noexcept
{
    Biquad f;
    const double w = 2.0 * std::numbers::pi * safeFrequency (sampleRate, frequency) / sampleRate;
    const double c = std::cos (w), alpha = std::sin (w) / std::numbers::sqrt2;   // sin(w) / (2 Q) with Q = 1 / sqrt 2
    f.set ((1.0 - c) * 0.5, 1.0 - c, (1.0 - c) * 0.5, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
    return f;
}

std::complex<double> Biquad::response (double sampleRate, double frequency) const noexcept
{
    const double w = 2.0 * std::numbers::pi * frequency / sampleRate;
    const std::complex<double> z1inv = std::polar (1.0, -w), z2inv = std::polar (1.0, -2.0 * w);
    return (b0 + b1 * z1inv + b2 * z2inv) / (1.0 + a1 * z1inv + a2 * z2inv);
}

void Biquad::process (const float* input, float* output, int count) noexcept
{
    if (identity)
    {
        if (input != output)
            std::copy (input, input + count, output);
        return;
    }

    for (int i = 0; i < count; ++i)
    {
        const double x = (double) input[i];
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        output[i] = (float) y;
    }
}

void BandSplitter::design (double sampleRate, double lowMidHz, double midHighHz, double airCutoffHz) noexcept
{
    const double lower = safeFrequency (sampleRate, lowMidHz);
    const double upper = safeFrequency (sampleRate, std::max (midHighHz, 1.2 * lower));
    air.takeCoefficients (airCutoffHz >= 20.0 && airCutoffHz < 0.45 * sampleRate ? Biquad::lowPass (sampleRate, airCutoffHz) : Biquad());
    lowCrossover.takeCoefficients (Biquad::lowPass (sampleRate, lower));
    highCrossover.takeCoefficients (Biquad::lowPass (sampleRate, upper));
}

void BandSplitter::process (const float* input, float* low, float* mid, float* high, int count) noexcept
{
    air.process (input, high, count);                    // high is used as the memory for the air-filtered input
    lowCrossover.process (high, low, count);
    highCrossover.process (high, mid, count);            // the second low-pass, for now in mid
    for (int i = 0; i < count; ++i)
    {
        const float a = high[i], lowPassed = low[i], secondLowPassed = mid[i];
        mid[i] = secondLowPassed - lowPassed;
        high[i] = a - secondLowPassed;
    }
}

std::complex<double> BandSplitter::response (double sampleRate, const BandGains& gains, double frequency) const noexcept
{
    const auto l1 = lowCrossover.response (sampleRate, frequency), l2 = highCrossover.response (sampleRate, frequency);
    return air.response (sampleRate, frequency) * (gains.low * l1 + gains.mid * (l2 - l1) + gains.high * (1.0 - l2));
}

void BandSplitter::reset() noexcept
{
    air.reset();
    lowCrossover.reset();
    highCrossover.reset();
}
}
