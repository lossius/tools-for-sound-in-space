#pragma once

#include "tripan/Triangulation.h"

#include <array>

namespace tripan
{
/** At most one entry per real speaker. */
inline constexpr int kMaxGainEntries = 64;

/** Gains for one source. Without virtual speakers at most three speakers receive signal; with
    virtual speakers the share sent to them is redistributed over their real neighbours, so more
    speakers can be involved. Always constant power (sum of squared gains = 1). */
struct SourceGains
{
    SourceGains()
    {
        speaker.fill (-1);
        gain.fill (0.0f);
    }

    int count = 0;                                  // number of valid entries
    std::array<int, kMaxGainEntries> speaker;       // real speaker indices
    std::array<float, kMaxGainEntries> gain;
};

/** Computes the speaker gains for a source at the given position.

    - Inside the hull: the containing triangle is found and its barycentric coordinates w
      (non-negative, summing to 1) give the gains g_i = w_i / sqrt(sum w_j^2), so that the
      sum of squared gains is 1 (constant power) everywhere.
    - Outside the hull: the source is projected onto the nearest hull edge and panned between
      that edge's two speakers with the same law.
    - With virtual speakers in the triangulation, their gain is split over the real speakers they
      are connected to (Triangulation::redistribution) and the result is renormalised.
    - Real-time safe: no allocation, no locks. Cost is linear in the number of triangles.
    - Non-finite source positions or an empty triangulation give count == 0.
*/
SourceGains computeGains (const Triangulation& triangulation, Vec2 source) noexcept;
}
