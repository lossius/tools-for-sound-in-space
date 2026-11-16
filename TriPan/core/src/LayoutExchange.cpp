// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file LayoutExchange.cpp
    @brief Hazard-pointer handover of SpeakerLayout objects between threads. */

#include "tripan/LayoutExchange.h"

#include <algorithm>

namespace tripan
{
LayoutExchange::~LayoutExchange()
{
    delete current.load();
}

void LayoutExchange::publish (std::unique_ptr<const SpeakerLayout> next)
{
    const SpeakerLayout* old = current.exchange (next.release(), std::memory_order_seq_cst);

    if (old != nullptr)
        retired.emplace_back (old);

    collect();
}

const SpeakerLayout* LayoutExchange::acquire() noexcept
{
    const SpeakerLayout* p = nullptr;

    do
    {
        p = current.load (std::memory_order_seq_cst);
        hazard.store (p, std::memory_order_seq_cst);
    }
    while (p != current.load (std::memory_order_seq_cst));

    return p;
}

void LayoutExchange::collect()
{
    const SpeakerLayout* inUse = hazard.load (std::memory_order_seq_cst);

    retired.erase (std::remove_if (retired.begin(), retired.end(),
                                   [inUse] (const std::unique_ptr<const SpeakerLayout>& t) { return t.get() != inUse; }),
                   retired.end());
}
}
