// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file Parameters.h
    @brief The host parameters every plugin of this repository has: positions, spreads and channel counts. */

#pragma once

#include "tlos/Limits.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace tlos::params
{
/** Coordinates are in metres, in the range [-kCoordRange, kCoordRange]. +y is front, +x is right. */
inline constexpr float kCoordRange = 10.0f;

/** Per-source spread: the radius in metres of the circle around a source over which its sound is
    spread (0 = a point source). */
inline constexpr float kMaxSpread = 10.0f;

/** How many of the host's input/output channels are used as sources/speakers. The parameter range
    is fixed at 1..kMaxChannels (hosts cannot change it per track); the effective value is clamped to
    the channel count of the track the plugin sits on. The default uses every channel. */
inline const juce::String kNumSourcesId  { "numSources" };
/** ID of the "Number of Speakers" parameter (see kNumSourcesId). */
inline const juce::String kNumSpeakersId { "numSpeakers" };

/** @return The parameter ID of the x or y (`axis`) coordinate of source `index` (zero-based), for example "src1x". */
inline juce::String sourceId  (int index, char axis) { return "src" + juce::String (index + 1) + juce::String::charToString ((juce::juce_wchar) axis); }
/** @return The parameter ID of the spread of source `index` (zero-based), for example "src1spread". */
inline juce::String spreadId (int index) { return "src" + juce::String (index + 1) + "spread"; }
/** @return The parameter ID of the x or y (`axis`) coordinate of speaker `index` (zero-based), for example "spk1x". */
inline juce::String speakerId (int index, char axis) { return "spk" + juce::String (index + 1) + juce::String::charToString ((juce::juce_wchar) axis); }

/** @return All host parameters, in the order a host lists them: the two counts, then x, y and spread of
    every source, then x and y of every speaker. The IDs are part of the saved sessions: never change them. */
inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using juce::AudioParameterFloat;
    using juce::NormalisableRange;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    const auto add = [&] (const juce::String& id, const juce::String& name)
    {
        layout.add (std::make_unique<AudioParameterFloat> (juce::ParameterID { id, 1 }, name,
                                                           NormalisableRange<float> (-kCoordRange, kCoordRange),
                                                           0.0f,
                                                           juce::AudioParameterFloatAttributes().withLabel ("m")));
    };

    layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { kNumSourcesId, 1 }, "Number of Sources",
                                                           1, kMaxChannels, kMaxChannels));
    layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { kNumSpeakersId, 1 }, "Number of Speakers",
                                                           1, kMaxChannels, kMaxChannels));

    // The order of the parameters is the order a host lists them in (automation lanes, parameter
    // menus): the two counts, then everything for source 1 (x, y, spread), source 2, ..., then
    // everything for speaker 1 (x, y), speaker 2, ...
    for (int i = 0; i < kMaxChannels; ++i)
    {
        add (sourceId (i, 'x'), "Source " + juce::String (i + 1) + " X");
        add (sourceId (i, 'y'), "Source " + juce::String (i + 1) + " Y");
        layout.add (std::make_unique<AudioParameterFloat> (juce::ParameterID { spreadId (i), 1 }, "Source " + juce::String (i + 1) + " Spread",
                                                           NormalisableRange<float> (0.0f, kMaxSpread), 0.0f,
                                                           juce::AudioParameterFloatAttributes().withLabel ("m")));
    }

    for (int i = 0; i < kMaxChannels; ++i)
    {
        add (speakerId (i, 'x'), "Speaker " + juce::String (i + 1) + " X");
        add (speakerId (i, 'y'), "Speaker " + juce::String (i + 1) + " Y");
    }

    return layout;
}
}
