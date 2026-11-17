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

void DistPanField::paintOverlay (juce::Graphics& g)
{
    const auto corners = speakerHull.getCorners();
    if (corners.size() < 2)
        return;

    juce::Path outline;
    for (size_t i = 0; i < corners.size(); ++i)
    {
        const auto p = toView (corners[i].x, corners[i].y);
        if (i == 0)
            outline.startNewSubPath (p);
        else
            outline.lineTo (p);
    }

    if (speakerHull.hasArea())
        outline.closeSubPath();

    // Thin lines in the colour of TriPan's triangulation: the edge of the layout, where the level starts to fall off
    // for a source outside it. No fill.
    g.setColour (juce::Colour (0xff7aa2c8).withAlpha (0.55f));
    g.strokePath (outline, juce::PathStrokeType (1.0f));
}

tlos::SourceGains DistPanField::getLinkGains (int, tlos::Vec2 position, double spread) const
{
    auto gains = distpan::computeGains (includedSpeakers.positions, speakerHull, position, spread, dbapProcessor.getSettings(),
                                       dbapProcessor.getWeights (includedSpeakers.channelOf));

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
