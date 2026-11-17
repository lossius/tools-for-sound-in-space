// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file CoreTests.cpp
    @brief Minimal dependency-free test runner for TriPanCore (no JUCE): triangulation, gains and virtual speakers. The shared
    code (limits, groups) is tested in shared/tests. */

#include "tlos/Hull.h"
#include "tripan/Gains.h"
#include "tripan/SpeakerLayout.h"
#include "tripan/Triangulation.h"
#include "tripan/VirtualSpeakers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <random>
#include <vector>

using namespace tripan;

/** Number of failed checks so far; the exit code of main(). */
static int failures = 0;

/** Counts and prints a failed condition, and carries on with the next check. */
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (! (cond)) {                                                      \
            std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);     \
            ++failures;                                                      \
        }                                                                    \
    } while (false)

/** True if `a` and `b` differ by at most `tol`. */
static bool isNear (double a, double b, double tol = 1e-6) { return std::abs (a - b) <= tol; }

/** Twice the signed area of the triangle (o, a, b). */
static double cross (Vec2 o, Vec2 a, Vec2 b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); }

/** Area of the triangle `t` of the points `p`. */
static double triArea (const std::vector<Vec2>& p, const Triangle& t) { return 0.5 * cross (p[(size_t) t.a], p[(size_t) t.b], p[(size_t) t.c]); }

/** Area of the convex hull of `p` (and its vertex count in `hullCount`), used to check that the triangles tile the hull exactly. */
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

/** True if `d` lies strictly inside the circumcircle of triangle `t` (the Delaunay criterion, checked independently of the library). */
static bool strictlyInsideCircumcircle (const std::vector<Vec2>& p, const Triangle& t, Vec2 d)
{
    const auto a = p[(size_t) t.a], b = p[(size_t) t.b], c = p[(size_t) t.c];
    const double adx = a.x - d.x, ady = a.y - d.y, bdx = b.x - d.x, bdy = b.y - d.y, cdx = c.x - d.x, cdy = c.y - d.y;
    const double det = (adx * adx + ady * ady) * (bdx * cdy - cdx * bdy)
                     + (bdx * bdx + bdy * bdy) * (cdx * ady - adx * cdy)
                     + (cdx * cdx + cdy * cdy) * (adx * bdy - bdx * ady);
    return det > 1e-9;
}

/** Sum of the squared gains: 1 for a constant-power result. */
static double sumSq (const SourceGains& g)
{
    double s = 0.0;
    for (int i = 0; i < g.count; ++i) s += (double) g.gain[(size_t) i] * g.gain[(size_t) i];
    return s;
}

/** The gain `g` gives to `speaker` (0 if it gets none). */
static float gainFor (const SourceGains& g, int speaker)
{
    for (int i = 0; i < g.count; ++i)
        if (g.speaker[(size_t) i] == speaker) return g.gain[(size_t) i];
    return 0.0f;
}

/** Checks a triangulation of `pts`: valid indices, counter-clockwise triangles, Delaunay property, and that the triangles cover the hull. */
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

/** Zero, one and two speakers give no or partial triangulations and sensible gains. */
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

/** Speakers on a line have no triangles; a source pans between its two neighbours along the line. */
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

/** A square (cocircular points) is triangulated consistently, with constant power everywhere. */
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

/** Grids and random layouts give valid Delaunay triangulations. */
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

/** Gains are constant power and only the three speakers of the containing triangle receive signal. */
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

/** Gains change continuously when a source crosses triangle edges and the hull boundary. */
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

/** Coincident and non-finite speakers never receive signal; the first of a coincident pair wins. */
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

/** Speakers on a circle of `radius` at the given azimuths (degrees, clockwise from the front). */
static std::vector<Vec2> ring (std::initializer_list<double> azimuthsDegrees, double radius = 5.0)
{
    std::vector<Vec2> pts;
    for (double a : azimuthsDegrees)
        pts.push_back ({ radius * std::sin (a * 3.14159265358979 / 180.0), radius * std::cos (a * 3.14159265358979 / 180.0) });
    return pts;
}

/** findVirtualSpeakers() on 5.1, quad, rings, open layouts and degenerate input. */
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
            CHECK (std::abs (p.strength - 1.0) < 1e-9);          // 5.1 clearly needs both
            if (std::hypot (p.position.x, p.position.y) < 1e-9) centre = true;
            if (std::abs (p.position.x) < 1e-9 && std::abs (p.position.y + 5.0) < 1e-9) back = true; // directly behind, same radius
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
    {
        // A front arc: the listener is not enclosed, so no gap fillers or ring centre; but five speakers on one circle are
        // one face, and its centre (inside the arc, on the axis) splits it symmetrically instead of leaving arbitrary diagonals.
        const auto arc = findVirtualSpeakers (ring ({ -60, -30, 0, 30, 60 }));
        CHECK (arc.size() == 1 && arc[0].strength > 0.999 && std::abs (arc[0].position.x) < 1e-9);
        CHECK (arc[0].position.y > 2.5 && arc[0].position.y < 5.0);                               // inside the arc's polygon
    }
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
    CHECK (c.size() == 1 && std::hypot (c[0].position.x - 2.0, c[0].position.y + 1.0) < 1e-9);

    // The count is capped.
    VirtualSpeakerOptions o;
    o.maxCount = 2;
    CHECK (findVirtualSpeakers (ring ({ 0, 120, 240 }), {}, o).size() == 2);
}

