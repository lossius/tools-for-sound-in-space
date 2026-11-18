#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Trond Lossius
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Generates the microphone setups for ViMiC in "microphone setups/" (ViMiC speaker XML files with a yaw and a pattern per microphone).

Run from anywhere:  python3 scripts/make_microphone_setups.py

Conventions (same as the plugin): metres, +y is front, +x is right, the listener (the middle of the array) is at the origin. The yaw of a
microphone is the direction of its axis in degrees, counter-clockwise from the front (positive = to the left); the pattern is the ratio a of
the directivity (a + (1 - a) cos(angle))^w: 1 omni, 0.7 subcardioid, 0.5 cardioid, 0.33 supercardioid, 0.3 hypercardioid, 0 figure-8.
The output format is the plugin's own "Export microphones" format (3 decimals, padded).
"""
import math
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "microphone setups"
OMNI, CARDIOID, SUPER = 1.0, 0.5, 1.0 / 3.0


def number(v):
    text = f"{v:.3f}"
    if text == "-0.000":
        text = "0.000"
    return text.rjust(7)


def azimuth_position(azimuth_deg, radius):
    """A point at an azimuth (counter-clockwise from the front, like yaws) and a distance."""
    a = math.radians(azimuth_deg)
    return -radius * math.sin(a), radius * math.cos(a)


def ring(azimuths, radius, pattern):
    """Microphones on a circle, pointing outwards (yaw = azimuth)."""
    return [(*azimuth_position(a, radius), a, pattern) for a in azimuths]


def write(name, title, description, mics):
    lines = ['<?xml version="1.0" encoding="UTF-8"?>', ""]
    lines.append(f'<ViMiCSpeakers version="1" count="{len(mics)}">')
    lines.append(f"  <!-- {title}: {description} -->")
    for i, (x, y, yaw, pattern) in enumerate(mics, start=1):
        lines.append(f'  <Speaker index="{str(i).rjust(2)}" x="{number(x)}" y="{number(y)}" yaw="{number(yaw)}" pattern="{pattern:.3f}"/>')
    lines.append("</ViMiCSpeakers>")
    (OUT / f"{name}.xml").write_text("\n".join(lines) + "\n", encoding="utf-8")


def main():
    OUT.mkdir(exist_ok=True)

    write("Stereo-AB", "Stereo AB", "two omnis 2 m apart, both pointing to the front",
          [(-1.0, 0.0, 0.0, OMNI), (1.0, 0.0, 0.0, OMNI)])
    write("Stereo-XY", "Stereo XY", "two coincident cardioids at +-45 degrees",
          [(0.0, 0.0, 45.0, CARDIOID), (0.0, 0.0, -45.0, CARDIOID)])
    write("Stereo-ORTF", "Stereo ORTF", "two cardioids 17 cm apart at +-55 degrees",
          [(-0.085, 0.0, 55.0, CARDIOID), (0.085, 0.0, -55.0, CARDIOID)])
    write("Stereo-NOS", "Stereo NOS", "two cardioids 30 cm apart at +-45 degrees",
          [(-0.15, 0.0, 45.0, CARDIOID), (0.15, 0.0, -45.0, CARDIOID)])
    write("Decca-tree", "Decca tree", "three omnis: left and right 2 m apart, the centre one 1.2 m in front of them",
          [(-1.0, 0.0, 0.0, OMNI), (0.0, 1.2, 0.0, OMNI), (1.0, 0.0, 0.0, OMNI)])
    write("IRT-cross", "IRT cross", "four cardioids at the corners of a 20 cm square, pointing diagonally outwards",
          [(-0.1, 0.1, 45.0, CARDIOID), (0.1, 0.1, -45.0, CARDIOID), (-0.1, -0.1, 135.0, CARDIOID), (0.1, -0.1, -135.0, CARDIOID)])
    write("Ring-4-cardioids", "Ring of 4 cardioids", "four cardioids on a circle of 1 m radius, pointing outwards, at 45, -45, 135 and -135 degrees",
          ring([45.0, -45.0, 135.0, -135.0], 1.0, CARDIOID))
    write("Ring-5-cardioids", "Ring of 5 cardioids", "five cardioids on a circle of 1.5 m radius at the ITU angles 0, +-30 and +-110 degrees, pointing outwards",
          ring([30.0, 0.0, -30.0, 110.0, -110.0], 1.5, CARDIOID))
    write("Ring-8-cardioids", "Ring of 8 cardioids", "eight cardioids on a circle of 1.5 m radius, 45 degrees apart, pointing outwards, the first at the front",
          ring([0.0, -45.0, -90.0, -135.0, 180.0, 135.0, 90.0, 45.0], 1.5, CARDIOID))
    write("Ring-8-supercardioids", "Ring of 8 supercardioids", "as Ring-8-cardioids, with sharper supercardioids",
          ring([0.0, -45.0, -90.0, -135.0, 180.0, 135.0, 90.0, 45.0], 1.5, SUPER))
    print(f"Wrote {len(list(OUT.glob('*.xml')))} setups to {OUT}")


if __name__ == "__main__":
    main()
