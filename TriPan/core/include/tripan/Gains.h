// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file Gains.h
    @brief Constant-power barycentric gains of one source for a single Triangulation. */

#pragma once

#include "tlos/SourceGains.h"
#include "tripan/Triangulation.h"

namespace tripan
{
using tlos::SourceGains;
using tlos::kMaxGainEntries;

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

    @param triangulation  Triangulation of the real (and optionally virtual) speakers.
    @param source         Source position in metres.
    @return               The speakers that receive signal and their gains (sum of squares is 1). */
SourceGains computeGains (const Triangulation& triangulation, Vec2 source) noexcept;
}
