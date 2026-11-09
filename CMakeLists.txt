# SPDX-FileCopyrightText: 2026 Trond Lossius
# SPDX-License-Identifier: AGPL-3.0-or-later

cmake_minimum_required(VERSION 3.22)

project(TriPan VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

option(TRIPAN_ENABLE_AAX "Build the AAX format (requires the Avid AAX SDK)" OFF)
set(TRIPAN_AAX_SDK_PATH "" CACHE PATH "Path to the Avid AAX SDK (not redistributable, keep outside the repo)")

# Copy the VST3 into the user's plug-in folder after every build (macOS only by default;
# the Windows system VST3 folder needs administrator rights). CI turns this off.
option(TRIPAN_INSTALL_VST3 "Copy the built VST3 to TRIPAN_VST3_INSTALL_DIR after building" ${APPLE})
set(TRIPAN_VST3_INSTALL_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/VST3" CACHE PATH "Where TRIPAN_INSTALL_VST3 copies the VST3")
# The same for the AudioUnit (macOS only).
option(TRIPAN_INSTALL_AU "Copy the built AudioUnit to TRIPAN_AU_INSTALL_DIR after building" ${APPLE})
set(TRIPAN_AU_INSTALL_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/Components" CACHE PATH "Where TRIPAN_INSTALL_AU copies the AudioUnit")

# Pro Tools only scans the system-wide folder, which is not writable by default (see README: AAX).
if(APPLE)
    set(TRIPAN_AAX_DEFAULT_DIR "/Library/Application Support/Avid/Audio/Plug-Ins")
else()
    set(TRIPAN_AAX_DEFAULT_DIR "$ENV{CommonProgramFiles}/Avid/Audio/Plug-Ins")
endif()
option(TRIPAN_INSTALL_AAX "Copy the built AAX plug-in to TRIPAN_AAX_INSTALL_DIR after building (only with TRIPAN_ENABLE_AAX)" ${TRIPAN_ENABLE_AAX})
set(TRIPAN_AAX_INSTALL_DIR "${TRIPAN_AAX_DEFAULT_DIR}" CACHE PATH "Where TRIPAN_INSTALL_AAX copies the AAX plug-in")

enable_testing()

# IDE organisation: everything JUCE creates goes in a "JUCE" folder, our own code is grouped under
# "Sources" (plugin), "Core" and "Tests".
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(CMAKE_FOLDER "JUCE")
add_subdirectory(JUCE)
set(CMAKE_FOLDER "")

add_subdirectory(core)
add_subdirectory(tests)

set(TRIPAN_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND TRIPAN_FORMATS AU)
endif()

if(TRIPAN_ENABLE_AAX)
    if(NOT EXISTS "${TRIPAN_AAX_SDK_PATH}")
        message(FATAL_ERROR "TRIPAN_ENABLE_AAX is ON but TRIPAN_AAX_SDK_PATH does not exist: '${TRIPAN_AAX_SDK_PATH}'")
    endif()
    juce_set_aax_sdk_path("${TRIPAN_AAX_SDK_PATH}")
    list(APPEND TRIPAN_FORMATS AAX)
endif()

juce_add_plugin(TriPan
    PRODUCT_NAME "TriPan"
    COMPANY_NAME "Trond Lossius"
    BUNDLE_ID no.trondlossius.tripan
    PLUGIN_MANUFACTURER_CODE Tlos
    PLUGIN_CODE TrPn
    FORMATS ${TRIPAN_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "The TriPan standalone app needs audio input to process the source channels."
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Spatial
    AU_MAIN_TYPE kAudioUnitType_Effect
    AAX_CATEGORY AAX_ePlugInCategory_Effect)

# Headers are listed too, so that they show up in the IDE's project navigator.
target_sources(TriPan PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/FieldComponent.cpp
    Source/FieldComponent.h
    Source/PositionIO.cpp
    Source/PositionIO.h
    Source/PositionTableWindow.cpp
    Source/PositionTableWindow.h
    Source/Parameters.h)
# In the IDE, sources and headers sit in separate subfolders, like in TriPanCore.
source_group("Sources/src" FILES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/FieldComponent.cpp
    Source/PositionIO.cpp
    Source/PositionTableWindow.cpp)
source_group("Sources/include" FILES
    Source/PluginProcessor.h
    Source/PluginEditor.h
    Source/FieldComponent.h
    Source/PositionIO.h
    Source/PositionTableWindow.h
    Source/Parameters.h)

target_compile_definitions(TriPan PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(TriPan
    PRIVATE
        TriPanCore
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# JUCE's module sources are compiled into our targets (they are not separate Xcode targets). Put
# them all in a "JUCE" group, with one subgroup per module and, as in TriPanCore, "src" (the
# compiled .mm/.cpp/.c units) and "include" (the headers, only listed so that the IDE shows them;
# they are not compiled). Later groups win over earlier ones; groups with FILES win over regexes.
source_group("JUCE" REGULAR_EXPRESSION "/JUCE/")
set(TRIPAN_JUCE_MODULES
    juce_audio_basics juce_audio_devices juce_audio_formats juce_audio_plugin_client
    juce_audio_processors juce_audio_processors_headless juce_audio_utils juce_core
    juce_data_structures juce_events juce_graphics juce_gui_basics juce_gui_extra)
foreach(module_name IN LISTS TRIPAN_JUCE_MODULES)
    set(module_dir "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/modules/${module_name}")
    source_group("JUCE/${module_name}/src" REGULAR_EXPRESSION "/JUCE/modules/${module_name}/")
    file(GLOB_RECURSE module_headers CONFIGURE_DEPENDS "${module_dir}/*.h" "${module_dir}/*.hpp")
    target_sources(TriPan PRIVATE ${module_headers})
    source_group(TREE "${module_dir}" PREFIX "JUCE/${module_name}/include" FILES ${module_headers})
endforeach()
source_group("Tests" REGULAR_EXPRESSION "/tests/")

if(TRIPAN_INSTALL_VST3)
    set(TRIPAN_VST3_BUNDLE "$<GENEX_EVAL:$<TARGET_PROPERTY:TriPan_VST3,JUCE_PLUGIN_ARTEFACT_FILE>>")
    set(TRIPAN_VST3_DEST "${TRIPAN_VST3_INSTALL_DIR}/$<TARGET_PROPERTY:TriPan,JUCE_PRODUCT_NAME>.vst3")
    add_custom_command(TARGET TriPan_VST3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${TRIPAN_VST3_INSTALL_DIR}"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${TRIPAN_VST3_DEST}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${TRIPAN_VST3_BUNDLE}" "${TRIPAN_VST3_DEST}"
        COMMENT "Copying TriPan.vst3 to ${TRIPAN_VST3_INSTALL_DIR}"
        VERBATIM)
endif()

if(APPLE AND TRIPAN_INSTALL_AU)
    set(TRIPAN_AU_BUNDLE "$<GENEX_EVAL:$<TARGET_PROPERTY:TriPan_AU,JUCE_PLUGIN_ARTEFACT_FILE>>")
    set(TRIPAN_AU_DEST "${TRIPAN_AU_INSTALL_DIR}/$<TARGET_PROPERTY:TriPan,JUCE_PRODUCT_NAME>.component")
    add_custom_command(TARGET TriPan_AU POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${TRIPAN_AU_INSTALL_DIR}"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${TRIPAN_AU_DEST}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${TRIPAN_AU_BUNDLE}" "${TRIPAN_AU_DEST}"
        COMMENT "Copying TriPan.component to ${TRIPAN_AU_INSTALL_DIR}"
        VERBATIM)
endif()

if(TRIPAN_ENABLE_AAX AND TRIPAN_INSTALL_AAX)
    set(TRIPAN_AAX_BUNDLE "$<GENEX_EVAL:$<TARGET_PROPERTY:TriPan_AAX,JUCE_PLUGIN_ARTEFACT_FILE>>")
    set(TRIPAN_AAX_DEST "${TRIPAN_AAX_INSTALL_DIR}/$<TARGET_PROPERTY:TriPan,JUCE_PRODUCT_NAME>.aaxplugin")
    add_custom_command(TARGET TriPan_AAX POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${TRIPAN_AAX_INSTALL_DIR}"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${TRIPAN_AAX_DEST}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${TRIPAN_AAX_BUNDLE}" "${TRIPAN_AAX_DEST}"
        COMMENT "Copying TriPan.aaxplugin to ${TRIPAN_AAX_INSTALL_DIR}"
        VERBATIM)
endif()

# Offline processor tests (no DAW needed)
add_executable(TriPanProcessorTests tests/ProcessorTests.cpp)
target_include_directories(TriPanProcessorTests PRIVATE Source)
target_link_libraries(TriPanProcessorTests
    PRIVATE
        TriPan
        TriPanCore
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
add_test(NAME TriPanProcessorTests COMMAND TriPanProcessorTests "${CMAKE_CURRENT_SOURCE_DIR}/speaker layouts")

# Editor interaction test; with a path argument it also writes a PNG snapshot of the editor.
add_executable(TriPanEditorTests tests/EditorTests.cpp)
target_include_directories(TriPanEditorTests PRIVATE Source)
target_link_libraries(TriPanEditorTests
    PRIVATE
        TriPan
        TriPanCore
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
add_test(NAME TriPanEditorTests COMMAND TriPanEditorTests)
