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
- Parameters (Source/Parameters.h): `src<N>x/y` and `spk<N>x/y`, N = 1..64, metres, range ±10, +y front.
  Speaker changes are picked up by a 30 Hz message-thread timer (`refreshTriangulation`), which
  publishes an immutable Triangulation through `TriangulationExchange` (hazard-pointer handover).
  processBlock computes per-source gains each block and ramps linearly from the previous block's gains.
- First run only (flag `layoutInitialised` in saved state): active channels get default rings.
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
