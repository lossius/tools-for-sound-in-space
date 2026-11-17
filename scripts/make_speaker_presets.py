#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Trond Lossius
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Generates the speaker layout presets in "speaker layouts/" (TriPan speaker XML files).

Run from anywhere:  python3 scripts/make_speaker_presets.py

Conventions (same as the plugin): metres, +y is front, +x is right. Angles are azimuths measured
clockwise from the front (positive = right). Speakers are placed on a circle of RADIUS metres.
The LFE channel of 5.1 / 7.1 has no real position. It is channel 4 (the usual film order L R C LFE Ls Rs), placed
halfway between the listener and the centre speaker and marked excluded="1", so it takes no part in the panning
(its output stays silent); un-exclude it in the speaker table or on the pad if a sub should be fed.
The output format is identical to the plugin's "Export speakers" files (3 decimals, padded).
"""
import math
import random
from pathlib import Path

RADIUS = 5.0
OUT = Path(__file__).resolve().parent.parent / "speaker layouts"


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


def write(name, title, description, points, virtual_speakers=None, excluded=()):
    """Writes one preset. Without virtual_speakers the file says virtual="auto" (loading a preset switches the
    virtual speakers to Auto, which most layouts are designed for). With a list of positions it says
    virtual="manual" and lists them as <VirtualSpeaker> entries, like the plugin's own export. `excluded` is a list of
    1-based speaker numbers that are excluded from the panning (all speakers then get an excluded="0" or "1" attribute,
    as in the plugin's own export)."""
    lines = ['<?xml version="1.0" encoding="UTF-8"?>', ""]
    mode = "auto" if virtual_speakers is None else "manual"
    lines.append(f'<TriPanSpeakers version="1" count="{len(points)}" virtual="{mode}">')
    lines.append(f"  <!-- {title}: {description} -->")
    for i, (x, y) in enumerate(points, start=1):
        flag = f' excluded="{1 if i in excluded else 0}"' if excluded else ""
        lines.append(f'  <Speaker index="{str(i).rjust(2)}" x="{number(x)}" y="{number(y)}"{flag}/>')
    for i, (x, y) in enumerate(virtual_speakers or [], start=1):
        lines.append(f'  <VirtualSpeaker index="{str(i).rjust(2)}" x="{number(x)}" y="{number(y)}"/>')
    lines.append("</TriPanSpeakers>")
    (OUT / f"{name}.xml").write_text("\n".join(lines) + "\n", encoding="utf-8")


def rect_grid(columns, rows, half_width=RADIUS, half_height=RADIUS):
    """columns x rows grid spanning +-half_width (x) and +-half_height (y), numbered row by row from the front-left."""
    xs = [-half_width + 2 * half_width * k / (columns - 1) for k in range(columns)]
    ys = [-half_height + 2 * half_height * k / (rows - 1) for k in range(rows)]
    return [(x, y) for y in reversed(ys) for x in xs]


def grid(n, extent=RADIUS):
    """n x n grid spanning +-extent, numbered row by row from the front-left."""
    return rect_grid(n, n, extent, extent)


def cell_centres(columns, rows, half_width=RADIUS, half_height=RADIUS):
    """The centre of every cell of a columns x rows grid of speakers ((columns-1) x (rows-1) cells), row by row from the front-left."""
    points = rect_grid(columns, rows, half_width, half_height)
    return [((points[r * columns + c][0] + points[r * columns + c + 1][0]) / 2,
             (points[r * columns + c][1] + points[(r + 1) * columns + c][1]) / 2)
            for r in range(rows - 1) for c in range(columns - 1)]


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

    lfe = (0.0, RADIUS / 2)

    write("5.1", "5.1 (ITU-R BS.775)",
          "L, R, C, LFE, Ls, Rs; the speakers at -30, 30, 0, -110, 110 degrees; the LFE (4) is excluded from the panning",
          circle([-30, 30, 0]) + [lfe] + circle([-110, 110]), excluded=(4,))

    write("7.1", "7.1",
          "L, R, C, LFE, Lss, Rss, Lrs, Rrs; the speakers at -30, 30, 0, -90, 90, -150, 150 degrees; the LFE (4) is excluded from the panning",
          circle([-30, 30, 0]) + [lfe] + circle([-90, 90, -150, 150]), excluded=(4,))

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

    # Grids and a corridor with a manual virtual speaker at the centre of every cell, so that a source in a cell is panned
    # to the four corners of that cell (the corners are cocircular, so the plain triangulation picks an arbitrary diagonal).
    write("Grid-3x3-virtual-centres", "3x3 grid with virtual speakers at the cell centres",
          "9 speakers as in Grid-3x3, plus 4 manual virtual speakers at the centres of the 4 squares",
          grid(3), cell_centres(3, 3))

    write("Grid-4x4-virtual-centres", "4x4 grid with virtual speakers at the cell centres",
          "16 speakers as in Grid-4x4, plus 9 manual virtual speakers at the centres of the 9 squares",
          grid(4), cell_centres(4, 4))

    # Corridor: 8 columns spanning +-5 m and 2 rows 3 m apart, so every cell is a 1.43 m x 3 m rectangle.
    CORRIDOR_HALF_HEIGHT = 1.5
    corridor_description = "16 speakers in 2 rows of 8, x spanning +-5 m, rows at y = +-1.5 m (every cell is a 1.43 m x 3 m rectangle), numbered row by row from the front-left"
    write("Corridor-8x2", "8x2 corridor", corridor_description,
          rect_grid(8, 2, RADIUS, CORRIDOR_HALF_HEIGHT))

    write("Corridor-8x2-virtual-centres", "8x2 corridor with virtual speakers at the cell centres",
          corridor_description + ", plus 7 manual virtual speakers at the centres of the 7 rectangles",
          rect_grid(8, 2, RADIUS, CORRIDOR_HALF_HEIGHT), cell_centres(8, 2, RADIUS, CORRIDOR_HALF_HEIGHT))

    write("Grid-8x8", "8x8 grid", "64 speakers on a square grid spanning +-5 m, numbered row by row from the front-left",
          grid(8))

    write("Grid-8x8-virtual-centres", "8x8 grid with virtual speakers at the cell centres",
          "64 speakers as in Grid-8x8, plus 49 manual virtual speakers at the centres of the 49 squares",
          grid(8), cell_centres(8, 8))

    write("Random-16", "16 random positions",
          "16 speakers at random positions within 6 m of the centre, at least 1.5 m apart (fixed seed)",
          random_layout(16, seed=20241107))

    print(f"Wrote {len(list(OUT.glob('*.xml')))} presets to {OUT}")


if __name__ == "__main__":
    main()
