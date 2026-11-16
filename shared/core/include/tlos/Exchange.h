// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file Exchange.h
    @brief Lock-free handover of an immutable object from the message thread to the audio thread. */

#pragma once

#include <algorithm>
#include <atomic>
#include <memory>
#include <vector>

namespace tlos
{
/** Hands immutable objects of type T from one writer thread (message thread) to one reader
    thread (audio thread) without locks or allocation on the reader side.

    Uses a single hazard pointer: the reader announces the object it is using, and the writer only
    deletes retired objects that are not announced.

    - Writer thread only: publish().
    - Reader thread only: acquire(), release().
    - acquire() returns nullptr until the first publish(). The returned pointer stays valid until
      the next acquire() or release() on the reader thread.

    @tparam T  The type of the handed-over objects (they are never modified after publish()). */
template <typename T>
class Exchange
{
public:
    /** Creates an exchange with nothing published yet. */
    Exchange() = default;

    /** Deletes the current and all retired objects. Neither thread may use the exchange any more. */
    ~Exchange()
    {
        delete current.load();
    }

    Exchange (const Exchange&) = delete;
    Exchange& operator= (const Exchange&) = delete;

    /** Makes `next` the current object. The previous one is kept until the reader has moved on.
        Writer thread only; may allocate and free. */
    void publish (std::unique_ptr<const T> next)
    {
        const T* old = current.exchange (next.release(), std::memory_order_seq_cst);

        if (old != nullptr)
            retired.emplace_back (old);

        collect();
    }

    /** Returns the current object and protects it from deletion, or nullptr if nothing has been
        published. Reader thread only; lock-free, no allocation. */
    const T* acquire() noexcept
    {
        const T* p = nullptr;

        do
        {
            p = current.load (std::memory_order_seq_cst);
            hazard.store (p, std::memory_order_seq_cst);
        }
        while (p != current.load (std::memory_order_seq_cst));

        return p;
    }

    /** Ends the protection of the object returned by acquire(). Reader thread only. */
    void release() noexcept { hazard.store (nullptr, std::memory_order_seq_cst); }

private:
    /** Deletes the retired objects that the reader is not using. Writer thread only. */
    void collect()
    {
        const T* inUse = hazard.load (std::memory_order_seq_cst);

        retired.erase (std::remove_if (retired.begin(), retired.end(),
                                       [inUse] (const std::unique_ptr<const T>& t) { return t.get() != inUse; }),
                       retired.end());
    }

    std::atomic<const T*> current { nullptr };  ///< Object to hand out next.
    std::atomic<const T*> hazard { nullptr };   ///< Object the reader is using right now.
    std::vector<std::unique_ptr<const T>> retired; ///< Replaced objects awaiting deletion (writer thread only).
};
}