/** Gains with virtual speakers: constant power, redistribution to real neighbours, symmetry for a 5.1 source at the origin. */
static void testVirtualSpeakerGains()
{
    const auto fivePointOne = ring ({ -30, 30, 0, -110, 110 });         // L, R, C, Ls, Rs
    std::vector<Vec2> v;
    for (const auto& virtualSpeaker : findVirtualSpeakers (fivePointOne))
        v.push_back (virtualSpeaker.position);
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

/** The positions of a list of virtual speakers. */
static std::vector<Vec2> positionsOf (const std::vector<VirtualSpeaker>& v)
{
    std::vector<Vec2> out;
    for (const auto& s : v) out.push_back (s.position);
    return out;
}

/** The decisions are soft: strengths rise smoothly with the gap, the ring-ness and the enclosure. */
static void testVirtualSpeakerStrengths()
{
    const auto backGapOf = [] (double surroundAzimuth) { return findVirtualSpeakers (ring ({ -30, 30, 0, -surroundAzimuth, surroundAzimuth })); };

    // Surrounds at 135 degrees -> back gap 90: nothing needed there; at 125 -> 110 degrees: fully filled.
    const auto gapStrength = [&] (double azimuth)
    {
        double best = 0.0;
        for (const auto& v : backGapOf (azimuth))
            if (v.position.y < -3.0) best = v.strength; // the one in the back
        return best;
    };
    CHECK (gapStrength (135.0) < 1e-6);                                 // gap 90 degrees
    CHECK (gapStrength (130.0) > 0.4 && gapStrength (130.0) < 0.6);     // gap 100: half
    CHECK (std::abs (gapStrength (125.0) - 1.0) < 1e-6);                // gap 110: full
    CHECK (gapStrength (132.0) < gapStrength (130.0) && gapStrength (130.0) < gapStrength (128.0)); // monotonic

    // Ring-ness: the centre speaker fades out as the radii become uneven, and in as speakers move away from the centre.
    const auto centreStrength = [] (double radiusOfOne)
    {
        auto pts = ring ({ 0, 90, 180, 270 });
        pts[0].y = radiusOfOne; // front speaker at a different distance
        for (const auto& v : findVirtualSpeakers (pts))
            if (std::hypot (v.position.x, v.position.y) < 1e-9) return v.strength;
        return 0.0;
    };
    CHECK (std::abs (centreStrength (5.0) - 1.0) < 1e-9);
    CHECK (centreStrength (5.8) > 0.999);                               // 15% spread: still a ring
    CHECK (centreStrength (6.5) > 0.0 && centreStrength (6.5) < 1.0);   // 28% spread: partly a ring
    CHECK (centreStrength (6.5) < centreStrength (6.0) || centreStrength (6.0) > 0.999);
    CHECK (centreStrength (8.0) < 1e-6);                                // far from a ring

    // Enclosure: as the largest gap grows from 160 to 180 degrees the strengths fade out, then nothing remains.
    // (Speakers at -a, 0, a: the back gap is 360 - 2a.)
    const auto totalStrength = [] (double a)
    {
        double sum = 0.0;
        for (const auto& v : findVirtualSpeakers (ring ({ -a, 0, a }))) sum += v.strength;
        return sum;
    };
    CHECK (totalStrength (100.0) > 2.9);                                // back gap 160: fully enclosed
    CHECK (totalStrength (95.0) > 0.1 && totalStrength (95.0) < totalStrength (100.0)); // gap 170: partly
    CHECK (totalStrength (90.0) < 1e-9 && totalStrength (80.0) < 1e-9); // gap 180 or more: nothing

    // A layout with a very small softness behaves like hard thresholds.
    VirtualSpeakerOptions hard;
    hard.gapSoftnessDegrees = hard.ringSoftness = hard.centreSoftness = 0.0;
    hard.enclosedBelowDegrees = 180.0;
    for (const auto& v : findVirtualSpeakers (ring ({ -30, 30, 0, -110, 110 }), {}, hard))
        CHECK (std::abs (v.strength - 1.0) < 1e-12);
}

/** SpeakerLayout blends nested layers with the strengths as weights. */
static void testSpeakerLayoutBlend()
{
    const auto base = ring ({ -30, 30, 0, -110, 110 });

    // Strength 1 reproduces a plain Triangulation with those virtual speakers.
    {
        const auto v = findVirtualSpeakers (base);
        const SpeakerLayout layout (base, v);
        const Triangulation tri (base, positionsOf (v));
        CHECK (layout.numLayers() == 1 && std::abs (layout.layerWeight (0) - 1.0) < 1e-12);

        std::mt19937 rng (21);
        std::uniform_real_distribution<double> u (-8.0, 8.0);
        for (int k = 0; k < 300; ++k)
        {
            const Vec2 s { u (rng), u (rng) };
            const auto a = computeGains (layout, s), b = computeGains (tri, s);
            CHECK (a.count == b.count);
            for (int i = 0; i < 5; ++i) CHECK (isNear (gainFor (a, i), gainFor (b, i), 1e-6));
        }
    }

    // No virtual speakers: one layer, identical to the plain triangulation.
    {
        const SpeakerLayout layout (base);
        CHECK (layout.numLayers() == 1 && layout.virtualSpeakers().empty());
        CHECK (computeGains (layout, { 0.0, 0.0 }).count == computeGains (Triangulation (base), { 0.0, 0.0 }).count);
    }

    // Partial strengths: weights add up to 1, strongest first, and the result keeps constant power.
    {
        const std::vector<VirtualSpeaker> v { { { 0.0, 0.0 }, 0.3 }, { { 0.0, -5.0 }, 0.8 }, { { 9.0, 9.0 }, 0.0 } };
        const SpeakerLayout layout (base, v);

        CHECK (layout.virtualSpeakers().size() == 2);                   // the zero-strength one is dropped
        CHECK (std::abs (layout.virtualSpeakers()[0].strength - 0.8) < 1e-12 && std::abs (layout.virtualSpeakers()[1].strength - 0.3) < 1e-12);
        CHECK (layout.numLayers() == 3);
        double sumWeights = 0.0;
        for (int i = 0; i < layout.numLayers(); ++i) sumWeights += layout.layerWeight (i);
        CHECK (isNear (sumWeights, 1.0, 1e-12));
        CHECK (isNear (layout.layerWeight (0), 0.2, 1e-12) && isNear (layout.layerWeight (1), 0.5, 1e-12) && isNear (layout.layerWeight (2), 0.3, 1e-12));
        CHECK (layout.layer (0).numVirtual() == 0 && layout.layer (2).numVirtual() == 2);
        CHECK (layout.full().numVirtual() == 2);

        std::mt19937 rng (22);
        std::uniform_real_distribution<double> u (-9.0, 9.0);
        for (int k = 0; k < 300; ++k)
        {
            const auto g = computeGains (layout, { u (rng), u (rng) });
            CHECK (isNear (sumSq (g), 1.0, 1e-5));
        }
    }

    // Equal strengths and odd input do not break anything.
    {
        const SpeakerLayout equalStrengths (base, std::vector<VirtualSpeaker> { { { 0.0, 0.0 }, 0.5 }, { { 0.0, -5.0 }, 0.5 } });
        CHECK (equalStrengths.numLayers() == 2); // the middle layer has no weight and is not built
        const SpeakerLayout nan (base, std::vector<VirtualSpeaker> { { { std::nan (""), 0.0 }, 1.0 } });
        CHECK (nan.virtualSpeakers().empty());
        const SpeakerLayout empty;
        CHECK (computeGains (empty, { 0.0, 0.0 }).count == 0 && empty.full().triangles().empty());
    }
}

/** The point of the soft decisions: while a speaker moves across a threshold, the gains of a source change smoothly. With hard thresholds (the old behaviour) they jump. */
static void testNoJumpsWhileSpeakersMove()
{
    VirtualSpeakerOptions hard;
    hard.gapSoftnessDegrees = hard.ringSoftness = hard.centreSoftness = 0.0;
    hard.enclosedBelowDegrees = 180.0;

    const Vec2 sources[] = { { 1.0, -3.0 }, { 0.0, -4.0 }, { 0.5, 0.5 }, { -2.0, -2.0 }, { 3.0, 3.0 }, { 0.0, -1.0 } };

    const auto sweep = [&] (const VirtualSpeakerOptions& options)
    {
        double worst = 0.0;
        std::vector<SourceGains> prev (std::size (sources));
        bool have = false;

        // The surrounds move from 145 degrees (back gap 70) to 105 degrees (back gap 150): across 90 and 110.
        for (double azimuth = 145.0; azimuth >= 105.0; azimuth -= 0.005)
        {
            const auto speakers = ring ({ -30, 30, 0, -azimuth, azimuth });
            const SpeakerLayout layout (speakers, findVirtualSpeakers (speakers, {}, options));

            for (size_t k = 0; k < std::size (sources); ++k)
            {
                const auto g = computeGains (layout, sources[k]);
                if (have)
                    for (int i = 0; i < 5; ++i)
                        worst = std::max (worst, (double) std::abs (gainFor (g, i) - gainFor (prev[k], i)));
                prev[k] = g;
            }
            have = true;
        }
        return worst;
    };

    const double soft = sweep ({});
    const double jumpy = sweep (hard);
    std::printf ("largest gain jump per 0.005 degree step while a surround speaker moves: soft %.4f, hard thresholds %.4f\n", soft, jumpy);
    CHECK (soft < 0.01);
    CHECK (jumpy > 0.1); // the hard version jumps, which is what the soft blend is for
}

/** The share sent to a virtual speaker is split over its real neighbours in inverse proportion to distance. */
static void testDistanceWeightedRedistribution()
{
    const std::vector<Vec2> real { { 0.0, 4.0 }, { 5.0, -3.0 }, { -6.0, -2.0 } };
    const Vec2 v { 1.0, 0.0 };
    const Triangulation tri (real, std::vector<Vec2> { v });

    const auto w = tri.redistribution (3);
    CHECK (w.size() == 3);

    double d[3], inverseNorm = 0.0;
    for (int i = 0; i < 3; ++i)
    {
        d[i] = std::hypot (real[(size_t) i].x - v.x, real[(size_t) i].y - v.y);
        inverseNorm += 1.0 / (d[i] * d[i]);
    }
    inverseNorm = std::sqrt (inverseNorm);

    double sumSquares = 0.0;
    for (const auto& e : w)
    {
        CHECK (isNear (e.weight, (1.0 / d[e.speaker]) / inverseNorm, 1e-6)); // w_i ~ 1 / d_i, unit norm
        sumSquares += (double) e.weight * e.weight;
    }
    CHECK (isNear (sumSquares, 1.0, 1e-6));

    // The nearest speaker gets the largest share, the farthest the smallest.
    int nearest = 0, farthest = 0;
    for (int i = 1; i < 3; ++i) { if (d[i] < d[nearest]) nearest = i; if (d[i] > d[farthest]) farthest = i; }
    float shareOf[3] {};
    for (const auto& e : w) shareOf[e.speaker] = e.weight;
    CHECK (shareOf[nearest] > shareOf[farthest] && nearest != farthest);

    // A source exactly on the virtual speaker is panned with these weights.
    const auto g = computeGains (tri, v);
    CHECK (g.count == 3);
    for (int i = 0; i < 3; ++i)
        CHECK (isNear (gainFor (g, i), shareOf[i], 1e-5));
    CHECK (isNear (sumSq (g), 1.0, 1e-5));

    // Equidistant neighbours (a ring centre) still share equally.
    const auto fivePointOne = ring ({ -30, 30, 0, -110, 110 });
    std::vector<Vec2> centre { { 0.0, 0.0 } };
    const Triangulation ringTri (fivePointOne, centre);
    for (const auto& e : ringTri.redistribution (5))
        CHECK (isNear (e.weight, 1.0 / std::sqrt (5.0), 1e-6));

    // A neighbour almost on top of the virtual speaker takes nearly everything, without blowing up.
    const Triangulation close (real, std::vector<Vec2> { { 0.0, 4.0005 } });
    float closest = 0.0f;
    bool finite = true;
    for (const auto& e : close.redistribution (3))
    {
        finite = finite && std::isfinite (e.weight);
        if (e.speaker == 0) closest = e.weight;
    }
    CHECK (finite && closest > 0.99f);
}

/** Per-source spread (MDAP-style): the source is panned from itself plus a ring of points around it. */
static void testSpread()
{
    const auto fivePointOne = ring ({ -30, 30, 0, -110, 110 });
    const SpeakerLayout layout (fivePointOne, findVirtualSpeakers (fivePointOne));
    const std::vector<Vec2> square { { 0, 0 }, { 6, 0 }, { 6, 6 }, { 0, 6 }, { 3, 3 } };
    const SpeakerLayout plain (square);

    // No spread: identical to the plain gains. Odd values count as no spread.
    {
        std::mt19937 rng (41);
        std::uniform_real_distribution<double> u (-8.0, 8.0);
        for (int k = 0; k < 200; ++k)
        {
            const Vec2 s { u (rng), u (rng) };
            const auto a = computeGains (layout, s), b = computeGains (layout, s, 0.0);
            CHECK (a.count == b.count);
            for (int i = 0; i < 5; ++i) CHECK (isNear (gainFor (a, i), gainFor (b, i), 1e-9));
            CHECK (computeGains (layout, s, -1.0).count == a.count && computeGains (layout, s, std::nan ("")).count == a.count);
        }
    }

    // Constant power for any position and spread.
    {
        std::mt19937 rng (42);
        std::uniform_real_distribution<double> u (-9.0, 9.0), spreadDistribution (0.0, 12.0);
        for (int k = 0; k < 500; ++k)
            CHECK (isNear (sumSq (computeGains (layout, { u (rng), u (rng) }, spreadDistribution (rng))), 1.0, 1e-5));
        CHECK (isNear (sumSq (computeGains (layout, { 1.0, 1.0 }, 1e9)), 1.0, 1e-5)); // absurd values are limited
    }

    // A source sitting on a speaker: without spread only that speaker; the more spread, the more it is shared.
    {
        const Vec2 onCorner { 0.0, 0.0 };
        const auto activeSpeakers = [] (const SourceGains& g) { int n = 0; for (int i = 0; i < g.count; ++i) if (g.gain[(size_t) i] > 1e-3f) ++n; return n; };

        const auto none = computeGains (plain, onCorner, 0.0);
        CHECK (activeSpeakers (none) == 1 && isNear (gainFor (none, 0), 1.0, 1e-6));

        double previous = 1.0;
        int previousCount = 1;
        for (double spread : { 0.5, 1.0, 2.0, 3.0 })
        {
            const auto g = computeGains (plain, onCorner, spread);
            CHECK (gainFor (g, 0) < previous);                // the speaker under the source gets less and less
            CHECK (activeSpeakers (g) >= previousCount);      // and more speakers take part
            previous = gainFor (g, 0);
            previousCount = activeSpeakers (g);
        }
        CHECK (previousCount >= 3);
    }

    // Mirror symmetry is kept (L<->R, Ls<->Rs).
    {
        const int mirror[] = { 1, 0, 2, 4, 3 };
        std::mt19937 rng (43);
        std::uniform_real_distribution<double> u (-8.0, 8.0);
        for (int k = 0; k < 300; ++k)
        {
            const Vec2 s { u (rng), u (rng) };
            const double spread = 0.5 + 3.0 * std::abs (u (rng)) / 8.0;
            const auto a = computeGains (layout, s, spread), b = computeGains (layout, { -s.x, s.y }, spread);
            for (int i = 0; i < 5; ++i) CHECK (isNear (gainFor (a, i), gainFor (b, mirror[i]), 1e-4));
        }
    }

    // Continuity: no jumps when the spread or the position changes in small steps.
    {
        double worst = 0.0;
        SourceGains prev;
        bool have = false;
        for (double spread = 0.0; spread <= 4.0; spread += 0.002)
        {
            const auto g = computeGains (layout, { 0.7, 1.3 }, spread);
            if (have) for (int i = 0; i < 5; ++i) worst = std::max (worst, (double) std::abs (gainFor (g, i) - gainFor (prev, i)));
            prev = g;
            have = true;
        }
        CHECK (worst < 0.02);

        worst = 0.0;
        have = false;
        for (double x = -9.0; x <= 9.0; x += 0.002)
        {
            const auto g = computeGains (layout, { x, 1.0 }, 1.5);
            if (have) for (int i = 0; i < 5; ++i) worst = std::max (worst, (double) std::abs (gainFor (g, i) - gainFor (prev, i)));
            prev = g;
            have = true;
        }
        CHECK (worst < 0.02);
    }

    // The empty layout gives nothing (no crash).
    CHECK (computeGains (SpeakerLayout(), { 0.0, 0.0 }, 2.0).count == 0);
}

/** Falloff outside the speakers: exactly the old gains inside and when it is off, the factor outside, continuous with spread. */
static void testOutsideFalloff()
{
    std::vector<Vec2> octagon;
    for (int i = 0; i < 8; ++i)
        octagon.push_back ({ 5.0 * std::sin (i * 3.14159265358979 / 4.0), 5.0 * std::cos (i * 3.14159265358979 / 4.0) });
    const SpeakerLayout layout (octagon, findVirtualSpeakers (octagon));
    CHECK (layout.hull().hasArea());

    const auto sameGains = [] (const SourceGains& a, const SourceGains& b)
    {
        if (a.count != b.count)
            return false;
        for (int i = 0; i < a.count; ++i)
            if (a.speaker[(size_t) i] != b.speaker[(size_t) i] || a.gain[(size_t) i] != b.gain[(size_t) i])
                return false;
        return true;
    };

    // Off: identical to the plain computation, inside and outside, with and without spread.
    for (const Vec2 p : { Vec2 { 0.3, 0.2 }, Vec2 { 0.0, 6.5 }, Vec2 { -9.0, 3.0 } })
        for (const double spread : { 0.0, 1.5 })
        {
            CHECK (sameGains (computeGains (layout, p, spread, 0.0), computeGains (layout, p, spread)));
            CHECK (sameGains (computeGains (layout, p, spread, -3.0), computeGains (layout, p, spread)));
        }

    // Inside the layout and on its boundary: the same gains as without falloff (spread 0).
    for (const Vec2 p : { Vec2 { 0.0, 0.0 }, Vec2 { 1.0, 2.0 }, Vec2 { 0.0, 5.0 }, Vec2 { -3.0, -3.0 } })
        CHECK (sameGains (computeGains (layout, p, 0.0, 6.0), computeGains (layout, p, 0.0)));

    // Outside, without spread: the same distribution, scaled by the factor.
    const Vec2 outside { 0.0, 6.0 }; // 1 m beyond the speaker at the front
    const double factor = tlos::outsideFactor (1.0, 6.0206);
    CHECK (isNear (factor, 0.5, 1e-3));
    const auto plain = computeGains (layout, outside, 0.0);
    const auto fallen = computeGains (layout, outside, 0.0, 6.0206);
    CHECK (fallen.count == plain.count);
    for (int i = 0; i < plain.count; ++i)
        CHECK (isNear ((double) fallen.gain[(size_t) i], factor * (double) plain.gain[(size_t) i], 1e-5));
    CHECK (isNear (sumSq (fallen), factor * factor, 1e-4));

    // With spread: the power is between the extremes, and it moves continuously and steadily while the source goes outwards.
    double previous = 2.0;
    for (int i = 0; i <= 160; ++i)
    {
        const double y = 3.0 + 0.05 * i; // from well inside to 6 m outside
        const double power = sumSq (computeGains (layout, { 0.0, y }, 2.0, 6.0));
        CHECK (power <= previous + 1e-6);
        CHECK (previous > 1.5 || previous - power < 0.06);
        CHECK (power > 0.0 && power <= 1.0 + 1e-5);
        previous = power;
    }
    CHECK (isNear (sumSq (computeGains (layout, { 0.0, 0.0 }, 2.0, 6.0)), 1.0, 1e-4));            // a spread circle fully inside

    // A spread circle half out costs less than a point source at the same place would, but more than one fully out.
    const double half = sumSq (computeGains (layout, { 0.0, 5.0 }, 2.0, 6.0));
    CHECK (half < 1.0 - 0.01 && half > sumSq (computeGains (layout, { 0.0, 9.0 }, 2.0, 6.0)));

    // The hull is of the real speakers: without area there is no falloff (the stereo pair must stay as it is).
    const std::vector<Vec2> pair { { -2.0, 3.0 }, { 2.0, 3.0 } };
    const SpeakerLayout stereo (pair);
    CHECK (! stereo.hull().hasArea());
    CHECK (sameGains (computeGains (stereo, { 5.0, -4.0 }, 0.0, 6.0), computeGains (stereo, { 5.0, -4.0 }, 0.0)));
    const std::vector<Vec2> line { { 0.0, 0.0 }, { 0.0, 2.0 }, { 0.0, 4.0 } };
    const SpeakerLayout column (line);
    CHECK (sameGains (computeGains (column, { 3.0, 9.0 }, 1.0, 6.0), computeGains (column, { 3.0, 9.0 }, 1.0)));
}

/** Gains of a source for the speakers with automatic virtual speakers, mirrored left-right: the largest difference over sources on a grid. */
static double mirrorAsymmetry (const std::vector<Vec2>& speakers, bool withVirtual, bool mirrorY = false)
{
    const SpeakerLayout layout (speakers, withVirtual ? findVirtualSpeakers (speakers) : std::vector<VirtualSpeaker> {});
    std::vector<int> mirror (speakers.size(), -1);
    for (size_t i = 0; i < speakers.size(); ++i)
        for (size_t j = 0; j < speakers.size(); ++j)
            if (std::hypot (mirrorY ? speakers[i].x - speakers[j].x : speakers[i].x + speakers[j].x, mirrorY ? speakers[i].y + speakers[j].y : speakers[i].y - speakers[j].y) < 1e-6)
                mirror[i] = (int) j;
    for (const int m : mirror)
        if (m < 0)
            return 1e9;                                              // not a symmetric layout

    double worst = 0.0;
    for (int ix = -9; ix <= 9; ++ix)
        for (int iy = -9; iy <= 9; ++iy)
        {
            const Vec2 p { 0.53 * ix + 0.011 * iy, 0.47 * iy - 0.007 * ix };      // not exactly on any symmetry line
            const Vec2 q = mirrorY ? Vec2 { p.x, -p.y } : Vec2 { -p.x, p.y };
            const auto g = computeGains (layout, p), h = computeGains (layout, q);
            for (size_t i = 0; i < speakers.size(); ++i)
                worst = std::max (worst, std::abs ((double) gainFor (g, (int) i) - (double) gainFor (h, mirror[i])));
        }
    return worst;
}

/** A grid of columns x rows speakers with the given spacing, centred on `centre`. */
static std::vector<Vec2> gridOf (int columns, int rows, double spacing, Vec2 centre = {})
{
    std::vector<Vec2> out;
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < columns; ++c)
            out.push_back ({ centre.x + (c - 0.5 * (columns - 1)) * spacing, centre.y + (r - 0.5 * (rows - 1)) * spacing });
    return out;
}

