# TriPan

A free, open-source (AGPLv3) spatial audio plugin that distributes n sound sources to m speakers in a
2D plane. Each source is panned to the three speakers of the Delaunay triangle that contains it,
with constant overall intensity.

Status: early development (M1 skeleton).

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

On macOS the VST3 is also copied to `~/Library/Audio/Plug-Ins/VST3` after each build. Use
`-DTRIPAN_INSTALL_VST3=OFF` to disable this, or `-DTRIPAN_VST3_INSTALL_DIR=<folder>` to change the location.

## Source spread
Each source has a **spread**: the radius, in metres, of a circle around it over which its sound is
spread (0 to 10 m, default 0 = a point source). It is a host parameter per source ("Source N Spread"), so it can
be automated and is saved with the session.

On the pad the spread is drawn as a semi-transparent circle around the source, in the source's colour,
and the source itself keeps its solid inner circle.

* **Set the spread:** hold **Option** (Alt on Windows) and drag the source **vertically**: moving the mouse
  up increases the spread, moving it down decreases it, and sideways movement has no effect. The change is
  relative to the value when you press, so you can start a drag anywhere on the inner circle. The spread is
  limited to 0..10 m at every step, so dragging down to 0 and further, then up again, increases the spread at
  once, which makes it easy to bring the spread back to 0 (just drag down). A readout shows the value while you
  drag, and the source does not move. Only Option is used (Cmd is not), so Cmd keeps its usual meaning in hosts.
* **Reset it:** Option + double-click the source.
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
* **Manual**: you place them. Set the speakers menu to Edit, then double-click empty space on the pad
  to add one, drag to move, double-click one to delete it (at most 64). Switching from Auto to
  Manual starts from the current automatic set. Positions are saved with the session.
* **Off**: real speakers only, as in the plain triangulation.

Virtual speakers are drawn as hollow amber squares ("v1", "v2", ...). The price is that sources
inside a ring are spread more widely than the strict three speakers of plain triangulation; switch to
Off if you prefer that. They are not host parameters. They are saved with the session, and a speaker
XML file records the setting and, for Manual, the virtual speakers (see below).

## Zooming
**Zoom In** and **Zoom Out** (third row) change the visible area of the pad. Each press of Zoom In
zooms a step towards the area that contains all speakers in use and stops when they just fill the pad;
Zoom Out zooms back in steps until the entire +-10 m area is shown again, as at start. The view stays
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
(`<TriPanSources>` / `<Source>` for sources.) Coordinates are metres, +y is front, range +-10.

A speaker file also records the virtual speaker setting in the optional `virtual` attribute (`auto`, `manual` or `off`). With `manual`
the file lists your own virtual speakers as `<VirtualSpeaker>` entries (at most 64); with `auto` or `off`
there is nothing more to store, because automatic virtual speakers are recomputed from the real ones. Importing
a speaker file restores the setting (and the manual list), replacing the current one. A file without the
attribute, such as one written by an earlier version, leaves the virtual speaker setting alone.
The presets in `speaker layouts/` say `virtual="auto"`, so loading one switches back to Auto, except the `-virtual-centres` ones, which bring their own manual virtual speakers.

Export writes the channels currently in use, with coordinates rounded to 3 decimal places and padded with spaces so that positive and negative values line up (import ignores the padding). Import checks the whole file first, applies every valid
entry by index. Importing speakers also sets "Number of Speakers" to the number of speakers in the
file, provided the track has that many outputs (otherwise the setting is left alone and the status
line says so). Importing sources never changes "Number of Sources". Import and export work whether a
kind is set to Edit or View.

## Speaker presets
The `speaker layouts/` folder has ready-made speaker layouts in the same XML format. Load one with
"Import speakers...". Number of Speakers is adjusted to
the preset automatically when the track has enough outputs. Angles are azimuths clockwise from the front, speakers are on a 5 m circle, and the LFE
channel is left out.

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
| `Corridor-8x2.xml` | 16 | 2 rows of 8 ("corridor"): x spans +-5 m, rows at y = +-1.5 m, so every cell is a 1.43 m x 3 m rectangle |
| `Corridor-8x2-virtual-centres.xml` | 16 + 7 virtual | as `Corridor-8x2.xml`, with manual virtual speakers at the centre of each of the 7 rectangles |
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
cmake -B build -G Xcode -DTRIPAN_ENABLE_AAX=ON -DTRIPAN_AAX_SDK_PATH=/path/to/AAX_SDK
cmake --build build --config Release --target TriPan_AAX
```
Pro Tools only loads AAX plugins that are signed through Avid's PACE process. Development builds
need a Pro Tools Developer build; release builds need signing arranged with Avid. This path has not
been built or tested yet, and the channel/stem formats Pro Tools accepts for 64-channel layouts
must be verified.

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
This builds a universal binary, signs with the hardened runtime, notarizes, staples and writes
zips to `dist/`. Add `DRY_RUN=1` to test the signing step only (no upload, zips are not releasable).

In CI, the macOS job signs and notarizes automatically when these repository secrets exist:
`MACOS_CERT_P12` (base64 of the exported .p12), `MACOS_CERT_PASSWORD`, `MACOS_SIGN_IDENTITY`,
`APPLE_ID`, `APPLE_TEAM_ID`, `APPLE_APP_PASSWORD`. Without them it uploads unsigned zips.

The Standalone app is signed with `resources/Standalone.entitlements` (audio input under the
hardened runtime). Windows builds are unsigned; Windows may show a SmartScreen warning.

## Continuous integration
`.github/workflows/build.yml` builds, tests and validates (pluginval, strictness 10) on macOS
(universal arm64 + x86_64) and Windows, and uploads zips. Pushing a tag such as `v0.1.0`
also publishes a GitHub release with the zips. AAX is not built in CI.

## Licence
AGPLv3 (see [LICENSE](LICENSE)); built with JUCE, which is used under its AGPLv3 option. Source files carry SPDX headers. If you distribute TriPan, you must make the corresponding source available under the same licence.
