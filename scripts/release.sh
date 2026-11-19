#!/bin/sh
# SPDX-FileCopyrightText: 2026 Trond Lossius
# SPDX-License-Identifier: AGPL-3.0-or-later
# One command for a macOS release of all four plugins: checks, builds (universal), tests, signs, notarizes, staples, zips, checksums.
#
#   scripts/release.sh check            only report what is ready and what is missing (changes nothing)
#   scripts/release.sh build            build, test, sign, notarize and zip the current version into dist/ (no git changes)
#   scripts/release.sh publish 0.2.0    the same for version 0.2.0, then set the version, commit, tag v0.2.0 and push
#                                       (creates the GitHub release with the notarized macOS files; the pushed tag makes GitHub add the Windows files)
#
# Defaults that you normally do not need to change:
#   SIGN_IDENTITY   found in your keychain ("Developer ID Application: ...")
#   NOTARY_PROFILE  tripan-notary (made once with: xcrun notarytool store-credentials tripan-notary ...)
# See RELEASING.md.
set -eu
cd "$(dirname "$0")/.."

MODE="${1:-check}"
VERSION="${2:-}"
PLUGINS="TriPan DistPan VecPan ViMiC"
NOTARY_PROFILE="${NOTARY_PROFILE:-tripan-notary}"
BUILD_DIR="${BUILD_DIR:-build-release}"
problems=0

ok()   { printf '  [ok]      %s\n' "$1"; }
fail() { printf '  [MISSING] %s\n' "$1"; problems=$((problems + 1)); }
note() { printf '  [note]    %s\n' "$1"; }

# ---------------------------------------------------------------- checks
echo "Checking:"

if [ -z "${SIGN_IDENTITY:-}" ]; then
    SIGN_IDENTITY="$(security find-identity -v -p codesigning | sed -n 's/.*"\(Developer ID Application: [^"]*\)".*/\1/p' | head -n 1)"
fi
if [ -n "$SIGN_IDENTITY" ]; then ok "signing certificate: $SIGN_IDENTITY"; else fail "no 'Developer ID Application' certificate in the keychain (RELEASING.md, step 1)"; fi

if xcrun notarytool history --keychain-profile "$NOTARY_PROFILE" >/dev/null 2>&1; then ok "notarization login '$NOTARY_PROFILE' works"; else fail "notarization login '$NOTARY_PROFILE' (RELEASING.md, step 2)"; fi

if [ "$(git rev-parse --abbrev-ref HEAD)" = main ]; then ok "on the main branch"; else fail "not on the main branch"; fi
if [ -z "$(git status --porcelain)" ]; then ok "no uncommitted changes"; else fail "uncommitted changes (commit or stash them first)"; fi
if git remote get-url origin >/dev/null 2>&1; then ok "GitHub repository: $(git remote get-url origin)"; elif [ "$MODE" = build ]; then note "no GitHub repository connected yet (needed for publish)"; else fail "no GitHub repository connected (RELEASING.md, step 3)"; fi
if [ -f JUCE/CMakeLists.txt ]; then ok "JUCE submodule present"; else fail "JUCE submodule missing: git submodule update --init"; fi
if command -v cmake >/dev/null 2>&1 && xcodebuild -version >/dev/null 2>&1; then ok "cmake and Xcode"; else fail "cmake or Xcode missing"; fi
current="$(sed -n 's/^project(ToolsForSoundInSpace VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
note "current version: $current"

if [ "$MODE" = check ]; then
    if [ "$problems" -eq 0 ]; then echo "Everything is ready. Next: scripts/release.sh publish <version>"; else echo "$problems thing(s) to fix first."; fi
    exit "$problems"
fi

[ "$problems" -eq 0 ] || { echo "Fix the missing items above first (scripts/release.sh check shows them again)." >&2; exit 1; }

case "$MODE" in
    build) ;;
    publish)
        case "$VERSION" in
            [0-9]*.[0-9]*.[0-9]*) ;;
            *) echo "Usage: scripts/release.sh publish <version>, for example 0.2.0" >&2; exit 1 ;;
        esac
        if git rev-parse "v$VERSION" >/dev/null 2>&1; then echo "The tag v$VERSION already exists." >&2; exit 1; fi
        sed -i '' "s/^project(ToolsForSoundInSpace VERSION [0-9.]*/project(ToolsForSoundInSpace VERSION $VERSION/" CMakeLists.txt
        ;;
    *) echo "Usage: scripts/release.sh check | build | publish <version>" >&2; exit 1 ;;
esac

# ---------------------------------------------------------------- build and test
echo "Building (universal binaries, Release) and running the tests..."
cmake -B "$BUILD_DIR" -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DTLOS_INSTALL_VST3=OFF -DTLOS_INSTALL_AU=OFF >/dev/null
cmake --build "$BUILD_DIR" --config Release
ctest --test-dir "$BUILD_DIR" -C Release --output-on-failure

# ---------------------------------------------------------------- sign, notarize, zip
rm -f dist/*-macOS-*.zip dist/SHA256SUMS-macOS.txt
for plugin in $PLUGINS; do
    echo "=== $plugin"
    SIGN_IDENTITY="$SIGN_IDENTITY" NOTARY_PROFILE="$NOTARY_PROFILE" PLUGIN="$plugin" NO_BUILD=1 BUILD_DIR="$BUILD_DIR" scripts/sign-and-notarize.sh
done

(cd dist && shasum -a 256 *-macOS-*.zip > SHA256SUMS-macOS.txt)
echo "macOS files in dist/:"
ls -1 dist/*-macOS-*.zip

# ---------------------------------------------------------------- publish
if [ "$MODE" = publish ]; then
    git add CMakeLists.txt
    git commit -m "Version $VERSION"
    git tag -a "v$VERSION" -m "Version $VERSION"
    git push origin main "v$VERSION"
    if command -v gh >/dev/null 2>&1; then
        gh release create "v$VERSION" dist/*-macOS-*.zip dist/SHA256SUMS-macOS.txt --title "Version $VERSION" --generate-notes
        echo "Release v$VERSION created with the notarized macOS files. GitHub is now building the Windows files and adds them to it (Actions tab)."
    else
        echo "Pushed v$VERSION. Install the GitHub command line tool (brew install gh, then gh auth login) to have this step upload the macOS files,"
        echo "or attach the files in dist/ to the release by hand (RELEASING.md)."
    fi
fi
