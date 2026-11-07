# TriPan

Free, GPL-licensed audio plugin (VST3, AU, Standalone; AAX optional) for macOS and Windows.
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
On macOS every build of `TriPan_VST3` also copies the bundle to `~/Library/Audio/Plug-Ins/VST3`
(`-DTRIPAN_INSTALL_VST3=OFF` disables it; `-DTRIPAN_VST3_INSTALL_DIR=...` changes the folder; CI and
the signing script turn it off).

Validation: `scripts/validate.sh` builds the Release VST3 and runs pluginval at strictness 10
(passes as of M5). pluginval can only scan an AU that is installed in ~/Library/Audio/Plug-Ins/Components.
For threading checks, build `TriPanProcessorTests` in a separate dir with
`-DCMAKE_CXX_FLAGS=-fsanitize=thread` (clean as of M5); it includes a test that automates parameters
on one thread while another processes audio.

AAX (optional): `-DTRIPAN_ENABLE_AAX=ON -DTRIPAN_AAX_SDK_PATH=/path/to/AAX_SDK`. The Avid AAX SDK is
not redistributable: keep it outside this repo. Check its current licence before publishing AAX builds.

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
- Edit mode (`Source/EditMode.h`, menu in the editor, saved in the state as property `editMode`, not a host
  parameter): 1 move sources (default), 2 move speakers, 3 move both, 4 view only. Locked handles can't be
  grabbed or moved, and "Reset layout" only resets unlocked kinds (disabled in mode 4). Host automation
  of the position parameters is unaffected by the mode.
- Scalable GUI: all controls sit in `content`, laid out at the base size 700x810 (`kBaseWidth/Height`, also
  the minimum window size) and scaled uniformly by a transform; the window has a fixed aspect ratio and
  grows up to 3x. Layout code (`layoutContent`) therefore works in base-size pixels. The window width is
  saved in the state (`editorWidth`) and restored. Tests check the mapping through the transform.
- Export/import (`Source/PositionIO.*`, four buttons in the editor): separate XML files for sources
  (`<TriPanSources version="1" count="n"><Source index="1" x=".." y=".."/>`) and speakers
  (`<TriPanSpeakers>/<Speaker>`). Export writes the channels in use, coordinates rounded to 3 decimals and left-padded to a fixed width (columns align; import ignores the spaces); import validates the whole file
  first, writes every valid entry (index 1..64, clamped to +-10 m) and never changes the counts.
  Import buttons follow the edit-mode locks; export is always enabled. The file dialogs
  (async `juce::FileChooser`) are not exercised by tests, only `exportPositionsTo/importPositionsFrom`.
- `numSources` / `numSpeakers` (int, 1..64, default 64): how many of the track's channels are used.
  The host range is fixed; the effective count is clamped to the track's channel count
  (`getActiveSources/Speakers`). Unused sources are silent, unused speakers are left out of the
  triangulation. The GUI shows them at the top with their maximum set to the channel count.
- Parameters (Source/Parameters.h): `src<N>x/y` and `spk<N>x/y`, N = 1..64, metres, range ±10, +y front.
  Speaker changes are picked up by a 30 Hz message-thread timer (`refreshTriangulation`), which
  publishes an immutable Triangulation through `TriangulationExchange` (hazard-pointer handover).
  processBlock computes per-source gains each block and ramps linearly from the previous block's gains.
- First run only (flag `layoutInitialised` in saved state): active channels get default rings.
- Editor: `Source/FieldComponent.*` draws the plane (±10 m), triangulation, source-to-speaker links
  and drags handles through the parameters (with change gestures). `tests/EditorTests.cpp` tests
  hit-testing/dragging and, given a path argument, writes a PNG snapshot Claude can look at:
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
GPLv3 (see LICENSE); JUCE is used under AGPLv3. No copy protection.
