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

    circleButton.setTooltip ("VBAP uses only the direction of a speaker. This moves every speaker that takes part to the reference circle (the distance "
                             "of the farthest one, dashed on the pad), keeping its direction, so that all speakers are at the same distance. "
                             "One undo step. Beyond that circle the level of a source falls off (Outside rolloff).");
    circleButton.onClick = [&p] { p.placeSpeakersOnCircle(); };
    addSettingsRow ("Speakers", circleButton);

    auto* parameter = p.getValueTreeState().getParameter (vecpan::params::kCentreRadiusId);
    jassert (parameter != nullptr);
    centreAttachment = std::make_unique<juce::SliderParameterAttachment> (*parameter, centreSlider, nullptr);

    // After the attachment, which installs its own text functions: two decimals and the unit.
    centreSlider.textFromValueFunction = [] (double v) { return juce::String (v, 2) + " m"; };
    centreSlider.valueFromTextFunction = [] (const juce::String& text) { return text.getDoubleValue(); };
    centreSlider.updateText();
}
