// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file ViMiCParameters.h
    @brief The host parameters that belong to ViMiC: the model settings and the yaw and pattern of every microphone and source. */

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "tlos/Parameters.h"
#include "vimic/Absorption.h"
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
inline const juce::String kDelayRenderId { "delayRender" };       ///< How a change of delay is rendered: smoothly (with a Doppler shift) or by a cross-fade.
inline const juce::String kFadeThresholdId { "fadeThreshold" };   ///< Cross-fade: how far a delay has to be from the one in use, in milliseconds, before it fades to the new one.
inline const juce::String kFadeTimeId { "fadeTime" };             ///< Cross-fade: how long a fade takes, in milliseconds.
inline const juce::String kTemperatureId { "temperature" };       ///< Room temperature in degrees Celsius.
inline const juce::String kSourceHeightId { "sourceHeight" };     ///< Height of the sources in metres.
inline const juce::String kMicHeightId { "micHeight" };           ///< Height of the microphones in metres.
inline const juce::String kDirectGainId { "directGain" };         ///< Gain of the direct sound in dB.
inline const juce::String kOutputGainId { "outputGain" };         ///< Output gain in dB.
inline const juce::String kRoomWidthId { "roomWidth" };           ///< Size of the room along x in metres.
inline const juce::String kRoomDepthId { "roomDepth" };           ///< Size of the room along y in metres.
inline const juce::String kRoomHeightId { "roomHeight" };         ///< Size of the room along z in metres.
inline const juce::String kReflOrderId { "reflOrder" };           ///< The highest number of reflections that is rendered: Off, 1st or 2nd order.
inline const juce::String kRefl1GainId { "refl1Gain" };           ///< Gain of the first-order reflections in dB.
inline const juce::String kRefl2GainId { "refl2Gain" };           ///< Gain of the second-order reflections in dB.
inline const juce::String kThresholdId { "renderThreshold" };     ///< Reflections below this level are not rendered, in dB (the lowest value: none are left out).
inline const juce::String kAirCutoffId { "airCutoff" };           ///< Cut-off of the air absorption of the reflections in Hz.
inline const juce::String kLowMidId { "absorbLowMid" };           ///< Crossover between the low and the middle absorption band in Hz.
inline const juce::String kMidHighId { "absorbMidHigh" };         ///< Crossover between the middle and the high absorption band in Hz.
/// @}

/** The model parameters in the order they are listed (and in which the processor reads them). */
enum Global : size_t
{
    Law, Power, Db, MinDistance, MicPower, Polarity, SourcePower, DelayModeIndex, DelayRenderIndex, FadeThreshold, FadeTime, Temperature, SourceHeight, MicHeight, DirectGain, OutputGain,
    RoomWidth, RoomDepth, RoomHeight, ReflOrder, Refl1Gain, Refl2Gain, RenderThreshold,
    /** The first parameter that only changes the filters of the reflections (from here on, a change does not change any path). */
    FilterFirst,
    AirCutoff = FilterFirst, LowMid, MidHigh,
    AbsorptionFirst,                         ///< The first of the 18 absorption parameters: wall w (vimic::Wall), band b (0 low, 1 mid, 2 high) is AbsorptionFirst + 3 w + b.
    GlobalCount = AbsorptionFirst + 18       ///< How many model parameters there are.
};

/** @return The ID of the absorption of a wall (vimic::Wall) in a band (0 low, 1 mid, 2 high), for example "absorbLeftLow". */
inline juce::String absorptionId (int wall, int band)
{
    static const char* walls[] { "Left", "Right", "Front", "Rear", "Floor", "Ceiling" };
    static const char* bands[] { "Low", "Mid", "High" };
    return juce::String ("absorb") + walls[wall] + bands[band];
}

/** @return The name of a wall for the interface. */
inline juce::String wallName (int wall)
{
    static const char* names[] { "Left", "Right", "Front", "Rear", "Floor", "Ceiling" };
    return names[wall];
}

/** The reflection order menu. */
enum class ReflectionOrder
{
    Off,      ///< Direct sound only.
    First,    ///< Direct sound and the 6 first-order reflections.
    Second    ///< And the 18 second-order reflections.
};

/** Smallest room dimension in metres. */
inline constexpr float kMinRoomSize = 1.0f;
/** Largest room dimension in metres. */
inline constexpr float kMaxRoomSize = 40.0f;
/** The lowest value of the render threshold in dB, which means that no reflection is left out. */
inline constexpr float kThresholdOff = -100.0f;

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

