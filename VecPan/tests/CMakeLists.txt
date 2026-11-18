# SPDX-FileCopyrightText: 2026 Trond Lossius
# SPDX-License-Identifier: AGPL-3.0-or-later

add_executable(VecPanCoreTests CoreTests.cpp)
target_link_libraries(VecPanCoreTests PRIVATE VecPanCore)
add_test(NAME VecPanCoreTests COMMAND VecPanCoreTests)

source_group("Tests" REGULAR_EXPRESSION "/tests/")
