cmake_minimum_required(VERSION 3.22)

project(TriPan VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

option(TRIPAN_ENABLE_AAX "Build the AAX format (requires the Avid AAX SDK)" OFF)
set(TRIPAN_AAX_SDK_PATH "" CACHE PATH "Path to the Avid AAX SDK (not redistributable, keep outside the repo)")

enable_testing()

add_subdirectory(JUCE)
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
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Spatial
    AU_MAIN_TYPE kAudioUnitType_Effect
    AAX_CATEGORY AAX_ePlugInCategory_Effect)

target_sources(TriPan PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/FieldComponent.cpp)

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
add_test(NAME TriPanProcessorTests COMMAND TriPanProcessorTests)

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
