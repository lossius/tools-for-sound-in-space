#!/bin/sh
# Builds (universal), signs with a Developer ID Application certificate and the hardened runtime,
# notarizes with Apple, staples the tickets and writes distributable zips to dist/.
#
# Required environment:
#   SIGN_IDENTITY   e.g. "Developer ID Application: Your Name (TEAMID)"
#   either NOTARY_PROFILE        a keychain profile created once with
#                                  xcrun notarytool store-credentials <profile>
#   or     APPLE_ID, TEAM_ID, APP_PASSWORD   (app-specific password; used in CI)
# Optional:
#   BUILD_DIR   build folder (default: build-release)
#   NO_BUILD=1  sign what is already in BUILD_DIR instead of building first
#   DRY_RUN=1   sign and zip only; skip notarization and stapling (for testing the signing step)
set -eu
cd "$(dirname "$0")/.."

: "${SIGN_IDENTITY:?Set SIGN_IDENTITY to your 'Developer ID Application: ...' certificate name}"
BUILD_DIR="${BUILD_DIR:-build-release}"
ART="$BUILD_DIR/TriPan_artefacts/Release"
ENTITLEMENTS="$(pwd)/resources/Standalone.entitlements"

if [ -z "${DRY_RUN:-}" ] && [ -z "${NOTARY_PROFILE:-}" ] && [ -z "${APPLE_ID:-}" ]; then
    echo "Set NOTARY_PROFILE (or APPLE_ID, TEAM_ID, APP_PASSWORD), or use DRY_RUN=1." >&2
    exit 1
fi

if [ -z "${NO_BUILD:-}" ]; then
    cmake -B "$BUILD_DIR" -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DTRIPAN_INSTALL_VST3=OFF >/dev/null
    cmake --build "$BUILD_DIR" --config Release --target TriPan_VST3 TriPan_AU TriPan_Standalone
fi

notarize() # $1 = zip to submit
{
    if [ -n "${NOTARY_PROFILE:-}" ]; then
        xcrun notarytool submit "$1" --keychain-profile "$NOTARY_PROFILE" --wait
    else
        xcrun notarytool submit "$1" --apple-id "$APPLE_ID" --team-id "$TEAM_ID" --password "$APP_PASSWORD" --wait
    fi
}

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
rm -rf dist
mkdir -p dist

for bundle in "$ART/VST3/TriPan.vst3" "$ART/AU/TriPan.component" "$ART/Standalone/TriPan.app"; do
    case "$bundle" in
        *.vst3)      label=VST3 ;;
        *.component) label=AU ;;
        *.app)       label=Standalone ;;
    esac

    echo "== $label: signing"
    if [ "$label" = Standalone ]; then
        codesign --force --timestamp --options runtime --entitlements "$ENTITLEMENTS" --sign "$SIGN_IDENTITY" "$bundle"
    else
        codesign --force --timestamp --options runtime --sign "$SIGN_IDENTITY" "$bundle"
    fi
    codesign --verify --strict --verbose=2 "$bundle"

    if [ -z "${DRY_RUN:-}" ]; then
        echo "== $label: notarizing (this can take a few minutes)"
        ditto -c -k --keepParent "$bundle" "$WORK/$label.zip"
        notarize "$WORK/$label.zip"
        xcrun stapler staple "$bundle"
        xcrun stapler validate "$bundle"
    fi

    ditto -c -k --keepParent "$bundle" "dist/TriPan-macOS-$label.zip"
done

if [ -z "${DRY_RUN:-}" ]; then
    spctl --assess --type execute --verbose "$ART/Standalone/TriPan.app"
else
    echo "DRY_RUN: notarization and stapling skipped; these zips will be blocked by Gatekeeper."
fi
echo "Done. Zips are in dist/"
