#pragma once

#include "tripan/Triangulation.h"

#include <vector>

namespace tripan
{
/** Thresholds for findVirtualSpeakers(). */
struct VirtualSpeakerOptions
{
    /** Angular gaps (seen from the listener) wider than this get virtual speakers. */
    double gapThresholdDegrees = 90.0;

    /** Speakers count as a ring around the listener if the difference between the largest and the
        smallest distance is at most this fraction of the mean distance. */
    double ringTolerance = 0.25;

    /** No centre speaker is added if a real speaker is closer to the listener than this fraction of
        the mean distance. */
    double centreClearance = 0.25;

    /** Upper limit for the number of virtual speakers. */
    int maxCount = 16;

    bool fillGaps = true;
    bool addCentre = true;
};

/** Finds where virtual (imaginary) speakers are useful, for a listener at `listener`.

    Nothing is added unless the listener is enclosed by the speakers (no angular gap of 180 degrees
    or more), because only then is there an intention to surround the listener.

    1. Gap filling: where two neighbouring speakers (in azimuth) are more than gapThresholdDegrees
       apart, evenly spaced virtual speakers are placed in the gap, with the distance interpolated
       between the two neighbours. Example: the open back of a 5.1 layout.
    2. Ring centre: if the speakers lie on (nearly) a circle around the listener, a virtual speaker
       is placed at the listener. Points on a circle have no unique Delaunay triangulation (any
       triangulation qualifies, so the result is arbitrary and asymmetric); with a centre speaker
       every triangle is "centre + two neighbours", which is symmetric.

    Non-finite and coincident positions are ignored. The result depends only on the speaker
    positions, so it is deterministic. Message thread / layout changes only (allocates). */
std::vector<Vec2> findVirtualSpeakers (std::span<const Vec2> speakers, Vec2 listener = {},
                                       const VirtualSpeakerOptions& options = {});
}
