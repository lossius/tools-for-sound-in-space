// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file PluginEditor.cpp
    @brief Creates the shared editor with DistPan's pad and the rolloff and blur sliders. */

#include "PluginEditor.h"
#include "DistPanParameters.h"

DistPanAudioProcessorEditor::DistPanAudioProcessorEditor (DistPanAudioProcessor& p)
    : SpatialEditor (p, std::make_unique<DistPanField> (p), {}, {}, 1), dbapProcessor (p)
{
    setUpSlider (rolloffSlider, rolloffLabel, "Rolloff", " dB",
                 "How fast a speaker's share falls with distance: the level drop in dB for each doubling of the distance "
                 "between the source and the speaker. 6 dB is the free-field value. More makes the sound more local.",
                 rolloffAttachment, distpan::params::kRolloffId);
    setUpSlider (blurSlider, blurLabel, "Blur", " m",
                 "Blurs the distance so that a source right on a speaker does not use that speaker alone, and the sound "
                 "moves smoothly when a source passes a speaker. More spreads the sound more evenly.",
                 blurAttachment, distpan::params::kBlurId);

    relayout(); // the controls above did not exist when the base class laid out the window
}

void DistPanAudioProcessorEditor::setUpSlider (juce::Slider& slider, juce::Label& label, const juce::String& caption,
                                               const juce::String& suffix, const juce::String& tooltip,
                                               std::unique_ptr<juce::SliderParameterAttachment>& attachment,
                                               const juce::String& parameterId)
{
    label.setText (caption, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centredRight);
    label.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.85f));
    addToContent (label);

    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 22);
    slider.setTooltip (tooltip);
    addToContent (slider);

    auto* parameter = dbapProcessor.getValueTreeState().getParameter (parameterId);
    jassert (parameter != nullptr);
    attachment = std::make_unique<juce::SliderParameterAttachment> (*parameter, slider, nullptr);

    // After the attachment, which installs its own text functions: two decimals and the unit.
    slider.textFromValueFunction = [suffix] (double v) { return juce::String (v, 2) + suffix; };
    slider.valueFromTextFunction = [] (const juce::String& text) { return text.getDoubleValue(); };
    slider.updateText();
}

void DistPanAudioProcessorEditor::layoutExtraControls (juce::Rectangle<int>& row)
{
    rolloffLabel.setBounds (row.removeFromLeft (52));
    rolloffSlider.setBounds (row.removeFromLeft (200));
    row.removeFromLeft (10);
    blurLabel.setBounds (row.removeFromLeft (34));
    blurSlider.setBounds (row.removeFromLeft (170));
    row.removeFromLeft (10);
}
