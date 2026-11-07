#!/usr/bin/env python3
"""Generates the speaker layout presets in Preset/ (TriPan speaker XML files).

Run from anywhere:  python3 scripts/make_speaker_presets.py

Conventions (same as the plugin): metres, +y is front, +x is right. Angles are azimuths measured
clockwise from the front (positive = right). Speakers are placed on a circle of RADIUS metres.
The LFE channel of 5.1 / 7.1 is not a speaker position and is left out.
The output format is identical to the plugin's "Export speakers" files (3 decimals, padded).
"""
import math
import random
from pathlib import Path

RADIUS = 5.0
OUT = Path(__file__).resolve().parent.parent / "Preset"


def polar(azimuth_deg, radius=RADIUS):
    a = math.radians(azimuth_deg)
    return radius * math.sin(a), radius * math.cos(a)


def circle(azimuths):
    return [polar(a) for a in azimuths]


def number(v):
    text = f"{v:.3f}"
    if text == "-0.000":
        text = "0.000"
    return text.rjust(7)


def write(name, title, description, points):
    lines = ['<?xml version="1.0" encoding="UTF-8"?>', ""]
    lines.append(f'<TriPanSpeakers version="1" count="{len(points)}">')
    lines.append(f"  <!-- {title}: {description} -->")
    for i, (x, y) in enumerate(points, start=1):
        lines.append(f'  <Speaker index="{str(i).rjust(2)}" x="{number(x)}" y="{number(y)}"/>')
    lines.append("</TriPanSpeakers>")
    (OUT / f"{name}.xml").write_text("\n".join(lines) + "\n", encoding="utf-8")


def grid(n, extent=RADIUS):
    """n x n grid spanning +-extent, numbered row by row from the front-left."""
    coords = [-extent + 2 * extent * k / (n - 1) for k in range(n)]
    return [(x, y) for y in reversed(coords) for x in coords]


def random_layout(count, seed, radius=6.0, min_distance=1.5):
    """Uniform random points in a disc, rejecting points closer than min_distance to earlier ones."""
    rng = random.Random(seed)
    points = []
    while len(points) < count:
        r = radius * math.sqrt(rng.random())
        a = rng.random() * 2 * math.pi
        p = (r * math.cos(a), r * math.sin(a))
        if all(math.dist(p, q) >= min_distance for q in points):
            points.append(p)
    return points


def main():
    OUT.mkdir(exist_ok=True)

    write("Quad", "Quad", "FL, FR, BL, BR at -45, 45, -135, 135 degrees",
          circle([-45, 45, -135, 135]))

    write("5.1", "5.1 (ITU-R BS.775, no LFE)", "L, R, C, Ls, Rs at -30, 30, 0, -110, 110 degrees",
          circle([-30, 30, 0, -110, 110]))

    write("7.1", "7.1 (no LFE)", "L, R, C, Lss, Rss, Lrs, Rrs at -30, 30, 0, -90, 90, -150, 150 degrees",
          circle([-30, 30, 0, -90, 90, -150, 150]))

    write("Hexagon-front-centre", "Hexagon, speaker at front centre",
          "6 speakers every 60 degrees clockwise from the front (0, 60, 120, 180, 240, 300)",
          circle([0, 60, 120, 180, 240, 300]))

    write("Hexagon-symmetric", "Hexagon, symmetric about the front",
          "6 speakers at -30, 30, 90, 150, -150, -90 degrees (clockwise from front-left)",
          circle([-30, 30, 90, 150, -150, -90]))

    write("Octagon-front-centre", "Octagon, speaker at front centre",
          "8 speakers every 45 degrees clockwise from the front",
          circle([0, 45, 90, 135, 180, 225, 270, 315]))

    write("Octagon-symmetric", "Octagon, symmetric about the front",
          "8 speakers at -22.5, 22.5, 67.5, 112.5, 157.5, -157.5, -112.5, -67.5 degrees",
          circle([-22.5, 22.5, 67.5, 112.5, 157.5, -157.5, -112.5, -67.5]))

    write("Grid-3x3", "3x3 grid", "9 speakers on a square grid spanning +-5 m, numbered row by row from the front-left",
          grid(3))

    write("Grid-4x4", "4x4 grid", "16 speakers on a square grid spanning +-5 m, numbered row by row from the front-left",
          grid(4))

    write("Random-16", "16 random positions",
          "16 speakers at random positions within 6 m of the centre, at least 1.5 m apart (fixed seed)",
          random_layout(16, seed=20241107))

    print(f"Wrote {len(list(OUT.glob('*.xml')))} presets to {OUT}")


if __name__ == "__main__":
    main()
