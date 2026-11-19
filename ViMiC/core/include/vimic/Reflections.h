// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file Reflections.h
    @brief The paths of the early reflections between a source and a microphone (JUCE-free). */

#pragma once

#include "vimic/Path.h"
#include "vimic/Room.h"

namespace vimic
{
/** The settings of the room model. */
struct RoomSettings
{
    Room room;                    ///< The size of the room.
    int order = 0;                ///< The highest number of reflections that is rendered: 0 (none), 1 or 2.
    double gainFirst = 1.0;       ///< Linear gain of the paths with one reflection.
    double gainSecond = 1.0;      ///< Linear gain of the paths with two reflections.
    double threshold = 0.0;       ///< Paths whose gain is below this (linear) are not rendered (0: render everything).
};

/** The path of one image of a source to a microphone: the length of the unfolded path (the image source to the microphone), the time of flight, and the gain from
    the distance law, the microphone's pattern for the direction in which the sound arrives, the source's pattern for the direction in which it left, the gain of
    the order and the fade for a source or microphone outside the walls. The colour that the walls give the sound is not in the gain: it is the filter of the image's
    filter class.
    @param source    The source.
    @param mic       The microphone.
    @param image     The image (see images()); the first entry is the direct sound, which gives the same as computeDirectPath().
    @param settings  The settings of the model.
    @param room      The room, its order gains and the threshold.
    @return          The path; gain 0 for a path below the threshold or for positions that are not finite. */
PathResult computeImagePath (const Source& source, const Microphone& mic, const Image& image, const PathSettings& settings, const RoomSettings& room) noexcept;
}
