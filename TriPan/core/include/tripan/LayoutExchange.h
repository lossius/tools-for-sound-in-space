// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file LayoutExchange.h
    @brief Lock-free handover of an immutable SpeakerLayout from the message thread to the audio thread. */

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
    /** Creates an exchange with nothing published yet. */
    LayoutExchange() = default;

    /** Deletes the current and all retired layouts. Neither thread may use the exchange any more. */
    ~LayoutExchange();

    LayoutExchange (const LayoutExchange&) = delete;
    LayoutExchange& operator= (const LayoutExchange&) = delete;

    /** Makes `next` the current layout. The previous one is kept until the reader has moved on.
        Writer thread only; may allocate and free. */
    void publish (std::unique_ptr<const SpeakerLayout> next);

    /** Returns the current layout and protects it from deletion, or nullptr if nothing has been
        published. Reader thread only; lock-free, no allocation. */
    const SpeakerLayout* acquire() noexcept;

    /** Ends the protection of the layout returned by acquire(). Reader thread only. */
    void release() noexcept { hazard.store (nullptr, std::memory_order_seq_cst); }

private:
    /** Deletes the retired layouts that the reader is not using. Writer thread only. */
    void collect();

    std::atomic<const SpeakerLayout*> current { nullptr };  ///< Layout to hand out next.
    std::atomic<const SpeakerLayout*> hazard { nullptr };   ///< Layout the reader is using right now.
    std::vector<std::unique_ptr<const SpeakerLayout>> retired; ///< Replaced layouts awaiting deletion (writer thread only).
};
}
