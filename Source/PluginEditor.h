#pragma once

#include "PluginProcessor.h"

class TriPanAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit TriPanAudioProcessorEditor (TriPanAudioProcessor&);
    ~TriPanAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override {}

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriPanAudioProcessorEditor)
};
