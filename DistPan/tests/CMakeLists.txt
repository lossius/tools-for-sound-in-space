# SPDX-FileCopyrightText: 2026 Trond Lossius
# SPDX-License-Identifier: AGPL-3.0-or-later

add_executable(DistPanCoreTests CoreTests.cpp)
target_link_libraries(DistPanCoreTests PRIVATE DistPanCore)
add_test(NAME DistPanCoreTests COMMAND DistPanCoreTests)

source_group("Tests" REGULAR_EXPRESSION "/tests/")
