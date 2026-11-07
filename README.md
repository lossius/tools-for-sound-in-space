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
GPLv3. Built with JUCE (AGPLv3).
