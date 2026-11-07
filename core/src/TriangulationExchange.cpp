#include "tripan/TriangulationExchange.h"

#include <algorithm>

namespace tripan
{
TriangulationExchange::~TriangulationExchange()
{
    delete current.load();
}

void TriangulationExchange::publish (std::unique_ptr<const Triangulation> next)
{
    const Triangulation* old = current.exchange (next.release(), std::memory_order_seq_cst);

    if (old != nullptr)
        retired.emplace_back (old);

    collect();
}

const Triangulation* TriangulationExchange::acquire() noexcept
{
    const Triangulation* p = nullptr;

    do
    {
        p = current.load (std::memory_order_seq_cst);
        hazard.store (p, std::memory_order_seq_cst);
    }
    while (p != current.load (std::memory_order_seq_cst));

    return p;
}

void TriangulationExchange::collect()
{
    const Triangulation* inUse = hazard.load (std::memory_order_seq_cst);

    retired.erase (std::remove_if (retired.begin(), retired.end(),
                                   [inUse] (const std::unique_ptr<const Triangulation>& t) { return t.get() != inUse; }),
                   retired.end());
}
}
