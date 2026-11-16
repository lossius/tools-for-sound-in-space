# SPDX-FileCopyrightText: 2026 Trond Lossius
# SPDX-License-Identifier: AGPL-3.0-or-later

add_executable(TlosCoreTests CoreTests.cpp)
target_link_libraries(TlosCoreTests PRIVATE TlosCore)
add_test(NAME TlosCoreTests COMMAND TlosCoreTests)

add_executable(TlosExchangeTests ExchangeTests.cpp)
target_link_libraries(TlosExchangeTests PRIVATE TlosCore)
add_test(NAME TlosExchangeTests COMMAND TlosExchangeTests)

source_group("Tests" REGULAR_EXPRESSION "/tests/")
