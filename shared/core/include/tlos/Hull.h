// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file Hull.h
    @brief The convex hull of the speakers, and the level falloff for sources outside it (JUCE-free). */

#pragma once

#include "tlos/Vec2.h"

#include <span>
#include <vector>

namespace tlos
{
/** The convex hull of a set of speakers: how far a point is outside the layout.

    Immutable once built. Building allocates (do it off the audio thread); distance() does not. */
class Hull
{
public:
    /** Creates an empty hull (no area: distance() is always 0). */
    Hull() = default;

    /** Builds the hull of the finite points (see convexHull()). Allocates. */
    explicit Hull (std::span<const Vec2> points);

    /** @return Whether the hull encloses an area: at least three corners. With fewer points, or points on one
        line, there is no "inside" to leave, and the falloff outside the layout does not apply. */
    bool hasArea() const noexcept { return corners.size() >= 3; }

    /** @return The corners, counter-clockwise (fewer than three if the points have no area). */
    std::span<const Vec2> getCorners() const noexcept { return corners; }

    /** @return The distance in metres from `p` to the hull: 0 inside and on the boundary, otherwise the
        distance to the nearest point of the boundary. Always 0 if the hull has no area or `p` is not finite.
        Real-time safe. */
    double distance (Vec2 p) const noexcept;

private:
    std::vector<Vec2> corners; ///< Corners of the hull, counter-clockwise.
};

/** The distance in metres outside the hull at which the level has dropped by `rolloffDb` (the reference distance). */
inline constexpr double kOutsideReferenceDistance = 1.0;

/** The level factor for a source `distanceOutside` metres outside the speaker layout:
    (1 + distanceOutside / kOutsideReferenceDistance) ^ (-rolloffDb / 6.0206). It is 1 at distance 0 (inside the
    layout and on its boundary), so there is no jump at the boundary, and it falls steadily and smoothly
    outside: each doubling of (1 + distance) costs `rolloffDb` dB. Non-finite or negative distances and a
    rolloff of 0 or less (or not finite) give 1. Real-time safe.
    @param distanceOutside  The distance from Hull::distance().
    @param rolloffDb        The level drop in dB for each doubling of (1 + distance / reference distance). */
double outsideFactor (double distanceOutside, double rolloffDb) noexcept;
}
