// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "tripan/Limits.h"

namespace tripan
{
bool isValidChannelCount (int n) noexcept
{
    return n >= 1 && n <= kMaxChannels;
}
}
