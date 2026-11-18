// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file PluginEditor.cpp
    @brief Creates the shared editor with VecPan's pad. */

#include "PluginEditor.h"
#include "VecPanParameters.h"

VecPanAudioProcessorEditor::VecPanAudioProcessorEditor (VecPanAudioProcessor& p)
    : SpatialEditor (p, std::make_unique<VecPanField> (p), "Show pairs",
                     "Draw a line between every two neighbouring speakers (seen from the listener) that VBAP pans between.")
{
    centreSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    centreSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 22);
    centreSlider.setTooltip ("Within this distance of the listener, a source is blended towards an even share over all speakers, "
                             "so that a source moving through the centre does not jump from one speaker pair to another. "
                             "0 = off: only a source exactly at the listener is shared equally.");
    addSettingsRow ("Centre radius", centreSlider);

    const auto action = p.getSpeakerAction();
    circleButton.setButtonText (action->title);
    circleButton.setTooltip (action->tooltip);
    circleButton.onClick = action->run;
    addSettingsRow ("Speakers", circleButton);

    auto* parameter = p.getValueTreeState().getParameter (vecpan::params::kCentreRadiusId);
    jassert (parameter != nullptr);
    centreAttachment = std::make_unique<juce::SliderParameterAttachment> (*parameter, centreSlider, nullptr);

    // After the attachment, which installs its own text functions: two decimals and the unit.
    centreSlider.textFromValueFunction = [] (double v) { return juce::String (v, 2) + " m"; };
    centreSlider.valueFromTextFunction = [] (const juce::String& text) { return text.getDoubleValue(); };
    centreSlider.updateText();
}
