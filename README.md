# Tools for Sound in Space

A collection of free, open-source (AGPLv3) plugins for spatial audio, all with the same pad, tables, source groups and
import/export, so that they look and work alike. So far: **TriPan** (triangulation panning), **DistPan** (distance-based
amplitude panning, DBAP) and **VecPan** (2D vector base amplitude panning, VBAP). ViMiC is planned.

Status: pre-release, version 0.1.0. TriPan, DistPan and VecPan work and are covered by automated tests and pluginval (strictness 10; the CI workflow runs
them on macOS and Windows, but has not run yet); they have not yet been tested in every host. AAX is optional and unverified in Pro Tools.

## TriPan

A free, open-source (AGPLv3) spatial audio plugin that distributes n sound sources to m speakers in a
2D plane. Each source is panned to the three speakers of the Delaunay triangle that contains it,
with constant overall intensity.

## Repository layout

| Folder | Contents |
|---|---|
| `shared/core` | `TlosCore`: JUCE-free code every plugin uses (geometry types, channel limits, source groups, lock-free hand-over) |
| `shared/ui` | `TlosUI`: the processor base class, host parameters, the XY pad, the editor window, table windows and position import/export |
| `DistPan/` | DistPan: `core/` (the DBAP gain law, JUCE-free), `Source/` (processor, pad and editor on top of `TlosUI`), `tests/` |
| `VecPan/` | VecPan: `core/` (the 2D VBAP gain law, JUCE-free), `Source/` (processor, pad and editor on top of `TlosUI`), `tests/` |
| `TriPan/` | TriPan itself: `core/` (triangulation and gains, JUCE-free), `Source/` (processor, pad and editor on top of `TlosUI`), `tests/` |
| `speaker layouts/` | Ready-made speaker layouts (XML) that any plugin of the family can import |

More plugins (ViMiC) are planned and will sit next to the others, reusing `shared/`.
The built plug-ins end up in `<build folder>/<plugin>/<plugin>_artefacts/`.

## DistPan

Distance-based amplitude panning (DBAP) in a 2D plane. Every speaker gets a share of a source that falls off with its
distance from it, g_i = k / d_i^a, with k chosen so that the total power is constant. Two settings, both host parameters:

- **Rolloff** (1-12 dB, default 6): the level drop for each doubling of the distance between a source and a speaker. 6 dB is the
  free-field value; more makes the sound more local.
- **Blur** (0-5 m, default 0.2): added to the distance (d^2 + blur^2), so that a source on top of a speaker does not use that
  speaker alone and the sound moves smoothly when a source passes one. A source's **spread** adds to the blur, so a larger
  spread distributes the sound more evenly.

- **Speaker weights** (0-1, default 1, one per speaker, "Speaker N Weight"): each speaker's weight multiplies its share
  before the total is normalised (as in the DBAP paper), so a speaker with weight 0.5 gets a smaller share and the others make up
  for it, and a speaker with weight 0 takes no part. Set them in the Weight column of the speaker table; they are saved in speaker
  files (`weight` attribute) and can be automated. Unlike trim, a weight changes the distribution, not just one output's level;
  unlike exclude it can be automated and set in between.

Unlike TriPan, DistPan needs no triangulation and has no virtual speakers; it works with any number of speakers (1 to 64),
also collinear or irregular layouts, and sources outside the speakers. On the pad, links are drawn to the speakers that get
a share within 20 dB of the strongest one. The pad always draws the convex hull of the speakers that take part in the panning as thin lines, like TriPan's triangulation: the edge of
the layout, where the level starts to fall off for a source outside it (see *Level outside the speakers*).

## Level outside the speakers

Both plugins have the host parameter **Outside rolloff** (0-24 dB, default 6, one slider in the settings window). A source outside
the speaker layout is lowered by `(1 + d)^(-r/6.02)`, where d is the distance in metres from the source to the edge of the
layout (the convex hull of the speakers) and r the rolloff: each doubling of 1 + d costs r dB, so 6 dB gives -6 dB one
metre outside and -12 dB three metres outside. Inside the layout and on its edge nothing changes, and the level falls
smoothly with no step at the edge. Set it to 0 for the old behaviour, a constant level everywhere. With fewer than three
speakers, or speakers on one line (a stereo pair), there is no inside to leave and the falloff does not apply.
In TriPan the sound outside the layout is panned as if the source stood on the nearest edge of the layout; the
rolloff comes on top of that. Sessions saved before this setting existed get the default (6 dB) when they are opened.

