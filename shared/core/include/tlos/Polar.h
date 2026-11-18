// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file Polar.h
    @brief Conversion between the plane's Cartesian coordinates and azimuth / distance (JUCE-free). */

#pragma once

#include "tlos/Vec2.h"

#include <cmath>

namespace tlos
{
/** A position as seen from the listener (the origin): azimuth in degrees and distance in metres. The azimuth is
    counter-clockwise from the front (+y): 0 = front, +90 = left, -90 = right, +-180 = back. This is the
    convention of ADM-OSC, which the OSC interface uses as well. */
struct Polar
{
    double azimuthDegrees = 0.0;   ///< Azimuth in degrees, -180 (exclusive) to +180 (inclusive).
    double distance = 0.0;         ///< Distance from the origin in metres, 0 or more.
};

/** Wraps an angle in degrees to -180 (exclusive) .. +180 (inclusive). Non-finite angles give 0. */
inline double wrapAzimuth (double degrees) noexcept
{
    if (! std::isfinite (degrees))
        return 0.0;

    double wrapped = std::fmod (degrees, 360.0);
    if (wrapped > 180.0)
        wrapped -= 360.0;
    else if (wrapped <= -180.0)
        wrapped += 360.0;

    return wrapped;
}

/** @return The azimuth and distance of a point. At the origin the azimuth is not defined and is returned as
    `azimuthAtOrigin` (callers keep the last one they showed, so that editing the distance of a source at the
    centre has a direction to work with). @param p  The point in metres. @param azimuthAtOrigin  See above. */
inline Polar toPolar (Vec2 p, double azimuthAtOrigin = 0.0) noexcept
{
    constexpr double kPi = 3.14159265358979323846;
    const double distance = std::hypot (p.x, p.y);

    if (distance < 1e-9)
        return { wrapAzimuth (azimuthAtOrigin), 0.0 };

    return { wrapAzimuth (std::atan2 (-p.x, p.y) * 180.0 / kPi), distance };
}

/** @return The point at the given azimuth (degrees, counter-clockwise from the front) and distance (metres).
    A negative distance is taken as 0. Non-finite input gives the origin. */
inline Vec2 fromPolar (double azimuthDegrees, double distance) noexcept
{
    constexpr double kPi = 3.14159265358979323846;

    if (! std::isfinite (azimuthDegrees) || ! std::isfinite (distance) || distance <= 0.0)
        return {};

    const double a = azimuthDegrees * kPi / 180.0;
    return { -distance * std::sin (a), distance * std::cos (a) };
}

/** @return The point for a Polar value (see the two-argument overload). @param p  Azimuth and distance. */
inline Vec2 fromPolar (Polar p) noexcept
{
    return fromPolar (p.azimuthDegrees, p.distance);
}
}
