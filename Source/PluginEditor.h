#pragma once

#include "FieldComponent.h"
#include "PluginProcessor.h"

class TriPanAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                         private juce::Timer
{
public:
    explicit TriPanAudioProcessorEditor (TriPanAudioProcessor&);
    ~TriPanAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    FieldComponent& getField() noexcept { return field; }

private:
    void timerCallback() override;
    void updateInfoText();

    TriPanAudioProcessor& owner;

    FieldComponent field;
    juce::ToggleButton showTriangulationButton { "Show triangulation" };
    juce::TextButton resetButton { "Reset layout" };
    juce::Label infoLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriPanAudioProcessorEditor)
};
