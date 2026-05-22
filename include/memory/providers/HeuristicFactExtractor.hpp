#pragma once
//
// HeuristicFactExtractor — default IFactExtractor. Wraps the existing
// sentence-splitter + capitalized-phrase entity guesser. No network, no
// API key, always available.
//
#include <memory/FactExtractor.hpp>
#include <memory/providers/IFactExtractor.hpp>

namespace agentcpp::memory {

class HeuristicFactExtractor final : public IFactExtractor {
public:
    HeuristicFactExtractor() = default;

    bool        available() const override { return true; }
    std::string name()      const override { return "heuristic"; }

    std::vector<ExtractedFact>
    extract(const RetainContent& content, int content_index) override {
        return inner_.extract(content, content_index);
    }

    FactExtractor& tunables() { return inner_; }

private:
    FactExtractor inner_;
};

} // namespace agentcpp::memory
