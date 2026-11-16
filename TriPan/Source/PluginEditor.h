// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file PluginEditor.h
    @brief TriPan's editor window: the shared editor with TriPan's pad. */

#pragma once

#include "PluginProcessor.h"
#include "TriPanField.h"
#include "tlos/SpatialEditor.h"

/** TriPan's editor window. All the controls come from tlos::SpatialEditor; TriPan adds its pad (with the
    triangulation) and the "Show triangulation" toggle. */
class TriPanAudioProcessorEditor final : public tlos::SpatialEditor
{
public:
    /** Creates the editor for `processor`. */
    explicit TriPanAudioProcessorEditor (TriPanAudioProcessor& processor);

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriPanAudioProcessorEditor)
};
