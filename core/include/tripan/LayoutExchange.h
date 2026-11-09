// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "tripan/SpeakerLayout.h"

#include <atomic>
#include <memory>
#include <vector>

namespace tripan
{
/** Hands immutable SpeakerLayout objects from one writer thread (message thread) to one reader
    thread (audio thread) without locks or allocation on the reader side.

    Uses a single hazard pointer: the reader announces the object it is using, and the writer only
    deletes retired objects that are not announced.

    - Writer thread only: publish().
    - Reader thread only: acquire(), release().
    - acquire() returns nullptr until the first publish(). The returned pointer stays valid until
      the next acquire() or release() on the reader thread.
*/
class LayoutExchange
{
public:
    LayoutExchange() = default;
    ~LayoutExchange();

    LayoutExchange (const LayoutExchange&) = delete;
    LayoutExchange& operator= (const LayoutExchange&) = delete;

    void publish (std::unique_ptr<const SpeakerLayout> next);

    const SpeakerLayout* acquire() noexcept;
    void release() noexcept { hazard.store (nullptr, std::memory_order_seq_cst); }

private:
    void collect();

    std::atomic<const SpeakerLayout*> current { nullptr };
    std::atomic<const SpeakerLayout*> hazard { nullptr };
    std::vector<std::unique_ptr<const SpeakerLayout>> retired; // writer thread only
};
}
