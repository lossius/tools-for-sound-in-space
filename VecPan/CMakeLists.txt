# SPDX-FileCopyrightText: 2026 Trond Lossius
# SPDX-License-Identifier: AGPL-3.0-or-later

# VecPan: vector base amplitude panning (VBAP) in 2D. The plugin, its pure C++ core and its tests.
add_subdirectory(core)
add_subdirectory(tests)

juce_add_plugin(VecPan
    PRODUCT_NAME "VecPan"
    COMPANY_NAME "Trond Lossius"
    BUNDLE_ID no.trondlossius.vecpan
    PLUGIN_MANUFACTURER_CODE Tlos
    PLUGIN_CODE VcPn
    FORMATS ${TLOS_PLUGIN_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "The VecPan standalone app needs audio input to process the source channels."
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Spatial
    AU_MAIN_TYPE kAudioUnitType_Effect
    AAX_CATEGORY AAX_ePlugInCategory_Effect)

# Headers are listed too, so that they show up in the IDE's project navigator.
target_sources(VecPan PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/VecPanField.cpp
    Source/VecPanField.h)
# In the IDE, sources and headers sit in separate subfolders.
source_group("Sources/src" FILES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/VecPanField.cpp)
source_group("Sources/include" FILES
    Source/PluginProcessor.h
    Source/PluginEditor.h
    Source/VecPanField.h)

tlos_finish_plugin(VecPan)
target_link_libraries(VecPan PUBLIC VecPanCore)

# Offline processor tests (no DAW needed)
add_executable(VecPanProcessorTests tests/ProcessorTests.cpp)
target_include_directories(VecPanProcessorTests PRIVATE Source)
# The OSC loopback test runs the message loop for a moment.
target_compile_definitions(VecPanProcessorTests PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(VecPanProcessorTests
    PRIVATE
        VecPan
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
add_test(NAME VecPanProcessorTests COMMAND VecPanProcessorTests "${CMAKE_SOURCE_DIR}/speaker layouts")

# Editor interaction test; with a path argument it also writes a PNG snapshot of the editor.
add_executable(VecPanEditorTests tests/EditorTests.cpp)
target_include_directories(VecPanEditorTests PRIVATE Source)
target_link_libraries(VecPanEditorTests
    PRIVATE
        VecPan
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
add_test(NAME VecPanEditorTests COMMAND VecPanEditorTests)
