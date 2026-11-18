# SPDX-FileCopyrightText: 2026 Trond Lossius
# SPDX-License-Identifier: AGPL-3.0-or-later

add_executable(ViMiCCoreTests CoreTests.cpp)
target_link_libraries(ViMiCCoreTests PRIVATE ViMiCCore)
add_test(NAME ViMiCCoreTests COMMAND ViMiCCoreTests)

source_group("Tests" REGULAR_EXPRESSION "/tests/")
