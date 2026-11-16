// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file Triangulation.h
    @brief Delaunay triangulation of the speaker positions (JUCE-free). */

#pragma once

#include "tlos/Vec2.h"

#include <span>
#include <vector>

namespace tripan
{
using tlos::Vec2;

/** Indices into the speaker array passed to Triangulation, counter-clockwise. */
struct Triangle
{
    int a; ///< First corner (point index; real speakers first, then virtual ones).
    int b; ///< Second corner.
    int c; ///< Third corner.
};

/** Directed boundary edge (counter-clockwise around the hull). */
struct Edge
{
    int a; ///< Point index of the start of the edge.
    int b; ///< Point index of the end of the edge.
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
        int speaker;  ///< Index of the real speaker.
        float weight; ///< Share of the virtual speaker's gain (amplitude weight).
    };

    /** Creates an empty triangulation (no points). */
    Triangulation() = default;

    /** Triangulates the points. Allocates: call off the audio thread.
        @param speakers         Real speaker positions (index = output channel).
        @param virtualSpeakers  Positions of virtual speakers to take part in the triangulation. */
    explicit Triangulation (std::span<const Vec2> speakers, std::span<const Vec2> virtualSpeakers = {});

    /** All points: the real speakers first, then the virtual ones. */
    std::span<const Vec2> speakers() const noexcept { return points; }

    /** Number of real speakers (including unusable ones such as duplicates). */
    int numReal() const noexcept { return realCount; }

    /** Number of virtual speakers. */
    int numVirtual() const noexcept { return virtualCount; }

    /** The real speakers sharing a triangle with virtual speaker `index` (>= numReal()). Their weights
        are inversely proportional to their distance from the virtual speaker (nearer speakers get
        more) and normalised to unit L2 norm, so that the redistributed power equals the virtual
        gain's. */
    std::span<const Weight> redistribution (int index) const noexcept;

    /** The triangles, counter-clockwise, as indices into speakers(). */
    std::span<const Triangle> triangles() const noexcept { return tris; }

    /** Hull edges (triangle case) or the chain of consecutive speakers (collinear case). */
    std::span<const Edge> boundaryEdges() const noexcept { return edges; }

    /** Index of the only usable speaker when there is exactly one, otherwise -1. */
    int loneSpeaker() const noexcept { return lone; }

private:
    /** Runs Bowyer-Watson on the usable points and fills tris and edges. */
    void build();

    /** Computes the weights of every virtual speaker (see redistribution()). */
    void buildRedistribution();

    int realCount = 0;                ///< See numReal().
    int virtualCount = 0;             ///< See numVirtual().
    std::vector<Weight> weights;      ///< Redistribution weights of all virtual speakers, one after the other.
    std::vector<int> weightStart;     ///< virtualCount + 1 offsets into #weights.
    std::vector<Vec2> points;         ///< Real speakers, then virtual ones.
    std::vector<Triangle> tris;       ///< See triangles().
    std::vector<Edge> edges;          ///< See boundaryEdges().
    int lone = -1;                    ///< See loneSpeaker().
};
}
