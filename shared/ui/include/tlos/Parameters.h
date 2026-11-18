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
/** @return The parameter ID of the group number (0 = none, 1 to 32) of source `index` (zero-based), for example "src1group". */
inline juce::String sourceGroupId (int index) { return "src" + juce::String (index + 1) + "group"; }
/** @return The parameter ID of the "excluded from the panning" switch of speaker `index` (zero-based), for example "spk1exclude". */
inline juce::String speakerExcludeId (int index) { return "spk" + juce::String (index + 1) + "exclude"; }

/** ID of the virtual speaker mode (Auto, Manual, Off) parameter, which only plugins with virtual speakers have. */
inline const juce::String kVirtualModeId { "virtualMode" };
/** ID of the number of manual virtual speakers parameter (0 to kMaxVirtualSpeakers), which only plugins with virtual speakers have. */
inline const juce::String kNumVirtualId { "numVirtualSpeakers" };
/** The most manual virtual speakers. */
inline constexpr int kMaxVirtualSpeakers = 64;
/** @return The parameter ID of the x or y (`axis`) coordinate of manual virtual speaker `index` (zero-based), for example "vspk1x". */
inline juce::String virtualId (int index, char axis) { return "vspk" + juce::String (index + 1) + juce::String::charToString ((juce::juce_wchar) axis); }

/** @return The parameter ID of the x or y (`axis`) coordinate of speaker `index` (zero-based), for example "spk1x". */
inline juce::String speakerId (int index, char axis) { return "spk" + juce::String (index + 1) + juce::String::charToString ((juce::juce_wchar) axis); }

/** The parameters that belong to a plugin's own panning method. */
struct PluginParameters
{
    /** Settings of the whole plugin (for example DistPan's rolloff and blur): listed with the other global parameters. */
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> global;

    /** Values per source (for example ViMiC's yaw and pattern): either empty, or `perSourceCount * kMaxChannels` parameters, the first `perSourceCount`
        for source 1, the next for source 2 and so on. They are listed with that source's other parameters (after its group). */
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> perSource;

    /** Values per speaker (for example DistPan's weight): either empty, or `perSpeakerCount * kMaxChannels` parameters, the
        first `perSpeakerCount` for speaker 1, the next for speaker 2 and so on. They are listed with that speaker's other parameters. */
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> perSpeaker;

    /** What the plugin calls its outputs, singular with a capital, in the names of the host parameters and groups ("Speaker 3 X"; ViMiC: "Microphone 3 X"). The IDs
        keep saying "spk". See SpatialProcessor::getOutputNoun(). */
    juce::String outputNoun { "Speaker" };
};

/** @param withVirtualSpeakers  Whether to include the virtual speaker parameters (for a plugin that has virtual speakers).
    @param extra                The plugin's own parameters.
    @return All host parameters, in the order a host lists them, and in groups for the hosts that show a tree (VST3 units, AU clumps):
    **Global** first (the number of sources and speakers, the outside rolloff, with virtual speakers their mode and number, then the plugin's own
    global parameters), then **Sources** (per source x, y, spread, trim, mute, solo, group), **Speakers** (per speaker x, y, trim, mute, solo,
    exclude, then the plugin's own per-speaker values) and, with virtual speakers, **Virtual Speakers** (x and y of each). Everything a user can
    change is a parameter, so that the host's undo covers it. The IDs are part of the saved sessions: never change them. */
inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout (bool withVirtualSpeakers = false, PluginParameters extra = {})
{
    using juce::AudioParameterFloat;
    using juce::NormalisableRange;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    using Group = juce::AudioProcessorParameterGroup;
    const auto makeGroup = [] (const juce::String& id, const juce::String& name) { return std::make_unique<Group> (id, name, " | "); };

    const auto makeCoordinate = [] (const juce::String& id, const juce::String& name)
    {
        return std::make_unique<AudioParameterFloat> (juce::ParameterID { id, 1 }, name,
                                                      NormalisableRange<float> (-kCoordRange, kCoordRange),
                                                      0.0f,
                                                      juce::AudioParameterFloatAttributes().withLabel ("m"));
    };
    const auto makeTrim = [] (const juce::String& id, const juce::String& name)
    {
        return std::make_unique<AudioParameterFloat> (juce::ParameterID { id, 1 }, name,
                                                      NormalisableRange<float> (kMinTrimDb, kMaxTrimDb), 0.0f,
                                                      juce::AudioParameterFloatAttributes().withLabel ("dB"));
    };
    const auto makeSwitch = [] (const juce::String& id, const juce::String& name)
    {
        return std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, false);
    };

    // Global: everything that is not about one source or speaker. A host lists these first.
    {
        auto global = makeGroup ("global", "Global");
        global->addChild (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { kNumSourcesId, 1 }, "Number of Sources",
                                                                     1, kMaxChannels, kMaxChannels));
        global->addChild (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { kNumSpeakersId, 1 }, "Number of Speakers",
                                                                     1, kMaxChannels, kMaxChannels));
        global->addChild (std::make_unique<AudioParameterFloat> (juce::ParameterID { kOutsideRolloffId, 1 }, "Outside Rolloff",
                                                                 NormalisableRange<float> (0.0f, kMaxOutsideRolloff), kDefaultOutsideRolloff,
                                                                 juce::AudioParameterFloatAttributes().withLabel ("dB")));
        if (withVirtualSpeakers)
        {
            global->addChild (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { kVirtualModeId, 1 }, "Virtual Speakers",
                                                                            juce::StringArray { "Auto", "Manual", "Off" }, 0));
            global->addChild (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { kNumVirtualId, 1 }, "Number of Virtual Speakers", 0, kMaxVirtualSpeakers, 0));
        }
        for (auto& parameter : extra.global)
            global->addChild (std::move (parameter));
        layout.add (std::move (global));
    }

    // Then one group per source and per speaker, in order: source 1 (x, y, spread, trim, mute, solo, group, then the plugin's own), source 2, ...
    {
        const size_t perSource = extra.perSource.size() / (size_t) kMaxChannels;
        jassert (extra.perSource.size() == perSource * (size_t) kMaxChannels);

        auto sources = makeGroup ("sources", "Sources");
        for (int i = 0; i < kMaxChannels; ++i)
        {
            const auto name = "Source " + juce::String (i + 1);
            auto source = makeGroup ("source" + juce::String (i + 1), name);
            source->addChild (makeCoordinate (sourceId (i, 'x'), name + " X"));
            source->addChild (makeCoordinate (sourceId (i, 'y'), name + " Y"));
            source->addChild (std::make_unique<AudioParameterFloat> (juce::ParameterID { spreadId (i), 1 }, name + " Spread",
                                                                     NormalisableRange<float> (0.0f, kMaxSpread), 0.0f,
                                                                     juce::AudioParameterFloatAttributes().withLabel ("m")));
            source->addChild (makeTrim (sourceTrimId (i), name + " Trim"));
            source->addChild (makeSwitch (sourceMuteId (i), name + " Mute"));
            source->addChild (makeSwitch (sourceSoloId (i), name + " Solo"));
            source->addChild (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { sourceGroupId (i), 1 }, name + " Group", 0, kMaxChannels / 2, 0));
            for (size_t k = 0; k < perSource; ++k)
                source->addChild (std::move (extra.perSource[(size_t) i * perSource + k]));
            sources->addChild (std::move (source));
        }
        layout.add (std::move (sources));
    }

    {
        const size_t perSpeaker = extra.perSpeaker.size() / (size_t) kMaxChannels;
        jassert (extra.perSpeaker.size() == perSpeaker * (size_t) kMaxChannels);

        auto speakers = makeGroup ("speakers", extra.outputNoun + "s");
        for (int i = 0; i < kMaxChannels; ++i)
        {
            const auto name = extra.outputNoun + " " + juce::String (i + 1);
            auto speaker = makeGroup ("speaker" + juce::String (i + 1), name);
            speaker->addChild (makeCoordinate (speakerId (i, 'x'), name + " X"));
            speaker->addChild (makeCoordinate (speakerId (i, 'y'), name + " Y"));
            speaker->addChild (makeTrim (speakerTrimId (i), name + " Trim"));
            speaker->addChild (makeSwitch (speakerMuteId (i), name + " Mute"));
            speaker->addChild (makeSwitch (speakerSoloId (i), name + " Solo"));
            speaker->addChild (makeSwitch (speakerExcludeId (i), name + " Exclude"));
            for (size_t k = 0; k < perSpeaker; ++k)
                speaker->addChild (std::move (extra.perSpeaker[(size_t) i * perSpeaker + k]));
            speakers->addChild (std::move (speaker));
        }
        layout.add (std::move (speakers));
    }

    if (withVirtualSpeakers)
    {
        auto virtuals = makeGroup ("virtualspeakers", "Virtual Speakers");
        for (int i = 0; i < kMaxVirtualSpeakers; ++i)
        {
            const auto name = "Virtual Speaker " + juce::String (i + 1);
            auto virtualSpeaker = makeGroup ("virtualspeaker" + juce::String (i + 1), name);
            virtualSpeaker->addChild (makeCoordinate (virtualId (i, 'x'), name + " X"));
            virtualSpeaker->addChild (makeCoordinate (virtualId (i, 'y'), name + " Y"));
            virtuals->addChild (std::move (virtualSpeaker));
        }
        layout.add (std::move (virtuals));
    }

    return layout;
}
}
