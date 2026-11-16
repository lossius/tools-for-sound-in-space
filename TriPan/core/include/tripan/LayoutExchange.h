// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file LayoutExchange.h
    @brief Lock-free handover of an immutable SpeakerLayout from the message thread to the audio thread. */

#pragma once

#include "tlos/Exchange.h"
#include "tripan/SpeakerLayout.h"

namespace tripan
{
/** Hands immutable SpeakerLayout objects from the message thread to the audio thread (see tlos::Exchange). */
using LayoutExchange = tlos::Exchange<SpeakerLayout>;
}
