// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file SpeakerLayout.h
    @brief The real speakers plus smoothly blended virtual speakers, and the gain computation on top of it. */

#pragma once

#include "tlos/Hull.h"
#include "tripan/Gains.h"
#include "tripan/Triangulation.h"
#include "tripan/VirtualSpeakers.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace tripan
{
/** The real speakers plus virtual speakers with strengths, ready for panning. Immutable.

    A virtual speaker with strength 1 is simply part of the triangulation. A virtual speaker that
    is only partly called for (strength s < 1) is faded in smoothly, so that the gains change
    continuously while speakers are moved, with no jump when a threshold is crossed and no memory
    of earlier states.

    This is done with nested layers. The virtual speakers are sorted by strength, s1 >= s2 >= ... >= sk.
    Layer 0 is the real speakers only, layer j adds the j strongest virtual speakers. The gains are
    the weighted sum of the gains of the layers, renormalised to constant power, with weights
    (1 - s1) for layer 0 and (s_j - s_(j+1)) for layer j. If all strengths are 1, only the last layer
    has weight and the result is exactly that of one Triangulation with all virtual speakers. */
class SpeakerLayout
{
public:
    /** Creates an empty layout (no layers, id 0). */
    SpeakerLayout() = default;

    /** Builds all layers. Allocates: call off the audio thread.
        @param realSpeakers     Positions of the real speakers (index = output channel).
        @param virtualSpeakers  Virtual speakers with their strengths; zero-strength ones are dropped. */
    SpeakerLayout (std::span<const Vec2> realSpeakers, std::span<const VirtualSpeaker> virtualSpeakers = {});

    /** Unique number of this layout (0 for a default-constructed one), so that a cached result can be
        tied to the layout it was computed from (a pointer is not enough: freed memory may be reused). */
    std::uint64_t id() const noexcept { return layoutId; }

    /** Number of nested layers (1 + number of virtual speakers with strength > 0, or 0 if empty). */
    int numLayers() const noexcept { return (int) layers.size(); }

    /** Triangulation of layer `index` (0 = real speakers only, j = plus the j strongest virtual ones). */
    const Triangulation& layer (int index) const noexcept { return layers[(size_t) index].triangulation; }

    /** Weight of layer `index` in the blend (non-negative; the weights sum to 1). */
    double layerWeight (int index) const noexcept { return layers[(size_t) index].weight; }

    /** The layer with all virtual speakers that have any strength (for drawing). Empty if there are no layers. */
    const Triangulation& full() const noexcept;

    /** The virtual speakers with strength > 0, strongest first. */
    std::span<const VirtualSpeaker> virtualSpeakers() const noexcept { return sorted; }

    /** The convex hull of the real speakers (virtual speakers are not outputs and do not count): what the
        level falloff outside the layout is measured from. */
    const tlos::Hull& hull() const noexcept { return realHull; }

private:
    /** An axis-aligned box in the plane. */
    struct Box
    {
        double minX;   ///< Left edge.
        double minY;   ///< Bottom edge.
        double maxX;   ///< Right edge.
        double maxY;   ///< Top edge.
    };

    /** One nested triangulation and its blend weight. */
    struct Layer
    {
        Triangulation triangulation; ///< Real speakers plus the strongest virtual ones.
        double weight;               ///< Share of this layer in the blended gains.

        /** Where this layer's gains can differ from those of the previous layer: boxes around the triangles that are new
            (or that touch a virtual speaker whose neighbours changed). A source outside all of them gets the previous layer's gains
            unchanged, which saves a search through all triangles for each of the (possibly many) layers. */
        std::vector<Box> changed;
        bool everywhere = true;      ///< True if the previous layer cannot be reused anywhere (the first layer, or the hull changed).
    };

    friend SourceGains computeGainsReference (const SpeakerLayout& layout, Vec2 source) noexcept;
    friend SourceGains computeGains (const SpeakerLayout& layout, Vec2 source) noexcept;

    std::uint64_t layoutId = 0;         ///< See id().
    std::vector<Layer> layers;          ///< Layer 0 first.
    std::vector<VirtualSpeaker> sorted; ///< Virtual speakers, strongest first.
    tlos::Hull realHull;                ///< Hull of the real speakers, see hull().
};

/** Gains of a source for the real speakers of the layout (constant power). Real-time safe.
    @param layout  The layout to pan within.
    @param source  Source position in metres.
    @return        Gains for the real speakers. */
SourceGains computeGains (const SpeakerLayout& layout, Vec2 source) noexcept;

/** The same as computeGains(layout, source), but evaluating every layer from scratch. Slower; used by the tests to
    check that the shortcut in computeGains() gives the same result. */
SourceGains computeGainsReference (const SpeakerLayout& layout, Vec2 source) noexcept;

/** Largest spread radius in metres that is used (larger values are limited to this). */
inline constexpr double kMaxSpreadRadius = 100.0;

/** As above, with spread (in the manner of MDAP, multiple-direction amplitude panning).

    The source is panned not from a single point but from the point plus 8 points evenly spaced on a
    circle of radius `spread` metres around it. The gains are the root of the mean of the squared
    gains of these 9 positions, so the power stays constant. Spread 0 (or less, or not finite) is the
    plain gain. A larger spread feeds more speakers more evenly and makes the panning smoother:
    - continuous in the position and in the spread,
    - symmetric: mirroring the layout and the source mirrors the gains.
    Costs up to 9 times the plain computation; real-time safe.
    @param layout  The layout to pan within.
    @param source  Source position in metres.
    @param spread  Radius of the spread circle in metres (limited to tripan::kMaxSpreadRadius).
    @return        Gains for the real speakers. */
SourceGains computeGains (const SpeakerLayout& layout, Vec2 source, double spread) noexcept;

/** As above, and with the level falling off for sources outside the speakers.

    The distance from the source to the hull of the real speakers (SpeakerLayout::hull()) gives the factor
    tlos::outsideFactor(distance, outsideRolloffDb), which scales the gains: 1 inside the layout and on its
    boundary, smaller outside. With spread, the factor is applied to each of the 9 positions before the squared
    gains are averaged, so a spread circle that is partly outside fades smoothly. Outside the layout the
    power is therefore no longer 1. The falloff does not apply (the result is exactly that of the overload
    above) if `outsideRolloffDb` is 0 or less, or if the real speakers have no area (fewer than three, or
    on one line).
    @param layout            The layout to pan within.
    @param source            Source position in metres.
    @param spread            Radius of the spread circle in metres.
    @param outsideRolloffDb  Level drop in dB per doubling of (1 + the distance outside in metres).
    @return                  Gains for the real speakers. */
SourceGains computeGains (const SpeakerLayout& layout, Vec2 source, double spread, double outsideRolloffDb) noexcept;
}
