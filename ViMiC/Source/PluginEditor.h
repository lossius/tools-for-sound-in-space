// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file PluginEditor.h
    @brief ViMiC's editor window: the shared editor with ViMiC's pad and the settings of the model. */

#pragma once

#include "AbsorptionPanel.h"
#include "PluginProcessor.h"
#include "ViMiCField.h"
#include "tlos/SpatialEditor.h"

#include <memory>
#include <vector>

/** ViMiC's editor window. All the controls of the family come from tlos::SpatialEditor; ViMiC adds its pad (with the pickup lobes) and, in the settings
    window under Panning, the controls of the model: distance law, exponents, minimum distance, polarity, delay mode, temperature, heights and gains. */
class ViMiCAudioProcessorEditor final : public tlos::SpatialEditor
{
public:
    /** Creates the editor for `processor`. */
    explicit ViMiCAudioProcessorEditor (ViMiCAudioProcessor& processor);

    /// @name Access to the controls (used by the tests)
    /// @{
    juce::ComboBox& getDistanceLawBox() noexcept { return lawBox; }              ///< Inverse / Exponential.
    juce::ComboBox& getDelayModeBox() noexcept { return delayBox; }              ///< Relative / Absolute.
    juce::ToggleButton& getPolarityButton() noexcept { return polarityButton; }  ///< Restrict the polarity.
    juce::ComboBox& getReflectionBox() noexcept { return reflectionBox; }        ///< Off / 1st order / 2nd order.
    AbsorptionPanel& getAbsorptionPanel() noexcept { return absorptionPanel; }   ///< The absorption of the walls.
    /** @return The slider of the parameter with this ID (see vimic::params), or nullptr. */
    juce::Slider* getSlider (const juce::String& parameterId) noexcept;
    /// @}

private:
    /** Adds a slider row for a parameter, with its text in the given unit and number of decimals. */
    void addSliderRow (const juce::String& caption, const juce::String& parameterId, const juce::String& suffix, int decimals, const juce::String& tooltip);

    ViMiCAudioProcessor& vimicProcessor;       ///< The processor this editor belongs to.

    juce::ComboBox lawBox;                     ///< Distance law.
    juce::ComboBox delayBox;                   ///< Delay mode.
    juce::ToggleButton polarityButton;         ///< Restrict polarity.
    juce::ComboBox reflectionBox;              ///< The reflection order.
    AbsorptionPanel absorptionPanel;           ///< The absorption of the walls.
    std::vector<std::unique_ptr<juce::Slider>> sliders;                                  ///< The slider rows.
    std::vector<juce::String> sliderIds;                                                 ///< The parameter ID of each slider.
    std::vector<std::unique_ptr<juce::SliderParameterAttachment>> sliderAttachments;     ///< Tie the sliders to their parameters.
    std::unique_ptr<juce::ComboBoxParameterAttachment> lawAttachment;     ///< Ties the distance law menu to its parameter.
    std::unique_ptr<juce::ComboBoxParameterAttachment> reflectionAttachment;   ///< Ties the reflection order menu to its parameter.
    std::unique_ptr<juce::ComboBoxParameterAttachment> delayAttachment;   ///< Ties the delay mode menu to its parameter.
    std::unique_ptr<juce::ButtonParameterAttachment> polarityAttachment;                 ///< Ties the toggle to its parameter.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ViMiCAudioProcessorEditor)
};
