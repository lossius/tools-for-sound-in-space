// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file PluginEditor.cpp
    @brief Creates the shared editor with ViMiC's pad and the settings of the model. */

#include "PluginEditor.h"
#include "ViMiCParameters.h"

#include <cmath>

ViMiCAudioProcessorEditor::ViMiCAudioProcessorEditor (ViMiCAudioProcessor& p)
    : SpatialEditor (p, std::make_unique<ViMiCField> (p), {}), vimicProcessor (p)
{
    namespace ids = vimic::params;
    auto& apvts = p.getValueTreeState();

    // Distance law.
    lawBox.addItem ("Inverse", 1);
    lawBox.addItem ("Exponential", 2);
    lawBox.setTooltip ("How the level falls with the distance between a source and a microphone. Inverse: 1 / d^q (q = 1 is the free field). "
                       "Exponential: a fixed number of dB per metre.");
    lawAttachment = std::make_unique<juce::ComboBoxParameterAttachment> (*apvts.getParameter (ids::kDistanceLawId), lawBox, nullptr);
    addSettingsRow ("Distance law", lawBox);

    addSliderRow ("Distance exponent", ids::kDistancePowerId, "", 2, "The exponent q of the inverse law: 1 is the free field; 2 falls faster, 0 gives no distance attenuation.");
    addSliderRow ("Decrease per metre", ids::kDistanceDbId, " dB", 1, "The exponential law: how many dB the level falls for every metre of distance.");
    addSliderRow ("Minimum distance", ids::kMinDistanceId, " m", 2, "Distances shorter than this count as this long, so that a source close to a microphone is never amplified.");

    addSliderRow ("Microphone exponent", ids::kMicPowerId, "", 0, "The exponent w of the microphone pattern: more than 1 makes every pattern artificially sharper.");
    polarityButton.setButtonText ("Restrict polarity");
    polarityButton.setTooltip ("Off: microphones with a rear lobe (figure-8, hypercardioid) give anti-phase signals for sound from behind. On: those are removed.");
    polarityAttachment = std::make_unique<juce::ButtonParameterAttachment> (*apvts.getParameter (ids::kMicPolarityId), polarityButton, nullptr);
    addSettingsRow ("Polarity", polarityButton);

    // Delay.
    delayBox.addItem ("Relative", 1);
    delayBox.addItem ("Absolute (Doppler)", 2);
    delayBox.setTooltip ("Relative: only the differences between the delays to the microphones are rendered (no latency to speak of, little Doppler). "
                         "Absolute: the true time of flight, so that a moving source gives a Doppler shift. Switching may click.");
    delayAttachment = std::make_unique<juce::ComboBoxParameterAttachment> (*apvts.getParameter (ids::kDelayModeId), delayBox, nullptr);
    addSettingsRow ("Delay", delayBox);
    addSliderRow ("Temperature", ids::kTemperatureId, " C", 1, "The temperature of the air, which sets the speed of sound and so the delays.");

    addSliderRow ("Source height", ids::kSourceHeightId, " m", 2, "The height of every source. It lengthens the paths to the microphones.");
    addSliderRow ("Microphone height", ids::kMicHeightId, " m", 2, "The height of every microphone.");
    addSliderRow ("Direct sound gain", ids::kDirectGainId, " dB", 1, "The gain of the direct sound.");
    addSliderRow ("Output gain", ids::kOutputGainId, " dB", 1, "The gain of all the microphone signals.");
}

void ViMiCAudioProcessorEditor::addSliderRow (const juce::String& caption, const juce::String& parameterId, const juce::String& suffix, int decimals, const juce::String& tooltip)
{
    auto slider = std::make_unique<juce::Slider>();
    slider->setSliderStyle (juce::Slider::LinearHorizontal);
    slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 22);
    slider->setTooltip (tooltip);
    addSettingsRow (caption, *slider);

    auto* parameter = vimicProcessor.getValueTreeState().getParameter (parameterId);
    jassert (parameter != nullptr);
    sliderAttachments.push_back (std::make_unique<juce::SliderParameterAttachment> (*parameter, *slider, nullptr));

    // After the attachment, which installs its own text functions: the unit and the decimals.
    slider->textFromValueFunction = [suffix, decimals] (double v)
    {
        const double shown = std::abs (v) < 0.5 * std::pow (10.0, -decimals) ? 0.0 : v;   // no "-0.0"
        return juce::String (shown, decimals) + suffix;
    };
    slider->valueFromTextFunction = [] (const juce::String& text) { return text.getDoubleValue(); };
    slider->updateText();

    sliderIds.push_back (parameterId);
    sliders.push_back (std::move (slider));
}

juce::Slider* ViMiCAudioProcessorEditor::getSlider (const juce::String& parameterId) noexcept
{
    for (size_t i = 0; i < sliderIds.size(); ++i)
        if (sliderIds[i] == parameterId)
            return sliders[i].get();

    return nullptr;
}
