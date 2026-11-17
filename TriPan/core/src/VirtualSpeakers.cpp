// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file VirtualSpeakers.cpp
    @brief Automatic detection of where virtual speakers are useful (gap fillers and ring centre). */

#include "tripan/VirtualSpeakers.h"
#include "tlos/Groups.h"
#include "tripan/Triangulation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <numbers>
#include <utility>

namespace tripan
{
namespace
{
/** 0 below `lo`, 1 above `hi`, smooth in between (zero slope at both ends). */
double smoothstep (double x, double lo, double hi) noexcept
{
    if (hi <= lo)
        return x >= hi ? 1.0 : 0.0;

    const double t = std::clamp ((x - lo) / (hi - lo), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

/** The circle through three points. `valid` is false if they are (nearly) collinear. */
struct Circle
{
    Vec2 centre;          ///< Centre of the circle.
    double radius = 0.0;  ///< Radius of the circle.
    bool valid = false;   ///< False for collinear points.
};

/** @return The circumcircle of a, b and c. */
Circle circumcircle (Vec2 a, Vec2 b, Vec2 c) noexcept
{
    const double bx = b.x - a.x, by = b.y - a.y, cx = c.x - a.x, cy = c.y - a.y;
    const double d = 2.0 * (bx * cy - by * cx);
    const double scale = bx * bx + by * by + cx * cx + cy * cy;

    if (! (std::abs (d) > 1e-12 * scale))
        return {};

    const double b2 = bx * bx + by * by, c2 = cx * cx + cy * cy;
    const double ux = (cy * b2 - by * c2) / d, uy = (bx * c2 - cx * b2) / d;
    return { { a.x + ux, a.y + uy }, std::hypot (ux, uy), true };
}

/** How far the fourth point of two triangles that share an edge is from the circle through the other three
    (the larger of the two ways to measure it), as a fraction of that circle's radius. 0 = cocircular. */
double cocircularDeviation (Vec2 a, Vec2 b, Vec2 c, Vec2 d) noexcept
{
    const auto first = circumcircle (a, b, c);
    const auto second = circumcircle (a, b, d);
    if (! first.valid || ! second.valid)
        return 1e9;

    const double d1 = std::abs (std::hypot (d.x - first.centre.x, d.y - first.centre.y) - first.radius) / first.radius;
    const double d2 = std::abs (std::hypot (c.x - second.centre.x, c.y - second.centre.y) - second.radius) / second.radius;
    return std::max (d1, d2);
}

/** Finds the faces made of (nearly) cocircular speakers and where to put a virtual speaker in each (see
    findVirtualSpeakers(), item 3). */
std::vector<VirtualSpeaker> findFaceCentres (std::span<const Vec2> speakers, const VirtualSpeakerOptions& o)
{
    constexpr double minStrength = 1e-6;

    const Triangulation tri (speakers);
    const auto triangles = tri.triangles();
    const auto points = tri.speakers();
    if (triangles.size() < 2)
        return {};

    // Every interior edge with the two triangles on its sides and their third corners.
    struct Side
    {
        int triangle;   ///< Index of the triangle.
        int third;      ///< The corner that is not on the edge.
    };
    std::map<std::pair<int, int>, std::vector<Side>> sides;
    for (size_t t = 0; t < triangles.size(); ++t)
    {
        const int corner[3] = { triangles[t].a, triangles[t].b, triangles[t].c };
        for (int k = 0; k < 3; ++k)
        {
            const int p = corner[k], q = corner[(k + 1) % 3], third = corner[(k + 2) % 3];
            sides[{ std::min (p, q), std::max (p, q) }].push_back ({ (int) t, third });
        }
    }

    // Edges between triangles that form a face, and the faces: triangles joined through such edges.
    struct Joint
    {
        int first;         ///< One triangle.
        int second;        ///< The other triangle.
        double strength;   ///< 1 if the four corners are cocircular, falling to 0 at faceZeroDeviation.
    };
    std::vector<Joint> joints;
    std::vector<int> parent (triangles.size());
    for (size_t i = 0; i < parent.size(); ++i)
        parent[i] = (int) i;
    const auto root = [&] (int i)
    {
        while (parent[(size_t) i] != i)
            i = parent[(size_t) i] = parent[(size_t) parent[(size_t) i]];
        return i;
    };

    for (const auto& [edge, list] : sides)
    {
        if (list.size() != 2)
            continue;

        const double deviation = cocircularDeviation (points[(size_t) edge.first], points[(size_t) edge.second],
                                                     points[(size_t) list[0].third], points[(size_t) list[1].third]);
        const double strength = 1.0 - smoothstep (deviation, o.faceFullDeviation, o.faceZeroDeviation);
        joints.push_back ({ list[0].triangle, list[1].triangle, strength });
        if (strength > minStrength)
            parent[(size_t) root (list[0].triangle)] = root (list[1].triangle);
    }

    // One face per group of triangles; its strength is that of its weakest joint.
    std::map<int, std::vector<int>> faces;
    for (size_t t = 0; t < triangles.size(); ++t)
        faces[root ((int) t)].push_back ((int) t);

    std::vector<VirtualSpeaker> result;
    for (const auto& [id, members] : faces)
    {
        if (members.size() < 2)
            continue;

        double strength = 1.0;
        for (const auto& j : joints)
            if (root (j.first) == id && root (j.second) == id)
                strength = std::min (strength, j.strength);

        if (strength <= minStrength)
            continue;

        std::vector<int> corners;
        Vec2 centroid;
        Vec2 centre;
        int circles = 0;
        for (const int t : members)
        {
            const int c[3] = { triangles[(size_t) t].a, triangles[(size_t) t].b, triangles[(size_t) t].c };
            for (const int v : c)
                if (std::find (corners.begin(), corners.end(), v) == corners.end())
                    corners.push_back (v);

            if (const auto circle = circumcircle (points[(size_t) c[0]], points[(size_t) c[1]], points[(size_t) c[2]]); circle.valid)
            {
                centre.x += circle.centre.x;
                centre.y += circle.centre.y;
                ++circles;
            }
        }

        std::vector<Vec2> positions;
        for (const int v : corners)
        {
            positions.push_back (points[(size_t) v]);
            centroid.x += points[(size_t) v].x;
            centroid.y += points[(size_t) v].y;
        }
        centroid.x /= (double) corners.size();
        centroid.y /= (double) corners.size();

        // The point: the centre of the circle if it lies well inside the face, the average of the corners if it
        // lies outside (a face that is only an arc), and a smooth blend of the two in between.
        Vec2 point = centroid;
        if (circles > 0)
        {
            centre.x /= (double) circles;
            centre.y /= (double) circles;

            double size = 0.0;
            for (const auto& p : positions)
                for (const auto& q : positions)
                    size = std::max (size, std::hypot (p.x - q.x, p.y - q.y));

            const auto hull = tlos::convexHull (positions);
            double inside = 1e300;
            for (size_t i = 0; i < hull.size(); ++i)
            {
                const auto a = hull[i], b = hull[(i + 1) % hull.size()];
                const double length = std::hypot (b.x - a.x, b.y - a.y);
                if (length > 0.0)
                    inside = std::min (inside, ((b.x - a.x) * (centre.y - a.y) - (b.y - a.y) * (centre.x - a.x)) / length);
            }

            const double w = size > 0.0 && hull.size() >= 3 ? smoothstep (inside / size, 0.0, 0.1) : 0.0;
            point = { centroid.x + w * (centre.x - centroid.x), centroid.y + w * (centre.y - centroid.y) };
        }

        if (std::isfinite (point.x) && std::isfinite (point.y))
            result.push_back ({ point, std::min (strength, 1.0) });
    }

    return result;
}
}

namespace
{
/** Gap fillers and the ring centre: what depends on the listener (see findVirtualSpeakers(), items 1 and 2). */
std::vector<VirtualSpeaker> findListenerBased (std::span<const Vec2> speakers, Vec2 listener, const VirtualSpeakerOptions& o)
{
    struct Polar
    {
        double azimuth; // radians, clockwise from +y (front)
        double radius;
    };

    constexpr double twoPi = 2.0 * std::numbers::pi;
    constexpr double toRadians = std::numbers::pi / 180.0;
    constexpr double atListener = 1e-6;
    constexpr double minStrength = 1e-6;

    std::vector<Polar> ring;
    bool speakerAtListener = false;

    for (const auto& s : speakers)
    {
        if (! (std::isfinite (s.x) && std::isfinite (s.y)))
            continue;

        const double dx = s.x - listener.x, dy = s.y - listener.y;
        const double r = std::hypot (dx, dy);

        if (r < atListener)
        {
            speakerAtListener = true;
            continue;
        }

        const double az = std::atan2 (dx, dy);
        const bool duplicate = std::any_of (ring.begin(), ring.end(), [&] (const Polar& p)
        {
            return std::abs (p.radius - r) < 1e-9 && std::abs (std::remainder (p.azimuth - az, twoPi)) < 1e-9;
        });

        if (! duplicate)
            ring.push_back ({ az, r });
    }

    const size_t n = ring.size();
    if (n < 2)
        return {};

    std::sort (ring.begin(), ring.end(), [] (const Polar& a, const Polar& b) { return a.azimuth < b.azimuth; });

    // Angular gap after each speaker, going clockwise (the last one wraps around).
    std::vector<double> gap (n);
    double largestGap = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double next = ring[(i + 1) % n].azimuth + (i + 1 == n ? twoPi : 0.0);
        gap[i] = next - ring[i].azimuth;
        largestGap = std::max (largestGap, gap[i]);
    }

    // How well the listener is enclosed: 1 below enclosedBelowDegrees, falling to 0 at 180 degrees.
    const double enclosed = 1.0 - smoothstep (largestGap, o.enclosedBelowDegrees * toRadians, std::numbers::pi);
    if (enclosed <= minStrength)
        return {};

    std::vector<VirtualSpeaker> result;
    const auto add = [&] (Vec2 p, double strength)
    {
        if (strength > minStrength)
            result.push_back ({ p, std::min (strength, 1.0) });
    };

    if (o.fillGaps)
    {
        const double lo = (o.gapThresholdDegrees - o.gapSoftnessDegrees) * toRadians;
        const double hi = (o.gapThresholdDegrees + o.gapSoftnessDegrees) * toRadians;

        for (size_t i = 0; i < n; ++i)
        {
            const double strength = enclosed * smoothstep (gap[i], lo, hi);
            if (strength <= minStrength)
                continue;

            const auto& from = ring[i];
            const auto& to = ring[(i + 1) % n];
            const double azimuth = from.azimuth + 0.5 * gap[i];
            const double radius = 0.5 * (from.radius + to.radius);
            add ({ listener.x + radius * std::sin (azimuth), listener.y + radius * std::cos (azimuth) }, strength);
        }
    }

    if (o.addCentre && n >= 3 && ! speakerAtListener)
    {
        double sum = 0.0, minR = 1e300, maxR = 0.0;
        for (const auto& p : ring)
        {
            sum += p.radius;
            minR = std::min (minR, p.radius);
            maxR = std::max (maxR, p.radius);
        }

        const double mean = sum / (double) n;
        const double spread = (maxR - minR) / mean;
        const double isRing = 1.0 - smoothstep (spread, o.ringTolerance - o.ringSoftness, o.ringTolerance + o.ringSoftness);
        const double clear = smoothstep (minR / mean, o.centreClearance - o.centreSoftness, o.centreClearance + o.centreSoftness);
        add (listener, enclosed * isRing * clear);
    }

    if ((int) result.size() > o.maxCount)
        result.resize ((size_t) std::max (o.maxCount, 0));

    return result;
}
}

std::vector<VirtualSpeaker> findVirtualSpeakers (std::span<const Vec2> speakers, Vec2 listener, const VirtualSpeakerOptions& o)
{
    auto result = findListenerBased (speakers, listener, o);

    if (o.addFaceCentres)
    {
        // A face centre that coincides with a virtual speaker that is there already (the centre of a ring) is merged with it.
        double size = 0.0;
        for (const auto& p : speakers)
            for (const auto& q : speakers)
                if (std::isfinite (p.x) && std::isfinite (p.y) && std::isfinite (q.x) && std::isfinite (q.y))
                    size = std::max (size, std::hypot (p.x - q.x, p.y - q.y));
        const double mergeDistance = std::max (1e-3, 0.01 * size);

        for (auto face : findFaceCentres (speakers, o))
        {
            if (face.strength >= o.faceSnapStrength)
                face.strength = 1.0;
            if (face.strength <= 1e-6)
                continue;

            auto same = std::find_if (result.begin(), result.end(), [&] (const VirtualSpeaker& v)
            {
                return std::hypot (v.position.x - face.position.x, v.position.y - face.position.y) < mergeDistance;
            });

            if (same != result.end())
                same->strength = std::max (same->strength, face.strength);
            else
                result.push_back (face);
        }
    }

    if ((int) result.size() > o.maxCount)
    {
        std::stable_sort (result.begin(), result.end(), [] (const VirtualSpeaker& a, const VirtualSpeaker& b) { return a.strength > b.strength; });
        result.resize ((size_t) std::max (o.maxCount, 0));
    }

    return result;
}
}