## VecPan

Two-dimensional vector base amplitude panning (VBAP, Pulkki) around the listener at the centre. A source is panned between the two
speakers on either side of its direction, seen from the listener, with constant power; a source exactly on a speaker's direction uses
that speaker alone. Only the direction counts, so the speakers need not lie on a circle, and the distance of a source inside the
layout changes nothing, with one exception near the listener: the **Centre radius** (0-5 m, default 0.5 m, a host parameter, under Panning in
the settings window). A source at the listener has no direction and is shared equally by all speakers; within the centre radius the sound is blended
smoothly (smoothstep of distance / radius) from that even share to the plain VBAP pan, so a source moving through the centre does not jump
from one speaker pair to another. 0 switches the blend off. Positions are shown in azimuth and distance by default (see *Coordinates*).

- **Speakers that do not surround the listener** (for example a stereo pair, or speakers on one side only): no vector base exists across a
  gap of 180 degrees or more, so the two speakers on either side of the gap share the sound with a sine/cosine cross-fade over the angle.
  The panning stays continuous and keeps constant power.
- **Spread** is an angular width: a source of spread *s* at distance *d* is panned as three directions, at its own azimuth and at plus
  and minus atan(*s* / *d*), and their powers are averaged (like MDAP). A source at the listener is shared equally (see the centre radius above).
- **Speakers in the same direction** count as one (the lowest-numbered takes the sound); a speaker exactly at the listener has no direction and
  takes no part.
- **Speakers at different distances**: VecPan pans by direction only and takes all speakers to be at one distance, the **reference radius** =
  the distance of the farthest speaker (the one a gain and delay compensation would align to). The pad always draws it as a dashed circle.
  Inside it the distance of a source does not matter; beyond it the level falls off by the **Outside rolloff** (the distance counted from
  the circle, not from the polygon of the speakers as in the other plugins), so you can see where gain reduction starts. As in the other
  plugins, a layout with no area (a stereo pair, speakers on one line) has no falloff. Differing speaker distances are best corrected with
  level and delay compensation (not part of this plugin). **Place speakers on circle** (Settings window, Panning) moves every speaker that
  takes part (in use and not excluded) to the reference radius, keeping its direction; it is one undo step and works whatever the Edit/View
  menu says.
- The pad can draw a line between every two neighbouring speakers that VBAP pans between (**Show pairs**), and links from each source to the
  one or two speakers it feeds.

## Coordinates: Cartesian or polar

The host parameters, the position files and the pad always use x and y in metres. How the tables and the pad's readouts show positions is a
setting (**Coordinates** in the settings window, also in the table windows): **Cartesian** (x, y) or **Polar** (azimuth in degrees,
counter-clockwise from the front, so +90 is on the left; and distance in metres). In polar mode the table columns are Azimuth and Distance, typing
one keeps the other, a source at the centre keeps its last azimuth, the pad draws distance rings and azimuth spokes instead of the grid,
and the tooltip and the label that follows a dragged source give azimuth and distance. TriPan and DistPan start in Cartesian, VecPan in Polar.
Position files may give `az` and `dist` instead of `x` and `y` for an entry (`x` and `y` win if both are there); exports always write x and y.

## Undo

Everything you change in the plugin is a host parameter: positions, spreads, trim, mute, solo, groups, exclusions and the
virtual speakers (mode, number and positions). So the host's undo (and its automation) covers all of it, not only the positions. One editor
action, for example grouping a selection or deleting a virtual speaker, is one undo step. Sessions saved by an earlier version, which kept
groups, exclusions and virtual speakers in the plugin's own state, open as before: they are moved into the parameters when loaded. The
settings that are not edits of the sound stay with the session only: the Edit/View menus, the window size, the room size and the OSC settings.

