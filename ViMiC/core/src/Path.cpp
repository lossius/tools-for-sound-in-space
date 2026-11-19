// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file Path.cpp
    @brief Distance law, time of flight and the combined gain of the direct path. */

#include "vimic/Path.h"
#include "vimic/Directivity.h"

#include <algorithm>
#include <cmath>

namespace vimic
{
Vec3 axisOf (double yaw, double pitch) noexcept
{
    const double horizontal = std::cos (pitch);
    return { -std::sin (yaw) * horizontal, std::cos (yaw) * horizontal, std::sin (pitch) };
}

double speedOfSound (double temperatureCelsius) noexcept
{
    const double t = std::isfinite (temperatureCelsius) ? std::clamp (temperatureCelsius, -40.0, 60.0) : 20.0;
    return 331.3 + 0.606 * t;
}

double distanceGain (double distance, const PathSettings& settings) noexcept
{
    const double d = std::max ({ std::isfinite (distance) ? distance : 0.0, settings.minDistance, 0.01 });

    if (settings.law == DistanceLaw::Exponential)
        return std::pow (10.0, -std::clamp (settings.dbPerMetre, 0.0, 60.0) * d / 20.0);

    return std::pow (d, -std::clamp (settings.power, 0.0, 9.0));
}

double sourceDirectivity (double pattern, int power, double cosAngle) noexcept
{
    return directivity (pattern, power, cosAngle, true);
}

PathResult computeDirectPath (tlos::Vec2 source, const Microphone& mic, const PathSettings& settings) noexcept
{
    return computeDirectPath (Source { source, 0.0, 1.0 }, mic, settings);
}

PathResult computeDirectPath (const Source& source, const Microphone& mic, const PathSettings& settings) noexcept
{
    PathResult result;

    if (! std::isfinite (source.position.x) || ! std::isfinite (source.position.y) || ! std::isfinite (mic.position.x) || ! std::isfinite (mic.position.y))
        return result;

    const double dx = source.position.x - mic.position.x;
    const double dy = source.position.y - mic.position.y;
    const double dz = source.height - mic.height;
    const double distance = std::sqrt (dx * dx + dy * dy + dz * dz);

    // The microphone hears the sound from the direction of the source: the angle from its axis to (dx, dy, dz). The source radiates towards the microphone, the opposite
    // direction: the angle from its axis to (-dx, -dy, -dz).
    const auto micAxis = axisOf (mic.yaw, mic.pitch), sourceAxis = axisOf (source.yaw, source.pitch);
    const double cosAngle = distance > 1e-9 ? (micAxis.x * dx + micAxis.y * dy + micAxis.z * dz) / distance : 1.0;
    const double cosRadiation = distance > 1e-9 ? -(sourceAxis.x * dx + sourceAxis.y * dy + sourceAxis.z * dz) / distance : 1.0;

    const double polarityFloor = settings.restrictPolarity ? 0.0 : -1.0;
    const double gain = distanceGain (distance, settings) * directivity (mic.pattern, settings.micPower, cosAngle, settings.restrictPolarity)
                      * sourceDirectivity (source.pattern, settings.sourcePower, cosRadiation);

    result.distance = distance;
    result.delaySeconds = distance / speedOfSound (settings.temperatureCelsius);
    result.gain = std::isfinite (gain) ? std::clamp (gain, polarityFloor, 1.0) : 0.0;
    return result;
}
}
