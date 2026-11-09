# TriPan

Free, AGPLv3-licensed audio plugin (VST3, AU, Standalone; AAX optional) for macOS and Windows.
Distributes n source channels to m speaker channels (up to 64 each). Sources and speakers lie in
one 2D plane, positions are xy plugin parameters. For each source, the speakers are triangulated
(Delaunay), the triangle containing the source is found, and the source is panned only to that
triangle's three speakers using barycentric weights, power-normalised so overall intensity is constant.

## Layout
- `core/` — `TriPanCore`: pure C++20 geometry and gain computation. **Must never include JUCE.**
- `tests/` — dependency-free tests of `TriPanCore`, run through `ctest`.
- `Source/` — JUCE plugin layer (processor, editor, parameters).
- `JUCE/` — git submodule, pinned to a release tag. Do not edit.

## Build and test
```
git submodule update --init
cmake -B build -G Xcode          # or: cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```
On macOS every build of `TriPan_AU` likewise copies the bundle to `~/Library/Audio/Plug-Ins/Components`
(`-DTRIPAN_INSTALL_AU=OFF`, `-DTRIPAN_AU_INSTALL_DIR=...`). On macOS every build of `TriPan_VST3` also copies the bundle to `~/Library/Audio/Plug-Ins/VST3`
(`-DTRIPAN_INSTALL_VST3=OFF` disables it; `-DTRIPAN_VST3_INSTALL_DIR=...` changes the folder; CI and
the signing script turn it off).

Validation: `scripts/validate.sh` builds the Release VST3 and runs pluginval at strictness 10
(passes as of M5). pluginval can only scan an AU that is installed in ~/Library/Audio/Plug-Ins/Components.
For threading checks, build `TriPanProcessorTests` in a separate dir with
`-DCMAKE_CXX_FLAGS=-fsanitize=thread` (clean as of M5); it includes a test that automates parameters
on one thread while another processes audio.

AAX (optional): `-DTRIPAN_ENABLE_AAX=ON -DTRIPAN_AAX_SDK_PATH=/path/to/AAX_SDK`. The Avid AAX SDK is
not redistributable: keep it outside this repo. Check its current licence before publishing AAX builds.

IDE layout (Xcode / Visual Studio): `source_group` calls in the CMake files give the project navigator the groups
`Sources`, `Core` (each with `src` and `include`), `Tests`, and `JUCE` (one subgroup per JUCE module, each with `src` =
the compiled `.mm/.cpp` units and `include` = the module's headers, added to the `TriPan` target only so the IDE lists
them). Headers must be listed in `target_sources` to show up; add new plugin files there and to the `source_group` lists.

Documentation: every file has a Doxygen `@file`/`@brief` block and every class, function, member and constant has a
`/** */` comment (also private ones and the test helpers; unnamed parameters in declarations should be named so that
`@param` works). `doxygen Doxyfile` (brew install doxygen) writes HTML to `build/doxygen/html` and must report no
warnings; keep it that way when adding code.

## Real-time rules (processBlock)
- No allocation, locks, file/network I/O, or logging.
- Triangulation is computed off the audio thread (on parameter change) and handed over lock-free
  (atomic pointer swap / double buffer).
- Apply gain changes with per-block ramps to avoid zipper noise.

## Design decisions
- Fixed parameter set: 64 sources and 64 speakers, each with x and y (256 parameters), allocated up front.
  Entries beyond the active channel count are inactive.
- Bus layouts: any enabled layout up to 64 channels (discrete, standard, or ambisonic orders 1-7 as
  channel-count carriers). n inputs and m outputs may differ.
- Gain law (core/src/Gains.cpp): barycentric weights w, g_i = w_i / sqrt(sum w_j^2), so sum g^2 = 1.
  Triangulation is Bowyer-Watson with a relative in-circle tolerance (cocircular layouts such as
  squares and rings are handled). Coincident speakers: first wins, later ones get no signal.
  Non-finite speaker positions are ignored.
- Top of the editor: row 1 sources, row 2 speakers (count, Edit/View menu, export, import), row 3 triangulation
  toggle, reset layout and the status/track-channels text (`layoutContent`). Edit / View per kind: a menu each, "Edit" or
  "View". Defaults: sources Edit, speakers View. Stored in the plugin state as `editSources` / `editSpeakers`
  (not host parameters; the old single `editMode` property from earlier versions is still read) and queried with
  `canEditSources()` / `canEditSpeakers()`. A kind set to View can't be grabbed or moved in the editor, and "Reset layout" only resets kinds set to Edit (disabled if both are View).
  Host automation of the position parameters is unaffected.
