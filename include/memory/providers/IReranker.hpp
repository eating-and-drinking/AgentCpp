#pragma once
//
// IReranker — pluggable rerank stage applied after RRF fusion.
//
// hindsight uses a cross-encoder model (Jina / BGE / Cohere). We expose the
// same slot:
//
//   * HeuristicReranker — combines normalized RRF, recency decay, and
//     temporal proximity. Always available, deterministic, no network.
//   * (future) HttpReranker — POST query + candidates to a configurable
//     cross-encoder endpoint, swap in returned scores. Interface is ready;
//     a concrete impl can be dropped in without changing MemoryEngine.
//
#include <memory/MemoryTypes.hpp>

#include <string>
#include <vector>

namespace agentcpp::memory {

class IReranker {
public:
    virtual ~IReranker() = default;

    virtual bool available() const = 0;
    virtual std::string name() const = 0;

    // Mutate `results` in place: assign combined_score, sort descending.
    virtual void rerank(std::vector<ScoredResult>& results,
                        const RecallQuery& query,
                        TimePoint           now) = 0;
};

} // namespace agentcpp::memory
