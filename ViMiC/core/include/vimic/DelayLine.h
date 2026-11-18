// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file DelayLine.h
    @brief A delay line with a 4-point interpolated, time-varying read, for the sound paths of one source (JUCE-free). */

#pragma once

#include <cstdint>
#include <vector>

namespace vimic
{
/** The sound of one source, kept so that any number of paths (one per microphone) can read it at their own delay.

    Each block is written with write() and then read with addTap() once per path. A tap adds the delayed signal, scaled by a gain, to an output
    buffer; the delay and the gain change linearly across the block (from the first value at the first sample to the second one a block later),
    so that a moving source gives a smooth change of delay (a Doppler shift) and no clicks. The signal is read with a 4-point (third-order Hermite,
    Catmull-Rom) interpolation. It needs two samples of the signal after the one it reads, so the smallest delay is 2 samples: the callers add that to
    every delay (a latency of 2 samples that the plugin reports).

    Only prepare() allocates; the others are real-time safe. */
class DelayLine
{
public:
    /** The smallest delay in samples that addTap() accepts (shorter delays are raised to this). */
    static constexpr double kMinDelay = 2.0;

    /** Allocates and clears the buffer. Not real-time safe.
        @param maxDelaySamples  The longest delay that will be asked for (longer ones are limited to it).
        @param maxBlockSize     The longest block that will be written. */
    void prepare (int maxDelaySamples, int maxBlockSize);

    /** Clears the stored signal (silence) and starts counting from zero again. */
    void reset() noexcept;

    /** @return The longest delay in samples that addTap() gives (0 before prepare()). */
    int maxDelay() const noexcept { return maxDelaySamples; }

    /** Appends a block of the signal.
        @param input  The samples.
        @param count  How many (at most the maxBlockSize of prepare(); more are cut). */
    void write (const float* input, int count) noexcept;

    /** Adds the last block that write() stored, delayed and scaled, to `output`.
        Sample j (0 .. count - 1) of the block is read `delay(j)` samples earlier, with `delay(j) = delayStart + (delayEnd - delayStart) j / count`,
        and scaled by `gainStart + (gainEnd - gainStart) j / count`. Delays are limited to kMinDelay .. maxDelay(). Does nothing if the
        block was not written with the same count.
        @param output      Where the result is added; `count` samples.
        @param count       The size of the last block.
        @param delayStart  The delay in samples at the start of the block.
        @param delayEnd    The delay in samples at the end of the block (the start of the next one).
        @param gainStart   The gain at the start.
        @param gainEnd     The gain at the end. */
    void addTap (float* output, int count, double delayStart, double delayEnd, float gainStart, float gainEnd) const noexcept;

private:
    std::vector<float> buffer;      ///< The stored signal, circular; the size is a power of two.
    std::uint32_t mask = 0;         ///< Size of the buffer minus one.
    std::int64_t written = 0;       ///< How many samples have been written since reset() (the time of the next sample).
    int lastCount = 0;              ///< Size of the last block written.
    int maxDelaySamples = 0;        ///< The longest delay.
    int maxBlock = 0;               ///< The longest block.
};
}
