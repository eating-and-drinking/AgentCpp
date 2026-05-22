#pragma once
//
// Graph (link-expansion) retrieval. Mirrors
// hindsight_api/engine/search/link_expansion_retrieval.py.
//
// Given a set of seed memory units (typically the top BM25 hits), perform a
// bounded spreading-activation walk over the bank's MemoryLinks. Each hop
// multiplies activation by the edge weight and a decay factor.
//
#include <memory/MemoryTypes.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace agentcpp::memory {

class GraphRetrieval {
public:
    GraphRetrieval(const std::vector<MemoryLink>& links,
                   const std::unordered_map<std::string, MemoryUnit>& unit_index)
        : links_(links), unit_index_(unit_index) {}

    // Expand from a set of seeds. Returns RetrievalResults with `activation`
    // populated, sorted by activation desc, capped at `k`.
    std::vector<RetrievalResult>
    expand(const std::vector<std::string>& seed_unit_ids,
           std::size_t k,
           int         max_hops = 2) const;

    // How much activation degrades per hop. hindsight uses ~0.5.
    double hop_decay = 0.5;

private:
    const std::vector<MemoryLink>&                       links_;
    const std::unordered_map<std::string, MemoryUnit>&   unit_index_;

    // adjacency built lazily inside expand()
    using Adjacency = std::unordered_map<std::string,
                          std::vector<const MemoryLink*>>;
    Adjacency buildAdjacency() const;
};

} // namespace agentcpp::memory
