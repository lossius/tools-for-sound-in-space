# SPDX-FileCopyrightText: 2026 Trond Lossius
# SPDX-License-Identifier: AGPL-3.0-or-later

# DistPan: distance-based amplitude panning (DBAP) in 2D. The plugin, its pure C++ core and its tests.
add_subdirectory(core)
add_subdirectory(tests)

juce_add_plugin(DistPan
    PRODUCT_NAME "DistPan"
    COMPANY_NAME "Trond Lossius"
    BUNDLE_ID no.trondlossius.distpan
    PLUGIN_MANUFACTURER_CODE Tlos
    PLUGIN_CODE DsPn
    FORMATS ${TLOS_PLUGIN_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "The DistPan standalone app needs audio input to process the source channels."
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Spatial
    AU_MAIN_TYPE kAudioUnitType_Effect
    AAX_CATEGORY AAX_ePlugInCategory_Effect)

# Headers are listed too, so that they show up in the IDE's project navigator.
target_sources(DistPan PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/DistPanField.cpp
    Source/DistPanField.h
    Source/DistPanParameters.h)
# In the IDE, sources and headers sit in separate subfolders.
source_group("Sources/src" FILES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/DistPanField.cpp)
source_group("Sources/include" FILES
    Source/PluginProcessor.h
    Source/PluginEditor.h
    Source/DistPanField.h
    Source/DistPanParameters.h)

tlos_finish_plugin(DistPan)
target_link_libraries(DistPan PUBLIC DistPanCore)

# Offline processor tests (no DAW needed)
add_executable(DistPanProcessorTests tests/ProcessorTests.cpp)
target_include_directories(DistPanProcessorTests PRIVATE Source)
target_link_libraries(DistPanProcessorTests
    PRIVATE
        DistPan
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
add_test(NAME DistPanProcessorTests COMMAND DistPanProcessorTests "${CMAKE_SOURCE_DIR}/speaker layouts")

# Editor interaction test; with a path argument it also writes a PNG snapshot of the editor.
add_executable(DistPanEditorTests tests/EditorTests.cpp)
target_include_directories(DistPanEditorTests PRIVATE Source)
target_link_libraries(DistPanEditorTests
    PRIVATE
        DistPan
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
add_test(NAME DistPanEditorTests COMMAND DistPanEditorTests)
