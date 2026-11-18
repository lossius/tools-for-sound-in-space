// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file Vbap.cpp
    @brief The 2D VBAP gain law and the speaker layout it works on. */

#include "vecpan/Vbap.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace vecpan
{
namespace
{
/** Pi. */
constexpr double kPi = std::numbers::pi;
/** A full turn in radians. */
constexpr double kTwoPi = 2.0 * std::numbers::pi;
constexpr double kSameDirection = 1e-9;   ///< Speakers closer than this (radians) in azimuth count as one.

/** Unit vector of an azimuth: x = -sin, y = cos. */
void direction (double azimuth, double& x, double& y) noexcept
{
    x = -std::sin (azimuth);
    y = std::cos (azimuth);
}

/** Wraps an angle to [0, 2 pi). */
double wrapPositive (double a) noexcept
{
    a = std::fmod (a, kTwoPi);
    return a < 0.0 ? a + kTwoPi : a;
}
}

double azimuthOf (tlos::Vec2 p) noexcept
{
    return std::atan2 (-p.x, p.y);
}

Layout::Layout (std::span<const tlos::Vec2> speakers)
{
    const int m = (int) std::min<size_t> (speakers.size(), (size_t) tlos::kMaxGainEntries);
    std::vector<tlos::Vec2> finite;
    finite.reserve ((size_t) m);

    for (int i = 0; i < m; ++i)
    {
        const auto& p = speakers[(size_t) i];
        if (! std::isfinite (p.x) || ! std::isfinite (p.y))
            continue;

        finite.push_back (p);
        if (std::hypot (p.x, p.y) < 1e-9)
            continue; // at the listener: no direction

        sorted.push_back ({ i, azimuthOf (p) });
    }

    speakerHull = tlos::Hull (finite);

    // By azimuth, the lowest speaker number first among equal directions; then drop later speakers in the same direction.
    std::stable_sort (sorted.begin(), sorted.end(), [] (const Entry& a, const Entry& b) { return a.azimuth < b.azimuth; });
    std::vector<Entry> unique;
    for (const auto& e : sorted)
        if (unique.empty() || e.azimuth - unique.back().azimuth > kSameDirection)
            unique.push_back (e);

    // The first and the last may be the same direction across the +-pi seam.
    if (unique.size() > 1 && (unique.front().azimuth + kTwoPi) - unique.back().azimuth <= kSameDirection)
        unique.pop_back();

    sorted = std::move (unique);

    const size_t n = sorted.size();
    neighbours.resize (n);
    for (size_t i = 0; i < n; ++i)
    {
        auto& pair = neighbours[i];
        if (n == 1)
        {
            pair.gap = kTwoPi;
            pair.wide = true;
            continue;
        }

        const auto& a = sorted[i];
        const auto& b = sorted[(i + 1) % n];
        pair.gap = wrapPositive (b.azimuth - a.azimuth);
        if (pair.gap <= kSameDirection)
            pair.gap = kTwoPi; // two speakers only, in the same direction cannot happen after the unique pass
        pair.wide = pair.gap >= kPi - 1e-9;

        if (! pair.wide)
        {
            double ax, ay, bx, by;
            direction (a.azimuth, ax, ay);
            direction (b.azimuth, bx, by);
            const double det = ax * by - ay * bx;      // = sin(gap), positive below pi
            pair.inverse[0] =  by / det;   pair.inverse[1] = -ay / det;
            pair.inverse[2] = -bx / det;   pair.inverse[3] =  ax / det;
        }
    }
}

std::vector<int> Layout::order() const
{
    std::vector<int> out;
    out.reserve (sorted.size());
    for (const auto& e : sorted)
        out.push_back (e.speaker);
    return out;
}

namespace
{
/** Adds the squared gains of a unit source in direction `azimuth` to `power` (indexed by speaker). */
void addPower (const Layout& layout, double azimuth, double weight, std::array<double, (size_t) tlos::kMaxGainEntries>& power) noexcept
{
    const auto& entries = layout.entries();
    const auto& pairs = layout.pairs();
    const size_t n = entries.size();

    if (n == 1)
    {
        power[(size_t) entries[0].speaker] += weight;
        return;
    }

    // The pair that starts at the last speaker at or before the direction (the last speaker if there is none: the arc wraps round).
    const double a = std::remainder (azimuth, kTwoPi);
    const auto above = std::upper_bound (entries.begin(), entries.end(), a, [] (double value, const Layout::Entry& e) { return value < e.azimuth; });
    const size_t found = above == entries.begin() ? n - 1 : (size_t) (above - entries.begin()) - 1;
    const double offset = std::min (wrapPositive (azimuth - entries[found].azimuth), pairs[found].gap);

    const auto& pair = pairs[found];
    const int first = entries[found].speaker, second = entries[(found + 1) % n].speaker;
    double ga, gb;

    if (pair.wide)
    {
        // No vector base: cross-fade over the angle (sine / cosine, constant power).
        const double f = std::clamp (offset / pair.gap, 0.0, 1.0);
        ga = std::cos (0.5 * kPi * f);
        gb = std::sin (0.5 * kPi * f);
    }
    else
    {
        double px, py;
        direction (azimuth, px, py);
        ga = std::max (px * pair.inverse[0] + py * pair.inverse[2], 0.0);
        gb = std::max (px * pair.inverse[1] + py * pair.inverse[3], 0.0);
        const double norm = std::sqrt (ga * ga + gb * gb);
        if (norm > 0.0)
        {
            ga /= norm;
            gb /= norm;
        }
    }

    power[(size_t) first] += weight * ga * ga;
    power[(size_t) second] += weight * gb * gb;
}
}

tlos::SourceGains computeGains (const Layout& layout, tlos::Vec2 source, double spread, const VbapSettings& settings) noexcept
{
    tlos::SourceGains result;

    if (! std::isfinite (source.x) || ! std::isfinite (source.y) || layout.size() == 0)
        return result;

    std::array<double, (size_t) tlos::kMaxGainEntries> power {};
    const double distance = std::hypot (source.x, source.y);

    if (distance < 1e-9)
    {
        // No direction: every speaker gets the same share.
        for (const auto& e : layout.entries())
            power[(size_t) e.speaker] = 1.0 / (double) layout.size();
    }
    else
    {
        const double azimuth = azimuthOf (source);
        const double width = std::isfinite (spread) && spread > 0.0 ? std::atan2 (spread, distance) : 0.0;

        if (width > 0.0)
        {
            addPower (layout, azimuth - width, 1.0 / 3.0, power);
            addPower (layout, azimuth,         1.0 / 3.0, power);
            addPower (layout, azimuth + width, 1.0 / 3.0, power);
        }
        else
        {
            addPower (layout, azimuth, 1.0, power);
        }
    }

    double factor = 1.0;
    if (settings.outsideRolloffDb > 0.0 && layout.hull().hasArea())
        factor = tlos::outsideFactor (layout.hull().distance (source), settings.outsideRolloffDb);

    for (int i = 0; i < tlos::kMaxGainEntries; ++i)
        if (power[(size_t) i] > 0.0)
        {
            result.speaker[(size_t) result.count] = i;
            result.gain[(size_t) result.count] = (float) (std::sqrt (power[(size_t) i]) * factor);
            ++result.count;
        }

    return result;
}
}
