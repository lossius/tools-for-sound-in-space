# SPDX-FileCopyrightText: 2026 Trond Lossius
# SPDX-License-Identifier: AGPL-3.0-or-later

# ViMiC: virtual microphone control. The plugin, its pure C++ core and its tests.
add_subdirectory(core)
add_subdirectory(tests)

juce_add_plugin(ViMiC
    PRODUCT_NAME "ViMiC"
    COMPANY_NAME "Trond Lossius"
    BUNDLE_ID no.trondlossius.vimic
    PLUGIN_MANUFACTURER_CODE Tlos
    PLUGIN_CODE ViMc
    FORMATS ${TLOS_PLUGIN_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "The ViMiC standalone app needs audio input to process the source channels."
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Spatial
    AU_MAIN_TYPE kAudioUnitType_Effect
    AAX_CATEGORY AAX_ePlugInCategory_Effect)

# Headers are listed too, so that they show up in the IDE's project navigator.
target_sources(ViMiC PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/ViMiCField.cpp
    Source/ViMiCField.h
    Source/AbsorptionPanel.cpp
    Source/AbsorptionPanel.h
    Source/ViMiCParameters.h)
# In the IDE, sources and headers sit in separate subfolders.
source_group("Sources/src" FILES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/AbsorptionPanel.cpp
    Source/ViMiCField.cpp)
source_group("Sources/include" FILES
    Source/PluginProcessor.h
    Source/PluginEditor.h
    Source/ViMiCField.h
    Source/AbsorptionPanel.h
    Source/ViMiCParameters.h)

tlos_finish_plugin(ViMiC)
target_link_libraries(ViMiC PUBLIC ViMiCCore)

# Offline processor tests (no DAW needed)
add_executable(ViMiCProcessorTests tests/ProcessorTests.cpp)
target_include_directories(ViMiCProcessorTests PRIVATE Source)
# The OSC loopback test runs the message loop for a moment.
target_compile_definitions(ViMiCProcessorTests PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(ViMiCProcessorTests
    PRIVATE
        ViMiC
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
add_test(NAME ViMiCProcessorTests COMMAND ViMiCProcessorTests "${CMAKE_SOURCE_DIR}/speaker layouts" "${CMAKE_SOURCE_DIR}/microphone setups")

# Editor interaction test; with a path argument it also writes a PNG snapshot of the editor.
add_executable(ViMiCEditorTests tests/EditorTests.cpp)
target_include_directories(ViMiCEditorTests PRIVATE Source)
target_link_libraries(ViMiCEditorTests
    PRIVATE
        ViMiC
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
add_test(NAME ViMiCEditorTests COMMAND ViMiCEditorTests)
