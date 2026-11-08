// Minimal dependency-free test runner for TriPanCore (no JUCE).
#include "tripan/Gains.h"
#include "tripan/Limits.h"
#include "tripan/Triangulation.h"
#include "tripan/VirtualSpeakers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <random>
#include <vector>

using namespace tripan;

static int failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (! (cond)) {                                                      \
            std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);     \
            ++failures;                                                      \
        }                                                                    \
    } while (false)

static bool isNear (double a, double b, double tol = 1e-6) { return std::abs (a - b) <= tol; }

static double cross (Vec2 o, Vec2 a, Vec2 b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); }

static double triArea (const std::vector<Vec2>& p, const Triangle& t) { return 0.5 * cross (p[(size_t) t.a], p[(size_t) t.b], p[(size_t) t.c]); }

static double hullArea (std::vector<Vec2> p, int* hullCount = nullptr)
{
    std::sort (p.begin(), p.end(), [] (Vec2 a, Vec2 b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    std::vector<Vec2> h (2 * p.size());
    size_t k = 0;
    for (size_t i = 0; i < p.size(); ++i)
    {
        while (k >= 2 && cross (h[k - 2], h[k - 1], p[i]) <= 1e-12) --k;
        h[k++] = p[i];
    }
    for (size_t i = p.size() - 1, t = k + 1; i > 0; --i)
    {
        while (k >= t && cross (h[k - 2], h[k - 1], p[i - 1]) <= 1e-12) --k;
        h[k++] = p[i - 1];
    }
    h.resize (k - 1);
    if (hullCount != nullptr)
    {
        // Count every input point on the hull boundary, including collinear ones.
        int onBoundary = 0;
        for (const auto& q : p)
            for (size_t i = 0; i < h.size(); ++i)
            {
                const auto u = h[i], v = h[(i + 1) % h.size()];
                const double len = std::hypot (v.x - u.x, v.y - u.y);
                const bool within = (q.x - u.x) * (v.x - u.x) + (q.y - u.y) * (v.y - u.y) >= -1e-12
                                 && (q.x - v.x) * (u.x - v.x) + (q.y - v.y) * (u.y - v.y) >= -1e-12;
                if (within && std::abs (cross (u, v, q)) / len < 1e-9) { ++onBoundary; break; }
            }
        *hullCount = onBoundary;
    }

    double a = 0.0;
    for (size_t i = 0; i < h.size(); ++i)
    {
        const auto u = h[i], v = h[(i + 1) % h.size()];
        a += u.x * v.y - v.x * u.y;
    }
    return 0.5 * std::abs (a);
}

static bool strictlyInsideCircumcircle (const std::vector<Vec2>& p, const Triangle& t, Vec2 d)
{
    const auto a = p[(size_t) t.a], b = p[(size_t) t.b], c = p[(size_t) t.c];
    const double adx = a.x - d.x, ady = a.y - d.y, bdx = b.x - d.x, bdy = b.y - d.y, cdx = c.x - d.x, cdy = c.y - d.y;
    const double det = (adx * adx + ady * ady) * (bdx * cdy - cdx * bdy)
                     + (bdx * bdx + bdy * bdy) * (cdx * ady - adx * cdy)
                     + (cdx * cdx + cdy * cdy) * (adx * bdy - bdx * ady);
    return det > 1e-9;
}

static double sumSq (const SourceGains& g)
{
    double s = 0.0;
    for (int i = 0; i < g.count; ++i) s += (double) g.gain[(size_t) i] * g.gain[(size_t) i];
    return s;
}

static float gainFor (const SourceGains& g, int speaker)
{
    for (int i = 0; i < g.count; ++i)
        if (g.speaker[(size_t) i] == speaker) return g.gain[(size_t) i];
    return 0.0f;
}

static void checkStructure (const std::vector<Vec2>& pts, const char* label)
{
    Triangulation tri (pts);
    int hc = 0;
    const double ha = hullArea (pts, &hc);

    double sum = 0.0;
    bool allCCW = true;
    for (const auto& t : tri.triangles())
    {
        const double a = triArea (pts, t);
        allCCW = allCCW && a > 0.0;
        sum += a;
    }

    bool delaunay = true;
    for (const auto& t : tri.triangles())
        for (size_t i = 0; i < pts.size(); ++i)
            if ((int) i != t.a && (int) i != t.b && (int) i != t.c && strictlyInsideCircumcircle (pts, t, pts[i]))
                delaunay = false;

    const int expected = 2 * (int) pts.size() - 2 - hc;
    if (! (allCCW && isNear (sum, ha, 1e-9 * std::max (1.0, ha)) && delaunay && (int) tri.triangles().size() == expected
           && (int) tri.boundaryEdges().size() == hc))
    {
        std::printf ("structure FAIL (%s): n=%zu ccw=%d area=%g hull=%g delaunay=%d tris=%zu expected=%d edges=%zu hull=%d\n",
                     label, pts.size(), allCCW, sum, ha, delaunay, tri.triangles().size(), expected, tri.boundaryEdges().size(), hc);
        ++failures;
    }
}

static void testLimits()
{
    CHECK (isValidChannelCount (1));
    CHECK (isValidChannelCount (64));
    CHECK (! isValidChannelCount (0));
    CHECK (! isValidChannelCount (65));
}

static void testDegenerateSpeakerCounts()
{
    CHECK (computeGains (Triangulation(), { 0, 0 }).count == 0);

    std::vector<Vec2> one { { 3, 4 } };
    const auto g1 = computeGains (Triangulation (one), { -10, 7 });
    CHECK (g1.count == 1 && g1.speaker[0] == 0 && isNear (g1.gain[0], 1.0));

    std::vector<Vec2> two { { -1, 0 }, { 1, 0 } };
    const Triangulation t2 (two);
    const auto mid = computeGains (t2, { 0, 0 });
    CHECK (mid.count == 2 && isNear (mid.gain[0], std::sqrt (0.5)) && isNear (mid.gain[1], std::sqrt (0.5)));
    const auto beyond = computeGains (t2, { 5, 3 });
    CHECK (beyond.count == 1 && beyond.speaker[0] == 1);

    CHECK (computeGains (t2, { std::nan (""), 0 }).count == 0);
}

static void testCollinear()
{
    // Unsorted on purpose: indices 0..3 at x = 3, 0, 2, 1.
    std::vector<Vec2> pts { { 3, 0 }, { 0, 0 }, { 2, 0 }, { 1, 0 } };
    const Triangulation tri (pts);
    CHECK (tri.triangles().empty());

    const auto g = computeGains (tri, { 1.5, 4.0 }); // between x=1 (idx 3) and x=2 (idx 2)
    CHECK (g.count == 2);
    CHECK (isNear (gainFor (g, 3), std::sqrt (0.5)) && isNear (gainFor (g, 2), std::sqrt (0.5)));
    CHECK (isNear (sumSq (g), 1.0));

    const auto end = computeGains (tri, { -9, 1 });
    CHECK (end.count == 1 && end.speaker[0] == 1);
}

static void testSquare()
{
    // Four cocircular speakers: must yield exactly two consistent triangles.
    std::vector<Vec2> sq { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
    checkStructure (sq, "square");
    const Triangulation tri (sq);
    CHECK (tri.triangles().size() == 2);

    const auto c = computeGains (tri, { 0.5, 0.5 });
    CHECK (isNear (sumSq (c), 1.0));

    // Exactly on a speaker: that speaker only.
    const auto v = computeGains (tri, { 1, 1 });
    CHECK (isNear (gainFor (v, 2), 1.0) && isNear (sumSq (v), 1.0));
}

static void testGridsAndRandom()
{
    std::vector<Vec2> grid;
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            grid.push_back ({ (double) x, (double) y });
    checkStructure (grid, "grid 8x8");

    std::mt19937 rng (12345);
    std::uniform_real_distribution<double> u (-5.0, 5.0);
    for (int trial = 0; trial < 200; ++trial)
    {
        const int n = 3 + (int) (rng() % 62);
        std::vector<Vec2> pts;
        for (int i = 0; i < n; ++i) pts.push_back ({ u (rng), u (rng) });
        checkStructure (pts, "random");
    }

    // Circular layout (all cocircular) plus a centre speaker, like a typical dome/ring.
    std::vector<Vec2> ring { { 0, 0 } };
    for (int i = 0; i < 16; ++i)
        ring.push_back ({ std::cos (i * 0.392699081698724), std::sin (i * 0.392699081698724) });
    checkStructure (ring, "ring16+centre");
}

static void testConstantPowerAndLocality()
{
    std::mt19937 rng (777);
    std::uniform_real_distribution<double> u (-5.0, 5.0), wide (-20.0, 20.0);

    for (int trial = 0; trial < 50; ++trial)
    {
        std::vector<Vec2> pts;
        for (int i = 0; i < 24; ++i) pts.push_back ({ u (rng), u (rng) });
        const Triangulation tri (pts);

        for (int k = 0; k < 200; ++k)
        {
            const auto g = computeGains (tri, { wide (rng), wide (rng) });
            bool nonNeg = true;
            for (int i = 0; i < g.count; ++i) nonNeg = nonNeg && g.gain[(size_t) i] >= 0.0f;
            CHECK (g.count >= 1 && g.count <= 3);
            CHECK (nonNeg);
            CHECK (isNear (sumSq (g), 1.0, 1e-5));
        }
    }
}

static void testContinuity()
{
    // Off-regular layout, sweep a source across and well beyond the hull.
    std::vector<Vec2> pts { { -2, -1.5 }, { 2.2, -1.2 }, { 2, 1.8 }, { -1.7, 2 }, { 0.1, 0.2 }, { 0.9, -0.6 }, { -0.8, 0.9 } };
    const Triangulation tri (pts);

    const double step = 1e-3;
    double worst = 0.0;
    for (int line = 0; line < 20; ++line)
    {
        const double angle = line * 0.314159265;
        const Vec2 dir { std::cos (angle), std::sin (angle) };
        SourceGains prev;
        bool havePrev = false;
        for (double r = -8.0; r <= 8.0; r += step)
        {
            const auto g = computeGains (tri, { 0.1 + dir.x * r, 0.2 + dir.y * r });
            if (havePrev)
                for (int s = 0; s < (int) pts.size(); ++s)
                    worst = std::max (worst, (double) std::abs (gainFor (g, s) - gainFor (prev, s)));
            prev = g;
            havePrev = true;
        }
    }
    std::printf ("max gain jump per %.0e step: %g\n", step, worst);
    CHECK (worst < 0.05);
}

static void testDuplicatesAndNonFinite()
{
    std::vector<Vec2> pts { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 0 }, { std::nan (""), 2 }, { 1, 1 } };
    const Triangulation tri (pts);
    CHECK (tri.triangles().size() == 2);

    for (double x = -0.5; x <= 1.5; x += 0.1)
        for (double y = -0.5; y <= 1.5; y += 0.1)
        {
            const auto g = computeGains (tri, { x, y });
            CHECK (isNear (sumSq (g), 1.0, 1e-5));
            CHECK (gainFor (g, 3) == 0.0f && gainFor (g, 4) == 0.0f); // duplicate and NaN never used
        }
}

static std::vector<Vec2> ring (std::initializer_list<double> azimuthsDegrees, double radius = 5.0)
{
    std::vector<Vec2> pts;
    for (double a : azimuthsDegrees)
        pts.push_back ({ radius * std::sin (a * 3.14159265358979 / 180.0), radius * std::cos (a * 3.14159265358979 / 180.0) });
    return pts;
}

static void testFindVirtualSpeakers()
{
    // 5.1: ring (centre speaker) plus the open back (a 140 degree gap between the surrounds).
    {
        const auto fivePointOne = ring ({ -30, 30, 0, -110, 110 });
        const auto v = findVirtualSpeakers (fivePointOne);
        CHECK (v.size() == 2);
        bool centre = false, back = false;
        for (const auto& p : v)
        {
            if (std::hypot (p.x, p.y) < 1e-9) centre = true;
            if (std::abs (p.x) < 1e-9 && std::abs (p.y + 5.0) < 1e-9) back = true; // directly behind, same radius
        }
        CHECK (centre && back);
    }

    // Closed rings only get the centre.
    CHECK (findVirtualSpeakers (ring ({ -45, 45, -135, 135 })).size() == 1);                          // quad
    CHECK (findVirtualSpeakers (ring ({ 0, 60, 120, 180, 240, 300 })).size() == 1);                   // hexagon
    CHECK (findVirtualSpeakers (ring ({ 0, 45, 90, 135, 180, 225, 270, 315 })).size() == 1);          // octagon
    CHECK (findVirtualSpeakers (ring ({ -30, 30, 0, -90, 90, -150, 150 })).size() == 1);              // 7.1

    // Wide gaps are filled with evenly spaced speakers: a triangle of speakers has 120 degree gaps.
    CHECK (findVirtualSpeakers (ring ({ 0, 120, 240 })).size() == 4); // 3 gap fillers + the centre

    // Nothing to do: stereo pair, a front arc, a line, one speaker, nothing, irregular layouts.
    CHECK (findVirtualSpeakers (ring ({ -30, 30 })).empty());
    CHECK (findVirtualSpeakers (ring ({ -60, -30, 0, 30, 60 })).empty());     // listener not enclosed
    CHECK (findVirtualSpeakers (std::vector<Vec2> { { -2, 1 }, { 0, 1 }, { 2, 1 }, { 4, 1 } }).empty());
    CHECK (findVirtualSpeakers (std::vector<Vec2> { { 1, 1 } }).empty());
    CHECK (findVirtualSpeakers (std::vector<Vec2> {}).empty());

    std::mt19937 rng (3);
    std::uniform_real_distribution<double> u (-6.0, 6.0);
    std::vector<Vec2> scatter;
    for (int i = 0; i < 40; ++i) scatter.push_back ({ u (rng), u (rng) });
    CHECK (findVirtualSpeakers (scatter).empty());                            // many points, no gap, no ring

    // A speaker at the listener position: no centre speaker (it already exists).
    auto withCentre = ring ({ 0, 90, 180, 270 });
    withCentre.push_back ({ 0.0, 0.0 });
    CHECK (findVirtualSpeakers (withCentre).empty());

    // Non-finite and duplicate positions are ignored; the listener can be elsewhere.
    auto messy = ring ({ 0, 90, 180, 270 });
    messy.push_back ({ std::nan (""), 1.0 });
    messy.push_back (messy[0]);
    CHECK (findVirtualSpeakers (messy).size() == 1);
    auto shifted = ring ({ 0, 90, 180, 270 });
    for (auto& p : shifted) { p.x += 2.0; p.y -= 1.0; }
    const auto c = findVirtualSpeakers (shifted, { 2.0, -1.0 });
    CHECK (c.size() == 1 && std::hypot (c[0].x - 2.0, c[0].y + 1.0) < 1e-9);

    // The count is capped.
    VirtualSpeakerOptions o;
    o.maxCount = 2;
    CHECK (findVirtualSpeakers (ring ({ 0, 120, 240 }), {}, o).size() == 2);
}

static void testVirtualSpeakerGains()
{
    const auto fivePointOne = ring ({ -30, 30, 0, -110, 110 });         // L, R, C, Ls, Rs
    const auto v = findVirtualSpeakers (fivePointOne);
    const Triangulation tri (fivePointOne, v);

    CHECK (tri.numReal() == 5 && tri.numVirtual() == 2);
    CHECK ((int) tri.speakers().size() == 7);

    // Each virtual speaker is connected to some real speakers with unit-norm weights.
    for (int i = 5; i < 7; ++i)
    {
        const auto w = tri.redistribution (i);
        CHECK (! w.empty());
        double sumSq = 0.0;
        for (const auto& e : w) { CHECK (e.speaker >= 0 && e.speaker < 5); sumSq += (double) e.weight * e.weight; }
        CHECK (isNear (sumSq, 1.0, 1e-6));
    }
    CHECK (tri.redistribution (0).empty() && tri.redistribution (99).empty()); // real or invalid index

    // A source at the origin goes equally to all five speakers (it used to favour R and the surrounds).
    const auto g = computeGains (tri, { 0.0, 0.0 });
    CHECK (g.count == 5);
    for (int i = 0; i < g.count; ++i)
        CHECK (isNear (g.gain[(size_t) i], 1.0 / std::sqrt (5.0), 1e-5));

    // Mirrored sources give mirrored gains: L<->R and Ls<->Rs.
    const int mirror[] = { 1, 0, 2, 4, 3 };
    std::mt19937 rng (5);
    std::uniform_real_distribution<double> u (-8.0, 8.0);
    for (int k = 0; k < 500; ++k)
    {
        const Vec2 s { u (rng), u (rng) };
        const auto a = computeGains (tri, s);
        const auto b = computeGains (tri, { -s.x, s.y });
        CHECK (isNear (sumSq (a), 1.0, 1e-5));
        for (int i = 0; i < 5; ++i)
            CHECK (isNear (gainFor (a, i), gainFor (b, mirror[i]), 1e-4));
    }

    // Continuity: no jumps when a source moves across and beyond the speakers.
    double worst = 0.0;
    for (int line = 0; line < 12; ++line)
    {
        const double angle = line * 0.5236 + 0.1;
        SourceGains prev;
        bool have = false;
        for (double r = -9.0; r <= 9.0; r += 1e-3)
        {
            const auto cur = computeGains (tri, { r * std::cos (angle), r * std::sin (angle) });
            if (have)
                for (int i = 0; i < 5; ++i)
                    worst = std::max (worst, (double) std::abs (gainFor (cur, i) - gainFor (prev, i)));
            prev = cur;
            have = true;
        }
    }
    CHECK (worst < 0.05);

    // Without virtual speakers the old behaviour is unchanged: at most three speakers.
    const Triangulation plain (fivePointOne);
    CHECK (plain.numVirtual() == 0 && computeGains (plain, { 0.0, 0.0 }).count <= 3);

    // Odd inputs: a virtual speaker on top of a real one is ignored, and virtual speakers with no real
    // speakers around them do not crash.
    const Triangulation dup (fivePointOne, std::vector<Vec2> { fivePointOne[0] });
    CHECK (isNear (sumSq (computeGains (dup, { 1.0, 1.0 })), 1.0, 1e-5));
    const Triangulation onlyVirtual (std::vector<Vec2> {}, std::vector<Vec2> { { 0, 0 }, { 1, 1 }, { 2, 0 } });
    CHECK (computeGains (onlyVirtual, { 0.5, 0.2 }).count == 0);
}

int main()
{
    testLimits();
    testDegenerateSpeakerCounts();
    testCollinear();
    testSquare();
    testGridsAndRandom();
    testConstantPowerAndLocality();
    testContinuity();
    testDuplicatesAndNonFinite();
    testFindVirtualSpeakers();
    testVirtualSpeakerGains();

    if (failures == 0)
        std::printf ("All tests passed\n");
    return failures == 0 ? 0 : 1;
}
