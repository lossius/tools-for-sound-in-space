#include "tripan/Triangulation.h"

#include <algorithm>
#include <cmath>

namespace tripan
{
namespace
{
double cross (Vec2 o, Vec2 a, Vec2 b) noexcept
{
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

/** True if d lies strictly inside the circumcircle of the counter-clockwise triangle abc.
    Uses a relative tolerance, so cocircular points (e.g. a square) count as outside, which keeps
    the cavity star-shaped and the triangulation consistent. */
bool inCircle (Vec2 a, Vec2 b, Vec2 c, Vec2 d) noexcept
{
    const double adx = a.x - d.x, ady = a.y - d.y;
    const double bdx = b.x - d.x, bdy = b.y - d.y;
    const double cdx = c.x - d.x, cdy = c.y - d.y;

    const double alift = adx * adx + ady * ady;
    const double blift = bdx * bdx + bdy * bdy;
    const double clift = cdx * cdx + cdy * cdy;

    const double det = alift * (bdx * cdy - cdx * bdy)
                     + blift * (cdx * ady - adx * cdy)
                     + clift * (adx * bdy - bdx * ady);

    const double permanent = alift * (std::abs (bdx * cdy) + std::abs (cdx * bdy))
                           + blift * (std::abs (cdx * ady) + std::abs (adx * cdy))
                           + clift * (std::abs (adx * bdy) + std::abs (bdx * ady));

    return det > 1e-12 * permanent;
}

bool hasReverse (const std::vector<Edge>& list, int a, int b)
{
    return std::any_of (list.begin(), list.end(), [=] (const Edge& e) { return e.a == b && e.b == a; });
}
}

Triangulation::Triangulation (std::span<const Vec2> speakers)
    : points (speakers.begin(), speakers.end())
{
    // 1. Collect usable (finite, distinct) speakers.
    double minX = 1e300, maxX = -1e300, minY = 1e300, maxY = -1e300;
    for (const auto& p : points)
    {
        if (! (std::isfinite (p.x) && std::isfinite (p.y)))
            continue;
        minX = std::min (minX, p.x);  maxX = std::max (maxX, p.x);
        minY = std::min (minY, p.y);  maxY = std::max (maxY, p.y);
    }

    if (minX > maxX)
        return; // nothing usable

    double extent = std::max (maxX - minX, maxY - minY);
    if (extent <= 0.0)
        extent = 1.0;
    const double eps = 1e-9 * extent;

    std::vector<int> ids;
    for (int i = 0; i < (int) points.size(); ++i)
    {
        const auto p = points[(size_t) i];
        if (! (std::isfinite (p.x) && std::isfinite (p.y)))
            continue;

        const bool duplicate = std::any_of (ids.begin(), ids.end(), [&] (int j)
        {
            return std::abs (points[(size_t) j].x - p.x) <= eps && std::abs (points[(size_t) j].y - p.y) <= eps;
        });

        if (! duplicate)
            ids.push_back (i);
    }

    if (ids.size() == 1)
    {
        lone = ids[0];
        return;
    }

    // 2. Collinear case: chain of consecutive speakers along the line.
    {
        const int first = ids[0];
        int farthest = ids[1];
        double best = -1.0;
        for (int id : ids)
        {
            const double dx = points[(size_t) id].x - points[(size_t) first].x;
            const double dy = points[(size_t) id].y - points[(size_t) first].y;
            if (dx * dx + dy * dy > best) { best = dx * dx + dy * dy; farthest = id; }
        }

        const Vec2 o = points[(size_t) first];
        const Vec2 dir { points[(size_t) farthest].x - o.x, points[(size_t) farthest].y - o.y };
        const double len = std::sqrt (dir.x * dir.x + dir.y * dir.y);

        const bool collinear = std::all_of (ids.begin(), ids.end(), [&] (int id)
        {
            return std::abs (cross (o, points[(size_t) farthest], points[(size_t) id])) / len <= eps;
        });

        if (collinear)
        {
            std::sort (ids.begin(), ids.end(), [&] (int l, int r)
            {
                const auto pl = points[(size_t) l], pr = points[(size_t) r];
                return (pl.x - o.x) * dir.x + (pl.y - o.y) * dir.y < (pr.x - o.x) * dir.x + (pr.y - o.y) * dir.y;
            });

            for (size_t k = 0; k + 1 < ids.size(); ++k)
                edges.push_back ({ ids[k], ids[k + 1] });
            return;
        }
    }

    // 3. Bowyer-Watson on coordinates normalised to roughly [-0.5, 0.5].
    const int n = (int) ids.size();
    const Vec2 centre { 0.5 * (minX + maxX), 0.5 * (minY + maxY) };

    std::vector<Vec2> q;
    q.reserve ((size_t) n + 3);
    for (int id : ids)
        q.push_back ({ (points[(size_t) id].x - centre.x) / extent, (points[(size_t) id].y - centre.y) / extent });

    constexpr double M = 1e5;
    q.push_back ({ -M, -M });
    q.push_back ({  M, -M });
    q.push_back ({ 0.0, M });

    std::vector<Triangle> work { { n, n + 1, n + 2 } };
    std::vector<Triangle> keep;
    std::vector<Edge> cavity;

    for (int k = 0; k < n; ++k)
    {
        keep.clear();
        cavity.clear();

        for (const auto& t : work)
        {
            if (inCircle (q[(size_t) t.a], q[(size_t) t.b], q[(size_t) t.c], q[(size_t) k]))
            {
                cavity.push_back ({ t.a, t.b });
                cavity.push_back ({ t.b, t.c });
                cavity.push_back ({ t.c, t.a });
            }
            else
            {
                keep.push_back (t);
            }
        }

        // Cavity boundary = directed edges whose reverse is not also in the cavity.
        for (const auto& e : cavity)
            if (! hasReverse (cavity, e.a, e.b))
                keep.push_back ({ e.a, e.b, k });

        work.swap (keep);
    }

    // 4. Drop triangles touching the super triangle and degenerate slivers; map back to input indices.
    const double minArea2 = 1e-12 * extent * extent;
    for (const auto& t : work)
    {
        if (t.a >= n || t.b >= n || t.c >= n)
            continue;

        const int a = ids[(size_t) t.a], b = ids[(size_t) t.b], c = ids[(size_t) t.c];
        if (cross (points[(size_t) a], points[(size_t) b], points[(size_t) c]) <= minArea2)
            continue;

        tris.push_back ({ a, b, c });
    }

    // 5. Boundary edges = directed triangle edges without a reverse partner.
    std::vector<Edge> all;
    for (const auto& t : tris)
    {
        all.push_back ({ t.a, t.b });
        all.push_back ({ t.b, t.c });
        all.push_back ({ t.c, t.a });
    }
    for (const auto& e : all)
        if (! hasReverse (all, e.a, e.b))
            edges.push_back (e);
}
}
