#pragma once
//
// Reciprocal Rank Fusion across multiple retrieval strategies.
//
// Direct port of hindsight_api/engine/search/fusion.reciprocal_rank_fusion.
// Source lists are tagged in order as "semantic", "bm25", "graph", "temporal".
// Documents missing from some lists are still scored — RRF naturally handles
// asymmetric coverage.
//
#include <memory/MemoryTypes.hpp>
#include <vector>

namespace agentcpp::memory {

// score(d) = sum_over_lists(1 / (k + rank(d)))
std::vector<MergedCandidate>
reciprocalRankFusion(const std::vector<std::vector<RetrievalResult>>& result_lists,
                     int k = 60);

// Min-max normalisation of one numeric field across a vector of ScoredResults.
// Used to put rrf / recency / temporal on a common scale before combining.
void normalizeOnDelta(std::vector<double>& values);

} // namespace agentcpp::memory
