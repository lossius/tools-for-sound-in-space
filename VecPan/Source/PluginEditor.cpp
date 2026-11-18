// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file PluginEditor.cpp
    @brief Creates the shared editor with VecPan's pad. */

#include "PluginEditor.h"

VecPanAudioProcessorEditor::VecPanAudioProcessorEditor (VecPanAudioProcessor& p)
    : SpatialEditor (p, std::make_unique<VecPanField> (p), "Show pairs",
                     "Draw a line between every two neighbouring speakers (seen from the listener) that VBAP pans between.")
{
}
