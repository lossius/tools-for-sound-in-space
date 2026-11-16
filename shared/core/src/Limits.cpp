// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file Limits.cpp
    @brief Channel-count validation. */

#include "tlos/Limits.h"

namespace tlos
{
bool isValidChannelCount (int n) noexcept
{
    return n >= 1 && n <= kMaxChannels;
}
}
