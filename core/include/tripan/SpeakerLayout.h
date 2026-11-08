#pragma once

#include "tripan/Gains.h"
#include "tripan/Triangulation.h"
#include "tripan/VirtualSpeakers.h"

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
    SpeakerLayout() = default;
    SpeakerLayout (std::span<const Vec2> realSpeakers, std::span<const VirtualSpeaker> virtualSpeakers = {});

    /** Unique number of this layout (0 for a default-constructed one), so that a cached result can be
        tied to the layout it was computed from (a pointer is not enough: freed memory may be reused). */
    std::uint64_t id() const noexcept { return layoutId; }

    int numLayers() const noexcept { return (int) layers.size(); }
    const Triangulation& layer (int index) const noexcept { return layers[(size_t) index].triangulation; }
    double layerWeight (int index) const noexcept { return layers[(size_t) index].weight; }

    /** The layer with all virtual speakers that have any strength (for drawing). Empty if there are no layers. */
    const Triangulation& full() const noexcept;

    /** The virtual speakers with strength > 0, strongest first. */
    std::span<const VirtualSpeaker> virtualSpeakers() const noexcept { return sorted; }

private:
    struct Layer
    {
        Triangulation triangulation;
        double weight;
    };

    std::uint64_t layoutId = 0;
    std::vector<Layer> layers;
    std::vector<VirtualSpeaker> sorted;
};

/** Gains of a source for the real speakers of the layout (constant power). Real-time safe. */
SourceGains computeGains (const SpeakerLayout& layout, Vec2 source) noexcept;

/** Largest spread radius in metres that is used (larger values are limited to this). */
inline constexpr double kMaxSpreadRadius = 100.0;

/** As above, with spread (in the manner of MDAP, multiple-direction amplitude panning).

    The source is panned not from a single point but from the point plus 8 points evenly spaced on a
    circle of radius `spread` metres around it. The gains are the root of the mean of the squared
    gains of these 9 positions, so the power stays constant. Spread 0 (or less, or not finite) is the
    plain gain. A larger spread feeds more speakers more evenly and makes the panning smoother:
    - continuous in the position and in the spread,
    - symmetric: mirroring the layout and the source mirrors the gains.
    Costs up to 9 times the plain computation; real-time safe. */
SourceGains computeGains (const SpeakerLayout& layout, Vec2 source, double spread) noexcept;
}
