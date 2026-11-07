# TriPan

A free, open-source (GPL) spatial audio plugin that distributes n sound sources to m speakers in a
2D plane. Each source is panned to the three speakers of the Delaunay triangle that contains it,
with constant overall intensity.

Status: early development (M1 skeleton).

## Build (macOS)
```
git clone --recursive <repo-url>
cd TriPan
cmake -B build -G Xcode
cmake --build build --config Release
```

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
cmake -B build -G Xcode -DTRIPAN_ENABLE_AAX=ON -DTRIPAN_AAX_SDK_PATH=/path/to/AAX_SDK
cmake --build build --config Release --target TriPan_AAX
```
Pro Tools only loads AAX plugins that are signed through Avid's PACE process. Development builds
need a Pro Tools Developer build; release builds need signing arranged with Avid. This path has not
been built or tested yet, and the channel/stem formats Pro Tools accepts for 64-channel layouts
must be verified.

## Continuous integration
`.github/workflows/build.yml` builds, tests and validates (pluginval, strictness 10) on macOS
(universal arm64 + x86_64) and Windows, and uploads zips. Pushing a tag such as `v0.1.0`
also publishes a GitHub release with the zips. AAX is not built in CI.

## Licence
GPLv3. Built with JUCE (AGPLv3).
