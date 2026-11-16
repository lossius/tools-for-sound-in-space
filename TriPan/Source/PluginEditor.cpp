// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file PluginEditor.cpp
    @brief Creates the shared editor with TriPan's pad. */

#include "PluginEditor.h"

TriPanAudioProcessorEditor::TriPanAudioProcessorEditor (TriPanAudioProcessor& p)
    : SpatialEditor (p, std::make_unique<TriPanField> (p), "Show triangulation",
                     "Draw the Delaunay triangles of the speakers (and the virtual speakers) on the pad.")
{
}
