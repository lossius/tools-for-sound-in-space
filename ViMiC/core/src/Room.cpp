// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file Room.cpp
    @brief The images of the shoebox room. */

#include "vimic/Room.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace vimic
{
namespace
{
/** The tables of the images, built once. */
struct Tables
{
    std::vector<Image> all;                                        ///< Every image up to kMaxOrder, the direct sound first, then by order.
    std::array<std::size_t, kMaxOrder + 1> counts {};              ///< How many images there are up to each order.
    std::vector<std::array<int, WallCount>> classes;               ///< The hits of every filter class.
    std::array<int, kMaxOrder + 1> classCounts {};                 ///< How many classes there are up to each order.

    /** Builds the tables. */
    Tables()
    {
        // Per axis the index pairs with at most kMaxOrder reflections.
        struct Axis { int p, n, order; };
        std::vector<Axis> axis;
        for (int p = 0; p <= 1; ++p)
            for (int n = -kMaxOrder - 1; n <= kMaxOrder + 1; ++n)
                if (const int order = std::abs (n - p) + std::abs (n); order <= kMaxOrder)
                    axis.push_back ({ p, n, order });

        for (int order = 0; order <= kMaxOrder; ++order)
        {
            for (const auto& x : axis)
                for (const auto& y : axis)
                    for (const auto& z : axis)
                        if (x.order + y.order + z.order == order)
                        {
                            Image image;
                            image.p = { x.p, y.p, z.p };
                            image.n = { x.n, y.n, z.n };
                            image.order = order;

                            // Low wall of an axis: |n - p| hits; high wall: |n|.
                            image.hits[Left] = std::abs (x.n - x.p);
                            image.hits[Right] = std::abs (x.n);
                            image.hits[Rear] = std::abs (y.n - y.p);
                            image.hits[Front] = std::abs (y.n);
                            image.hits[Floor] = std::abs (z.n - z.p);
                            image.hits[Ceiling] = std::abs (z.n);

                            // Images with the same hits share a filter.
                            auto found = std::find (classes.begin(), classes.end(), image.hits);
                            if (found == classes.end())
                            {
                                classes.push_back (image.hits);
                                found = classes.end() - 1;
                            }
                            image.filterClass = (int) (found - classes.begin());
                            all.push_back (image);
                        }

            counts[(size_t) order] = all.size();
            classCounts[(size_t) order] = (int) classes.size();
        }
    }
};

/** @return The tables. */
const Tables& tables()
{
    static const Tables t;
    return t;
}

/** The image of a position along one axis of length `length`, whose low wall is at `low`. */
double axisImage (double coordinate, double low, double length, int p, int n) noexcept
{
    const double u = coordinate - low;
    return low + (1 - 2 * p) * u + 2.0 * n * length;
}
}

std::span<const Image> images (int maxOrder) noexcept
{
    const auto& t = tables();
    return { t.all.data(), t.counts[(size_t) std::clamp (maxOrder, 0, kMaxOrder)] };
}

int filterClassCount (int maxOrder) noexcept
{
    return tables().classCounts[(size_t) std::clamp (maxOrder, 0, kMaxOrder)];
}

const std::array<int, WallCount>& classHits (int filterClass) noexcept
{
    const auto& t = tables();
    return t.classes[(size_t) std::clamp (filterClass, 0, (int) t.classes.size() - 1)];
}

Vec3 imagePosition (Vec3 position, const Room& room, const Image& image) noexcept
{
    return { axisImage (position.x, -0.5 * room.width, room.width, image.p[0], image.n[0]),
             axisImage (position.y, -0.5 * room.depth, room.depth, image.p[1], image.n[1]),
             axisImage (position.z, 0.0, room.height, image.p[2], image.n[2]) };
}

Vec3 micImagePosition (Vec3 position, const Room& room, const Image& image) noexcept
{
    // |S' - M| = |S - M*| with M* the image of M with the index n' = -(1 - 2p) n (and the same p).
    const auto flipped = [] (int p, int n) { return -(1 - 2 * p) * n; };
    return { axisImage (position.x, -0.5 * room.width, room.width, image.p[0], flipped (image.p[0], image.n[0])),
             axisImage (position.y, -0.5 * room.depth, room.depth, image.p[1], flipped (image.p[1], image.n[1])),
             axisImage (position.z, 0.0, room.height, image.p[2], flipped (image.p[2], image.n[2])) };
}

double insideFactor (Vec3 position, const Room& room) noexcept
{
    const auto outside = [] (double coordinate, double low, double high) { return std::max ({ 0.0, low - coordinate, coordinate - high }); };
    const double distance = std::max ({ outside (position.x, -0.5 * room.width, 0.5 * room.width),
                                        outside (position.y, -0.5 * room.depth, 0.5 * room.depth),
                                        outside (position.z, 0.0, room.height) });
    return std::clamp (1.0 - distance / 0.5, 0.0, 1.0);
}
}
