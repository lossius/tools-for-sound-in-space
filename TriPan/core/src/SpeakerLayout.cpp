// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file SpeakerLayout.cpp
    @brief Nested virtual-speaker layers and the blended, optionally spread, gain computation. */

#include "tripan/SpeakerLayout.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <set>

namespace tripan
{
SpeakerLayout::SpeakerLayout (std::span<const Vec2> realSpeakers, std::span<const VirtualSpeaker> virtualSpeakers)
{
    static std::atomic<std::uint64_t> counter { 1 };
    layoutId = counter.fetch_add (1);

    realHull = tlos::Hull (realSpeakers);

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

        layers.push_back ({ Triangulation (realSpeakers, positions), k == 0 ? 1.0 : weight, {}, true });

        // Where does this layer differ from the one before it? (Point indices are the same in both: the real
        // speakers, then the virtual ones in the same order, and this layer has more of them.)
        if (layers.size() >= 2)
        {
            auto& layer = layers.back();
            const auto& before = layers[layers.size() - 2].triangulation;
            const auto& now = layer.triangulation;

            const auto sameBoundary = [&]
            {
                const auto a = before.boundaryEdges(), b = now.boundaryEdges();
                return a.size() == b.size() && std::equal (a.begin(), a.end(), b.begin(), [] (const Edge& p, const Edge& q) { return p.a == q.a && p.b == q.b; });
            };

            layer.everywhere = ! sameBoundary() || before.loneSpeaker() != now.loneSpeaker() || before.triangles().empty() != now.triangles().empty();

            if (! layer.everywhere)
            {
                const auto key = [] (const Triangle& t)
                {
                    std::array<int, 3> k { t.a, t.b, t.c };
                    std::sort (k.begin(), k.end());
                    return k;
                };
                std::set<std::array<int, 3>> existing;
                for (const auto& t : before.triangles())
                    existing.insert (key (t));

                // Virtual speakers that were there before and whose real neighbours (and so weights) changed.
                std::vector<bool> changedNeighbours ((size_t) now.numReal() + (size_t) before.numVirtual(), false);
                for (int v = now.numReal(); v < now.numReal() + before.numVirtual(); ++v)
                {
                    const auto p = before.redistribution (v), q = now.redistribution (v);
                    changedNeighbours[(size_t) v] = p.size() != q.size()
                        || ! std::equal (p.begin(), p.end(), q.begin(), [] (const Triangulation::Weight& x, const Triangulation::Weight& y) { return x.speaker == y.speaker && x.weight == y.weight; });
                }

                const auto points = now.speakers();
                for (const auto& t : now.triangles())
                {
                    const bool isNew = existing.count (key (t)) == 0;
                    const bool touchesChanged = (t.a >= now.numReal() && t.a < (int) changedNeighbours.size() && changedNeighbours[(size_t) t.a])
                                             || (t.b >= now.numReal() && t.b < (int) changedNeighbours.size() && changedNeighbours[(size_t) t.b])
                                             || (t.c >= now.numReal() && t.c < (int) changedNeighbours.size() && changedNeighbours[(size_t) t.c]);
                    if (! isNew && ! touchesChanged)
                        continue;

                    const Vec2 p[3] { points[(size_t) t.a], points[(size_t) t.b], points[(size_t) t.c] };
                    constexpr double margin = 1e-7;
                    layer.changed.push_back ({ std::min ({ p[0].x, p[1].x, p[2].x }) - margin, std::min ({ p[0].y, p[1].y, p[2].y }) - margin,
                                               std::max ({ p[0].x, p[1].x, p[2].x }) + margin, std::max ({ p[0].y, p[1].y, p[2].y }) + margin });
                }
            }
        }
    }
}

const Triangulation& SpeakerLayout::full() const noexcept
{
    static const Triangulation empty;
    return layers.empty() ? empty : layers.back().triangulation;
}

namespace
{
/** True if the point is inside (or on the edge of) one of the boxes. */
template <typename Boxes>
bool insideAny (const Boxes& boxes, Vec2 p) noexcept
{
    return std::any_of (boxes.begin(), boxes.end(), [p] (const auto& b) { return p.x >= b.minX && p.x <= b.maxX && p.y >= b.minY && p.y <= b.maxY; });
}
}

SourceGains computeGainsReference (const SpeakerLayout& layout, Vec2 source) noexcept
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


SourceGains computeGains (const SpeakerLayout& layout, Vec2 source) noexcept
{
    if (layout.numLayers() == 0)
        return {};

    if (layout.numLayers() == 1)
        return computeGains (layout.layer (0), source);

    SourceGains out;
    double acc[kMaxGainEntries] {};

    SourceGains g;
    for (int l = 0; l < layout.numLayers(); ++l)
    {
        // A layer only differs from the one before it inside a few boxes; elsewhere the gains are the same, so they are not searched for again.
        if (l == 0 || layout.layers[(size_t) l].everywhere || insideAny (layout.layers[(size_t) l].changed, source))
            g = computeGains (layout.layer (l), source);
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

namespace
{
/** The spread average (see computeGains with spread), with a weight for each of the 9 positions.

    The squared gains of every position are added with the weight of that position. With `normalise` the result
    is scaled to constant power (the weights then only decide how much each position counts); without it
    the result is the root of the weighted mean, so weights below 1 lower the level. */
template <typename Weight>
SourceGains spreadAverage (const SpeakerLayout& layout, Vec2 source, double spread, Weight weightAt, bool normalise) noexcept
{
    SourceGains out;
    double power[kMaxGainEntries] {};
    int positions = 0;

    const auto accumulate = [&] (Vec2 position)
    {
        const auto g = computeGains (layout, position);
        const double weight = weightAt (position);
        ++positions;

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

            power[slot] += weight * (double) g.gain[(size_t) i] * (double) g.gain[(size_t) i];
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

    // Normalised: the mean of the squared gains sums to 1 when each term does. Otherwise it sums to the mean weight.
    const double inv = normalise ? 1.0 / total : 1.0 / (double) positions;
    for (int i = 0; i < out.count; ++i)
        out.gain[(size_t) i] = (float) std::sqrt (power[i] * inv);

    return out;
}
}

SourceGains computeGains (const SpeakerLayout& layout, Vec2 source, double spread) noexcept
{
    if (! (spread > 1e-6) || ! std::isfinite (spread) || ! std::isfinite (source.x) || ! std::isfinite (source.y))
        return computeGains (layout, source);

    return spreadAverage (layout, source, std::min (spread, kMaxSpreadRadius), [] (Vec2) { return 1.0; }, true);
}

SourceGains computeGains (const SpeakerLayout& layout, Vec2 source, double spread, double outsideRolloffDb) noexcept
{
    if (! (outsideRolloffDb > 0.0) || ! std::isfinite (outsideRolloffDb) || ! layout.hull().hasArea())
        return computeGains (layout, source, spread);

    // The level factor of a position; its square is the weight of that position's squared gains.
    const auto weightAt = [&] (Vec2 p)
    {
        const double factor = tlos::outsideFactor (layout.hull().distance (p), outsideRolloffDb);
        return factor * factor;
    };

    if (! (spread > 1e-6) || ! std::isfinite (spread) || ! std::isfinite (source.x) || ! std::isfinite (source.y))
    {
        auto g = computeGains (layout, source);
        const float factor = (float) tlos::outsideFactor (layout.hull().distance (source), outsideRolloffDb);
        for (int i = 0; i < g.count; ++i)
            g.gain[(size_t) i] *= factor;
        return g;
    }

    return spreadAverage (layout, source, std::min (spread, kMaxSpreadRadius), weightAt, false);
}
}
