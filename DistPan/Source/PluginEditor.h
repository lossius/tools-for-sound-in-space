// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file PluginEditor.h
    @brief DistPan's editor window: the shared editor with DistPan's pad and the rolloff and blur sliders. */

#pragma once

#include "DistPanField.h"
#include "PluginProcessor.h"
#include "tlos/SpatialEditor.h"

/** DistPan's editor window. All the controls of the family come from tlos::SpatialEditor; DistPan adds its
    pad and two sliders (rolloff and blur) in the row below the display options. */
class DistPanAudioProcessorEditor final : public tlos::SpatialEditor
{
public:
    /** Creates the editor for `processor`. */
    explicit DistPanAudioProcessorEditor (DistPanAudioProcessor& processor);

    /// @name Access to the controls (used by the tests)
    /// @{
    juce::Slider& getRolloffSlider() noexcept { return rolloffSlider; } ///< The rolloff slider.
    juce::Slider& getBlurSlider() noexcept { return blurSlider; }       ///< The blur slider.
    /// @}

protected:
    /** Places the two sliders at the left of the status row. */
    void layoutExtraControls (juce::Rectangle<int>& row) override;

private:
    /** Sets up a slider with its caption and attaches it to the parameter. */
    void setUpSlider (juce::Slider& slider, juce::Label& label, const juce::String& caption, const juce::String& suffix,
                      const juce::String& tooltip, std::unique_ptr<juce::SliderParameterAttachment>& attachment,
                      const juce::String& parameterId);

    DistPanAudioProcessor& dbapProcessor; ///< The processor this editor belongs to.

    juce::Label rolloffLabel { {}, "Rolloff" }; ///< Caption of the rolloff slider.
    juce::Label blurLabel { {}, "Blur" };       ///< Caption of the blur slider.
    juce::Slider rolloffSlider;                 ///< Rolloff in dB per doubling of the distance.
    juce::Slider blurSlider;                    ///< Blur in metres.

    std::unique_ptr<juce::SliderParameterAttachment> rolloffAttachment; ///< Ties the rolloff slider to its parameter.
    std::unique_ptr<juce::SliderParameterAttachment> blurAttachment;    ///< Ties the blur slider to its parameter.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DistPanAudioProcessorEditor)
};
