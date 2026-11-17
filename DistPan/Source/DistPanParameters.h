// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file DistPanParameters.h
    @brief The host parameters that belong to DistPan's panning method: rolloff and blur. */

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <vector>

namespace distpan::params
{
/** ID of the rolloff parameter (dB per doubling of the distance). Part of saved sessions: never change it. */
inline const juce::String kRolloffId { "rolloff" };
/** ID of the blur parameter (metres). Part of saved sessions: never change it. */
inline const juce::String kBlurId { "blur" };

/** Smallest rolloff in dB per doubling of the distance. */
inline constexpr float kMinRolloff = 1.0f;
/** Largest rolloff in dB per doubling of the distance. */
inline constexpr float kMaxRolloff = 12.0f;
/** Default rolloff: 6 dB, the free-field value. */
inline constexpr float kDefaultRolloff = 6.0f;
/** Largest blur in metres. */
inline constexpr float kMaxBlur = 5.0f;
/** Default blur in metres. */
inline constexpr float kDefaultBlur = 0.2f;

/** @return The parameter ID of the weight of speaker `index` (zero-based), for example "spk1weight". Part of saved sessions. */
inline juce::String speakerWeightId (int index) { return "spk" + juce::String (index + 1) + "weight"; }

/** @return The plugin's own parameters (rolloff, blur, then the weight of every speaker), to be added after the shared ones. */
inline std::vector<std::unique_ptr<juce::RangedAudioParameter>> createParameters()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> out;

    out.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kRolloffId, 1 }, "Rolloff",
                                                                juce::NormalisableRange<float> (kMinRolloff, kMaxRolloff), kDefaultRolloff,
                                                                juce::AudioParameterFloatAttributes().withLabel ("dB")));
    out.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kBlurId, 1 }, "Blur",
                                                                juce::NormalisableRange<float> (0.0f, kMaxBlur), kDefaultBlur,
                                                                juce::AudioParameterFloatAttributes().withLabel ("m")));

    // The weight of a speaker (as in the DBAP paper): 1 is a full speaker, less gives it a smaller share, 0 takes it out of the panning.
    for (int i = 0; i < 64; ++i)
        out.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { speakerWeightId (i), 1 }, "Speaker " + juce::String (i + 1) + " Weight",
                                                                    juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f));
    return out;
}
}
