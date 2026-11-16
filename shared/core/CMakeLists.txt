# SPDX-FileCopyrightText: 2026 Trond Lossius
# SPDX-License-Identifier: AGPL-3.0-or-later

# TlosCore: what every plugin in this repository shares, in pure C++ (geometry types, channel limits,
# source groups, the lock-free handover). Must never depend on JUCE.
add_library(TlosCore STATIC
    src/Limits.cpp
    src/Groups.cpp
    src/Hull.cpp)

# Headers are listed so that they show up in the IDE's project navigator.
file(GLOB TLOS_CORE_HEADERS CONFIGURE_DEPENDS include/tlos/*.h)
target_sources(TlosCore PRIVATE ${TLOS_CORE_HEADERS})
source_group("Shared Core/include" FILES ${TLOS_CORE_HEADERS})
source_group("Shared Core/src" REGULAR_EXPRESSION "shared/core/src/.*")

find_package(Threads REQUIRED)
target_link_libraries(TlosCore PUBLIC Threads::Threads)

target_include_directories(TlosCore PUBLIC include)
target_compile_features(TlosCore PUBLIC cxx_std_20)
