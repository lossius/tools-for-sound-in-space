# SPDX-FileCopyrightText: 2026 Trond Lossius
# SPDX-License-Identifier: AGPL-3.0-or-later

# DistPanCore: DistPan's pure C++ gain law (DBAP). Must never depend on JUCE.
add_library(DistPanCore STATIC src/Dbap.cpp)

# Headers are listed so that they show up in the IDE's project navigator.
file(GLOB DISTPAN_CORE_HEADERS CONFIGURE_DEPENDS include/distpan/*.h)
target_sources(DistPanCore PRIVATE ${DISTPAN_CORE_HEADERS})
source_group("Core/include" FILES ${DISTPAN_CORE_HEADERS})
source_group("Core/src" REGULAR_EXPRESSION "DistPan/core/src/.*")

target_link_libraries(DistPanCore PUBLIC TlosCore)
target_include_directories(DistPanCore PUBLIC include)
target_compile_features(DistPanCore PUBLIC cxx_std_20)
