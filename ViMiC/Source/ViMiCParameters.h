// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file ViMiCParameters.h
    @brief The host parameters that belong to ViMiC: the model settings and the yaw and pattern of every microphone and source. */

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "tlos/Parameters.h"
#include "vimic/Path.h"

#include <memory>

namespace vimic::params
{
/** @name Parameter IDs (part of saved sessions: never change them) */
/// @{
inline const juce::String kDistanceLawId { "distanceLaw" };       ///< Distance law: Inverse or Exponential.
inline const juce::String kDistancePowerId { "distancePower" };   ///< The exponent q of the inverse law.
inline const juce::String kDistanceDbId { "distanceDb" };         ///< The decrease in dB per metre of the exponential law.
inline const juce::String kMinDistanceId { "minDistance" };       ///< The shortest distance that counts, in metres.
inline const juce::String kMicPowerId { "micPower" };             ///< The exponent w of the microphone pattern.
inline const juce::String kMicPolarityId { "micPolarity" };       ///< Whether the pattern may not give anti-phase gains.
inline const juce::String kSourcePowerId { "sourcePower" };       ///< The exponent of the sources' radiation pattern.
inline const juce::String kDelayModeId { "delayMode" };           ///< Relative or Absolute delays.
inline const juce::String kTemperatureId { "temperature" };       ///< Room temperature in degrees Celsius.
inline const juce::String kSourceHeightId { "sourceHeight" };     ///< Height of the sources in metres.
inline const juce::String kMicHeightId { "micHeight" };           ///< Height of the microphones in metres.
inline const juce::String kDirectGainId { "directGain" };         ///< Gain of the direct sound in dB.
inline const juce::String kOutputGainId { "outputGain" };         ///< Output gain in dB.
/// @}

/** The model parameters in the order they are listed (and in which the processor reads them). */
enum Global : size_t
{
    Law, Power, Db, MinDistance, MicPower, Polarity, SourcePower, DelayModeIndex, Temperature, SourceHeight, MicHeight, DirectGain, OutputGain,
    GlobalCount   ///< How many model parameters there are.
};

/** @return The ID of the yaw (direction of the source's axis in degrees, counter-clockwise from the front) of source `index` (zero-based), for example "src1yaw". */
inline juce::String srcYawId (int index) { return "src" + juce::String (index + 1) + "yaw"; }
/** @return The ID of the radiation pattern ratio `a` (1 omni, 0.5 cardioid, 0 figure-8) of source `index` (zero-based), for example "src1pattern". */
inline juce::String srcPatternId (int index) { return "src" + juce::String (index + 1) + "pattern"; }

/** @return The ID of the yaw (direction of the axis in degrees, counter-clockwise from the front) of microphone `index` (zero-based), for example "spk1yaw". */
inline juce::String micYawId (int index) { return "spk" + juce::String (index + 1) + "yaw"; }
/** @return The ID of the pattern ratio `a` (1 omni, 0.5 cardioid, 0 figure-8) of microphone `index` (zero-based), for example "spk1pattern". */
inline juce::String micPatternId (int index) { return "spk" + juce::String (index + 1) + "pattern"; }

/** The delay modes. */
enum class DelayMode
{
    Relative,   ///< Only the differences between the delays to the microphones are rendered (no latency to speak of, little Doppler).
    Absolute    ///< The true time of flight: a moving source gives a Doppler shift.
};

/** Smallest gain in dB of the gain parameters (and so the gain that counts as off). */
inline constexpr float kMinGainDb = -60.0f;
/** Largest gain in dB of the gain parameters. */
inline constexpr float kMaxGainDb = 12.0f;
/** Largest height in metres. */
inline constexpr float kMaxHeight = 40.0f;

/** @return The plugin's own parameters: the model settings (global, listed with the shared global parameters) and the yaw and the pattern of every
    source and of every microphone (listed with that channel's other parameters). */
inline tlos::params::PluginParameters createParameters()
{
    using juce::AudioParameterFloat;
    using juce::ParameterID;
    using juce::NormalisableRange;

    tlos::params::PluginParameters parameters;
    parameters.outputNoun = "Microphone";
    auto& g = parameters.global;

    const auto gain = [] (const juce::String& id, const juce::String& name) { return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (kMinGainDb, kMaxGainDb), 0.0f, juce::AudioParameterFloatAttributes().withLabel ("dB")); };

