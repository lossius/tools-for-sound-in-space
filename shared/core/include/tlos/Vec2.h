// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file Vec2.h
    @brief Points in the panning plane and virtual speakers (JUCE-free). */

#pragma once

namespace tlos
{
/** A point in the panning plane, in metres. +y is the front, +x the right. */
struct Vec2
{
    double x = 0.0; ///< Horizontal position (positive = right).
    double y = 0.0; ///< Depth position (positive = front).
};

/** A virtual (imaginary) speaker with a strength between 0 and 1. A strength below 1 means the
    layout only partly calls for this speaker. Plugins that do not use virtual speakers never see one. */
struct VirtualSpeaker
{
    Vec2 position;           ///< Position in metres.
    double strength = 1.0;   ///< 0 (not called for) to 1 (fully part of the layout).
};
}
