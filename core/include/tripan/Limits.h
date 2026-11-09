// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

namespace tripan
{
/** Maximum number of input (source) and output (speaker) channels.
    64 = 7th-order ambisonic channel count, used here only as a channel-count ceiling. */
inline constexpr int kMaxChannels = 64;

/** True if n is a valid channel count for sources or speakers. */
bool isValidChannelCount (int n) noexcept;
}
