# SPDX-FileCopyrightText: 2026 Trond Lossius
# SPDX-License-Identifier: AGPL-3.0-or-later

# VecPanCore: VecPan's pure C++ gain law (2D vector base amplitude panning). Must never depend on JUCE.
add_library(VecPanCore STATIC src/Vbap.cpp)

# Headers are listed so that they show up in the IDE's project navigator.
file(GLOB VECPAN_CORE_HEADERS CONFIGURE_DEPENDS include/vecpan/*.h)
target_sources(VecPanCore PRIVATE ${VECPAN_CORE_HEADERS})
source_group("Core/include" FILES ${VECPAN_CORE_HEADERS})
source_group("Core/src" REGULAR_EXPRESSION "VecPan/core/src/.*")

target_link_libraries(VecPanCore PUBLIC TlosCore)
target_include_directories(VecPanCore PUBLIC include)
target_compile_features(VecPanCore PUBLIC cxx_std_20)
