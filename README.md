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

## Licence
GPLv3. Built with JUCE (AGPLv3).
