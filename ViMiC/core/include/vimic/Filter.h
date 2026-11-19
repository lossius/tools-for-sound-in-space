// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file Filter.h
    @brief Biquad low-pass filters and the splitting of a source into the three absorption bands (JUCE-free). */

#pragma once

#include "vimic/Absorption.h"

#include <complex>

namespace vimic
{
/** A second-order section in direct form II transposed, `y = (b0 + b1 z^-1 + b2 z^-2) / (1 + a1 z^-1 + a2 z^-2) x`. */
class Biquad
{
public:
    /** The identity (passes the signal unchanged). */
    Biquad() = default;

    /** A 2nd-order Butterworth low-pass with its -3 dB point at `frequency`. */
    static Biquad lowPass (double sampleRate, double frequency) noexcept;

    /** @return The response of the filter at `frequency` (a complex number; its size is the gain). */
    std::complex<double> response (double sampleRate, double frequency) const noexcept;

    /** Filters a block. In place is allowed (`input` may equal `output`). */
    void process (const float* input, float* output, int count) noexcept;

    /** Clears the memory of the filter. */
    void reset() noexcept { z1 = z2 = 0.0; }

    /** Takes over the coefficients of another section and keeps this one's memory, so that a change of design does not click. */
    void takeCoefficients (const Biquad& other) noexcept
    {
        b0 = other.b0; b1 = other.b1; b2 = other.b2; a1 = other.a1; a2 = other.a2; identity = other.identity;
    }

    /** @return Whether the section does nothing (b = (1, 0, 0), a = (0, 0)). */
    bool isIdentity() const noexcept { return identity; }

private:
    /** Normalises the coefficients by a0. */
    void set (double b0In, double b1In, double b2In, double a0, double a1In, double a2In) noexcept;

    double b0 = 1.0;   ///< Numerator coefficient of z^0.
    double b1 = 0.0;   ///< Numerator coefficient of z^-1.
    double b2 = 0.0;   ///< Numerator coefficient of z^-2.
    double a1 = 0.0;   ///< Denominator coefficient of z^-1 (a0 is 1).
    double a2 = 0.0;   ///< Denominator coefficient of z^-2.
    double z1 = 0.0;   ///< The first memory.
    double z2 = 0.0;   ///< The second memory.
    bool identity = true;                  ///< Whether it does nothing.
};

/** Splits a source into the three absorption bands, once per source, so that the filter of every wall sequence is only three multiplications: with the band signals
    `low`, `mid` and `high`, a path whose walls have the band gains (gl, gm, gh) is `gl low + gm mid + gh high`. The bands add up to the input exactly: after the (optional)
    low-pass of the air, `low` is the 2nd-order Butterworth low-pass at the lower crossover, `high` what is above the upper crossover (the input minus its low-pass) and
    `mid` the difference between the two low-passes. So with all gains 1 the output is the (air-filtered) input, with a gain of 1 in one band and 0 in the others it is that band. */
class BandSplitter
{
public:
    /** Designs the filters (a new design keeps their memory, so changing a setting while sound is going through does not click).
        @param sampleRate   The sample rate.
        @param lowMidHz     The lower crossover in Hz.
        @param midHighHz    The upper crossover in Hz (at least 1.2 times the lower one).
        @param airCutoffHz  The cut-off of the air absorption (below 20 Hz or above 0.45 of the sample rate: none). */
    void design (double sampleRate, double lowMidHz, double midHighHz, double airCutoffHz) noexcept;

    /** Splits a block. @param input  The samples. @param low  Receives the low band. @param mid  Receives the middle band. @param high  Receives the high band.
        @param count  How many samples. The outputs must not overlap the input. */
    void process (const float* input, float* low, float* mid, float* high, int count) noexcept;

    /** @return The response of a path with these band gains at `frequency`: `gl LP1 + gm (LP2 - LP1) + gh (1 - LP2)`, times the air low-pass. */
    std::complex<double> response (double sampleRate, const BandGains& gains, double frequency) const noexcept;

    /** Clears the memory of the filters. */
    void reset() noexcept;

private:
    Biquad air;           ///< The low-pass of the air (or nothing).
    Biquad lowCrossover;  ///< The low-pass at the lower crossover.
    Biquad highCrossover; ///< The low-pass at the upper crossover.
};
}
