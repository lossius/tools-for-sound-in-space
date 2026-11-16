# SPDX-FileCopyrightText: 2026 Trond Lossius
# SPDX-License-Identifier: AGPL-3.0-or-later

# TlosUI: the JUCE part that every plugin shares: the processor base class (parameters, state), the XY pad,
# the table windows, position import/export and the editor window.
add_library(TlosUI STATIC
    src/SpatialProcessor.cpp
    src/FieldComponent.cpp
    src/PositionIO.cpp
    src/PositionTableWindow.cpp
    src/SpatialEditor.cpp)

# Headers are listed so that they show up in the IDE's project navigator.
file(GLOB TLOS_UI_HEADERS CONFIGURE_DEPENDS include/tlos/*.h)
target_sources(TlosUI PRIVATE ${TLOS_UI_HEADERS})
source_group("Shared UI/include" FILES ${TLOS_UI_HEADERS})
source_group("Shared UI/src" REGULAR_EXPRESSION "shared/ui/src/.*")

target_include_directories(TlosUI PUBLIC include)

# These must be the same in everything that includes JUCE headers (the plugins define them too).
target_compile_definitions(TlosUI PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

target_link_libraries(TlosUI
    PUBLIC
        TlosCore
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
