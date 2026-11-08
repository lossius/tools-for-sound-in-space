#include "tripan/Gains.h"

#include <algorithm>
#include <cmath>

namespace tripan
{
namespace
{
constexpr double kInsideTolerance = -1e-9;

double cross (Vec2 o, Vec2 a, Vec2 b) noexcept
{
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

/** Up to three entries (indices into the triangulation's points, real or virtual) with unit L2 norm. */
struct Raw
{
    int count = 0;
    int index[3] { -1, -1, -1 };
    double gain[3] { 0.0, 0.0, 0.0 };
};

/** Normalises non-negative weights to unit L2 norm (constant power). */
Raw makeRaw (int count, const int* indices, const double* w) noexcept
{
    Raw out;
    double sumSq = 0.0;
    for (int i = 0; i < count; ++i)
        sumSq += w[i] * w[i];

    if (sumSq <= 0.0)
        return out;

    const double inv = 1.0 / std::sqrt (sumSq);
    for (int i = 0; i < count; ++i)
    {
        out.index[i] = indices[i];
        out.gain[i] = w[i] * inv;
    }
    out.count = count;
    return out;
}

/** The gains of the triangulation's points (real and virtual) for a source at s. */
Raw computeRaw (const Triangulation& tri, Vec2 s) noexcept;

/** Turns raw gains into gains for real speakers: the share of a virtual speaker is split over its
    real neighbours, then everything is renormalised. */
SourceGains expand (const Triangulation& tri, const Raw& raw) noexcept
{
    SourceGains out;
    double acc[kMaxGainEntries] {};

    const auto add = [&] (int speaker, double g)
    {
        for (int i = 0; i < out.count; ++i)
            if (out.speaker[(size_t) i] == speaker)
            {
                acc[i] += g;
                return;
            }

        if (out.count < kMaxGainEntries)
        {
            out.speaker[(size_t) out.count] = speaker;
            acc[out.count] = g;
            ++out.count;
        }
    };

    for (int i = 0; i < raw.count; ++i)
    {
        if (raw.index[i] < tri.numReal())
            add (raw.index[i], raw.gain[i]);
        else
            for (const auto& w : tri.redistribution (raw.index[i]))
                add (w.speaker, raw.gain[i] * (double) w.weight);
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

Raw computeRaw (const Triangulation& tri, Vec2 s) noexcept
{
    if (! (std::isfinite (s.x) && std::isfinite (s.y)))
        return {};

    if (const int lone = tri.loneSpeaker(); lone >= 0)
    {
        const double w = 1.0;
        return makeRaw (1, &lone, &w);
    }

    const auto pts = tri.speakers();

    // Inside a triangle: pick the one where the smallest barycentric weight is largest,
    // which is robust on shared edges.
    double bestMin = -1e300;
    int bestIdx[3] {};
    double bestW[3] {};
    bool found = false;

    for (const auto& t : tri.triangles())
    {
        const Vec2 a = pts[(size_t) t.a], b = pts[(size_t) t.b], c = pts[(size_t) t.c];
        const double area2 = cross (a, b, c);
        const double wb = cross (a, s, c) / area2;
        const double wc = cross (a, b, s) / area2;
        const double wa = 1.0 - wb - wc;
        const double m = std::min ({ wa, wb, wc });

        if (m > bestMin)
        {
            bestMin = m;
            bestIdx[0] = t.a;  bestIdx[1] = t.b;  bestIdx[2] = t.c;
            bestW[0] = wa;     bestW[1] = wb;     bestW[2] = wc;
            found = true;
        }
    }

    if (found && bestMin >= kInsideTolerance)
    {
        for (double& w : bestW)
            w = std::max (w, 0.0);
        return makeRaw (3, bestIdx, bestW);
    }

    // Outside the hull (or no triangles, i.e. collinear speakers): nearest boundary edge.
    double bestDist = 1e300;
    int edgeIdx[2] {};
    double edgeW[2] {};
    bool edgeFound = false;

    for (const auto& e : tri.boundaryEdges())
    {
        const Vec2 a = pts[(size_t) e.a], b = pts[(size_t) e.b];
        const double dx = b.x - a.x, dy = b.y - a.y;
        const double len2 = dx * dx + dy * dy;
        if (len2 <= 0.0)
            continue;

        const double t = std::clamp (((s.x - a.x) * dx + (s.y - a.y) * dy) / len2, 0.0, 1.0);
        const double px = a.x + t * dx - s.x, py = a.y + t * dy - s.y;
        const double d2 = px * px + py * py;

        if (d2 < bestDist)
        {
            bestDist = d2;
            edgeIdx[0] = e.a;  edgeIdx[1] = e.b;
            edgeW[0] = 1.0 - t;  edgeW[1] = t;
            edgeFound = true;
        }
    }

    if (! edgeFound)
        return {};

    // Drop a zero-weight endpoint so exactly the used speakers are reported.
    if (edgeW[1] <= 0.0) { const double w = 1.0; return makeRaw (1, &edgeIdx[0], &w); }
    if (edgeW[0] <= 0.0) { const double w = 1.0; return makeRaw (1, &edgeIdx[1], &w); }
    return makeRaw (2, edgeIdx, edgeW);
}
}

SourceGains computeGains (const Triangulation& tri, Vec2 s) noexcept
{
    return expand (tri, computeRaw (tri, s));
}
}
