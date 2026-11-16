// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file Hull.cpp
    @brief Distance to the convex hull and the level falloff outside it. */

#include "tlos/Hull.h"
#include "tlos/Groups.h"

#include <algorithm>
#include <cmath>

namespace tlos
{
Hull::Hull (std::span<const Vec2> points)
    : corners (convexHull (std::vector<Vec2> (points.begin(), points.end())))
{
}

double Hull::distance (Vec2 p) const noexcept
{
    if (! hasArea() || ! std::isfinite (p.x) || ! std::isfinite (p.y))
        return 0.0;

    // Inside (or on the boundary) of a counter-clockwise convex polygon: never to the right of an edge.
    bool inside = true;
    double nearest = 1e300;

    for (size_t i = 0; i < corners.size(); ++i)
    {
        const Vec2 a = corners[i], b = corners[(i + 1) % corners.size()];
        const double ex = b.x - a.x, ey = b.y - a.y;

        if (ex * (p.y - a.y) - ey * (p.x - a.x) < 0.0)
            inside = false;

        const double len2 = ex * ex + ey * ey;
        const double t = len2 > 0.0 ? std::clamp (((p.x - a.x) * ex + (p.y - a.y) * ey) / len2, 0.0, 1.0) : 0.0;
        const double dx = a.x + t * ex - p.x, dy = a.y + t * ey - p.y;
        nearest = std::min (nearest, dx * dx + dy * dy);
    }

    return inside ? 0.0 : std::sqrt (nearest);
}

double outsideFactor (double distanceOutside, double rolloffDb) noexcept
{
    if (! (distanceOutside > 0.0) || ! std::isfinite (distanceOutside) || ! (rolloffDb > 0.0) || ! std::isfinite (rolloffDb))
        return 1.0;

    const double exponent = rolloffDb / (20.0 * std::log10 (2.0));
    return std::pow (1.0 + distanceOutside / kOutsideReferenceDistance, -exponent);
}
}
