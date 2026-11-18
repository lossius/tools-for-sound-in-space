# SPDX-FileCopyrightText: 2026 Trond Lossius
# SPDX-License-Identifier: AGPL-3.0-or-later

cmake_minimum_required(VERSION 3.22)

project(ToolsForSoundInSpace VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

option(TLOS_ENABLE_AAX "Build the AAX format (requires the Avid AAX SDK)" OFF)
set(TLOS_AAX_SDK_PATH "" CACHE PATH "Path to the Avid AAX SDK (not redistributable, keep outside the repo)")

# Copy the VST3 into the user's plug-in folder after every build (macOS only by default;
# the Windows system VST3 folder needs administrator rights). CI turns this off.
option(TLOS_INSTALL_VST3 "Copy the built VST3 to TLOS_VST3_INSTALL_DIR after building" ${APPLE})
set(TLOS_VST3_INSTALL_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/VST3" CACHE PATH "Where TLOS_INSTALL_VST3 copies the VST3")
# The same for the AudioUnit (macOS only).
option(TLOS_INSTALL_AU "Copy the built AudioUnit to TLOS_AU_INSTALL_DIR after building" ${APPLE})
set(TLOS_AU_INSTALL_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/Components" CACHE PATH "Where TLOS_INSTALL_AU copies the AudioUnit")

# Pro Tools only scans the system-wide folder, which is not writable by default (see README: AAX).
if(APPLE)
    set(TLOS_AAX_DEFAULT_DIR "/Library/Application Support/Avid/Audio/Plug-Ins")
else()
    set(TLOS_AAX_DEFAULT_DIR "$ENV{CommonProgramFiles}/Avid/Audio/Plug-Ins")
endif()
option(TLOS_INSTALL_AAX "Copy the built AAX plug-in to TLOS_AAX_INSTALL_DIR after building (only with TLOS_ENABLE_AAX)" ${TLOS_ENABLE_AAX})
set(TLOS_AAX_INSTALL_DIR "${TLOS_AAX_DEFAULT_DIR}" CACHE PATH "Where TLOS_INSTALL_AAX copies the AAX plug-in")

enable_testing()

# IDE organisation: everything JUCE creates goes in a "JUCE" folder, our own code is grouped under
# "Sources" (plugin), "Core" and "Tests".
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(CMAKE_FOLDER "JUCE")
add_subdirectory(JUCE)
set(CMAKE_FOLDER "")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(TlosPlugin)

add_subdirectory(shared)
add_subdirectory(TriPan)
add_subdirectory(DistPan)
add_subdirectory(VecPan)
add_subdirectory(ViMiC)