/** How a change of the delay of a path is rendered. */
enum class DelayRender
{
    Smooth,      ///< The delay is read with an interpolation and changes smoothly: a moving source gives a Doppler shift (in the Absolute mode).
    CrossFade    ///< The delay stays as it is until the new one is further away than the threshold; then the two are cross-faded. No Doppler shift.
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
    g.push_back (std::make_unique<juce::AudioParameterChoice> (ParameterID { kDelayRenderId, 1 }, "Delay Change", juce::StringArray { "Smooth", "Cross-fade" }, 0));
    g.push_back (std::make_unique<AudioParameterFloat> (ParameterID { kFadeThresholdId, 1 }, "Fade Threshold", NormalisableRange<float> (0.02f, 10.0f, 0.0f, 0.4f), 0.5f, juce::AudioParameterFloatAttributes().withLabel ("ms")));
    g.push_back (std::make_unique<AudioParameterFloat> (ParameterID { kFadeTimeId, 1 }, "Fade Time", NormalisableRange<float> (5.0f, 500.0f, 0.0f, 0.5f), 50.0f, juce::AudioParameterFloatAttributes().withLabel ("ms")));
    g.push_back (std::make_unique<AudioParameterFloat> (ParameterID { kTemperatureId, 1 }, "Temperature", NormalisableRange<float> (-20.0f, 40.0f), 20.0f, juce::AudioParameterFloatAttributes().withLabel ("C")));
    g.push_back (std::make_unique<AudioParameterFloat> (ParameterID { kSourceHeightId, 1 }, "Source Height", NormalisableRange<float> (0.0f, kMaxHeight), 1.5f, juce::AudioParameterFloatAttributes().withLabel ("m")));
    g.push_back (std::make_unique<AudioParameterFloat> (ParameterID { kMicHeightId, 1 }, "Microphone Height", NormalisableRange<float> (0.0f, kMaxHeight), 1.5f, juce::AudioParameterFloatAttributes().withLabel ("m")));
    g.push_back (gain (kDirectGainId, "Direct Gain"));
    g.push_back (gain (kOutputGainId, "Output Gain"));

    // The room and its reflections.
    const auto size = [] (const juce::String& id, const juce::String& name, float defaultValue)
    {
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (kMinRoomSize, kMaxRoomSize), defaultValue, juce::AudioParameterFloatAttributes().withLabel ("m"));
    };
    g.push_back (size (kRoomWidthId, "Room Width", 10.0f));
    g.push_back (size (kRoomDepthId, "Room Depth", 10.0f));
    g.push_back (size (kRoomHeightId, "Room Height", 3.0f));
    g.push_back (std::make_unique<juce::AudioParameterChoice> (ParameterID { kReflOrderId, 1 }, "Reflections", juce::StringArray { "Off", "1st order", "2nd order" }, 0));
    g.push_back (gain (kRefl1GainId, "1st Order Gain"));
    g.push_back (gain (kRefl2GainId, "2nd Order Gain"));
    g.push_back (std::make_unique<AudioParameterFloat> (ParameterID { kThresholdId, 1 }, "Render Threshold", NormalisableRange<float> (kThresholdOff, -20.0f), kThresholdOff, juce::AudioParameterFloatAttributes().withLabel ("dB")));
    g.push_back (std::make_unique<AudioParameterFloat> (ParameterID { kAirCutoffId, 1 }, "Air Cutoff", NormalisableRange<float> (500.0f, 19000.0f, 0.0f, 0.4f), 10000.0f, juce::AudioParameterFloatAttributes().withLabel ("Hz")));
    g.push_back (std::make_unique<AudioParameterFloat> (ParameterID { kLowMidId, 1 }, "Absorption Low/Mid Crossover", NormalisableRange<float> (50.0f, 2000.0f, 0.0f, 0.5f), 500.0f, juce::AudioParameterFloatAttributes().withLabel ("Hz")));
    g.push_back (std::make_unique<AudioParameterFloat> (ParameterID { kMidHighId, 1 }, "Absorption Mid/High Crossover", NormalisableRange<float> (1000.0f, 12000.0f, 0.0f, 0.5f), 3500.0f, juce::AudioParameterFloatAttributes().withLabel ("Hz")));

    // The absorption of the walls: four walls of drywall, a plaster ceiling and a wooden floor.
    const char* defaults[vimic::WallCount] { "drywall", "drywall", "drywall", "drywall", "wood", "plaster" };
    for (int wall = 0; wall < vimic::WallCount; ++wall)
    {
        const auto& surface = vimic::findSurfacePreset (defaults[wall])->surface;
        const double values[3] { surface.low, surface.mid, surface.high };
        for (int band = 0; band < 3; ++band)
            g.push_back (std::make_unique<AudioParameterFloat> (ParameterID { absorptionId (wall, band), 1 }, wallName (wall) + " Absorption " + juce::String (band == 0 ? "Low" : band == 1 ? "Mid" : "High"),
                                                                NormalisableRange<float> (0.1f, 99.9f), (float) values[band], juce::AudioParameterFloatAttributes().withLabel ("%")));
    }

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
