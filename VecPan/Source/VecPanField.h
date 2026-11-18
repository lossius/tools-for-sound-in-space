// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file VecPanField.h
    @brief VecPan's XY pad: the shared pad plus the speaker pairs and the links that show how VBAP shares a source. */

#pragma once

#include "PluginProcessor.h"
#include "tlos/FieldComponent.h"
#include "vecpan/Vbap.h"

#include <vector>

/** The pad of VecPan. Everything that every plugin's pad does comes from tlos::FieldComponent; this class draws, under the
    sources, a line between every two neighbouring speakers (as seen from the listener) that VBAP can pan between (pairs less than
    180 degrees apart), and adds the links from each source to the one or two speakers it feeds (more with a spread). It always marks the reference circle (the distance of the farthest speaker) beyond
    which the level of a source falls off, as a dashed outline with a faint tint. */
class VecPanField final : public tlos::FieldComponent
{
public:
    /** Creates the pad for `processor`. */
    explicit VecPanField (VecPanAudioProcessor& processor);

protected:
    /** Rebuilds the drawn layout when the speakers changed. */
    void updateOverlay (const tlos::IncludedSpeakers& speakers, const std::vector<tlos::VirtualSpeaker>& virtualSpeakers) override;

    /** Draws the reference circle, where the level of a source starts to fall off: a dashed outline and a faint tint inside, like the
        hull in the other plugins (only if the speakers surround an area, as the falloff needs). Always shown, whether or not "Show pairs" is on. */
    void paintBackdrop (juce::Graphics& g) override;

    /** Draws a thin line between every pair of neighbouring speakers that VBAP pans between. */
    void paintOverlay (juce::Graphics& g) override;

    /** The speakers a source feeds according to VBAP (see vecpan::computeGains), including the falloff outside the layout. */
    tlos::SourceGains getLinkGains (int source, tlos::Vec2 position, double spread) const override;

private:
    VecPanAudioProcessor& vbapProcessor;       ///< The processor, for the settings.
    tlos::IncludedSpeakers includedSpeakers;   ///< Speakers that take part in the panning as of the last timer tick.
    vecpan::Layout layout;                     ///< Their VBAP layout.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VecPanField)
};
