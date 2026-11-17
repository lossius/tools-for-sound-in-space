// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file TriPanField.cpp
    @brief Drawing of the triangulation and the links of TriPan's pad. */

#include "TriPanField.h"
#include "tripan/Gains.h"

#include <algorithm>

TriPanField::TriPanField (TriPanAudioProcessor& p)
    : FieldComponent (p)
{
    refresh(); // the base class could not call updateOverlay() while it was being constructed
}

void TriPanField::updateOverlay (const tlos::IncludedSpeakers& speakers, const std::vector<tlos::VirtualSpeaker>& virtualSpeakers)
{
    const auto samePosition = [] (const tlos::Vec2& p, const tlos::Vec2& q) { return juce::exactlyEqual (p.x, q.x) && juce::exactlyEqual (p.y, q.y); };
    const auto sameVirtual = [&] (const tlos::VirtualSpeaker& p, const tlos::VirtualSpeaker& q)
    {
        return samePosition (p.position, q.position) && juce::exactlyEqual (p.strength, q.strength);
    };

    const bool changed = triangulation == nullptr
                      || speakers.channelOf != triangulatedSpeakers.channelOf
                      || speakers.positions.size() != triangulatedSpeakers.positions.size()
                      || ! std::equal (speakers.positions.begin(), speakers.positions.end(), triangulatedSpeakers.positions.begin(), samePosition)
                      || virtualSpeakers.size() != triangulatedVirtual.size()
                      || ! std::equal (virtualSpeakers.begin(), virtualSpeakers.end(), triangulatedVirtual.begin(), sameVirtual);

    if (changed)
    {
        triangulation = std::make_unique<tripan::SpeakerLayout> (speakers.positions, virtualSpeakers);
        triangulatedSpeakers = speakers;
        triangulatedVirtual = virtualSpeakers;
    }
}

void TriPanField::paintOverlay (juce::Graphics& g)
{
    if (triangulation == nullptr)
        return;

    g.setColour (juce::Colour (0xff7aa2c8).withAlpha (0.55f));
    const auto& full = triangulation->full(); // the triangulation with all virtual speakers
    const auto pts = full.speakers();
    for (const auto& t : full.triangles())
    {
        juce::Path path;
        const auto a = toView (pts[(size_t) t.a].x, pts[(size_t) t.a].y);
        const auto b = toView (pts[(size_t) t.b].x, pts[(size_t) t.b].y);
        const auto c = toView (pts[(size_t) t.c].x, pts[(size_t) t.c].y);
        path.startNewSubPath (a);
        path.lineTo (b);
        path.lineTo (c);
        path.closeSubPath();
        g.strokePath (path, juce::PathStrokeType (1.0f));
    }
}

tlos::SourceGains TriPanField::getLinkGains (int, tlos::Vec2 position, double spread) const
{
    if (triangulation == nullptr)
        return {};

    auto gains = tripan::computeGains (*triangulation, position, spread, processor.getOutsideRolloff());

    // The triangulation numbers the speakers that take part; the pad wants output channels.
    for (int i = 0; i < gains.count; ++i)
        gains.speaker[(size_t) i] = triangulatedSpeakers.channelOf[(size_t) gains.speaker[(size_t) i]];

    return gains;
}
