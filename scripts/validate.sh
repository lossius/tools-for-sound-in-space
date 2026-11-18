#!/bin/sh
# SPDX-FileCopyrightText: 2026 Trond Lossius
# SPDX-License-Identifier: AGPL-3.0-or-later
# Builds the Release VST3 of a plugin (PLUGIN, default TriPan) and validates it with pluginval at strictness 10.
# Requires pluginval (brew install --cask pluginval).
set -e
cd "$(dirname "$0")/.."

PLUGINVAL="${PLUGINVAL:-/Applications/pluginval.app/Contents/MacOS/pluginval}"
PLUGIN="${PLUGIN:-TriPan}"   # TriPan, DistPan, VecPan or ViMiC
BUILD_DIR="${BUILD_DIR:-build}"   # an Xcode build folder; use another one if build/ was made with a different generator

cmake -B "$BUILD_DIR" -G Xcode >/dev/null
cmake --build "$BUILD_DIR" --config Release --target "${PLUGIN}_VST3"
"$PLUGINVAL" --strictness-level 10 --timeout-ms 120000 "$BUILD_DIR/$PLUGIN/${PLUGIN}_artefacts/Release/VST3/$PLUGIN.vst3"
