#pragma once

#include "tripan/Triangulation.h"

#include <atomic>
#include <memory>
#include <vector>

namespace tripan
{
/** Hands immutable Triangulation objects from one writer thread (message thread) to one reader
    thread (audio thread) without locks or allocation on the reader side.

    Uses a single hazard pointer: the reader announces the object it is using, and the writer only
    deletes retired objects that are not announced.

    - Writer thread only: publish().
    - Reader thread only: acquire(), release().
    - acquire() returns nullptr until the first publish(). The returned pointer stays valid until
      the next acquire() or release() on the reader thread.
*/
class TriangulationExchange
{
public:
    TriangulationExchange() = default;
    ~TriangulationExchange();

    TriangulationExchange (const TriangulationExchange&) = delete;
    TriangulationExchange& operator= (const TriangulationExchange&) = delete;

    void publish (std::unique_ptr<const Triangulation> next);

    const Triangulation* acquire() noexcept;
    void release() noexcept { hazard.store (nullptr, std::memory_order_seq_cst); }

private:
    void collect();

    std::atomic<const Triangulation*> current { nullptr };
    std::atomic<const Triangulation*> hazard { nullptr };
    std::vector<std::unique_ptr<const Triangulation>> retired; // writer thread only
};
}
