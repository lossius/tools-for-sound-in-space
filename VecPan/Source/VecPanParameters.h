// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file VecPanParameters.h
    @brief The host parameter that belongs to VecPan's panning method: the centre radius. */

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "tlos/Parameters.h"

#include <memory>

namespace vecpan::params
{
/** ID of the centre radius parameter (metres). Part of saved sessions: never change it. */
inline const juce::String kCentreRadiusId { "centreRadius" };

/** Largest centre radius in metres. */
inline constexpr float kMaxCentreRadius = 5.0f;
/** Default centre radius in metres: small, so that only a source very close to the listener is affected. */
inline constexpr float kDefaultCentreRadius = 0.5f;

/** @return The plugin's own parameters: the centre radius (global, listed with the shared global parameters). */
inline tlos::params::PluginParameters createParameters()
{
    tlos::params::PluginParameters parameters;
    parameters.global.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kCentreRadiusId, 1 }, "Centre Radius",
                                                                              juce::NormalisableRange<float> (0.0f, kMaxCentreRadius), kDefaultCentreRadius,
                                                                              juce::AudioParameterFloatAttributes().withLabel ("m")));
    return parameters;
}
}