- Spread: parameter `src<N>spread` (0..10 m, default 0, host-visible). `computeGains(layout, pos, spread)` (core/SpeakerLayout)
  pans from the position plus 8 points on a circle of radius `spread` (rotated 22.5 deg, mirror-symmetric) and averages the
  squared gains (MDAP-like, constant power, continuous in position and spread; spread <= 0 = plain). The audio thread's
  gain cache key includes the spread. On the pad (`FieldComponent`): semi-transparent circle of radius spread, drawn under
  the sources; only the inner circle (`kSourceRadius` 9 px) is a mouse target. Option (Alt) only, not Cmd: Option + vertical
  drag from a source changes the spread relative to its value at the press (up = more, down = less, sideways ignored,
  limited to 0..10 m at every step so there is no dead zone), Option + double-click resets it. The mouse handlers
  delegate to `pressAt/dragTo/release/doubleClickAt` so tests need no real mouse events. Source XML has an optional `spread` attribute.
  `TooltipWindow` in the editor makes all tooltips visible. `readNumber` in PositionIO rejects non-numeric attributes.
- Virtual (imaginary) speakers: `Triangulation(real, virtual)` triangulates real + virtual points (real first);
  `computeGains` splits the gain of a virtual point over its real neighbours with weights ~ 1/distance (unit L2 norm;
  `kDistanceExponent` in Triangulation.cpp; equidistant neighbours, e.g. a ring centre, still get equal shares)
  (`redistribution()`) and renormalises, so `SourceGains` can have up to 64 entries. `findVirtualSpeakers`
  (core/VirtualSpeakers) returns `VirtualSpeaker {position, strength}`: one gap filler per angular gap (strength
  ramps 0..1 over 90..110 deg), a centre speaker for rings (soft ring tolerance 0.25 +-0.05, soft clearance), all
  scaled by an enclosure factor that fades from 1 at largest gap 160 deg to 0 at 180 deg (nothing if the listener,
  the origin, is not enclosed). Strengths < 1 are blended by `SpeakerLayout` (core): virtual speakers sorted by
  strength, nested layers (real + the j strongest), layer weights s_j - s_(j+1), gains = weighted sum, renormalised
  (continuous in the speaker positions, no state; up to 17 triangulations, ~0.5 ms/block worst case for 64
  sources). `LayoutExchange` hands the immutable `SpeakerLayout` to the audio thread (hazard pointer). The audio
  thread skips sources whose position and layout `id()` are unchanged. Processor mode Auto/Manual/Off
  (`virtualMode`, `manualVirtual` in the state, max 64, not host parameters; manual ones have strength 1);
  Manual ones are edited on the pad (double-click add/delete, drag) when speakers are set to Edit. Known limit:
  cocircular grid cells (3x3, 4x4) still have arbitrary diagonals.
- Source groups: `tripan::SourceGroups` (core/Groups: group id per source, 0 = none, 1..32; groups of one are dissolved;
  `group()` merges touched groups whole and keeps the smallest id, `ungroup()` dissolves whole groups, `membersOf(source, activeCount)`;
  text form "0 0 1 1 ..." (64 numbers)) plus `clampTranslation` (rigid move limited to +-10 m per axis, so the group slides along a wall),
  `clampSpreadChange` (common spread change limited to 0..10) and `convexHull` (for drawing). The assignment is saved in the plugin
  state as `sourceGroups` (not a host parameter; `getSourceGroups/setSourceGroups`). On the pad (`FieldComponent`): the selection is
  GUI state only (not saved). Click = select the source and its group; Shift-click toggles (with group); rectangle on empty space
  selects (Shift extends); dragging a selected source moves `movingSet()` (the selection, else its group) with offsets taken from
  the positions at the press (nothing stored, so automation/table edits/imports never desync a group). Cmd-drag moves one source
  only; Option-drag spread of one, Option+Shift of `movingSet` (also Option+Shift double-click reset). Cmd+G / Cmd+Shift+G (pad has
  keyboard focus; `keyPressed`), Group/Ungroup buttons in row 3, Group column in the sources table, optional `group` attribute in source
  XML (an import where any entry has it replaces all groups). Groups and moving need sources on Edit; members above `numSources` keep
  their group but are ignored when moving. Drawing: palette colour per id (`FieldComponent::groupColour`), ring, widened hull band, "G<n>" tag.
- Zoom (`FieldComponent::zoomIn/zoomOut`, buttons in row 3): the view is (centre, half extent); full view = +-10 m
  centred on 0. Zoom In steps by 1.5x towards `fittedView()` (bounding box of the speakers in use, min +-2 m) and
  stops there; Zoom Out steps back to the full view. The view is fixed once set (it doesn't follow dragged
  speakers) and not saved. `toView/fromView` and the grid use it, so hit-testing and dragging work zoomed.
- Footer row (bottom of `content`, 11 px, `juce::HyperlinkButton`s sized to their text): left "Trond Lossius" -> trondlossius.no,
  centre "Help" -> trondlossius.no/software/, right "FTVS, INN" -> inn.no faculty page.
- Scalable GUI: all controls sit in `content`, laid out at the base size 700x860 (`kBaseWidth/Height`, also
  the minimum window size) and scaled uniformly by a transform; the window has a fixed aspect ratio and
  grows up to 3x. Layout code (`layoutContent`) therefore works in base-size pixels. The window width is
  saved in the state (`editorWidth`) and restored. Tests check the mapping through the transform.
