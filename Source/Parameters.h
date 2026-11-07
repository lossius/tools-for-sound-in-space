#pragma once

#include "tripan/Limits.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace tripan::params
{
/** Coordinates are in metres, in the range [-kCoordRange, kCoordRange]. +y is front, +x is right. */
inline constexpr float kCoordRange = 10.0f;

inline juce::String sourceId  (int index, char axis) { return "src" + juce::String (index + 1) + juce::String::charToString ((juce::juce_wchar) axis); }
inline juce::String speakerId (int index, char axis) { return "spk" + juce::String (index + 1) + juce::String::charToString ((juce::juce_wchar) axis); }

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

    for (int i = 0; i < kMaxChannels; ++i)
    {
        add (sourceId (i, 'x'), "Source " + juce::String (i + 1) + " X");
        add (sourceId (i, 'y'), "Source " + juce::String (i + 1) + " Y");
    }

    for (int i = 0; i < kMaxChannels; ++i)
    {
        add (speakerId (i, 'x'), "Speaker " + juce::String (i + 1) + " X");
        add (speakerId (i, 'y'), "Speaker " + juce::String (i + 1) + " Y");
    }

    return layout;
}
}
