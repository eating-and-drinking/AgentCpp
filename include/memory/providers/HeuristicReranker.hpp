#pragma once
//
// HeuristicReranker — default IReranker. Wraps the existing weighted
// combiner (RRF + recency + temporal_proximity). No network.
//
#include <memory/Reranker.hpp>
#include <memory/providers/IReranker.hpp>

namespace agentcpp::memory {

class HeuristicReranker final : public IReranker {
public:
    HeuristicReranker() = default;

    bool        available() const override { return true; }
    std::string name()      const override { return "heuristic"; }

    void rerank(std::vector<ScoredResult>& results,
                const RecallQuery&         query,
                TimePoint                  now) override {
        inner_.rerank(results, query, now);
    }

    Reranker& tunables() { return inner_; }

private:
    Reranker inner_;
};

} // namespace agentcpp::memory
