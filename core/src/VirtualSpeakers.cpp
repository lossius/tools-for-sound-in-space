#include "tripan/VirtualSpeakers.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace tripan
{
std::vector<Vec2> findVirtualSpeakers (std::span<const Vec2> speakers, Vec2 listener, const VirtualSpeakerOptions& o)
{
    struct Polar
    {
        double azimuth; // radians, clockwise from +y (front)
        double radius;
    };

    constexpr double twoPi = 2.0 * std::numbers::pi;
    constexpr double atListener = 1e-6;

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

    // The listener must be enclosed by the speakers, otherwise there is nothing to fill in.
    if (largestGap >= std::numbers::pi - 1e-9)
        return {};

    const auto at = [&] (double azimuth, double radius) -> Vec2
    {
        return { listener.x + radius * std::sin (azimuth), listener.y + radius * std::cos (azimuth) };
    };

    std::vector<Vec2> result;
    const double threshold = o.gapThresholdDegrees * std::numbers::pi / 180.0;

    if (o.fillGaps && threshold > 0.0)
        for (size_t i = 0; i < n; ++i)
        {
            if (gap[i] <= threshold + 1e-9)
                continue;

            const int count = (int) std::ceil (gap[i] / threshold - 1e-9) - 1;
            const auto& from = ring[i];
            const auto& to = ring[(i + 1) % n];

            for (int j = 1; j <= count; ++j)
            {
                const double t = (double) j / (double) (count + 1);
                result.push_back (at (from.azimuth + gap[i] * t, from.radius + (to.radius - from.radius) * t));
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
        if (maxR - minR <= o.ringTolerance * mean && minR > o.centreClearance * mean)
            result.push_back (listener);
    }

    if ((int) result.size() > o.maxCount)
        result.resize ((size_t) std::max (o.maxCount, 0));

    return result;
}
}
