// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file TriPanField.h
    @brief TriPan's XY pad: the shared pad plus the triangulation and the barycentric links. */

#pragma once

#include "PluginProcessor.h"
#include "tlos/FieldComponent.h"
#include "tripan/SpeakerLayout.h"

#include <memory>
#include <vector>

/** The pad of TriPan. Everything that every plugin's pad does (dragging, groups, zoom, ...) comes from
    tlos::FieldComponent; this class adds what belongs to the triangulation panning: the triangles drawn
    under the sources and the links to the (up to three, with virtual speakers more) speakers each
    source feeds. */
class TriPanField final : public tlos::FieldComponent
{
public:
    /** Creates the pad for `processor` and builds the layout that is drawn. */
    explicit TriPanField (TriPanAudioProcessor& processor);

protected:
    /** Rebuilds the drawn layout when the speakers or the virtual speakers changed. */
    void updateOverlay (const tlos::IncludedSpeakers& speakers, const std::vector<tlos::VirtualSpeaker>& virtualSpeakers) override;

    /** Draws the triangles of the triangulation (including those of the virtual speakers). */
    void paintOverlay (juce::Graphics& g) override;

    /** The speakers a source feeds according to the triangulation (see tripan::computeGains), including the falloff outside the layout. */
    tlos::SourceGains getLinkGains (int source, tlos::Vec2 position, double spread) const override;

private:
    tlos::IncludedSpeakers triangulatedSpeakers;             ///< Speakers the drawn layout was built from.
    std::vector<tlos::VirtualSpeaker> triangulatedVirtual;   ///< Virtual speakers the drawn layout was built from.
    std::unique_ptr<tripan::SpeakerLayout> triangulation;    ///< The layout that is drawn.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriPanField)
};
