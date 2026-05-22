#pragma once
//
// NullEmbedder — default IEmbedder. Never produces vectors and reports
// available()==false, so MemoryEngine skips the embedding-driven semantic
// channel and uses its BM25-alt-params fallback.
//
#include <memory/providers/IEmbedder.hpp>

namespace agentcpp::memory {

class NullEmbedder final : public IEmbedder {
public:
    bool        available() const override { return false; }
    std::string name()      const override { return "null"; }
    std::size_t dimension() const override { return 0; }

    std::vector<std::vector<float>>
    embed(const std::vector<std::string>& /*texts*/) override {
        return {};
    }
};

} // namespace agentcpp::memory
