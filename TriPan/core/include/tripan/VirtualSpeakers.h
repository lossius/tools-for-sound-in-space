// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file VirtualSpeakers.h
    @brief Finds where virtual (imaginary) speakers should be placed for a given speaker layout. */

#pragma once

#include "tripan/Triangulation.h"

#include <vector>

namespace tripan
{
using tlos::VirtualSpeaker;

/** Thresholds for findVirtualSpeakers(). Every decision is soft: instead of switching at a
    threshold, the strength of the virtual speaker rises smoothly from 0 to 1 over the width given
    here, so that the panning changes continuously while speakers are moved. */
struct VirtualSpeakerOptions
{
    /** Angular gaps (seen from the listener) wider than this get a virtual speaker. The default
        ramps the strength from 0 at 90 degrees (a quad's gaps need nothing) to 1 at 110 degrees
        (the back of a 5.1 layout is 140 degrees). */
    double gapThresholdDegrees = 100.0;
    /** Half the width of the ramp: strength 0 at threshold - softness, 1 at threshold + softness. */
    double gapSoftnessDegrees = 10.0;

    /** The listener counts as enclosed if the largest angular gap is below this; the strength of
        everything fades to 0 between this and 180 degrees (listener outside the speakers). */
    double enclosedBelowDegrees = 160.0;

    /** Speakers count as a ring around the listener if the difference between the largest and the
        smallest distance is at most this fraction of the mean distance. */
    double ringTolerance = 0.25;
    double ringSoftness = 0.05;      ///< Half the width of the soft transition around #ringTolerance.

    /** No centre speaker if a real speaker is closer to the listener than this fraction of the mean distance. */
    double centreClearance = 0.25;
    double centreSoftness = 0.05;    ///< Half the width of the soft transition around #centreClearance.

    /** Upper limit for the number of virtual speakers. */
    int maxCount = 64;

    bool fillGaps = true;   ///< Add virtual speakers in wide angular gaps.
    bool addCentre = true;  ///< Add a virtual speaker at the listener for ring layouts.
};

/** Finds where virtual (imaginary) speakers are useful, for a listener at `listener`.

    1. Gap filling: where two neighbouring speakers (in azimuth) are far apart, one virtual speaker
       is placed in the middle of the gap (direction halfway between them, distance interpolated).
       Example: the open back of a 5.1 layout.
    2. Ring centre: if the speakers lie on (nearly) a circle around the listener, a virtual speaker
       is placed at the listener. Points on a circle have no unique Delaunay triangulation (any
       triangulation qualifies, so the result is arbitrary and asymmetric); with a centre speaker
       every triangle is "centre + two neighbours", which is symmetric.

    Both only apply if the listener is enclosed by the speakers, because only then is there an
    intention to surround the listener. Each speaker has a strength in (0, 1] from the soft
    thresholds in `options`; speakers with no strength are not returned.

    Non-finite and coincident positions are ignored. The result depends only on the speaker
    positions, so it is deterministic and has no memory. Layout changes only (allocates).

    @param speakers  Real speaker positions in metres.
    @param listener  Listening position, normally the origin.
    @param options   Thresholds and switches.
    @return          The virtual speakers with strength > 0, at most `options.maxCount`. */
std::vector<VirtualSpeaker> findVirtualSpeakers (std::span<const Vec2> speakers, Vec2 listener = {},
                                                 const VirtualSpeakerOptions& options = {});
}
