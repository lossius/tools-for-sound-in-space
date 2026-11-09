#!/bin/sh
# SPDX-FileCopyrightText: 2026 Trond Lossius
# SPDX-License-Identifier: AGPL-3.0-or-later
# Builds the Release VST3 and validates it with pluginval at strictness 10.
# Requires pluginval (brew install --cask pluginval).
set -e
cd "$(dirname "$0")/.."

PLUGINVAL="${PLUGINVAL:-/Applications/pluginval.app/Contents/MacOS/pluginval}"

cmake -B build -G Xcode >/dev/null
cmake --build build --config Release --target TriPan_VST3
"$PLUGINVAL" --strictness-level 10 --timeout-ms 120000 build/TriPan_artefacts/Release/VST3/TriPan.vst3
