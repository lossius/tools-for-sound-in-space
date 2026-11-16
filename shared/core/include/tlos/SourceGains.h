// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file SourceGains.h
    @brief The speaker gains of one source (JUCE-free), the result every panning method produces. */

#pragma once

#include <array>

namespace tlos
{
/** Capacity of SourceGains: at most one entry per real speaker. */
inline constexpr int kMaxGainEntries = 64;

/** Gains for one source: which speakers receive signal and how much. Constant power
    (sum of squared gains = 1) is the convention, but the type does not enforce it. */
struct SourceGains
{
    /** Creates an empty result (no speakers, all gains zero). */
    SourceGains()
    {
        speaker.fill (-1);
        gain.fill (0.0f);
    }

    int count = 0;                                  ///< Number of valid entries in #speaker and #gain.
    std::array<int, kMaxGainEntries> speaker;       ///< Real speaker indices (-1 for unused entries).
    std::array<float, kMaxGainEntries> gain;        ///< Linear gain for the speaker at the same index.
};
}
