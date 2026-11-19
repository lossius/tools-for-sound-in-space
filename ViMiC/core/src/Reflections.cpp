// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file Reflections.cpp
    @brief The gain and delay of an image path. */

#include "vimic/Reflections.h"
#include "vimic/Directivity.h"

#include <algorithm>
#include <cmath>

namespace vimic
{
PathResult computeImagePath (const Source& source, const Microphone& mic, const Image& image, const PathSettings& settings, const RoomSettings& room) noexcept
{
    if (image.order == 0)
        return computeDirectPath (source, mic, settings);

    PathResult result;

    if (! std::isfinite (source.position.x) || ! std::isfinite (source.position.y) || ! std::isfinite (mic.position.x) || ! std::isfinite (mic.position.y))
        return result;

    const Vec3 s { source.position.x, source.position.y, source.height };
    const Vec3 m { mic.position.x, mic.position.y, mic.height };

    // The unfolded path: from the image of the source to the microphone.
    const auto s2 = imagePosition (s, room.room, image);
    const double dx = s2.x - m.x, dy = s2.y - m.y, dz = s2.z - m.z;
    const double distance = std::sqrt (dx * dx + dy * dy + dz * dz);
    if (! (distance > 1e-9))
        return result;

    // The microphone hears the sound arrive from the image source: the angle from its axis to that direction.
    const auto micAxis = axisOf (mic.yaw, mic.pitch), sourceAxis = axisOf (source.yaw, source.pitch);
    const double cosMic = (micAxis.x * dx + micAxis.y * dy + micAxis.z * dz) / distance;

    // The sound left the source towards the image of the microphone for the same walls.
    const auto m2 = micImagePosition (m, room.room, image);
    const double ex = m2.x - s.x, ey = m2.y - s.y, ez = m2.z - s.z;
    const double leg = std::sqrt (ex * ex + ey * ey + ez * ez);
    const double cosSource = leg > 1e-9 ? (sourceAxis.x * ex + sourceAxis.y * ey + sourceAxis.z * ez) / leg : 1.0;

    const double orderGain = image.order == 1 ? room.gainFirst : room.gainSecond;
    const double fade = insideFactor (s, room.room) * insideFactor (m, room.room);
    const double polarityFloor = settings.restrictPolarity ? 0.0 : -1.0;
    double gain = distanceGain (distance, settings) * directivity (mic.pattern, settings.micPower, cosMic, settings.restrictPolarity)
                * sourceDirectivity (source.pattern, settings.sourcePower, cosSource) * orderGain * fade;
    gain = std::isfinite (gain) ? std::clamp (gain, polarityFloor, 1.0) : 0.0;

    result.distance = distance;
    result.delaySeconds = distance / speedOfSound (settings.temperatureCelsius);
    result.gain = std::abs (gain) < room.threshold ? 0.0 : gain;
    return result;
}
}
