// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file Groups.cpp
    @brief Source group bookkeeping, rigid-move clamping and convex hull. */

#include "tripan/Groups.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace tripan
{
SourceGroups::SourceGroups (const Ids& ids)
    : groupIds (ids)
{
    normalise();
}

void SourceGroups::normalise() noexcept
{
    std::array<int, kMaxGroupId + 1> count {};
    for (auto& id : groupIds)
    {
        if (id < 1 || id > kMaxGroupId)
            id = 0;
        else
            ++count[(size_t) id];
    }

    for (auto& id : groupIds)
        if (id > 0 && count[(size_t) id] < 2)
            id = 0;
}

int SourceGroups::groupOf (int source) const noexcept
{
    return source >= 0 && source < kMaxChannels ? groupIds[(size_t) source] : 0;
}

bool SourceGroups::any() const noexcept
{
    return std::any_of (groupIds.begin(), groupIds.end(), [] (int id) { return id > 0; });
}

std::vector<int> SourceGroups::membersOf (int source, int activeCount) const
{
    const int id = groupOf (source);
    std::vector<int> out;

    if (id > 0 && source < activeCount)
        for (int i = 0; i < kMaxChannels && i < activeCount; ++i)
            if (groupIds[(size_t) i] == id)
                out.push_back (i);

    if (out.size() < 2)
        return { source };

    return out;
}

int SourceGroups::sizeOf (int id, int activeCount) const noexcept
{
    if (id < 1)
        return 0;

    int n = 0;
    for (int i = 0; i < kMaxChannels && i < activeCount; ++i)
        n += groupIds[(size_t) i] == id ? 1 : 0;
    return n;
}

int SourceGroups::group (const std::vector<int>& sources)
{
    std::array<bool, kMaxChannels> chosen {};
    int distinct = 0;
    for (const int s : sources)
        if (s >= 0 && s < kMaxChannels && ! chosen[(size_t) s])
        {
            chosen[(size_t) s] = true;
            ++distinct;
        }

    if (distinct < 2)
        return 0;

    // The groups that are touched are merged whole.
    std::array<bool, kMaxGroupId + 1> touched {};
    int newId = 0;
    for (int i = 0; i < kMaxChannels; ++i)
    {
        const int id = groupIds[(size_t) i];
        if (chosen[(size_t) i] && id > 0)
        {
            touched[(size_t) id] = true;
            newId = newId == 0 ? id : std::min (newId, id);
        }
    }

    if (newId == 0)
    {
        std::array<bool, kMaxGroupId + 1> used {};
        for (const int id : groupIds)
            if (id > 0)
                used[(size_t) id] = true;

        newId = 1;
        while (newId < kMaxGroupId && used[(size_t) newId])
            ++newId;
    }

    for (int i = 0; i < kMaxChannels; ++i)
        if (chosen[(size_t) i] || (groupIds[(size_t) i] > 0 && touched[(size_t) groupIds[(size_t) i]]))
            groupIds[(size_t) i] = newId;

    normalise();
    return newId;
}

int SourceGroups::ungroup (const std::vector<int>& sources)
{
    std::array<bool, kMaxGroupId + 1> doomed {};
    int dissolved = 0;
    for (const int s : sources)
    {
        const int id = groupOf (s);
        if (id > 0 && ! doomed[(size_t) id])
        {
            doomed[(size_t) id] = true;
            ++dissolved;
        }
    }

    for (auto& id : groupIds)
        if (id > 0 && doomed[(size_t) id])
            id = 0;

    return dissolved;
}

std::string SourceGroups::toString() const
{
    std::string out;
    for (size_t i = 0; i < groupIds.size(); ++i)
    {
        if (i > 0)
            out += ' ';
        out += std::to_string (groupIds[i]);
    }
    return out;
}

SourceGroups SourceGroups::fromString (const std::string& text)
{
    Ids ids {};
    std::istringstream in (text);
    for (auto& id : ids)
    {
        long v = 0;
        if (! (in >> v))
            break;
        id = v >= 0 && v <= kMaxGroupId ? (int) v : 0;
    }
    return SourceGroups (ids);
}

Vec2 clampTranslation (const std::vector<Vec2>& points, Vec2 delta, double limit)
{
    double minX = 1e300, maxX = -1e300, minY = 1e300, maxY = -1e300;
    bool any = false;
    for (const auto& p : points)
    {
        if (! std::isfinite (p.x) || ! std::isfinite (p.y))
            continue;
        any = true;
        minX = std::min (minX, p.x);  maxX = std::max (maxX, p.x);
        minY = std::min (minY, p.y);  maxY = std::max (maxY, p.y);
    }

    if (! any || ! std::isfinite (delta.x) || ! std::isfinite (delta.y))
        return { 0.0, 0.0 };

    // Smallest and largest movement that keep every point inside; if a point is outside already the
    // movement is limited to bringing it back in.
    const auto limited = [] (double wanted, double lo, double hi) { return std::min (std::max (wanted, lo), hi); };
    return { limited (delta.x, -limit - minX, limit - maxX),
             limited (delta.y, -limit - minY, limit - maxY) };
}

double clampSpreadChange (const std::vector<double>& spreads, double change, double maxSpread)
{
    if (spreads.empty() || ! std::isfinite (change))
        return 0.0;

    const auto [lo, hi] = std::minmax_element (spreads.begin(), spreads.end());
    return std::min (std::max (change, -*lo), maxSpread - *hi);
}

std::vector<Vec2> convexHull (std::vector<Vec2> pts)
{
    pts.erase (std::remove_if (pts.begin(), pts.end(), [] (const Vec2& p) { return ! std::isfinite (p.x) || ! std::isfinite (p.y); }), pts.end());
    std::sort (pts.begin(), pts.end(), [] (const Vec2& a, const Vec2& b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    pts.erase (std::unique (pts.begin(), pts.end(), [] (const Vec2& a, const Vec2& b) { return a.x == b.x && a.y == b.y; }), pts.end());

    if (pts.size() < 3)
        return pts;

    const auto turn = [] (const Vec2& o, const Vec2& a, const Vec2& b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); };

    std::vector<Vec2> hull (2 * pts.size());
    size_t k = 0;
    for (size_t i = 0; i < pts.size(); ++i) // lower hull
    {
        while (k >= 2 && turn (hull[k - 2], hull[k - 1], pts[i]) <= 0.0)
            --k;
        hull[k++] = pts[i];
    }
    for (size_t i = pts.size() - 1, lower = k + 1; i > 0; --i) // upper hull
    {
        while (k >= lower && turn (hull[k - 2], hull[k - 1], pts[i - 1]) <= 0.0)
            --k;
        hull[k++] = pts[i - 1];
    }

    hull.resize (k - 1);
    return hull;
}
}
