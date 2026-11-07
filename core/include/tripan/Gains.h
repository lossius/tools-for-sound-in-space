#pragma once

#include "tripan/Triangulation.h"

#include <array>

namespace tripan
{
/** Gains for one source: at most three speakers receive signal. */
struct SourceGains
{
    int count = 0;                              // number of valid entries (0..3)
    std::array<int, 3> speaker { -1, -1, -1 };  // speaker indices
    std::array<float, 3> gain { 0.0f, 0.0f, 0.0f };
};

/** Computes the speaker gains for a source at the given position.

    - Inside the hull: the containing triangle is found and its barycentric coordinates w
      (non-negative, summing to 1) give the gains g_i = w_i / sqrt(sum w_j^2), so that the
      sum of squared gains is 1 (constant power) everywhere.
    - Outside the hull: the source is projected onto the nearest hull edge and panned between
      that edge's two speakers with the same law.
    - Real-time safe: no allocation, no locks. Cost is linear in the number of triangles.
    - Non-finite source positions or an empty triangulation give count == 0.
*/
SourceGains computeGains (const Triangulation& triangulation, Vec2 source) noexcept;
}
