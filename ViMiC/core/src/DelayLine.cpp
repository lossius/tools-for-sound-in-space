// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file DelayLine.cpp
    @brief The delay line and its interpolated taps. */

#include "vimic/DelayLine.h"

#include <algorithm>
#include <cmath>

namespace vimic
{
namespace
{
/** The four weights of the 4-point Hermite (Catmull-Rom) interpolation for the fractional position mu (0..1) between the second and the third sample. */
inline void hermiteWeights (double mu, float& w0, float& w1, float& w2, float& w3) noexcept
{
    const double mu2 = mu * mu, mu3 = mu2 * mu;
    w0 = (float) (-0.5 * mu3 + mu2 - 0.5 * mu);
    w1 = (float) (1.5 * mu3 - 2.5 * mu2 + 1.0);
    w2 = (float) (-1.5 * mu3 + 2.0 * mu2 + 0.5 * mu);
    w3 = (float) (0.5 * mu3 - 0.5 * mu2);
}
}

void DelayLine::prepare (int maxDelay, int maxBlockSize)
{
    maxDelaySamples = std::max (maxDelay, 4);
    maxBlock = std::max (maxBlockSize, 1);

    std::uint32_t size = 16;
    while ((int64_t) size < (int64_t) maxDelaySamples + maxBlock + 8)
        size <<= 1;

    buffer.assign (size, 0.0f);
    mask = size - 1;
    written = 0;
    lastCount = 0;
}

void DelayLine::reset() noexcept
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    written = 0;
    lastCount = 0;
}

void DelayLine::write (const float* input, int count) noexcept
{
    if (buffer.empty())
        return;

    count = std::clamp (count, 0, maxBlock);
    for (int i = 0; i < count; ++i)
        buffer[(std::uint32_t) (written + i) & mask] = input[i];

    written += count;
    lastCount = count;
}

void DelayLine::writeMix (const float* low, const float* mid, const float* high, const float* gains, const float* gainsEnd, int count) noexcept
{
    if (buffer.empty())
        return;

    count = std::clamp (count, 0, maxBlock);
    const float stepLow = (gainsEnd[0] - gains[0]) / (float) std::max (count, 1);
    const float stepMid = (gainsEnd[1] - gains[1]) / (float) std::max (count, 1);
    const float stepHigh = (gainsEnd[2] - gains[2]) / (float) std::max (count, 1);

    for (int i = 0; i < count; ++i)
        buffer[(std::uint32_t) (written + i) & mask] = (gains[0] + stepLow * (float) i) * low[i] + (gains[1] + stepMid * (float) i) * mid[i]
                                                       + (gains[2] + stepHigh * (float) i) * high[i];

    written += count;
    lastCount = count;
}

void DelayLine::addTap (float* output, int count, double delayStart, double delayEnd, float gainStart, float gainEnd) const noexcept
{
    if (buffer.empty() || count <= 0 || count != lastCount)
        return;

    const double lo = kMinDelay, hi = (double) maxDelaySamples;
    const double d0 = std::clamp (std::isfinite (delayStart) ? delayStart : lo, lo, hi);
    const double d1 = std::clamp (std::isfinite (delayEnd) ? delayEnd : lo, lo, hi);
    const std::int64_t blockStart = written - count;          // time of the first sample of the block
    const float gainStep = (gainEnd - gainStart) / (float) count;
    const float* data = buffer.data();

    if (d0 == d1)
    {
        // A constant delay: the same four weights for every sample, and (unless the read wraps round the buffer) contiguous input, which the
        // compiler can vectorise.
        const double position = (double) blockStart - d0;
        const double base = std::floor (position);
        const std::int64_t i0 = (std::int64_t) base;
        float w0, w1, w2, w3;
        hermiteWeights (position - base, w0, w1, w2, w3);

        const std::uint32_t first = (std::uint32_t) (i0 - 1) & mask;
        if ((std::int64_t) first + count + 3 <= (std::int64_t) mask + 1)
        {
            const float* in = data + first;
            for (int j = 0; j < count; ++j)
                output[j] += (gainStart + gainStep * (float) j) * (w0 * in[j] + w1 * in[j + 1] + w2 * in[j + 2] + w3 * in[j + 3]);
        }
        else
        {
            for (int j = 0; j < count; ++j)
            {
                const std::uint32_t k = (std::uint32_t) (i0 - 1 + j) & mask;
                output[j] += (gainStart + gainStep * (float) j)
                           * (w0 * data[k] + w1 * data[(k + 1) & mask] + w2 * data[(k + 2) & mask] + w3 * data[(k + 3) & mask]);
            }
        }

        return;
    }

    // A changing delay: the read position moves by 1 - (d1 - d0) / count samples per sample (a Doppler shift). The position within the block is kept
    // relative to the integer position of its first sample, in float: a block is short, so the error is far below a thousandth of a sample.
    const double position0 = (double) blockStart - d0;
    const double base0 = std::floor (position0);
    const std::int64_t i00 = (std::int64_t) base0;
    const float fraction0 = (float) (position0 - base0);
    const float step = (float) (1.0 - (d1 - d0) / (double) count);

    for (int j = 0; j < count; ++j)
    {
        const float q = fraction0 + step * (float) j;
        const int whole = (int) std::floor (q);
        const float mu = q - (float) whole, mu2 = mu * mu, mu3 = mu2 * mu;
        const float w0 = -0.5f * mu3 + mu2 - 0.5f * mu;
        const float w1 = 1.5f * mu3 - 2.5f * mu2 + 1.0f;
        const float w2 = -1.5f * mu3 + 2.0f * mu2 + 0.5f * mu;
        const float w3 = 0.5f * mu3 - 0.5f * mu2;

        const std::uint32_t k = (std::uint32_t) (i00 - 1 + whole) & mask;
        output[j] += (gainStart + gainStep * (float) j)
                   * (w0 * data[k] + w1 * data[(k + 1) & mask] + w2 * data[(k + 2) & mask] + w3 * data[(k + 3) & mask]);
    }
}
}
