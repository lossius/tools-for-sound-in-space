#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Trond Lossius
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Generates the microphone setups for ViMiC in "microphone setups/" (ViMiC speaker XML files with a yaw and a pattern per microphone).

Run from anywhere:  python3 scripts/make_microphone_setups.py

Conventions (same as the plugin): metres, +y is front, +x is right, the listener (the middle of the array) is at the origin. The yaw of a
microphone is the direction of its axis in degrees, counter-clockwise from the front (positive = to the left); the pattern is the ratio a of
the directivity (a + (1 - a) cos(angle))^w: 1 omni, 0.7 subcardioid, 0.5 cardioid, 0.33 supercardioid, 0.3 hypercardioid, 0 figure-8.
A microphone may also have a height z in metres above the floor (default 1.5, the listener's ear height), a pitch (the elevation of its
axis in degrees, positive upwards, default 0) and be excluded from the panning (a LFE channel); those attributes are written only when a setup uses them.
The output format is the plugin's own "Export microphones" format (3 decimals, padded).
"""
import math
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "microphone setups"
OMNI, CARDIOID, SUPER = 1.0, 0.5, 1.0 / 3.0
EAR_HEIGHT = 1.5   # the default height of sources and microphones


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


def dome(layers, radius, pattern):
    """Microphones on a sphere of `radius` around the listener's head, pointing outwards. `layers` = (azimuth, elevation, excluded) with the elevation in degrees;
    the position follows the direction, the yaw is the azimuth and the pitch the elevation. An excluded one (LFE) sits at the head."""
    mics = []
    for azimuth, elevation, excluded in layers:
        r = 0.0 if excluded else radius
        x, y = azimuth_position(azimuth, r * math.cos(math.radians(elevation)))
        mics.append((x, y, azimuth, pattern, EAR_HEIGHT + r * math.sin(math.radians(elevation)), elevation, excluded))
    return mics


def write(name, title, description, mics):
    lines = ['<?xml version="1.0" encoding="UTF-8"?>', ""]
    lines.append(f'<ViMiCSpeakers version="1" count="{len(mics)}">')
    lines.append(f"  <!-- {title}: {description} -->")
    three_d = any(len(m) > 4 for m in mics)
    for i, m in enumerate(mics, start=1):
        x, y, yaw, pattern = m[:4]
        z, pitch, excluded = (m[4:] + (EAR_HEIGHT, 0.0, False)[len(m) - 4:]) if three_d else (None, None, None)
        extra = f' z="{number(z)}" pitch="{number(pitch)}"' if three_d else ""
        flag = f' excluded="{1 if excluded else 0}"' if any(len(q) > 6 and q[6] for q in mics) else ""
        lines.append(f'  <Speaker index="{str(i).rjust(2)}" x="{number(x)}" y="{number(y)}"{extra} yaw="{number(yaw)}" pattern="{pattern:.3f}"{flag}/>')
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
    # Dolby Atmos beds: the ITU azimuths (counter-clockwise from the front), the top speakers 45 degrees up. The LFE (4) is excluded.
    bed_5 = [(30.0, 0.0, False), (-30.0, 0.0, False), (0.0, 0.0, False), (0.0, 0.0, True), (110.0, 0.0, False), (-110.0, 0.0, False)]
    bed_7 = [(30.0, 0.0, False), (-30.0, 0.0, False), (0.0, 0.0, False), (0.0, 0.0, True), (90.0, 0.0, False), (-90.0, 0.0, False), (135.0, 0.0, False), (-135.0, 0.0, False)]
    tops = [(45.0, 45.0, False), (-45.0, 45.0, False), (135.0, 45.0, False), (-135.0, 45.0, False)]
    write("Atmos-7.1.4-cardioids", "Atmos 7.1.4", "12 cardioids on a sphere of 1 m radius around the head, in the order L, R, C, LFE, Lss, Rss, Lrs, Rrs, Ltf, Rtf, Ltr, Rtr; the top four are tilted 45 degrees up; the LFE (4) is excluded",
          dome(bed_7 + tops, 1.0, CARDIOID))
    write("Atmos-5.1.4-cardioids", "Atmos 5.1.4", "10 cardioids on a sphere of 1 m radius around the head, in the order L, R, C, LFE, Ls, Rs, Ltf, Rtf, Ltr, Rtr; the top four are tilted 45 degrees up; the LFE (4) is excluded",
          dome(bed_5 + tops, 1.0, CARDIOID))
    print(f"Wrote {len(list(OUT.glob('*.xml')))} setups to {OUT}")


if __name__ == "__main__":
    main()