/** Cocircular faces get a virtual speaker, so that grids, polygons and arcs are panned symmetrically, for any listener position and any layout. */
static void testCocircularFaces()
{
    // The layer shortcut in computeGains must give what evaluating every layer from scratch gives.
    {
        std::mt19937 rng (21);
        std::uniform_real_distribution<double> u (-8.0, 8.0);
        for (int trial = 0; trial < 20; ++trial)
        {
            auto speakers = gridOf (4 + trial % 3, 3 + trial % 4, 4.0);
            if (trial % 2 == 1)
                for (auto& p : speakers)
                    p = { p.x + 0.05 * u (rng), p.y + 0.05 * u (rng) };
            const SpeakerLayout layout (speakers, findVirtualSpeakers (speakers, { 100.0, 100.0 }));
            for (int k = 0; k < 200; ++k)
            {
                const Vec2 s { u (rng), u (rng) };
                const auto a = computeGains (layout, s), b = computeGainsReference (layout, s);
                for (int i = 0; i < (int) speakers.size(); ++i)
                    CHECK (isNear ((double) gainFor (a, i), (double) gainFor (b, i), 1e-9));
            }
        }
    }

    // One grid cell: its centre, strength 1, whether or not the listener is at the centre or enclosed at all.
    for (const Vec2 centre : { Vec2 { 0, 0 }, Vec2 { 0, 10 }, Vec2 { -7, 3 } })
    {
        const auto v = findVirtualSpeakers (gridOf (2, 2, 10.0, centre));
        CHECK (v.size() == 1 && v[0].strength > 0.999 && isNear (v[0].position.x, centre.x, 1e-6) && isNear (v[0].position.y, centre.y, 1e-6));
    }

    // A grid of columns x rows speakers has (columns - 1) x (rows - 1) cell centres, at the centres of the cells.
    for (const auto& [columns, rows] : { std::pair { 3, 3 }, std::pair { 4, 4 }, std::pair { 8, 8 }, std::pair { 8, 2 }, std::pair { 5, 3 } })
    {
        const double spacing = 10.0 / (double) (std::max (columns, rows) - 1);
        const auto grid = gridOf (columns, rows, spacing, { 1.5, -2.0 });
        const auto v = findVirtualSpeakers (grid, { 100.0, 100.0 });    // a listener far away: only the cell centres
        CHECK ((int) v.size() == (columns - 1) * (rows - 1));
        for (const auto& f : v)
        {
            CHECK (f.strength > 0.999);
            const double fx = (f.position.x - 1.5) / spacing + 0.5 * (columns - 1), fy = (f.position.y + 2.0) / spacing + 0.5 * (rows - 1);
            CHECK (isNear (fx - std::floor (fx), 0.5, 1e-6) && isNear (fy - std::floor (fy), 0.5, 1e-6));   // in the middle of a cell
        }
    }

    // Typed values, rounded to three decimals, are still a grid.
    {
        auto grid = gridOf (4, 4, 10.0 / 3.0);
        for (auto& p : grid)
            p = { std::round (p.x * 1000.0) / 1000.0, std::round (p.y * 1000.0) / 1000.0 };
        const auto v = findVirtualSpeakers (grid);
        CHECK (v.size() == 9);
        for (const auto& f : v)
            CHECK (f.strength > 0.99);
    }

    // The faces fade out as the cocircularity goes away: nothing is left at a few per cent.
    for (const double jitter : { 0.0005, 0.003, 0.01, 0.02, 0.05 })
    {
        std::mt19937 rng (7);
        std::uniform_real_distribution<double> u (-1.0, 1.0);
        auto grid = gridOf (3, 3, 5.0);
        for (auto& p : grid)
            p = { p.x + 5.0 * jitter * u (rng), p.y + 5.0 * jitter * u (rng) };
        const auto v = findVirtualSpeakers (grid, { 100.0, 100.0 });
        double strongest = 0.0;
        for (const auto& f : v)
            strongest = std::max (strongest, f.strength);
        if (jitter <= 0.003)
            CHECK (v.size() == 4 && strongest > 0.5);
        if (jitter >= 0.05)
            CHECK (strongest < 0.5);                                    // a cell may be nearly cocircular by chance, but only weakly
    }

    // Polygons: the centre of a regular polygon, merged with the ring centre.
    for (const int sides : { 5, 6, 8, 12 })
    {
        std::vector<Vec2> polygon;
        for (int i = 0; i < sides; ++i)
            polygon.push_back ({ 5.0 * std::sin (2.0 * 3.14159265358979 * i / sides), 5.0 * std::cos (2.0 * 3.14159265358979 * i / sides) });
        const auto v = findVirtualSpeakers (polygon);
        CHECK (v.size() == 1 && isNear (v[0].position.x, 0.0, 1e-6) && isNear (v[0].position.y, 0.0, 1e-6) && v[0].strength > 0.999);
        // Not around the listener: still the centre of the polygon, found without the ring rule.
        const auto shifted = findVirtualSpeakers (polygon, { 20.0, 0.0 });
        CHECK (shifted.size() == 1 && isNear (shifted[0].position.x, 0.0, 1e-6) && shifted[0].strength > 0.999);
    }

    // Symmetry of the panning: with the face centres the gains of mirrored sources are mirrored; without them they are not.
    for (const auto& [columns, rows] : { std::pair { 2, 2 }, std::pair { 3, 3 }, std::pair { 4, 4 }, std::pair { 8, 8 }, std::pair { 8, 2 }, std::pair { 5, 3 } })
    {
        const auto grid = gridOf (columns, rows, 10.0 / (double) (std::max (columns, rows) - 1));
        CHECK (mirrorAsymmetry (grid, true) < 1e-5);
        CHECK (mirrorAsymmetry (grid, true, true) < 1e-5);
    }
    CHECK (mirrorAsymmetry (gridOf (3, 3, 5.0), false) > 0.05);                        // the problem that this fixes
    CHECK (mirrorAsymmetry (gridOf (4, 4, 3.0), false) > 0.05);
    {
        std::vector<Vec2> arc;
        for (const double azimuth : { -60.0, -30.0, 0.0, 30.0, 60.0 })
            arc.push_back ({ 5.0 * std::sin (azimuth * 3.14159265358979 / 180.0), 5.0 * std::cos (azimuth * 3.14159265358979 / 180.0) });
        CHECK (mirrorAsymmetry (arc, true) < 1e-5);
        CHECK (mirrorAsymmetry (arc, false) > 0.01);
    }

    // Continuity: move one corner of a square cell through the cocircular position; the gains never jump.
    {
        const std::vector<Vec2> sources { { 1.0, 2.0 }, { -3.0, 1.0 }, { 0.2, -0.1 }, { 4.0, 4.0 }, { -4.5, -2.5 } };
        std::vector<std::vector<double>> previous;
        double biggestStep = 0.0, biggestStrength = 0.0;
        bool anyFace = false, anyNone = false;
        for (int step = -240; step <= 240; ++step)
        {
            const double t = 0.0025 * step;                          // 5 m radius circle: up to 0.6 m off the square
            std::vector<Vec2> cell { { -5, -5 }, { 5, -5 }, { 5 + t, 5 + t }, { -5, 5 } };
            const auto virtualSpeakers = findVirtualSpeakers (cell, { 20.0, 20.0 });     // the listener is far away: only the face rule applies
            double strength = 0.0;
            for (const auto& v : virtualSpeakers)
                strength = std::max (strength, v.strength);
            biggestStrength = std::max (biggestStrength, strength);
            anyFace = anyFace || strength > 0.99;
            anyNone = anyNone || virtualSpeakers.empty();

            const SpeakerLayout layout (cell, virtualSpeakers);
            std::vector<std::vector<double>> now;
            for (const auto& source : sources)
            {
                const auto g = computeGains (layout, source);
                std::vector<double> row;
                for (int i = 0; i < 4; ++i)
                    row.push_back ((double) gainFor (g, i));
                now.push_back (row);
                CHECK (isNear (sumSq (g), 1.0, 1e-5));
            }

            if (! previous.empty())
                for (size_t k = 0; k < now.size(); ++k)
                    for (size_t i = 0; i < 4; ++i)
                        biggestStep = std::max (biggestStep, std::abs (now[k][i] - previous[k][i]));
            previous = now;
        }
        CHECK (anyFace && anyNone && biggestStrength > 0.99);       // it goes from nothing, through a face, to nothing
        CHECK (biggestStep < 0.03);                                    // 0.0025 m per step: no jumps
    }

    // Any layout: constant power and finite gains, also with random positions, mixed faces, duplicates and a lone speaker.
    {
        std::mt19937 rng (99);
        std::uniform_real_distribution<double> u (-8.0, 8.0);
        std::vector<std::vector<Vec2>> layouts;
        for (int n = 3; n <= 24; ++n)
        {
            std::vector<Vec2> random;
            for (int i = 0; i < n; ++i)
                random.push_back ({ u (rng), u (rng) });
            layouts.push_back (random);
        }
        auto mixed = gridOf (3, 3, 4.0);
        for (const double azimuth : { 10.0, 100.0, 190.0 })
            mixed.push_back ({ 9.0 * std::sin (azimuth * 0.017453), 9.0 * std::cos (azimuth * 0.017453) });
        layouts.push_back (mixed);
        auto duplicated = gridOf (3, 3, 4.0);
        duplicated.push_back (duplicated[0]);
        layouts.push_back (duplicated);
        layouts.push_back (gridOf (8, 8, 1.4));
        layouts.push_back (gridOf (1, 5, 2.0));                                    // a line
        layouts.push_back (std::vector<Vec2> { { 1, 1 } });

        for (const auto& speakers : layouts)
        {
            const auto virtualSpeakers = findVirtualSpeakers (speakers);
            CHECK ((int) virtualSpeakers.size() <= VirtualSpeakerOptions().maxCount);
            CHECK (virtualSpeakers.size() == findVirtualSpeakers (speakers).size());    // no memory, deterministic
            const SpeakerLayout layout (speakers, virtualSpeakers);
            for (int k = 0; k < 40; ++k)
            {
                const auto g = computeGains (layout, { u (rng), u (rng) });
                CHECK (g.count > 0 && isNear (sumSq (g), 1.0, 1e-4));
                for (int i = 0; i < g.count; ++i)
                    CHECK (std::isfinite (g.gain[(size_t) i]) && g.speaker[(size_t) i] >= 0 && g.speaker[(size_t) i] < (int) speakers.size());
            }
        }
    }
}

/** Runs all core tests. @return 0 if every check passed, otherwise 1. */
int main()
{
    testDegenerateSpeakerCounts();
    testCollinear();
    testSquare();
    testGridsAndRandom();
    testConstantPowerAndLocality();
    testContinuity();
    testDuplicatesAndNonFinite();
    testFindVirtualSpeakers();
    testVirtualSpeakerGains();
    testDistanceWeightedRedistribution();
    testSpread();
    testOutsideFalloff();
    testCocircularFaces();
    testVirtualSpeakerStrengths();
    testSpeakerLayoutBlend();
    testNoJumpsWhileSpeakersMove();

    if (failures == 0)
        std::printf ("All tests passed\n");
    return failures == 0 ? 0 : 1;
}
