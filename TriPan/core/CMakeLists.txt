# SPDX-FileCopyrightText: 2026 Trond Lossius
# SPDX-License-Identifier: AGPL-3.0-or-later

# TriPanCore: TriPan's pure C++ geometry and gain computation (triangulation, virtual speakers). Must never depend on JUCE.
add_library(TriPanCore STATIC
    src/Triangulation.cpp
    src/Gains.cpp
    src/VirtualSpeakers.cpp
    src/SpeakerLayout.cpp)

# Headers are listed so that they show up in the IDE's project navigator.
file(GLOB TRIPAN_CORE_HEADERS CONFIGURE_DEPENDS include/tripan/*.h)
target_sources(TriPanCore PRIVATE ${TRIPAN_CORE_HEADERS})
source_group("Core/include" FILES ${TRIPAN_CORE_HEADERS})
source_group("Core/src" REGULAR_EXPRESSION "core/src/.*")

find_package(Threads REQUIRED)
target_link_libraries(TriPanCore PUBLIC TlosCore Threads::Threads)

target_include_directories(TriPanCore PUBLIC include)
target_compile_features(TriPanCore PUBLIC cxx_std_20)
