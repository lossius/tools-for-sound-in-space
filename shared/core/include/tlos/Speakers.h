// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file Speakers.h
    @brief The speakers that take part in the panning (JUCE-free). */

#pragma once

#include "tlos/Vec2.h"

#include <vector>

namespace tlos
{
/** The speakers in use that are not excluded from the panning, in channel order.

    Panning methods work on these as if the others did not exist. `positions[i]` belongs to output channel
    `channelOf[i]`, so a gain computed for entry i is sent to that channel. */
struct IncludedSpeakers
{
    std::vector<Vec2> positions;  ///< Positions in metres.
    std::vector<int> channelOf;   ///< Zero-based output channel of each entry (ascending).
};
}
