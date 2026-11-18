// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file Directivity.h
    @brief The directivity pattern of a virtual microphone (JUCE-free). */

#pragma once

namespace vimic
{
/** The common microphone characteristics, as values of the ratio `a` of directivity(). */
enum class PatternPreset
{
    Omni,            ///< a = 1.
    Subcardioid,     ///< a = 0.7.
    Cardioid,        ///< a = 0.5.
    Supercardioid,   ///< a = 1/3.
    Hypercardioid,   ///< a = 0.3.
    Figure8          ///< a = 0.
};

/** @return The ratio `a` of a preset (see the table in the ViMiC manual: 1, 0.7, 0.5, 0.33, 0.3, 0). */
double presetRatio (PatternPreset preset) noexcept;

/** The directivity of a virtual microphone for a sound arriving at the angle delta from its axis:
    `(a + (1 - a) cos(delta)) ^ w`.

    The pattern has no frequency dependence, as in ViMiC. A larger exponent w makes it artificially sharper; with an even w the result is never
    negative. With the polarity restricted, negative values (the rear lobe of a figure-8 or a hypercardioid, anti-phase) are set to 0.

    @param ratio             `a`, limited to 0..1 (1 omni, 0.5 cardioid, 0 figure-8).
    @param power             `w`, an integer 0..9 (limited to that range).
    @param cosAngle          cos(delta), limited to -1..1.
    @param restrictPolarity  Whether negative values are set to 0.
    @return                  A gain between -1 and 1 (0 and 1 with the polarity restricted); 0 for a value that is not finite. */
double directivity (double ratio, int power, double cosAngle, bool restrictPolarity) noexcept;
}
