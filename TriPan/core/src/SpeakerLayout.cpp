// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file SpeakerLayout.cpp
    @brief Nested virtual-speaker layers and the blended, optionally spread, gain computation. */

#include "tripan/SpeakerLayout.h"

#include <algorithm>
#include <atomic>
#include <cmath>

namespace tripan
{
SpeakerLayout::SpeakerLayout (std::span<const Vec2> realSpeakers, std::span<const VirtualSpeaker> virtualSpeakers)
{
    static std::atomic<std::uint64_t> counter { 1 };
    layoutId = counter.fetch_add (1);

    for (const auto& v : virtualSpeakers)
        if (v.strength > 1e-9 && std::isfinite (v.position.x) && std::isfinite (v.position.y))
            sorted.push_back ({ v.position, std::min (v.strength, 1.0) });

    std::stable_sort (sorted.begin(), sorted.end(), [] (const VirtualSpeaker& a, const VirtualSpeaker& b) { return a.strength > b.strength; });

    const size_t k = sorted.size();
    std::vector<Vec2> positions;

    // Layer j has the j strongest virtual speakers; its weight is s_j - s_(j+1) (s_0 = 1, s_(k+1) = 0).
    // Layers without weight are not built.
    for (size_t j = 0; j <= k; ++j)
    {
        const double upper = j == 0 ? 1.0 : sorted[j - 1].strength;
        const double lower = j == k ? 0.0 : sorted[j].strength;
        const double weight = upper - lower;

        if (weight <= 1e-9 && ! (k == 0 && j == 0))
            continue;

        positions.clear();
        for (size_t i = 0; i < j; ++i)
            positions.push_back (sorted[i].position);

        layers.push_back ({ Triangulation (realSpeakers, positions), k == 0 ? 1.0 : weight });
    }
}

const Triangulation& SpeakerLayout::full() const noexcept
{
    static const Triangulation empty;
    return layers.empty() ? empty : layers.back().triangulation;
}

SourceGains computeGains (const SpeakerLayout& layout, Vec2 source) noexcept
{
    if (layout.numLayers() == 0)
        return {};

    if (layout.numLayers() == 1)
        return computeGains (layout.layer (0), source);

    SourceGains out;
    double acc[kMaxGainEntries] {};

    for (int l = 0; l < layout.numLayers(); ++l)
    {
        const auto g = computeGains (layout.layer (l), source);
        const double weight = layout.layerWeight (l);

        for (int i = 0; i < g.count; ++i)
        {
            const int speaker = g.speaker[(size_t) i];
            int slot = -1;
            for (int j = 0; j < out.count; ++j)
                if (out.speaker[(size_t) j] == speaker)
                {
                    slot = j;
                    break;
                }

            if (slot < 0)
            {
                if (out.count >= kMaxGainEntries)
                    continue;
                slot = out.count++;
                out.speaker[(size_t) slot] = speaker;
            }

            acc[slot] += weight * (double) g.gain[(size_t) i];
        }
    }

    double sumSq = 0.0;
    for (int i = 0; i < out.count; ++i)
        sumSq += acc[i] * acc[i];

    if (sumSq <= 0.0)
        return SourceGains {};

    const double inv = 1.0 / std::sqrt (sumSq);
    for (int i = 0; i < out.count; ++i)
        out.gain[(size_t) i] = (float) (acc[i] * inv);

    return out;
}

SourceGains computeGains (const SpeakerLayout& layout, Vec2 source, double spread) noexcept
{
    if (! (spread > 1e-6) || ! std::isfinite (spread) || ! std::isfinite (source.x) || ! std::isfinite (source.y))
        return computeGains (layout, source);

    spread = std::min (spread, kMaxSpreadRadius);

    SourceGains out;
    double power[kMaxGainEntries] {};

    const auto accumulate = [&] (Vec2 position)
    {
        const auto g = computeGains (layout, position);

        for (int i = 0; i < g.count; ++i)
        {
            const int speaker = g.speaker[(size_t) i];
            int slot = -1;
            for (int j = 0; j < out.count; ++j)
                if (out.speaker[(size_t) j] == speaker)
                {
                    slot = j;
                    break;
                }

            if (slot < 0)
            {
                if (out.count >= kMaxGainEntries)
                    continue;
                slot = out.count++;
                out.speaker[(size_t) slot] = speaker;
            }

            power[slot] += (double) g.gain[(size_t) i] * (double) g.gain[(size_t) i];
        }
    };

    // The ring is rotated by 22.5 degrees so that it is mirror-symmetric about both axes without
    // having points exactly on them.
    constexpr int ringPoints = 8;
    constexpr double pi = 3.14159265358979323846;

    accumulate (source);
    for (int k = 0; k < ringPoints; ++k)
    {
        const double angle = (22.5 + 360.0 * k / ringPoints) * pi / 180.0;
        accumulate ({ source.x + spread * std::cos (angle), source.y + spread * std::sin (angle) });
    }

    double total = 0.0;
    for (int i = 0; i < out.count; ++i)
        total += power[i];

    if (total <= 0.0)
        return SourceGains {};

    const double inv = 1.0 / total; // the mean of the squared gains sums to 1 when each term does
    for (int i = 0; i < out.count; ++i)
        out.gain[(size_t) i] = (float) std::sqrt (power[i] * inv);

    return out;
}
}
