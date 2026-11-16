// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file Dbap.h
    @brief Distance-based amplitude panning (DBAP) in two dimensions (JUCE-free). */

#pragma once

#include "tlos/SourceGains.h"
#include "tlos/Vec2.h"

#include <span>

namespace distpan
{
/** The two settings of the panning law. */
struct DbapSettings
{
    /** Level drop in dB for each doubling of the distance between a source and a speaker. 6 dB is the
        free-field value (the gain is proportional to 1/distance); larger values make the sound more local. */
    double rolloffDb = 6.0;

    /** Spatial blur in metres: the distance used is sqrt(d^2 + blur^2), so that a source right on a
        speaker does not use that speaker alone and the gains change smoothly when a source passes a speaker. */
    double blur = 0.2;
};

/** The exponent a of the gain law g_i ~ 1 / d_i^a that corresponds to a rolloff in dB per doubling of the distance. */
double rolloffExponent (double rolloffDb) noexcept;

/** Computes the speaker gains of a source with DBAP.

    Every speaker takes part: with d_i the distance from the source to speaker i (including the blur),
    the gain is g_i = k / d_i^a, where a follows from the rolloff (see rolloffExponent()) and
    k = 1 / sqrt(sum_j 1 / d_j^(2a)), so that the sum of squared gains is 1 (constant power) wherever the
    source is. The spread of a source adds to the blur: d_i^2 = distance^2 + blur^2 + spread^2, so a larger
    spread distributes the sound more evenly. Speakers with non-finite positions get no signal.

    Real-time safe: no allocation, no locks. Cost is linear in the number of speakers.

    @param speakers  The speakers in use, in metres. Entry i of the result belongs to speakers[i]; at most
                     tlos::kMaxGainEntries speakers are used.
    @param source    The source position in metres.
    @param spread    The source's spread radius in metres (0 = a point source).
    @param settings  Rolloff and blur.
    @return          One entry per speaker (count == number of speakers used), or count == 0 if the source
                     position is not finite or there is no valid speaker. */
tlos::SourceGains computeGains (std::span<const tlos::Vec2> speakers, tlos::Vec2 source, double spread,
                                const DbapSettings& settings) noexcept;
}
