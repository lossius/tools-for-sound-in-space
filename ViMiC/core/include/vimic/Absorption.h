// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file Absorption.h
    @brief Absorption of the walls in three bands, surface presets, and the filters that give a reflection its colour (JUCE-free). */

#pragma once

#include "vimic/Room.h"

#include <array>
#include <span>
#include <string_view>

namespace vimic
{
/** How much a surface absorbs in a low, a middle and a high band, in percent of the energy (0.1 .. 99.9; 0.1 reflects nearly everything). */
struct Surface
{
    double low = 10.0;    ///< Absorption below the lower crossover, in percent.
    double mid = 20.0;    ///< Absorption between the crossovers, in percent.
    double high = 30.0;   ///< Absorption above the upper crossover, in percent.
};

/** The absorption of all the walls, and where the three bands meet. */
struct Walls
{
    std::array<Surface, WallCount> surface {};   ///< The absorption of each wall.
    double lowMidHz = 500.0;                     ///< The crossover between the low and the middle band in Hz.
    double midHighHz = 3500.0;                   ///< The crossover between the middle and the high band in Hz.
};

/** A named surface. */
struct SurfacePreset
{
    std::string_view name;   ///< The name, for example "carpet".
    Surface surface;         ///< The absorption in the three bands.
};

/** @return The surface presets: zero, full, acoustic tiles, audience, brick, carpet, thin carpet, drywall, grass, metal, sand and trees (absorption in the
    three bands as in the surface table of the ViMiC package), and concrete, glass, wood and plaster (textbook values for common materials). */
std::span<const SurfacePreset> surfacePresets() noexcept;

/** @return The preset with this name, or nullptr. */
const SurfacePreset* findSurfacePreset (std::string_view name) noexcept;

/** @return The pressure factor of a reflection off a surface with an absorption of `percent` of the energy: `sqrt (1 - percent / 100)` (the amplitude that is
    left after the reflection; the percentage is limited to 0 .. 99.9). */
double reflectionFactor (double percent) noexcept;

/** The gain of a wall sequence in each band: the product over the walls of their reflection factors, each to the power of how many times the path hits it. */
struct BandGains
{
    double low = 1.0;    ///< Gain in the low band.
    double mid = 1.0;    ///< Gain in the middle band.
    double high = 1.0;   ///< Gain in the high band.
};

/** @return The gains in the three bands of a path that hits the walls `hits` times each. */
BandGains bandGains (const std::array<int, WallCount>& hits, const Walls& walls) noexcept;
}
