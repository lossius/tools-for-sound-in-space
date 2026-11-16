// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file Groups.h
    @brief Source groups: which sources belong together, and the geometry of moving a group rigidly. */

#pragma once

#include "tlos/Limits.h"
#include "tlos/Vec2.h"

#include <array>
#include <string>
#include <vector>

namespace tlos
{
/** Which sources are grouped. Every source has a group id: 0 = not in a group, 1, 2, ... = the group it
    belongs to. Sources with the same id move together. A group always has at least two members (a
    group that would be left with one is dissolved), and the ids are small positive numbers that stay
    the same when other groups change, so that a group keeps its colour. All 64 sources are kept,
    including those not currently in use. */
class SourceGroups
{
public:
    /** Largest group id: with at least two members each, 64 sources make at most 32 groups. */
    static constexpr int kMaxGroupId = kMaxChannels / 2;

    /** Group ids of all sources, indexed by zero-based source number. */
    using Ids = std::array<int, kMaxChannels>;

    /** Creates an assignment with no groups. */
    SourceGroups() = default;

    /** Creates an assignment from ids; invalid ids are set to 0 and groups of one source are dissolved. */
    explicit SourceGroups (const Ids& ids);

    /** @return The group id of a source (0 if it is not in a group or the index is out of range). */
    int groupOf (int source) const noexcept;

    /** @return All group ids (index = source). */
    const Ids& ids() const noexcept { return groupIds; }

    /** @return Whether any source is in a group. */
    bool any() const noexcept;

    /** @return The sources that move with `source` and are below `activeCount`: all members of its group
        in ascending order, or just `source` if it is in no group (or alone among the active ones). */
    std::vector<int> membersOf (int source, int activeCount = kMaxChannels) const;

    /** @return The number of members of group `id` below `activeCount`. */
    int sizeOf (int id, int activeCount = kMaxChannels) const noexcept;

    /** Groups the given sources (duplicates and out-of-range numbers are ignored). If some of them are
        in groups already, those groups are merged whole into the new one, which keeps the smallest id
        among them; otherwise it gets the smallest unused id.
        @return The id of the group, or 0 (and nothing changes) if fewer than two different sources were given. */
    int group (const std::vector<int>& sources);

    /** Dissolves the groups that any of the given sources belong to. @return The number of groups dissolved. */
    int ungroup (const std::vector<int>& sources);

    /** Writes the ids as text, "0 0 1 1 ..." (always 64 numbers), for saving. */
    std::string toString() const;

    /** Reads text written by toString(). Anything missing or invalid is read as 0 (no group). */
    static SourceGroups fromString (const std::string& text);

    /** @return Whether both have the same ids. */
    bool operator== (const SourceGroups&) const = default;

private:
    /** Sets ids outside 0..kMaxGroupId to 0 and dissolves groups with one member. */
    void normalise() noexcept;

    Ids groupIds {}; ///< Group id per source.
};

/** Limits a movement of a set of points so that they all stay inside the square +-`limit`, without changing
    their positions relative to each other. The limits apply to x and y separately, so a group pushed
    diagonally against a wall slides along it. Points that are not finite are ignored.
    @param points  The positions before the move.
    @param delta   The wanted movement.
    @param limit   Half the side of the square in metres.
    @return        The movement to use: `delta`, reduced as far as needed. */
Vec2 clampTranslation (const std::vector<Vec2>& points, Vec2 delta, double limit);

/** Limits a common change of spread so that every spread stays within 0..`maxSpread`, which keeps the
    differences between them.
    @param spreads    The current spreads.
    @param change     The wanted change (positive = more).
    @param maxSpread  The largest allowed spread.
    @return           The change to use: `change`, reduced as far as needed. */
double clampSpreadChange (const std::vector<double>& spreads, double change, double maxSpread);

/** @return The centre of gravity (the mean position) of the finite points, or the origin if there are none. */
Vec2 centroid (const std::vector<Vec2>& points);

/** The largest factor by which the points can be scaled away from `centre` (their offsets from it multiplied)
    while all of them stay inside the square +-`limit`. A factor of 1 or more is always possible for points
    that are inside. Points at the centre do not limit it; if nothing limits it, a very large number is returned.
    @param points  The positions.
    @param centre  The fixed point of the scaling.
    @param limit   Half the side of the square in metres. */
double maxScaleFactor (const std::vector<Vec2>& points, Vec2 centre, double limit);

/** @return `p` turned by `angle` radians counter-clockwise (seen with +y up) around `centre`. */
Vec2 rotateAbout (Vec2 p, Vec2 centre, double angle);

/** Limits a rotation of a set of points around `centre` so that they all stay inside the square +-`limit`.
    The rotation is followed from `current` towards `wanted` in steps of one degree and stops where the first
    point would leave the square (found to a tiny fraction of a degree), so a rotation can never jump over a
    forbidden range and can always be reversed.
    @param points   The positions at angle 0.
    @param centre   The centre of the rotation.
    @param current  The angle in radians that is in effect now (must be a valid one).
    @param wanted   The angle in radians that is wanted.
    @param limit    Half the side of the square in metres.
    @return         The angle to use, between `current` and `wanted`. */
double clampRotation (const std::vector<Vec2>& points, Vec2 centre, double current, double wanted, double limit);

/** @return The corners of the convex hull of the points, counter-clockwise, without repeated points
    (fewer than three points give the distinct points as they are; collinear points give the two ends). */
std::vector<Vec2> convexHull (std::vector<Vec2> points);
}
