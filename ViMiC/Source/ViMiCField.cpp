// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file ViMiCField.cpp
    @brief The lobes and links of ViMiC's pad. */

#include "ViMiCField.h"
#include "vimic/Directivity.h"

#include <algorithm>
#include <cmath>
#include <numbers>

ViMiCField::ViMiCField (ViMiCAudioProcessor& p)
    : FieldComponent (p), vimicProcessor (p)
{
    refresh(); // the base class could not call updateOverlay() while it was being constructed
}

void ViMiCField::updateOverlay (const tlos::IncludedSpeakers& speakers, const std::vector<tlos::VirtualSpeaker>&)
{
    includedSpeakers = speakers;
}

juce::Path ViMiCField::lobePath (double ratio, int power, bool restrictPolarity, float radius)
{
    juce::Path path;
    const int steps = 90;

    for (int i = 0; i <= steps; ++i)
    {
        const double angle = 2.0 * std::numbers::pi * i / steps;                       // from the axis, counter-clockwise
        const double size = std::abs (vimic::directivity (ratio, power, std::cos (angle), restrictPolarity));
        const float x = (float) (-std::sin (angle) * size) * radius;                    // the axis points up the screen, counter-clockwise is to the left
        const float y = (float) (-std::cos (angle) * size) * radius;
        if (i == 0)
            path.startNewSubPath (x, y);
        else
            path.lineTo (x, y);
    }

    path.closeSubPath();
    return path;
}

void ViMiCField::paintBackdrop (juce::Graphics& g)
{
    const auto settings = vimicProcessor.getPathSettings();
    const auto mics = vimicProcessor.getMicrophones (includedSpeakers);

    // The colour of the speaker lines of the other plugins: a faint tint with an outline.
    const auto blue = juce::Colour (0xff7aa2c8);
    for (const auto& mic : mics)
    {
        const auto centre = toView (mic.position.x, mic.position.y);
        auto lobe = lobePath (mic.pattern, settings.micPower, settings.restrictPolarity, 34.0f);
        lobe.applyTransform (juce::AffineTransform::rotation ((float) -mic.yaw).translated (centre.x, centre.y));
        g.setColour (blue.withAlpha (0.14f));
        g.fillPath (lobe);
        g.setColour (blue.withAlpha (0.65f));
        g.strokePath (lobe, juce::PathStrokeType (1.0f));
    }

    // Sources that radiate more in one direction: the lobe of the pattern and an arrow along the yaw, in the source's own colour. Omni sources have no direction.
    const auto groups = vimicProcessor.getSourceGroups();
    for (int s = 0; s < vimicProcessor.getActiveSources(); ++s)
    {
        const auto source = vimicProcessor.getSource (s);
        if (source.pattern > 0.999)
            continue;

        const auto centre = toView (source.position.x, source.position.y);
        const auto colour = sourceColour (s, groups);
        const auto turn = juce::AffineTransform::rotation ((float) -source.yaw).translated (centre.x, centre.y);

        auto lobe = lobePath (source.pattern, settings.sourcePower, true, 40.0f);
        lobe.applyTransform (turn);
        g.setColour (colour.withAlpha (0.14f));
        g.fillPath (lobe);
        g.setColour (colour.withAlpha (0.7f));
        g.strokePath (lobe, juce::PathStrokeType (1.0f));

        // The arrow starts outside the source's circle and points the way it radiates (up the screen before it is turned).
        juce::Path arrow;
        arrow.startNewSubPath (0.0f, -12.0f);
        arrow.lineTo (0.0f, -44.0f);
        arrow.startNewSubPath (-4.0f, -39.0f);
        arrow.lineTo (0.0f, -45.0f);
        arrow.lineTo (4.0f, -39.0f);
        arrow.applyTransform (turn);
        g.strokePath (arrow, juce::PathStrokeType (1.5f));
    }
}

tlos::SourceGains ViMiCField::getLinkGains (int sourceIndex, tlos::Vec2 position, double) const
{
    auto source = vimicProcessor.getSource (sourceIndex);
    source.position = position;            // the position the pad asks about (a source being dragged)
    const auto settings = vimicProcessor.getPathSettings();
    const auto mics = vimicProcessor.getMicrophones (includedSpeakers);

    std::vector<float> gains (mics.size(), 0.0f);
    float strongest = 0.0f;
    for (size_t i = 0; i < mics.size(); ++i)
    {
        gains[i] = (float) std::abs (vimic::computeDirectPath (source, mics[i], settings).gain);
        strongest = std::max (strongest, gains[i]);
    }

    // Leave out what is more than 20 dB below the strongest, so that the pad stays readable.
    tlos::SourceGains shown;
    for (size_t i = 0; i < mics.size() && shown.count < tlos::kMaxGainEntries; ++i)
        if (gains[i] > 0.0f && gains[i] >= 0.1f * strongest)
        {
            shown.speaker[(size_t) shown.count] = includedSpeakers.channelOf[i];   // output channel
            shown.gain[(size_t) shown.count] = gains[i];
            ++shown.count;
        }

    return shown;
}
