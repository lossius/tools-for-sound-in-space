// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file Absorption.cpp
    @brief Surface presets and the gains of wall sequences. */

#include "vimic/Absorption.h"

#include <algorithm>
#include <cmath>

namespace vimic
{
namespace
{
/** The presets. The first twelve follow the surface table of the ViMiC package (VimicSurfaceProperties.txt, New BSD licence); the others are
    typical textbook absorption coefficients (125-250 Hz, 500-2000 Hz, 4 kHz and up). */
const SurfacePreset kPresets[] {
    { "zero",           {  0.1,  0.1,  0.1 } },
    { "full",           { 99.9, 99.9, 99.9 } },
    { "acoustic tiles", { 40.0, 58.0, 53.0 } },
    { "audience",       { 40.0, 98.0, 91.5 } },
    { "brick",          {  2.0,  4.0,  5.0 } },
    { "carpet",         {  6.0, 37.0, 63.0 } },
    { "thin carpet",    {  5.0, 20.0, 44.0 } },
    { "drywall",        { 19.0, 50.0, 68.0 } },
    { "grass",          { 40.0, 35.0, 32.0 } },
    { "metal",          { 76.0, 95.0, 77.0 } },
    { "sand",           { 35.0, 50.0, 67.5 } },
    { "trees",          { 54.0, 47.0, 43.0 } },
    { "concrete",       {  1.0,  2.0,  3.0 } },
    { "glass",          { 18.0,  4.0,  2.0 } },
    { "wood",           { 15.0, 10.0,  8.0 } },
    { "plaster",        {  2.0,  3.0,  4.0 } },
};
}

std::span<const SurfacePreset> surfacePresets() noexcept
{
    return kPresets;
}

const SurfacePreset* findSurfacePreset (std::string_view name) noexcept
{
    for (const auto& preset : kPresets)
        if (preset.name == name)
            return &preset;

    return nullptr;
}

double reflectionFactor (double percent) noexcept
{
    const double p = std::isfinite (percent) ? std::clamp (percent, 0.0, 99.9) : 0.0;
    return std::sqrt (1.0 - 0.01 * p);
}

BandGains bandGains (const std::array<int, WallCount>& hits, const Walls& walls) noexcept
{
    BandGains gains;
    for (int w = 0; w < WallCount; ++w)
        if (hits[(size_t) w] > 0)
        {
            const auto& s = walls.surface[(size_t) w];
            gains.low *= std::pow (reflectionFactor (s.low), hits[(size_t) w]);
            gains.mid *= std::pow (reflectionFactor (s.mid), hits[(size_t) w]);
            gains.high *= std::pow (reflectionFactor (s.high), hits[(size_t) w]);
        }

    return gains;
}
}
