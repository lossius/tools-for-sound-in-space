// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file DistPanField.h
    @brief DistPan's XY pad: the shared pad plus the links that show how DBAP shares a source over the speakers. */

#pragma once

#include "PluginProcessor.h"
#include "tlos/FieldComponent.h"
#include "tlos/Hull.h"

#include <vector>

/** The pad of DistPan. Everything that every plugin's pad does comes from tlos::FieldComponent; this class
    always marks the convex hull of the speakers that take part in the panning (the edge of the layout, where the level
    starts to fall off for a source outside it) as a dashed outline with a faint tint inside, and adds the links from each source to the speakers that get a
    noticeable share of it. With DBAP every speaker gets something, so links are drawn only for the speakers
    within 20 dB of the strongest one. */
class DistPanField final : public tlos::FieldComponent
{
public:
    /** Creates the pad for `processor`. */
    explicit DistPanField (DistPanAudioProcessor& processor);

protected:
    /** Remembers the speakers in use, for getLinkGains(). */
    void updateOverlay (const tlos::IncludedSpeakers& speakers, const std::vector<tlos::VirtualSpeaker>& virtualSpeakers) override;

    /** Draws the convex hull of the speakers (the edge of the layout, where the level starts to fall off): a dashed outline and a faint tint
        inside, the same in every plugin. Always shown. */
    void paintBackdrop (juce::Graphics& g) override;

    /** The speakers a source feeds according to DBAP with the current rolloff and blur (the strong ones only). */
    tlos::SourceGains getLinkGains (int source, tlos::Vec2 position, double spread) const override;

private:
    DistPanAudioProcessor& dbapProcessor;      ///< The processor, for the rolloff and the blur.
    tlos::IncludedSpeakers includedSpeakers;   ///< Speakers that take part in the panning as of the last timer tick.
    tlos::Hull speakerHull;                    ///< Their hull, for the falloff outside the layout.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DistPanField)
};
