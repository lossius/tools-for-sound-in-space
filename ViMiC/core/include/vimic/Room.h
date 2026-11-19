// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file Room.h
    @brief The shoebox room and its image sources (the image method of Allen and Berkley) (JUCE-free). */

#pragma once

#include "tlos/Vec2.h"

#include <array>
#include <span>

namespace vimic
{
/** The six walls, in the order of the absorption parameters. */
enum Wall : int
{
    Left,      ///< The wall at x = -width / 2.
    Right,     ///< The wall at x = +width / 2.
    Front,     ///< The wall at y = +depth / 2.
    Rear,      ///< The wall at y = -depth / 2.
    Floor,     ///< The floor, z = 0.
    Ceiling,   ///< The ceiling, z = height.
    WallCount  ///< The number of walls.
};

/** A point in 3D: x to the right, y to the front, z up. */
struct Vec3
{
    double x = 0.0;   ///< Right.
    double y = 0.0;   ///< Front.
    double z = 0.0;   ///< Up.
};

/** A shoebox room centred on the origin of the plane: `x` from -width / 2 to width / 2, `y` from -depth / 2 to depth / 2, `z` from the floor (0) to the ceiling (height). */
struct Room
{
    double width = 10.0;    ///< Size along x in metres.
    double depth = 10.0;    ///< Size along y in metres.
    double height = 3.0;    ///< Size along z in metres.
};

/** One image of a source in the walls of the room, or the source itself (the direct sound). The image method mirrors the source in the walls; the
    straight path from the image to a microphone has the length and the arrival time of the reflected path. For every axis the image has an index
    pair (p, n) (Allen and Berkley): with the position `u` measured from the low wall (0 .. L), the image is at `(1 - 2p) u + 2 n L`. The path then hits the low wall
    `|n - p|` times and the high wall `|n|` times, so the order (the number of reflections) is the sum of those over the axes. */
struct Image
{
    std::array<int, 3> p {};          ///< Per axis (x, y, z): 0 or 1.
    std::array<int, 3> n {};          ///< Per axis: the image index.
    std::array<int, WallCount> hits {};   ///< How many times the path hits each wall.
    int order = 0;                    ///< The number of reflections (0 for the direct sound).
    int filterClass = 0;              ///< Which wall filter the path needs: images with the same hits share one; 0 is no reflection.
};

/** The largest number of reflections that is rendered. */
inline constexpr int kMaxOrder = 2;

/** @return The images of a source with at most `maxOrder` reflections (0, 1 or 2; others are limited), the direct sound first, then by order:
    1, 7 or 25 entries (6 images of the first order and 18 of the second: 6 along one axis between two parallel walls and 12 over two axes). The list does not
    depend on the room or on any position. */
std::span<const Image> images (int maxOrder) noexcept;

/** @return How many distinct wall filters the images up to `maxOrder` need, counting the direct sound (class 0): 1, 7 or 22. */
int filterClassCount (int maxOrder) noexcept;

/** @return The hits per wall of a filter class (class 0 has none). */
const std::array<int, WallCount>& classHits (int filterClass) noexcept;

/** @return The image of a source in 3D. @param position  The source position in the plane (x, y) with its height. @param room  The room. @param image  The image. */
Vec3 imagePosition (Vec3 position, const Room& room, const Image& image) noexcept;

/** @return The image of a microphone for the same walls in the reverse order: the straight path from a source to it has the direction in which the source's
    sound leaves for the reflected path (what the source's radiation pattern needs). */
Vec3 micImagePosition (Vec3 position, const Room& room, const Image& image) noexcept;

/** @return How much of a reflection a source or microphone at `position` keeps, 1 inside the room and falling to 0 within half a metre outside any wall
    (the walls only reflect what is inside them; this avoids a sudden change when something crosses a wall). */
double insideFactor (Vec3 position, const Room& room) noexcept;
}