## Parameters in the host
The host lists the global parameters first: the number of sources and speakers, the outside rolloff, the virtual speaker mode and number (TriPan) or
the rolloff and blur (DistPan) or the centre radius (VecPan). After them come the parameters of each source (x, y, spread, trim, mute, solo, group), of each speaker (x, y, trim, mute,
solo, exclude, and DistPan's weight) and, in TriPan, the positions of the manual virtual speakers. Hosts that show parameters as a tree (VST3 and AU hosts such as
Cubase, Studio One, Bitwig and Logic) get the same structure as folders: Global, Sources, Speakers and Virtual Speakers, with one folder per source and per speaker.

## Trim, mute, solo and exclude

Every source and every speaker has a **Trim** (-60 to +12 dB), a **Mute** switch and a **Solo** switch, all host parameters
that can be automated. A source's trim is a gain on its signal before the panning; a speaker's trim is a gain on its output
after the panning. A muted source or speaker is silent. If any source is soloed, only the soloed sources are heard, and
the same for speakers; sources and speakers are soloed separately, and mute wins over solo. Changes are ramped over a block,
so switching never clicks.

Muting a speaker silences its output but does not move its share to the others: the level of the other speakers stays
as it was. To take a speaker out of the panning altogether, **exclude** it: the panning is then computed as if the speaker
did not exist, so the others share its signal and the total power stays constant. Use it for a broken speaker or one that should
not take part (a subwoofer). Exclude applies to speakers only ("Speaker N Exclude" in the host's parameter list).

Set them in the tables (Trim, Mute, Solo and, for speakers, Excl. columns), or right-click a source or speaker on the pad for
a menu with Mute, Solo and Exclude from panning. Muted or silenced sources and speakers are drawn dimmed with an M (muted) or S
(soloed) badge; excluded speakers are hollow and struck through. The position files carry the settings too (see
*Saving and loading positions*). Note that the plugins now have a lot of host parameters (about 970 in TriPan, 900 in DistPan), which some hosts list slowly.

## Settings window

The **Settings...** button, at the right end of the third row below the two **Table...** buttons, opens a window with what is set once and then left alone, so that the plugin
window stays uncluttered:

- **Room**: the room size.
- **Panning**: the outside rolloff, the virtual speakers menu (TriPan), DistPan's own **DBAP rolloff** and **DBAP blur**, and VecPan's **Centre radius**.
- **OSC**: the settings and a live view of the traffic (see *OSC control*).

The window opens beside the plugin window, stays in front of it, and closing it only hides it. The room size is also in the top
row of the two table windows.

## Room size

The **Room size** slider (2 to 100 m, default 10; in the settings window and in the sources and speakers tables) sets the half-width of the room: the pad shows that much on each side of the
listener, with a grid that follows, and sources and speakers stop at its edge when you drag them. It is saved with the session. Positions
outside the room are still possible, typed in the tables, loaded from files, or automated by the host, and sound as usual: the room
only limits the pad. The position parameters reach +-100 m, so the host shows real metres. A new room size shows the whole room again,
and "Reset layout" places the speakers on a circle of half the room half-width and the sources on a quarter of it.

## OSC control

The **OSC** section of the settings window controls the plugin over OSC (UDP) and has changes sent to a controller or another program.
Receiving and sending are switched on separately, **both are off in a new session** (an instance that listens takes its port, so two instances
cannot share one), and the ports, the host to send to and the address format are saved with the session. The window shows what is happening:
a lamp for **In**, **Out** and **Ignored** that lights for a moment whenever a message was received, sent or not understood, each with a running
count and the last message as text, and a line that says whether the port is open, for example "listening on port 9000" or "port 9000 is in use
or not allowed". OSC listens on all network interfaces and has no password: use it on a network you trust.

Messages are applied inside a change gesture, so hosts record them like moves with the mouse. Numbers are 1-based; values are
floats or ints; address patterns with wildcards are not supported.

| Address | Arguments |
|---|---|
| `/source/N/xy` | x, y in metres (`/source/N/x` and `/y` take one) |
| `/source/N/spread`, `/trim`, `/mute`, `/solo` | metres, dB, 0 or 1, 0 or 1 |
| `/speaker/N/xy`, `/x`, `/y`, `/trim`, `/mute`, `/solo` | as for sources |
| `/speaker/N/exclude` | 0 or 1: exclude from the panning |
| `/speaker/N/weight` | DistPan only: 0 to 1 |
| `/room/size` | half-width of the room in metres |
| `/outside/rolloff` | dB per doubling outside the layout |
| `/sources/count`, `/speakers/count` | the number in use |
| `/dbap/rolloff`, `/dbap/blur` | DistPan only |
| `/vbap/centre` | VecPan only: the centre radius in metres |
| `/dump` | send everything now |

**ADM-OSC** objects work as well, for sources: `/adm/obj/N/xyz` (three floats from -1 to 1, x right, y front, z is accepted
and ignored), `x`, `y`, `z`, `aed` (azimuth in degrees counter-clockwise from the front, elevation in degrees, distance 0 to 1),
`azim`, `elev`, `dist`, `gain` (linear) and `mute`. Normalised positions and distances are fractions of the room half-width (the
Room size slider); the elevation only shortens the distance, because the plane is flat.

When sending is on, everything is sent once, then only what changes, at most 30 times a second. **Send as** chooses the own
addresses, ADM-OSC (positions, gain and mute of the sources) or both. A controller that sends back what it receives does no harm: only
changes are sent, so the echo stops by itself. Incoming messages of both kinds are always understood.

## API documentation

The code is documented with Doxygen. Install it (`brew install doxygen`), run `doxygen Doxyfile` in the repository root and
open `build/doxygen/html/index.html`.

## Build (macOS)
```
git clone --recursive <repo-url>
cd TriPan
cmake -B build -G Xcode
cmake --build build --config Release
```

On macOS the VST3 is also copied to `~/Library/Audio/Plug-Ins/VST3` and the AudioUnit to
`~/Library/Audio/Plug-Ins/Components` after each build. Use `-DTLOS_INSTALL_VST3=OFF` /
`-DTLOS_INSTALL_AU=OFF` to disable this, or `-DTLOS_VST3_INSTALL_DIR=<folder>` /
`-DTLOS_AU_INSTALL_DIR=<folder>` to change the location.

## Source spread
Each source has a **spread**: the radius, in metres, of a circle around it over which its sound is
spread (0 to 50 m, default 0 = a point source). It is a host parameter per source ("Source N Spread"), so it can
be automated and is saved with the session.

On the pad the spread is drawn as a semi-transparent circle around the source, in the source's colour,
and the source itself keeps its solid inner circle.

* **Set the spread:** hold **Option** (Alt on Windows) and drag the source **vertically**: moving the mouse
  up increases the spread, moving it down decreases it, and sideways movement has no effect. The change is
  relative to the value when you press, so you can start a drag anywhere on the inner circle. The spread is
  limited to 0..50 m at every step, so dragging down to 0 and further, then up again, increases the spread at
  once, which makes it easy to bring the spread back to 0 (just drag down). A readout shows the value while you
  drag, and the source does not move. If the source is in a group (or part of a selection) the spread of all of
  them changes by the same amount; add **Cmd** (Ctrl on Windows) to change only the source you grab.
* **Reset it:** Option + double-click the source (Option+Cmd for that source only when it is in a group).
* **Moving** works as before: drag the inner circle without a modifier.
* **Only the inner circle takes the mouse.** The spread circle is display only, so a source with a large spread
  never gets in the way of other sources inside it. Sources must be set to Edit.
* Hover over the pad for a tooltip that reminds you of the gesture.

How it sounds: the source is panned not from one point but from its position plus 8 points evenly spaced on
the circle (the idea of MDAP, multiple-direction amplitude panning), and the squared gains are averaged, so the
power stays constant. A larger spread feeds more speakers more evenly and softens the panning, which also
smooths the jump between neighbouring triangles. Spread 0 is exactly the plain panning. The gains change
continuously with the spread and with the position, and mirrored sources still give mirrored gains. Cost: up
to 9 times the work of a point source, only for sources that move or change their spread (about 0.7 ms per
block for 64 such sources in the worst case).

The spread is saved in the source XML files as an optional `spread` attribute (metres); entries without
it leave the spread unchanged.

## Source groups
Two or more sources can be grouped, so that they move together and keep their positions relative to each
other. Several groups can exist side by side. The groups are saved with your project as host parameters ("Source N Group"), so the host's undo covers grouping and ungrouping, and they can be automated, but they only affect how you move sources in the editor. The sources'
x and y positions stay ordinary parameters, so a move is recorded as automation of every member, and
automation or table edits of one source do not need the group.

* **Select:** click a source (the whole group is selected if it is in one); **Shift-click** adds or removes a
  source (with its group); **drag a rectangle** on empty space to select what is inside it (Shift adds to the
  current selection); click empty space to clear. Selected sources have a white ring.
* **Group / Ungroup:** the buttons in the third row, or **Cmd+G** and **Cmd+Shift+G** (Ctrl on Windows) while
  the pad has the keyboard focus. Grouping a selection that contains members of existing groups merges those
  groups whole. Ungrouping dissolves the whole group of every selected source. A group of one source does not exist.
* **Moving and shaping a group:** the modifiers are read when you press the mouse button and apply to the whole drag.
  "The group" is the group of the source you grab, or the selection if it is part of one.

  | Drag | macOS / Windows | Effect |
  |---|---|---|
  | plain | | Moves the whole group, keeping its shape. Every member stays inside the room (the pad, +-10 m by default): the group stops at the wall (and slides along it) without being deformed. |
  | **Cmd** | Cmd / Ctrl | Moves the source you grab only. It stays in the group, which gets a new shape. |
  | **Option** | Option / Alt | Changes the spread of the whole group by the same amount (up = more). No member goes below 0 or above 50 m. |
  | **Option + Cmd** | Option+Cmd / Alt+Ctrl | Changes the spread of the grabbed source only. |
  | **Option + Shift** | Option+Shift / Alt+Shift | Scales the group around its centre of gravity: up = the sources move apart, down = closer together (about 0.5 % per pixel, not below 5 % of the size at the press). The shape is kept, and expansion stops when a member reaches the wall. |
  | **Cmd + Shift** | Cmd+Shift / Ctrl+Shift | Rotates the group around its centre of gravity: up = counter-clockwise, down = clockwise (one degree per pixel). The rotation stops where a member would leave the pad, and you can turn back at once. |

  Spread, scaling and rotation are vertical drags (sideways movement does nothing). Scaling and rotation need at
  least two sources (a group or a selection) and show a readout and the centre of gravity while you drag.
  Double-clicking with Option resets the spread of the group (Option+Cmd: of one source).
* **Look:** all members of a group take the colour of the member with the lowest number (and get their own colours back when ungrouped); a ring marks the members and a soft band surrounds their outline, with a
  number "G1", "G2", ... above it. Hovering a grouped source names its group in the tooltip. The table window for
  sources has a Group column.
* Groups need sources set to **Edit**. If you lower the number of sources, hidden members keep their group
  but do not move.
* The source XML files get an optional `group` attribute per source (1 to 32, 0 or missing = none). An import
  where any source has it replaces the groups; a file without it leaves them alone.

## Virtual (imaginary) speakers
Speakers that lie on a circle around the listener (5.1, quad, hexagon, octagon, ...) have no unique
Delaunay triangulation. Triangulation then picks an arbitrary, asymmetric set of triangles: in a 5.1
layout a source at the origin used to favour the right front speaker and the surrounds. Virtual
speakers fix this. They take part in the triangulation but are not outputs: the gain a source sends
to a virtual speaker is shared (at constant power) between the real speakers around it, in inverse
proportion to their distance from the virtual speaker, so nearer speakers get more. The
menu on the fourth row selects where they come from:

* **Auto** (default): found from the speaker layout. A virtual speaker is added at the listener
  (the origin) when the speakers form a ring around it, and in the middle of angular gaps wider than
  about 100 degrees (for example the open back of 5.1). Nothing is added when the speakers do not
  surround the origin. With Auto, a source at the origin in 5.1 feeds all five speakers equally, and
  mirrored positions give mirrored gains.

  The decisions are soft. Each automatic virtual speaker has a strength between 0 and 1 that rises
  smoothly with how much the layout calls for it (a gap fades in between 90 and 110 degrees, the centre
  speaker fades out as the radii become uneven, everything fades out as the listener leaves the
  speakers). The panning is a smooth blend between the layout with and without the virtual speaker, so
  dragging or automating a speaker never makes the sound jump when a threshold is crossed. (With hard
  thresholds, a surround speaker moving across 100 degrees changed a source's gain by up to 0.12 in one
  step; with the blend the largest step is 0.0001.) Weak virtual speakers are drawn faded.

  Auto also fills every **cocircular cell**: where four or more speakers lie on a common circle (grid
  cells, regular polygons, an arc), the triangulation could choose either diagonal, and that arbitrary
  choice would pan a source differently on either side of the grid. Auto puts a virtual speaker in the
  middle of such a cell, so grids, rings and arcs are panned symmetrically. The strength fades in as the
  corners approach a common circle (within 0.3 % of the radius it is full, beyond 3 % it is gone), so
  moving a speaker through a cocircular position makes no jump. Works for any layout, with the listener anywhere.
* **Manual**: you place them. Set the speakers menu to Edit, then double-click empty space on the pad
  to add one, drag to move, double-click one to delete it (at most 64). Switching from Auto to
  Manual starts from the current automatic set. Positions are saved with the session.
* **Off**: real speakers only, as in the plain triangulation.

Virtual speakers are drawn as hollow amber squares ("v1", "v2", ...). The price is that sources
inside a ring are spread more widely than the strict three speakers of plain triangulation; switch to
Off if you prefer that. The mode, the number of manual virtual speakers and their positions are host parameters ("Virtual Speakers", "Number of Virtual Speakers", "Virtual Speaker N X/Y"), so the host's undo covers adding, moving and deleting them. They are saved with the session, and a speaker
XML file records the setting and, for Manual, the virtual speakers (see below).

## Level meters
Tick **Show levels** in the third row to see the signal on the pad: a ring around each speaker shows its output level, with a small dot that holds the
peak for a second, and a thinner ring around each source shows its input level. The scale is -60 to 0 dBFS; the ring is green, amber above -12 dB and red above -3 dB.
It is peak metering, it rises at once and falls in about a second, and the setting is not saved with the session. It costs one pass over the buffers
and nothing when the toggle is off (the audio thread always records the peaks, the pad only reads them while the toggle is on).

## Zooming
The two magnifying-glass buttons, **Zoom In** (+) and **Zoom Out** (-), in the third row change the visible area of the pad. Each press of Zoom In
zooms a step towards the area that contains all speakers in use and stops when they just fill the pad;
Zoom Out zooms back in steps until the entire room is shown again, as at start. The view stays
where it is while you drag speakers, so press Zoom In again after moving them if you want it refitted.
The grid labels follow the zoom. The zoom is not saved with the session.

## Edit and View
In the top rows of the window, one row for sources and one for speakers, a menu switches each kind of
position between **Edit** (can be dragged in the window) and **View** (locked against dragging and "Reset layout"; importing and exporting still work). By default sources
are set to Edit and speakers to View. The choice is saved with the session. It only affects the
window: automation from the host can still move everything.

## Saving and loading positions
The export and import buttons on the sources and speakers rows save and load source and speaker positions as XML files, with a
file dialog each time. Sources and speakers are separate files:
```xml
<TriPanSpeakers version="1" count="3" virtual="manual">
  <Speaker index=" 1" x="  0.000" y="  5.000"/>
  <Speaker index=" 2" x="  4.330" y=" -2.500"/>
  <Speaker index=" 3" x=" -4.330" y=" -2.500"/>
  <VirtualSpeaker index=" 1" x="  0.000" y="  0.000"/>
</TriPanSpeakers>
```
(`<TriPanSources>` / `<Source>` for sources.) Coordinates are metres, +y is front, range +-100.

A speaker file also records the virtual speaker setting in the optional `virtual` attribute (`auto`, `manual` or `off`). With `manual`
the file lists your own virtual speakers as `<VirtualSpeaker>` entries (at most 64); with `auto` or `off`
there is nothing more to store, because automatic virtual speakers are recomputed from the real ones. Importing
a speaker file restores the setting (and the manual list), replacing the current one. A file without the
attribute, such as one written by an earlier version, leaves the virtual speaker setting alone.
The presets in `speaker layouts/` say `virtual="auto"`, so loading one switches back to Auto, except the `-virtual-centres` ones, which bring their own manual virtual speakers.

The optional attributes `trim` (dB), `mute`, `solo` (0 or 1) and, for speakers, `excluded` (0 or 1) carry the settings above.
They are written for every entry as soon as one entry has a non-default value. An import where at least one entry has an
attribute defines it for every channel (channels not in the file go back to the default); a file without it leaves things as they are.

Export writes the channels currently in use, with coordinates rounded to 3 decimal places and padded with spaces so that positive and negative values line up (import ignores the padding). Import checks the whole file first, applies every valid
entry by index. Importing speakers also sets "Number of Speakers" to the number of speakers in the
file, provided the track has that many outputs (otherwise the setting is left alone and the status
line says so). Importing sources never changes "Number of Sources". Import and export work whether a
kind is set to Edit or View.

## Speaker presets
The `speaker layouts/` folder has ready-made speaker layouts in the same XML format. Load one with
"Import speakers...". Number of Speakers is adjusted to
the preset automatically when the track has enough outputs. Angles are azimuths clockwise from the front and speakers are on a 5 m circle. The 5.1 and 7.1
presets have the usual channel order (L, R, C, LFE, Ls, Rs ...); the LFE is placed halfway between you and the centre speaker
and is **excluded** from the panning, so it stays silent and takes no part. Un-exclude it (Excl. column of the speaker table, or
right-click it on the pad) if you want it to be fed.

| File | Speakers | Layout |
|---|---|---|
| `Quad.xml` | 4 | FL, FR, BL, BR at -45, 45, -135, 135 degrees |
| `5.1.xml` | 5 | L, R, C, Ls, Rs at -30, 30, 0, -110, 110 degrees |
| `7.1.xml` | 7 | L, R, C, Lss, Rss, Lrs, Rrs at -30, 30, 0, -90, 90, -150, 150 degrees |
| `Hexagon-front-centre.xml` | 6 | every 60 degrees, speaker 1 at the front centre |
| `Hexagon-symmetric.xml` | 6 | at -30, 30, 90, 150, -150, -90 degrees |
| `Octagon-front-centre.xml` | 8 | every 45 degrees, speaker 1 at the front centre |
| `Octagon-symmetric.xml` | 8 | at -22.5, 22.5, 67.5, 112.5, 157.5, -157.5, -112.5, -67.5 degrees |
| `Grid-3x3.xml` | 9 | square grid spanning +-5 m, numbered row by row from the front-left |
| `Grid-4x4.xml` | 16 | square grid spanning +-5 m, numbered row by row from the front-left |
| `Grid-3x3-virtual-centres.xml` | 9 + 4 virtual | as `Grid-3x3.xml`, with manual virtual speakers at the centre of each of the 4 squares |
| `Grid-4x4-virtual-centres.xml` | 16 + 9 virtual | as `Grid-4x4.xml`, with manual virtual speakers at the centre of each of the 9 squares |
| `Corridor-8x2.xml` | 16 | 2 rows of 8 ("corridor"): x spans +-5 m, rows at y = +-0.714 m, so every cell is a 1.43 m square |
| `Corridor-8x2-virtual-centres.xml` | 16 + 7 virtual | as `Corridor-8x2.xml`, with manual virtual speakers at the centre of each of the 7 squares |
| `Grid-8x8.xml` | 64 | square grid spanning +-5 m, numbered row by row from the front-left (needs a track with 64 outputs) |
| `Grid-8x8-virtual-centres.xml` | 64 + 49 virtual | as `Grid-8x8.xml`, with manual virtual speakers at the centre of each of the 49 squares |
| `Random-16.xml` | 16 | random positions within 6 m, at least 1.5 m apart (fixed seed) |

The files are generated by `scripts/make_speaker_presets.py`; edit and re-run that script to change them.

## Build (Windows)
```
git clone --recursive <repo-url>
cd TriPan
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target TriPan_VST3 TriPan_Standalone
```

## AAX (Pro Tools), optional
The Avid AAX SDK is not included and cannot be redistributed. Download it from Avid, keep it outside
this repository, and check its current licence terms before publishing AAX builds.
```
cmake -B build -G Xcode -DTLOS_ENABLE_AAX=ON -DTLOS_AAX_SDK_PATH=/path/to/AAX_SDK
cmake --build build --config Release --target TriPan_AAX
```
Every AAX build is copied to Pro Tools' system folder (`/Library/Application Support/Avid/Audio/Plug-Ins` on
macOS, `%CommonProgramFiles%\Avid\Audio\Plug-Ins` on Windows). The folder must be writable by your user
(on macOS it is writable by the `admin` group by default). Turn this off with `-DTLOS_INSTALL_AAX=OFF`
or change the folder with `-DTLOS_AAX_INSTALL_DIR=...`.

