// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file Parameters.h
    @brief The host parameters every plugin of this repository has: positions, spreads and channel counts. */

#pragma once

#include "tlos/Limits.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace tlos::params
{
/** Coordinates are in metres, in the range [-kCoordRange, kCoordRange]. +y is front, +x is right. This is the range of
    the host parameters, so a host shows real metres. The pad works within the room (see kDefaultRoomHalf), which is smaller. */
inline constexpr float kCoordRange = 100.0f;

/** The room: the area the pad shows and keeps the sources and speakers in when they are dragged is a square of
    +-"room half extent" metres. It is a setting saved with the session (not a host parameter), by default 10 m. */
inline constexpr double kDefaultRoomHalf = 10.0;
/** Smallest room half extent in metres. */
inline constexpr double kMinRoomHalf = 2.0;
/** Largest room half extent in metres (the range of the position parameters). */
inline constexpr double kMaxRoomHalf = (double) kCoordRange;

/** Per-source spread: the radius in metres of the circle around a source over which its sound is
    spread (0 = a point source). */
inline constexpr float kMaxSpread = 50.0f;

/** How many of the host's input/output channels are used as sources/speakers. The parameter range
    is fixed at 1..kMaxChannels (hosts cannot change it per track); the effective value is clamped to
    the channel count of the track the plugin sits on. The default uses every channel. */
inline const juce::String kNumSourcesId  { "numSources" };
/** ID of the "Number of Speakers" parameter (see kNumSourcesId). */
inline const juce::String kNumSpeakersId { "numSpeakers" };

/** ID of the "Outside Rolloff" parameter: the level drop in dB for each doubling of (1 + distance outside the
    speaker layout in metres), see tlos::outsideFactor(). 0 = the level stays constant outside the layout. Part of
    saved sessions: never change it. */
inline const juce::String kOutsideRolloffId { "outsideRolloff" };
/** Largest outside rolloff in dB. */
inline constexpr float kMaxOutsideRolloff = 24.0f;
/** Default outside rolloff in dB (the free-field value). */
inline constexpr float kDefaultOutsideRolloff = 6.0f;

/** Range of a trim in dB: the gain of a source (before the panning) or of a speaker (after it). 0 dB = unchanged. */
inline constexpr float kMinTrimDb = -60.0f;
/** Largest trim in dB. */
inline constexpr float kMaxTrimDb = 12.0f;

/** @return The parameter ID of the x or y (`axis`) coordinate of source `index` (zero-based), for example "src1x". */
inline juce::String sourceId  (int index, char axis) { return "src" + juce::String (index + 1) + juce::String::charToString ((juce::juce_wchar) axis); }
/** @return The parameter ID of the spread of source `index` (zero-based), for example "src1spread". */
inline juce::String spreadId (int index) { return "src" + juce::String (index + 1) + "spread"; }
/** @return The parameter ID of the trim (dB) of source `index` (zero-based), for example "src1trim". */
inline juce::String sourceTrimId (int index) { return "src" + juce::String (index + 1) + "trim"; }
/** @return The parameter ID of the mute switch of source `index` (zero-based), for example "src1mute". */
inline juce::String sourceMuteId (int index) { return "src" + juce::String (index + 1) + "mute"; }
/** @return The parameter ID of the solo switch of source `index` (zero-based), for example "src1solo". */
inline juce::String sourceSoloId (int index) { return "src" + juce::String (index + 1) + "solo"; }
/** @return The parameter ID of the trim (dB) of speaker `index` (zero-based), for example "spk1trim". */
inline juce::String speakerTrimId (int index) { return "spk" + juce::String (index + 1) + "trim"; }
/** @return The parameter ID of the mute switch of speaker `index` (zero-based), for example "spk1mute". */
inline juce::String speakerMuteId (int index) { return "spk" + juce::String (index + 1) + "mute"; }
/** @return The parameter ID of the solo switch of speaker `index` (zero-based), for example "spk1solo". */
inline juce::String speakerSoloId (int index) { return "spk" + juce::String (index + 1) + "solo"; }
/** @return The parameter ID of the x or y (`axis`) coordinate of speaker `index` (zero-based), for example "spk1x". */
inline juce::String speakerId (int index, char axis) { return "spk" + juce::String (index + 1) + juce::String::charToString ((juce::juce_wchar) axis); }

/** @return All host parameters, in the order a host lists them: the two counts, then x, y, spread, trim, mute
    and solo of every source, then x, y, trim, mute and solo of every speaker, then the outside rolloff. The IDs are
    part of the saved sessions: never change them. */
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

    const auto addTrim = [&] (const juce::String& id, const juce::String& name)
    {
        layout.add (std::make_unique<AudioParameterFloat> (juce::ParameterID { id, 1 }, name,
                                                           NormalisableRange<float> (kMinTrimDb, kMaxTrimDb), 0.0f,
                                                           juce::AudioParameterFloatAttributes().withLabel ("dB")));
    };
    const auto addSwitch = [&] (const juce::String& id, const juce::String& name)
    {
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, false));
    };

    // The order of the parameters is the order a host lists them in (automation lanes, parameter
    // menus): the two counts, then everything for source 1 (x, y, spread, trim, mute, solo), source 2, ...,
    // then everything for speaker 1 (x, y, trim, mute, solo), speaker 2, ...
    for (int i = 0; i < kMaxChannels; ++i)
    {
        add (sourceId (i, 'x'), "Source " + juce::String (i + 1) + " X");
        add (sourceId (i, 'y'), "Source " + juce::String (i + 1) + " Y");
        layout.add (std::make_unique<AudioParameterFloat> (juce::ParameterID { spreadId (i), 1 }, "Source " + juce::String (i + 1) + " Spread",
                                                           NormalisableRange<float> (0.0f, kMaxSpread), 0.0f,
                                                           juce::AudioParameterFloatAttributes().withLabel ("m")));
        addTrim (sourceTrimId (i), "Source " + juce::String (i + 1) + " Trim");
        addSwitch (sourceMuteId (i), "Source " + juce::String (i + 1) + " Mute");
        addSwitch (sourceSoloId (i), "Source " + juce::String (i + 1) + " Solo");
    }

    for (int i = 0; i < kMaxChannels; ++i)
    {
        add (speakerId (i, 'x'), "Speaker " + juce::String (i + 1) + " X");
        add (speakerId (i, 'y'), "Speaker " + juce::String (i + 1) + " Y");
        addTrim (speakerTrimId (i), "Speaker " + juce::String (i + 1) + " Trim");
        addSwitch (speakerMuteId (i), "Speaker " + juce::String (i + 1) + " Mute");
        addSwitch (speakerSoloId (i), "Speaker " + juce::String (i + 1) + " Solo");
    }

    layout.add (std::make_unique<AudioParameterFloat> (juce::ParameterID { kOutsideRolloffId, 1 }, "Outside Rolloff",
                                                       NormalisableRange<float> (0.0f, kMaxOutsideRolloff), kDefaultOutsideRolloff,
                                                       juce::AudioParameterFloatAttributes().withLabel ("dB")));

    return layout;
}
}
