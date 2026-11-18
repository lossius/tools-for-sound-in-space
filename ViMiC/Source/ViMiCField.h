// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file ViMiCField.h
    @brief ViMiC's XY pad: the shared pad plus the pickup lobes of the microphones and the links from the sources to them. */

#pragma once

#include "PluginProcessor.h"
#include "tlos/FieldComponent.h"

#include <vector>

/** The pad of ViMiC. Everything that every plugin's pad does comes from tlos::FieldComponent; this class draws, under everything else, the pickup
    lobe of every microphone (a polar plot of its directivity pattern, along its yaw) and adds the links from each source to the microphones that
    hear it noticeably (within 20 dB of the strongest). */
class ViMiCField final : public tlos::FieldComponent
{
public:
    /** Creates the pad for `processor`. */
    explicit ViMiCField (ViMiCAudioProcessor& processor);

    /** @return The lobe of a microphone as a closed path around (0, 0): its directivity for sound from every direction, at `radius` pixels for a gain of 1, with the
        axis pointing up the screen; the caller rotates and moves it. Negative gains (without the polarity restriction) are plotted by their size.
        @param ratio             The pattern ratio `a`.
        @param power             The exponent `w`.
        @param restrictPolarity  Whether negative gains are removed.
        @param radius            Size in pixels for a gain of 1. */
    static juce::Path lobePath (double ratio, int power, bool restrictPolarity, float radius);

protected:
    /** Remembers the microphones in use, for the lobes and the links. */
    void updateOverlay (const tlos::IncludedSpeakers& speakers, const std::vector<tlos::VirtualSpeaker>& virtualSpeakers) override;

    /** Draws the lobe of every microphone in use and, for every source that does not radiate equally in every direction, its radiation lobe and an arrow
        along its yaw. Always shown. */
    void paintBackdrop (juce::Graphics& g) override;

    /** The microphones a source feeds and how much (the direct sound), the strong ones only. */
    tlos::SourceGains getLinkGains (int source, tlos::Vec2 position, double spread) const override;

private:
    ViMiCAudioProcessor& vimicProcessor;       ///< The processor, for the settings and the microphones' orientation.
    tlos::IncludedSpeakers includedSpeakers;   ///< Microphones that take part as of the last timer tick.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ViMiCField)
};
