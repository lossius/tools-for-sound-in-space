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
    juce::Slider& getSourceCountSlider() noexcept { return sourceCount; }
    juce::Slider& getSpeakerCountSlider() noexcept { return speakerCount; }
    juce::ComboBox& getModeBox() noexcept { return modeBox; }
    juce::TextButton& getResetButton() noexcept { return resetButton; }

    /** Re-reads the parameters and channel counts into the controls (also called by a timer). */
    void syncControls();

private:
    void timerCallback() override { syncControls(); }

    /** Wires a count slider to its parameter. The slider shows the effective value (parameter
        clamped to the track's channel count) and its maximum is the channel count. */
    void setUpCountSlider (juce::Slider&, const juce::String& parameterId);
    void syncCountSlider (juce::Slider&, int effectiveValue, int channels);

    TriPanAudioProcessor& owner;

    FieldComponent field;

    juce::Label sourcesLabel  { {}, "Sources" };
    juce::Label speakersLabel { {}, "Speakers" };
    juce::Slider sourceCount, speakerCount;
    juce::Label infoLabel;
    juce::Label modeLabel { {}, "Mode" };
    juce::ComboBox modeBox;
    juce::ToggleButton showTriangulationButton { "Show triangulation" };
    juce::TextButton resetButton { "Reset layout" };

    bool syncing = false;
    bool dragging = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriPanAudioProcessorEditor)
};
