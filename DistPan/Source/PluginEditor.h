// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file PluginEditor.h
    @brief DistPan's editor window: the shared editor with DistPan's pad and the rolloff and blur sliders. */

#pragma once

#include "DistPanField.h"
#include "PluginProcessor.h"
#include "tlos/SpatialEditor.h"

/** DistPan's editor window. All the controls of the family come from tlos::SpatialEditor; DistPan adds its
    pad (with the convex hull of the speakers, switched by "Show hull") and two sliders (rolloff and blur), which are in the settings window under Panning. */
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

private:
    /** Sets up a slider, attaches it to the parameter and adds it to the settings window with its caption. */
    void setUpSlider (juce::Slider& slider, const juce::String& caption, const juce::String& suffix,
                      const juce::String& tooltip, std::unique_ptr<juce::SliderParameterAttachment>& attachment,
                      const juce::String& parameterId);

    DistPanAudioProcessor& dbapProcessor; ///< The processor this editor belongs to.

    juce::Slider rolloffSlider;                 ///< Rolloff in dB per doubling of the distance.
    juce::Slider blurSlider;                    ///< Blur in metres.

    std::unique_ptr<juce::SliderParameterAttachment> rolloffAttachment; ///< Ties the rolloff slider to its parameter.
    std::unique_ptr<juce::SliderParameterAttachment> blurAttachment;    ///< Ties the blur slider to its parameter.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DistPanAudioProcessorEditor)
};
