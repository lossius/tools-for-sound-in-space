// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file PluginEditor.h
    @brief VecPan's editor window: the shared editor with VecPan's pad. */

#pragma once

#include "PluginProcessor.h"
#include "VecPanField.h"
#include "tlos/SpatialEditor.h"

/** VecPan's editor window. All the controls of the family come from tlos::SpatialEditor; VecPan adds its pad, which
    can show the speaker pairs it pans between, and has no controls of its own. */
class VecPanAudioProcessorEditor final : public tlos::SpatialEditor
{
public:
    /** Creates the editor for `processor`. */
    explicit VecPanAudioProcessorEditor (VecPanAudioProcessor& processor);

    /// @name Access to the controls (used by the tests)
    /// @{
    juce::Slider& getCentreRadiusSlider() noexcept { return centreSlider; }   ///< The centre radius slider.
    /// @}

private:
    juce::Slider centreSlider;                                              ///< Centre radius in metres.
    std::unique_ptr<juce::SliderParameterAttachment> centreAttachment;      ///< Ties the slider to its parameter.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VecPanAudioProcessorEditor)
};
