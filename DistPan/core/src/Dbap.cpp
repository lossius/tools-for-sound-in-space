// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file Dbap.cpp
    @brief The DBAP gain law. */

#include "distpan/Dbap.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace distpan
{
double rolloffExponent (double rolloffDb) noexcept
{
    // 20 log10 (2) dB is the level change of a gain that is halved: 1 / d^a drops by a * 6.0206 dB per doubling.
    return rolloffDb / (20.0 * std::log10 (2.0));
}

tlos::SourceGains computeGains (std::span<const tlos::Vec2> speakers, tlos::Vec2 source, double spread,
                                const DbapSettings& settings) noexcept
{
    tlos::SourceGains result;

    if (! std::isfinite (source.x) || ! std::isfinite (source.y))
        return result;

    const int m = (int) std::min<size_t> (speakers.size(), (size_t) tlos::kMaxGainEntries);
    const double a = rolloffExponent (std::isfinite (settings.rolloffDb) ? std::max (settings.rolloffDb, 0.0) : 0.0);
    const double blur = std::isfinite (settings.blur) ? std::max (settings.blur, 0.0) : 0.0;
    const double spreadRadius = std::isfinite (spread) ? std::max (spread, 0.0) : 0.0;
    const double extra = blur * blur + spreadRadius * spreadRadius;

    std::array<double, (size_t) tlos::kMaxGainEntries> weight {};
    double sumOfSquares = 0.0;
    bool any = false;

    for (int i = 0; i < m; ++i)
    {
        const auto& p = speakers[(size_t) i];
        if (! std::isfinite (p.x) || ! std::isfinite (p.y))
            continue;

        const double dx = p.x - source.x, dy = p.y - source.y;
        const double d2 = std::max (dx * dx + dy * dy + extra, 1e-12); // never divide by zero, even with no blur
        const double w = std::pow (d2, -0.5 * a);                      // 1 / d^a

        weight[(size_t) i] = w;
        sumOfSquares += w * w;
        any = true;
    }

    if (! any || ! (sumOfSquares > 0.0) || ! std::isfinite (sumOfSquares))
        return result;

    const double k = 1.0 / std::sqrt (sumOfSquares);
    result.count = m;
    for (int i = 0; i < m; ++i)
    {
        result.speaker[(size_t) i] = i;
        result.gain[(size_t) i] = (float) (weight[(size_t) i] * k);
    }

    return result;
}
}
