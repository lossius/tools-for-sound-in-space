#include "tripan/VirtualSpeakers.h"

#include <algorithm>
#include <cmath>
#include <numbers>

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
}

std::vector<VirtualSpeaker> findVirtualSpeakers (std::span<const Vec2> speakers, Vec2 listener, const VirtualSpeakerOptions& o)
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
