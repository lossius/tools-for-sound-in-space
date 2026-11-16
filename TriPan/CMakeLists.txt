# SPDX-FileCopyrightText: 2026 Trond Lossius
# SPDX-License-Identifier: AGPL-3.0-or-later

# TriPan: the plugin, its pure C++ core and its tests.
add_subdirectory(core)
add_subdirectory(tests)

juce_add_plugin(TriPan
    PRODUCT_NAME "TriPan"
    COMPANY_NAME "Trond Lossius"
    BUNDLE_ID no.trondlossius.tripan
    PLUGIN_MANUFACTURER_CODE Tlos
    PLUGIN_CODE TrPn
    FORMATS ${TLOS_PLUGIN_FORMATS}
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
    Source/TriPanField.cpp
    Source/TriPanField.h)
# In the IDE, sources and headers sit in separate subfolders, like in TriPanCore.
source_group("Sources/src" FILES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/TriPanField.cpp)
source_group("Sources/include" FILES
    Source/PluginProcessor.h
    Source/PluginEditor.h
    Source/TriPanField.h)

tlos_finish_plugin(TriPan)
target_link_libraries(TriPan PUBLIC TriPanCore)

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
add_test(NAME TriPanProcessorTests COMMAND TriPanProcessorTests "${CMAKE_SOURCE_DIR}/speaker layouts")

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
