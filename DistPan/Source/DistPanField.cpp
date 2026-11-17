// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file DistPanField.cpp
    @brief The links of DistPan's pad. */

#include "DistPanField.h"
#include "distpan/Dbap.h"

#include <algorithm>

DistPanField::DistPanField (DistPanAudioProcessor& p)
    : FieldComponent (p), dbapProcessor (p)
{
    refresh(); // the base class could not call updateOverlay() while it was being constructed
}

void DistPanField::updateOverlay (const tlos::IncludedSpeakers& speakers, const std::vector<tlos::VirtualSpeaker>&)
{
    includedSpeakers = speakers;
    speakerHull = tlos::Hull (speakers.positions);
}

tlos::SourceGains DistPanField::getLinkGains (int, tlos::Vec2 position, double spread) const
{
    auto gains = distpan::computeGains (includedSpeakers.positions, speakerHull, position, spread, dbapProcessor.getSettings());

    // Leave out what is more than 20 dB below the strongest share, so that the pad stays readable.
    float strongest = 0.0f;
    for (int i = 0; i < gains.count; ++i)
        strongest = std::max (strongest, gains.gain[(size_t) i]);

    tlos::SourceGains shown;
    for (int i = 0; i < gains.count; ++i)
        if (gains.gain[(size_t) i] >= 0.1f * strongest)
        {
            shown.speaker[(size_t) shown.count] = includedSpeakers.channelOf[(size_t) gains.speaker[(size_t) i]]; // output channel
            shown.gain[(size_t) shown.count] = gains.gain[(size_t) i];
            ++shown.count;
        }

    return shown;
}
