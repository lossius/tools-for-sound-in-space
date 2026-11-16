// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file ExchangeTests.cpp
    @brief Stress test for LayoutExchange: one writer publishing, one reader reading continuously. Run under ThreadSanitizer/AddressSanitizer for the strongest check. */

#include "tripan/Gains.h"
#include "tripan/LayoutExchange.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

using namespace tripan;

/** Runs the stress test. @return 0 if every gain set had unit power. */
int main()
{
    LayoutExchange ex;
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
            if (const auto* t = ex.acquire())
            {
                const auto g = computeGains (*t, { 0.3, 0.2 });
                double s = 0.0;
                for (int i = 0; i < g.count; ++i) s += (double) g.gain[(size_t) i] * g.gain[(size_t) i];
                if (std::abs (s - 1.0) > 1e-5) ++bad;
                ++reads;
            }
        }
        ex.release();
    });

    for (int iter = 0; iter < 3000; ++iter)
    {
        std::vector<Vec2> pts;
        const int n = 3 + iter % 20;
        for (int i = 0; i < n; ++i)
            pts.push_back ({ std::cos (i * 6.2831853 / n + iter * 0.01), std::sin (i * 6.2831853 / n + iter * 0.01) });
        ex.publish (std::make_unique<const SpeakerLayout> (pts, findVirtualSpeakers (pts)));
    }

    stop = true;
    reader.join();

    std::printf ("reads=%ld bad=%d\n", reads.load(), bad.load());
    return bad.load() == 0 ? 0 : 1;
}
