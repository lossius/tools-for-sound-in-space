# SPDX-FileCopyrightText: 2026 Trond Lossius
# SPDX-License-Identifier: AGPL-3.0-or-later

# ViMiCCore: ViMiC's pure C++ model (virtual microphones: directivity, distance law, delays, interpolated delay lines). Must never depend on JUCE.
add_library(ViMiCCore STATIC src/Directivity.cpp src/Path.cpp src/DelayLine.cpp)

# Headers are listed so that they show up in the IDE's project navigator.
file(GLOB VIMIC_CORE_HEADERS CONFIGURE_DEPENDS include/vimic/*.h)
target_sources(ViMiCCore PRIVATE ${VIMIC_CORE_HEADERS})
source_group("Core/include" FILES ${VIMIC_CORE_HEADERS})
source_group("Core/src" REGULAR_EXPRESSION "ViMiC/core/src/.*")

target_link_libraries(ViMiCCore PUBLIC TlosCore)
target_include_directories(ViMiCCore PUBLIC include)
target_compile_features(ViMiCCore PUBLIC cxx_std_20)