    g.push_back (std::make_unique<juce::AudioParameterChoice> (ParameterID { kDistanceLawId, 1 }, "Distance Law", juce::StringArray { "Inverse", "Exponential" }, 0));
    g.push_back (std::make_unique<AudioParameterFloat> (ParameterID { kDistancePowerId, 1 }, "Distance Exponent", NormalisableRange<float> (0.0f, 9.0f), 1.0f));
    g.push_back (std::make_unique<AudioParameterFloat> (ParameterID { kDistanceDbId, 1 }, "Distance Decrease", NormalisableRange<float> (0.0f, 60.0f), 6.0f, juce::AudioParameterFloatAttributes().withLabel ("dB/m")));
    g.push_back (std::make_unique<AudioParameterFloat> (ParameterID { kMinDistanceId, 1 }, "Minimum Distance", NormalisableRange<float> (0.1f, 10.0f), 1.0f, juce::AudioParameterFloatAttributes().withLabel ("m")));
    g.push_back (std::make_unique<juce::AudioParameterInt> (ParameterID { kMicPowerId, 1 }, "Microphone Exponent", 0, 9, 1));
    g.push_back (std::make_unique<juce::AudioParameterBool> (ParameterID { kMicPolarityId, 1 }, "Restrict Polarity", true));
    g.push_back (std::make_unique<juce::AudioParameterInt> (ParameterID { kSourcePowerId, 1 }, "Source Exponent", 0, 9, 1));
    g.push_back (std::make_unique<juce::AudioParameterChoice> (ParameterID { kDelayModeId, 1 }, "Delay", juce::StringArray { "Relative", "Absolute" }, 0));
    g.push_back (std::make_unique<AudioParameterFloat> (ParameterID { kTemperatureId, 1 }, "Temperature", NormalisableRange<float> (-20.0f, 40.0f), 20.0f, juce::AudioParameterFloatAttributes().withLabel ("C")));
    g.push_back (std::make_unique<AudioParameterFloat> (ParameterID { kSourceHeightId, 1 }, "Source Height", NormalisableRange<float> (0.0f, kMaxHeight), 1.5f, juce::AudioParameterFloatAttributes().withLabel ("m")));
    g.push_back (std::make_unique<AudioParameterFloat> (ParameterID { kMicHeightId, 1 }, "Microphone Height", NormalisableRange<float> (0.0f, kMaxHeight), 1.5f, juce::AudioParameterFloatAttributes().withLabel ("m")));
    g.push_back (gain (kDirectGainId, "Direct Gain"));
    g.push_back (gain (kOutputGainId, "Output Gain"));

    for (int i = 0; i < 64; ++i)
    {
        parameters.perSource.push_back (std::make_unique<AudioParameterFloat> (ParameterID { srcYawId (i), 1 }, "Source " + juce::String (i + 1) + " Yaw",
                                                                               NormalisableRange<float> (-180.0f, 180.0f), 0.0f, juce::AudioParameterFloatAttributes().withLabel ("deg")));
        parameters.perSource.push_back (std::make_unique<AudioParameterFloat> (ParameterID { srcPatternId (i), 1 }, "Source " + juce::String (i + 1) + " Pattern",
                                                                               NormalisableRange<float> (0.0f, 1.0f), 1.0f));
        parameters.perSpeaker.push_back (std::make_unique<AudioParameterFloat> (ParameterID { micYawId (i), 1 }, "Microphone " + juce::String (i + 1) + " Yaw",
                                                                                NormalisableRange<float> (-180.0f, 180.0f), 0.0f, juce::AudioParameterFloatAttributes().withLabel ("deg")));
        parameters.perSpeaker.push_back (std::make_unique<AudioParameterFloat> (ParameterID { micPatternId (i), 1 }, "Microphone " + juce::String (i + 1) + " Pattern",
                                                                                NormalisableRange<float> (0.0f, 1.0f), 0.5f));
    }

    return parameters;
}
}
