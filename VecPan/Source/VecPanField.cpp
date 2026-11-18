// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file VecPanField.cpp
    @brief The speaker pairs and links of VecPan's pad. */

#include "VecPanField.h"

VecPanField::VecPanField (VecPanAudioProcessor& p)
    : FieldComponent (p), vbapProcessor (p)
{
    refresh(); // the base class could not call updateOverlay() while it was being constructed
}

void VecPanField::updateOverlay (const tlos::IncludedSpeakers& speakers, const std::vector<tlos::VirtualSpeaker>&)
{
    includedSpeakers = speakers;
    layout = vecpan::Layout (speakers.positions);
}

void VecPanField::paintBackdrop (juce::Graphics& g)
{
    const double radius = layout.referenceRadius();
    if (! (radius > 0.0) || ! layout.hull().hasArea())
        return;

    // Dashed, in the colour of the speaker pairs: inside it the level does not depend on the distance, beyond it the outside rolloff applies.
    juce::Path circle;
    const auto centre = toView (0.0, 0.0);
    const auto edge = toView (radius, 0.0);
    const float r = std::abs (edge.x - centre.x);
    circle.addEllipse (centre.x - r, centre.y - r, 2.0f * r, 2.0f * r);

    juce::Path dashed;
    const float dashes[] { 5.0f, 4.0f };
    juce::PathStrokeType (1.2f).createDashedStroke (dashed, circle, dashes, 2);
    g.setColour (juce::Colour (0xff7aa2c8).withAlpha (0.65f));
    g.fillPath (dashed);
}

void VecPanField::paintOverlay (juce::Graphics& g)
{
    const auto& entries = layout.entries();
    const auto& pairs = layout.pairs();

    // Thin lines in the colour of TriPan's triangulation. A single speaker has no pair; a wide gap (180 degrees or more)
    // has no vector base, so it gets no line.
    g.setColour (juce::Colour (0xff7aa2c8).withAlpha (0.55f));
    if (entries.size() < 2)
        return;

    for (size_t i = 0; i < entries.size(); ++i)
    {
        if (pairs[i].wide)
            continue;

        const auto& a = includedSpeakers.positions[(size_t) entries[i].speaker];
        const auto& b = includedSpeakers.positions[(size_t) entries[(i + 1) % entries.size()].speaker];
        const auto p = toView (a.x, a.y), q = toView (b.x, b.y);
        g.drawLine (p.x, p.y, q.x, q.y, 1.0f);
    }
}

tlos::SourceGains VecPanField::getLinkGains (int, tlos::Vec2 position, double spread) const
{
    const auto gains = vecpan::computeGains (layout, position, spread, vbapProcessor.getSettings());

    tlos::SourceGains shown;
    for (int i = 0; i < gains.count; ++i)
    {
        shown.speaker[(size_t) shown.count] = includedSpeakers.channelOf[(size_t) gains.speaker[(size_t) i]]; // output channel
        shown.gain[(size_t) shown.count] = gains.gain[(size_t) i];
        ++shown.count;
    }

    return shown;
}