Pro Tools only loads AAX plugins that are signed through Avid's PACE process. Development builds
need a Pro Tools Developer build; release builds need signing arranged with Avid. This path has not
been tested in Pro Tools yet, and the channel/stem formats Pro Tools accepts for 64-channel layouts
must be verified. The plug-in compiles and links against AAX SDK 2.10.0 (unsigned `.aaxplugin`, macOS Release build).
The SDK's `LICENSE.txt` offers it under Avid's commercial licence or under GPLv3; GPLv3 can be combined with
AGPLv3, but read the licence yourself before distributing an AAX build.

## Signing and notarization (macOS)
Unsigned downloads are blocked by Gatekeeper, so releases are signed with a **Developer ID
Application** certificate (not "Apple Development") and notarized.

One-time setup:
1. Create the certificate: Xcode > Settings > Accounts > Manage Certificates > + > Developer ID Application
   (only the Apple Developer account holder can create it).
2. Store notarization credentials in your keychain (uses an app-specific password from appleid.apple.com):
   `xcrun notarytool store-credentials tripan-notary --apple-id <id> --team-id <TEAMID>`

Then:
```
SIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)" NOTARY_PROFILE=tripan-notary \
    scripts/sign-and-notarize.sh
```
Set `PLUGIN=DistPan` for the other plugin (TriPan is the default). This builds a universal binary, signs with the hardened runtime, notarizes, staples and writes
zips to `dist/`. Add `DRY_RUN=1` to test the signing step only (no upload, zips are not releasable).

In CI, the macOS job signs and notarizes automatically when these repository secrets exist:
`MACOS_CERT_P12` (base64 of the exported .p12), `MACOS_CERT_PASSWORD`, `MACOS_SIGN_IDENTITY`,
`APPLE_ID`, `APPLE_TEAM_ID`, `APPLE_APP_PASSWORD`. Without them it uploads unsigned zips.

The Standalone app is signed with `resources/Standalone.entitlements` (audio input under the
hardened runtime). Windows builds are unsigned; Windows may show a SmartScreen warning.

## Continuous integration
`.github/workflows/build.yml` builds, tests and validates (pluginval, strictness 10) on macOS
(universal arm64 + x86_64) and Windows, and uploads zips of both plugins. Pushing a tag such as `v0.1.0`
also publishes a GitHub release with the zips. AAX is not built in CI.

## Licence
AGPLv3 (see [LICENSE](LICENSE)); built with JUCE, which is used under its AGPLv3 option. Source files carry SPDX headers. If you distribute TriPan, you must make the corresponding source available under the same licence.