- Table windows (`Source/PositionTableWindow.*`): a "Table..." button at the right end of the sources row and of the
  speakers row opens a `PositionTableWindow` (native title bar, always on top so it is in front of the host's floating plug-in window, owned by the editor, close = hide, and the button toggles it; opened beside the plug-in window, right if the screen has room, else left, else centred over it: `chooseBesidePosition`). Sources: table of
  x, y, spread for the sources in use; speakers: x, y for the speakers in use, plus a second table to the right with
  the manual virtual speakers while `canEditVirtualSpeakers()` (mode Manual and speakers on Edit). Top row: count
  slider (the same parameter as in the main window) and, in the speakers window, the Auto/Manual/Off menu. Cells are
  edited by a click, Return commits; non-numbers are rejected, values are limited to +-10 m (spread 0..10) and go
  through the parameters with change gestures. The table edits regardless of the Edit/View menus (like import does).
  Width is fixed to what the content needs (changes when the virtual table appears), height is resizable between 4 rows
  and all rows (limited to the screen; the limits include the native title bar); the `TableListBox` header stays put
  and the rows scroll. A 10 Hz timer follows parameter changes; a cell being edited is not overwritten.
- Export/import (`Source/PositionIO.*`, four buttons in the editor): separate XML files for sources
  (`<TriPanSources version="1" count="n"><Source index="1" x=".." y=".."/>`) and speakers
  (`<TriPanSpeakers virtual="auto|manual|off">/<Speaker>`, plus `<VirtualSpeaker>` children when manual; an import
  without the `virtual` attribute leaves the virtual setting alone, an unknown value fails the import; manual lists are
  capped at 64 and clamped; `setVirtualLayout` applies mode + list without copying the automatic speakers). Export writes the channels in use, coordinates rounded to 3 decimals and left-padded to a fixed width (columns align; import ignores the spaces); import validates the whole file
  first, writes every valid entry (index 1..64, clamped to +-10 m). Speaker imports set `numSpeakers` to the highest
  index in the file if the track has that many outputs (else unchanged); source imports never change `numSources`.
  Import and export are always enabled, in Edit and in View (View only locks dragging and reset). The file dialogs
  (async `juce::FileChooser`) are not exercised by tests, only `exportPositionsTo/importPositionsFrom`.
- `speaker layouts/*.xml`: speaker layouts (quad, 5.1, 7.1, hexagon x2, octagon x2, grids 3x3/4x4/8x8, 8x2 corridor, random; the `-virtual-centres` ones have manual virtual speakers at the cell centres, `virtual="manual"`), generated by
  `scripts/make_speaker_presets.py` (regenerate rather than hand-edit). `ProcessorTests` imports each one
  (the "speaker layouts" folder is passed as an argument by ctest) and checks geometry and constant power.
- `numSources` / `numSpeakers` (int, 1..64, default 64): how many of the track's channels are used.
  The host range is fixed; the effective count is clamped to the track's channel count
  (`getActiveSources/Speakers`). Unused sources are silent, unused speakers are left out of the
  triangulation. The GUI shows them at the top with their maximum set to the channel count.
- Parameter order (as listed by hosts, set by the order in `createLayout`): Number of Sources, Number of Speakers, then
  per source x, y, spread (source 1, 2, ...), then per speaker x, y (speaker 1, 2, ...). `ProcessorTests` checks it.
- Parameters (Source/Parameters.h): `src<N>x/y` and `spk<N>x/y`, N = 1..64, metres, range ±10, +y front.
  Speaker changes are picked up by a 30 Hz message-thread timer (`refreshTriangulation`), which
  publishes an immutable Triangulation through `TriangulationExchange` (hazard-pointer handover).
  processBlock computes per-source gains each block and ramps linearly from the previous block's gains.
- First run only (flag `layoutInitialised` in saved state): active channels get default rings.
- Editor: `Source/FieldComponent.*` draws the plane (±10 m), triangulation, source-to-speaker links
  and drags handles through the parameters (with change gestures). `tests/EditorTests.cpp` tests
  hit-testing/dragging/groups and, given a path argument, writes a PNG snapshot Claude can look at:
  `build/Debug/TriPanEditorTests /path/out.png`.
- `tests/ProcessorTests.cpp` drives the processor offline (energy conservation, locality, state, 64x64).
- Edge cases handled and tested in M2: source outside the convex hull (project to nearest hull edge),
  fewer than 3 speakers, collinear speakers, coincident points.
- Claude cannot listen or drive a DAW: prefer numerical tests in `tests/`, and ask the user for
  host checks (Reaper, Pro Tools, Windows).

## Milestones
M1 skeleton (pass-through, 64-ch layouts) · M2 SpatialCore + tests · M3 processor wiring ·
M4 2D editor · M5 state, pluginval, 64x64 stress · M6 AAX + Windows CI · M7 signing/notarization.

## Licence
AGPLv3 (see LICENSE), because JUCE is used under AGPLv3 and the combined binary must follow it. Every source file carries an SPDX header (`SPDX-FileCopyrightText`, `SPDX-License-Identifier: AGPL-3.0-or-later`); add it to new files. No copy protection.
