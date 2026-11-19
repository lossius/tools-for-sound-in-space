// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file Path.h
    @brief The gain and delay of the direct sound between a source and a virtual microphone (JUCE-free). */

#pragma once

#include "tlos/Vec2.h"
#include "vimic/Room.h"

namespace vimic
{
/** How the level falls with the distance between a source and a microphone. */
enum class DistanceLaw
{
    Inverse,       ///< `1 / d^q`: q = 1 is the free field.
    Exponential    ///< `10^(-k d / 20)`: k dB per metre.
};

/** The settings of the model that apply to every path. */
struct PathSettings
{
    DistanceLaw law = DistanceLaw::Inverse;   ///< The distance law.
    double power = 1.0;                       ///< The exponent q of the inverse law (0..9).
    double dbPerMetre = 6.0;                  ///< The decrease k of the exponential law, in dB per metre (0..60).
    double minDistance = 1.0;                 ///< Distances shorter than this count as this long, so that the gain never exceeds 1 (metres).
    int micPower = 1;                         ///< The exponent w of the microphone pattern (0..9).
    int sourcePower = 1;                      ///< The exponent of the source pattern (0..9).
    bool restrictPolarity = true;             ///< Whether the pattern may not give negative (anti-phase) gains.
    double temperatureCelsius = 20.0;         ///< Room temperature, which sets the speed of sound.
};

/** The unit vector of an axis that points in the direction yaw (counter-clockwise from the front, +y) and pitch (up from the horizontal plane):
    `(-sin yaw cos pitch, cos yaw cos pitch, sin pitch)`. With a pitch of 0 it lies in the plane, as the azimuths do.
    @param yaw    The yaw in radians. @param pitch  The pitch in radians. */
Vec3 axisOf (double yaw, double pitch) noexcept;

/** A virtual microphone: a position in the plane with a height, and the direction of its axis. */
struct Microphone
{
    tlos::Vec2 position;       ///< Position in the plane in metres.
    double yaw = 0.0;          ///< Direction of its axis in radians, counter-clockwise from the front (+y), like azimuths.
    double pattern = 0.5;      ///< The ratio `a` of its pattern (1 omni, 0.5 cardioid, 0 figure-8).
    double height = 1.5;       ///< Height above the floor in metres.
    double pitch = 0.0;        ///< Tilt of its axis from the horizontal plane in radians (up is positive).
};

/** A sound source: a position in the plane with a height, and the direction in which it radiates most. */
struct Source
{
    /** A source at the origin, 1.5 m up, facing the front, radiating equally in every direction. */
    Source() = default;

    /** A source with a position, a yaw, a pattern, a height and a pitch (explicit, so that `{ x, y }` is always a position of the omni overloads). */
    explicit Source (tlos::Vec2 positionInMetres, double yawRadians = 0.0, double patternRatio = 1.0, double heightInMetres = 1.5, double pitchRadians = 0.0)
        : position (positionInMetres), yaw (yawRadians), pattern (patternRatio), height (heightInMetres), pitch (pitchRadians) {}

    tlos::Vec2 position;       ///< Position in the plane in metres.
    double yaw = 0.0;          ///< Direction in which it radiates most, in radians, counter-clockwise from the front (+y).
    double pattern = 1.0;      ///< The ratio `a` of its radiation pattern: 1 radiates equally in every direction (the default), 0.5 like a cardioid, 0 like a figure-8.
    double height = 1.5;       ///< Height above the floor in metres.
    double pitch = 0.0;        ///< Tilt of its axis from the horizontal plane in radians (up is positive).
};

/** Gain and time of flight of one path. */
struct PathResult
{
    double gain = 0.0;         ///< Linear gain: distance law times pattern, limited to -1 or 0 .. 1 (0 if nothing could be computed).
    double distance = 0.0;     ///< Length of the path in metres (3D, with the heights).
    double delaySeconds = 0.0; ///< Time of flight in seconds.
};

/** @return The speed of sound in m/s in air of the given temperature: `331.3 + 0.606 T`; limited to -40..60 degrees Celsius. */
double speedOfSound (double temperatureCelsius) noexcept;

/** @return The gain due to the distance alone (see DistanceLaw), for a path of `distance` metres; the distance is limited to at least
    `settings.minDistance` (and that to at least 0.01 m), so with the default minimum of 1 m it is never above 1. */
double distanceGain (double distance, const PathSettings& settings) noexcept;

/** @return The gain of a source's radiation pattern for a microphone at the angle phi from its axis (cos(phi) given):
    `(a + (1 - a) cos(phi)) ^ w` with `a` the source's pattern and `w` = settings.sourcePower, never negative (a source does not radiate in anti-phase). 1 for an omni source. */
double sourceDirectivity (double pattern, int power, double cosAngle) noexcept;

/** The direct path from a source to a microphone: the length with the heights, the time of flight, and the gain from the distance law, the
    microphone's pattern (the angle between its axis and the direction to the source, in 3D) and the source's radiation pattern
    (the angle between the source's axis and the direction to the microphone).
    @param source    The source.
    @param mic       The microphone.
    @param settings  The settings of the model.
    @return          The result; gain 0 if a position is not finite. */
PathResult computeDirectPath (const Source& source, const Microphone& mic, const PathSettings& settings) noexcept;

/** As above for an omni source at a position. */
PathResult computeDirectPath (tlos::Vec2 source, const Microphone& mic, const PathSettings& settings) noexcept;
}
