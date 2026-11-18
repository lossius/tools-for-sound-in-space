// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file Directivity.cpp
    @brief The microphone pattern. */

#include "vimic/Directivity.h"

#include <algorithm>
#include <cmath>

namespace vimic
{
double presetRatio (PatternPreset preset) noexcept
{
    switch (preset)
    {
        case PatternPreset::Omni:          return 1.0;
        case PatternPreset::Subcardioid:   return 0.7;
        case PatternPreset::Cardioid:      return 0.5;
        case PatternPreset::Supercardioid: return 1.0 / 3.0;
        case PatternPreset::Hypercardioid: return 0.3;
        case PatternPreset::Figure8:       return 0.0;
    }

    return 1.0;
}

double directivity (double ratio, int power, double cosAngle, bool restrictPolarity) noexcept
{
    if (! std::isfinite (ratio) || ! std::isfinite (cosAngle))
        return 0.0;

    const double a = std::clamp (ratio, 0.0, 1.0);
    const double base = a + (1.0 - a) * std::clamp (cosAngle, -1.0, 1.0);

    double value = 1.0;
    for (int i = 0; i < std::clamp (power, 0, 9); ++i)
        value *= base;

    return restrictPolarity ? std::max (value, 0.0) : value;
}
}
