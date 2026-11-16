// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file ExchangeTests.cpp
    @brief Stress test for tlos::Exchange: one writer publishing, one reader reading continuously. Run under
    ThreadSanitizer/AddressSanitizer for the strongest check. */

#include "tlos/Exchange.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <thread>

/** An object whose fields must always agree: a reader that sees them differ has seen a torn or freed object. */
struct Snapshot
{
    /** Creates a snapshot where every entry equals `v`. */
    explicit Snapshot (int v) { values.fill (v); }

    std::array<int, 64> values {}; ///< All entries are equal in a valid snapshot.
};

/** Runs the stress test. @return 0 if the reader never saw an inconsistent snapshot. */
int main()
{
    tlos::Exchange<Snapshot> ex;
    if (ex.acquire() != nullptr)
    {
        std::printf ("FAIL: expected nullptr before first publish\n");
        return 1;
    }

    std::atomic<bool> stop { false };
    std::atomic<long> reads { 0 };
    std::atomic<int> bad { 0 };

    std::thread reader ([&]
    {
        while (! stop.load())
        {
            if (const auto* s = ex.acquire())
            {
                for (const int v : s->values)
                    if (v != s->values[0])
                        ++bad;
                ++reads;
            }
        }
        ex.release();
    });

    for (int iter = 0; iter < 20000; ++iter)
        ex.publish (std::make_unique<const Snapshot> (iter));

    stop = true;
    reader.join();

    std::printf ("reads=%ld bad=%d\n", reads.load(), bad.load());
    return bad.load() == 0 ? 0 : 1;
}
