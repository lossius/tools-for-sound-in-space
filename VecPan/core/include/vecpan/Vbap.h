// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file Vbap.h
    @brief Two-dimensional vector base amplitude panning (VBAP) around the listener (JUCE-free). */

#pragma once

#include "tlos/Hull.h"
#include "tlos/SourceGains.h"
#include "tlos/Vec2.h"

#include <span>
#include <vector>

namespace vecpan
{
/** The speakers as VBAP sees them: their directions from the listener (the origin), in order around the circle, and
    for every pair of neighbours the inverse of the matrix that VBAP needs. Built once when the layout changes
    (off the audio thread) and never changed afterwards, so that the audio thread can share it.

    Only the direction of a speaker matters, not its distance, so speakers need not lie on a circle. A speaker exactly at the
    listener has no direction and takes no part. Speakers in the same direction (within 1e-9 rad) count as one: the one
    with the lowest number takes the sound. */
class Layout
{
public:
    /** An empty layout: no speakers. */
    Layout() = default;

    /** Builds the layout. @param speakers  The speakers in use, in metres. Entry i of the gains belongs to speakers[i]; at most
        tlos::kMaxGainEntries are used; speakers with non-finite positions or at the origin do not take part. */
    explicit Layout (std::span<const tlos::Vec2> speakers);

    /** @return How many speakers take part in the panning. */
    int size() const noexcept { return (int) sorted.size(); }

    /** @return The convex hull of the speakers (all of them, for the level outside the layout). */
    const tlos::Hull& hull() const noexcept { return speakerHull; }

    /** @return The speakers that take part, in order of increasing azimuth (the index into the `speakers` the layout was built from). */
    std::vector<int> order() const;

    /** One speaker that takes part: its index and its azimuth. */
    struct Entry
    {
        int speaker = 0;         ///< Index into the speakers the layout was built from.
        double azimuth = 0.0;    ///< Direction from the listener in radians, counter-clockwise from the front (+y), -pi to +pi.
    };

    /** The neighbouring pair of speakers from sorted[i] to sorted[i + 1] (the last one wraps to the first), with what
        VBAP needs to pan between them. */
    struct Pair
    {
        double gap = 0.0;      ///< Angle from the first to the second speaker, counter-clockwise, in radians (0 .. 2 pi]; 2 pi for a single speaker.
        bool wide = false;     ///< True if the gap is pi or more: no vector base exists, and the sound is cross-faded instead.
        double inverse[4] {};  ///< Inverse of the matrix whose rows are the two speaker directions (row-major), for gaps below pi.
    };

    /** @return The speakers that take part in order of azimuth. */
    const std::vector<Entry>& entries() const noexcept { return sorted; }

    /** @return The pairs, one per entry (see Pair). */
    const std::vector<Pair>& pairs() const noexcept { return neighbours; }

private:
    std::vector<Entry> sorted;       ///< The speakers that take part, by azimuth.
    std::vector<Pair> neighbours;    ///< The pair after each entry.
    tlos::Hull speakerHull;          ///< Hull of all finite speakers.
};

/** The settings of the panning law. */
struct VbapSettings
{
    /** Level drop in dB for each doubling of (1 + the distance outside the speaker layout in metres), see
        tlos::outsideFactor(). 0 = the level stays constant outside the layout. The distance of a source inside the layout never
        changes its level (VBAP uses only the direction), except for the blend near the listener, see centreRadius. */
    double outsideRolloffDb = 0.0;

    /** Radius in metres around the listener inside which the sound is blended towards an even share over all speakers. At the
        listener a source has no direction and every speaker gets the same share; the blend grows smoothly (smoothstep of
        distance / radius) from there to plain VBAP at this distance, so that a source moving through the centre does not jump.
        0 = no blend: a source anywhere but exactly at the listener is panned by its direction alone. */
    double centreRadius = 0.0;
};

/** The azimuth of a point as seen from the origin: radians counter-clockwise from the front (+y), x = -d sin(az), y = d cos(az). */
double azimuthOf (tlos::Vec2 p) noexcept;

/** Computes the speaker gains of a source with 2D VBAP.

    The source direction lies between two neighbouring speakers (as seen from the listener, less than 180 degrees apart); the
    gains of that pair are those of Pulkki's vector base: the source direction is made from the two speaker directions, and
    the two gains are scaled to a sum of squares of 1 (constant power). In a gap of 180 degrees or more (speakers that do not
    surround the listener) no vector base exists; there the two bounding speakers share the sound with a sine/cosine cross-fade
    over the angle, which is continuous and keeps the power constant. A single speaker gets everything. A source at the
    listener (no direction) is shared equally by all speakers.

    A spread (metres) widens the source into an angular width: three directions, at the source azimuth and at plus and minus
    atan(spread / distance) from it, are panned and their squared gains averaged (MDAP-like; constant power, continuous). A
    source at the listener with a spread is shared equally.

    Within VbapSettings::centreRadius of the listener the powers are blended with an even share over all speakers (see there).
    The level outside the layout falls off as in the other plugins (see VbapSettings). Real-time safe: no allocation, no locks.

    @param layout    The speakers (build it once when they change).
    @param source    The source position in metres.
    @param spread    The source's spread radius in metres (0 = a point source).
    @param settings  The centre radius and the outside rolloff.
    @return          Entries for the speakers with a non-zero gain (speaker = index into the layout's speakers), or count == 0 if the
                     source position is not finite or no speaker takes part. */
tlos::SourceGains computeGains (const Layout& layout, tlos::Vec2 source, double spread, const VbapSettings& settings = {}) noexcept;
}
