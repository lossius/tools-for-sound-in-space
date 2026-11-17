#!/bin/sh
# SPDX-FileCopyrightText: 2026 Trond Lossius
# SPDX-License-Identifier: AGPL-3.0-or-later
# Builds the Release VST3 and AudioUnit of both plugins and installs them in the user's plug-in folders
# (~/Library/Audio/Plug-Ins/VST3 and .../Components), so that a host loads the current code.
# The install steps are on in this build folder only; scripts/sign-and-notarize.sh and scripts/validate.sh
# use build-release, where they are off, so a host keeps loading what is installed until this script runs.
set -e
cd "$(dirname "$0")/.."

BUILD_DIR="${BUILD_DIR:-build-install}"

cmake -B "$BUILD_DIR" -G Xcode -DTLOS_INSTALL_VST3=ON -DTLOS_INSTALL_AU=ON >/dev/null
cmake --build "$BUILD_DIR" --config Release --target TriPan_VST3 TriPan_AU DistPan_VST3 DistPan_AU
killall -9 AudioComponentRegistrar 2>/dev/null || true   # the system re-reads the installed AudioUnits
echo "Installed TriPan and DistPan (VST3 and AudioUnit) in ~/Library/Audio/Plug-Ins"
