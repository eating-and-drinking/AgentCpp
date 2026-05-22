#pragma once
//
// Heuristic combined scorer. hindsight uses a cross-encoder model (typically
// Jina or BGE) at hindsight_api/engine/cross_encoder.py to rescore the top
// merged candidates. AgentCpp has no model, so we substitute a deterministic
// blend of RRF, recency, and temporal-proximity weights:
//
//     combined = w_rrf * rrf_normalized
//              + w_rec * recency
//              + w_tmp * temporal
//
// Defaults match the typical hindsight reranker weights so swapping in a
// real cross-encoder later is a one-line change.
//
#include <memory/MemoryTypes.hpp>
#include <vector>

namespace agentcpp::memory {

class Reranker {
public:
    // Score and sort. `now` is used to compute recency.
    void rerank(std::vector<ScoredResult>& results,
                const RecallQuery&         query,
                TimePoint                  now) const;

    double w_rrf = 0.55;
    double w_rec = 0.20;
    double w_tmp = 0.25;

    // Half-life for recency decay, in seconds. Anything older than this
    // contributes less than 0.5 to `recency`.
    double recency_halflife_sec = 60.0 * 60.0 * 24.0 * 30.0;  // 30 days
};

} // namespace agentcpp::memory
