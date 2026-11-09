// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <span>
#include <vector>

namespace tripan
{
struct Vec2
{
    double x = 0.0;
    double y = 0.0;
};

/** Indices into the speaker array passed to Triangulation, counter-clockwise. */
struct Triangle
{
    int a, b, c;
};

/** Directed boundary edge (counter-clockwise around the hull). */
struct Edge
{
    int a, b;
};

/** Delaunay triangulation of the speaker positions.

    Build it off the audio thread. Once built it is immutable, so the audio thread may read it
    freely (hand a new one over with an atomic pointer swap).

    Virtual (imaginary) speakers: extra points that take part in the triangulation but are not
    outputs. Points are numbered real speakers first (0..numReal-1), then virtual ones. The gain a
    source sends to a virtual speaker is redistributed over the real speakers next to it (see
    redistribution() and computeGains()), which fills gaps in the layout and removes ambiguities
    such as the arbitrary diagonals of cocircular layouts.

    Degenerate input is handled as follows:
    - Non-finite positions are ignored (such speakers never receive signal).
    - Coincident speakers: only the first one is used, later duplicates never receive signal.
    - 0 usable speakers: no output. 1 usable speaker: that speaker only.
    - All usable speakers collinear: no triangles; the "hull" is the chain of consecutive
      speakers along the line (boundaryEdges()).
*/
class Triangulation
{
public:
    /** A real speaker that receives part of a virtual speaker's gain (the weights of one virtual
        speaker have unit L2 norm). */
    struct Weight
    {
        int speaker;
        float weight;
    };

    Triangulation() = default;
    explicit Triangulation (std::span<const Vec2> speakers, std::span<const Vec2> virtualSpeakers = {});

    /** All points: the real speakers first, then the virtual ones. */
    std::span<const Vec2> speakers() const noexcept { return points; }
    int numReal() const noexcept { return realCount; }
    int numVirtual() const noexcept { return virtualCount; }

    /** The real speakers sharing a triangle with virtual speaker `index` (>= numReal()). Their weights
        are inversely proportional to their distance from the virtual speaker (nearer speakers get
        more) and normalised to unit L2 norm, so that the redistributed power equals the virtual
        gain's. */
    std::span<const Weight> redistribution (int index) const noexcept;
    std::span<const Triangle> triangles() const noexcept { return tris; }

    /** Hull edges (triangle case) or the chain of consecutive speakers (collinear case). */
    std::span<const Edge> boundaryEdges() const noexcept { return edges; }

    /** Index of the only usable speaker when there is exactly one, otherwise -1. */
    int loneSpeaker() const noexcept { return lone; }

private:
    void build();
    void buildRedistribution();

    int realCount = 0;
    int virtualCount = 0;
    std::vector<Weight> weights;
    std::vector<int> weightStart; // virtualCount + 1 offsets into `weights`
    std::vector<Vec2> points;
    std::vector<Triangle> tris;
    std::vector<Edge> edges;
    int lone = -1;
};
}
