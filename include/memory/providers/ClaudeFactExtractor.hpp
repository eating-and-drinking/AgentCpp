#pragma once
//
// ClaudeFactExtractor — IFactExtractor backed by the Anthropic Messages API
// via the existing api::ClaudeClient. Available iff a non-empty API key was
// configured on the client.
//
// Falls back internally to a HeuristicFactExtractor on parse error / API
// failure, so a temporary network blip cannot break retain().
//
#include <memory/providers/HeuristicFactExtractor.hpp>
#include <memory/providers/IFactExtractor.hpp>

#include <memory>
#include <string>

namespace agentcpp::api { class ClaudeClient; }

namespace agentcpp::memory {

class ClaudeFactExtractor final : public IFactExtractor {
public:
    ClaudeFactExtractor(std::shared_ptr<agentcpp::api::ClaudeClient> client,
                        std::string model = "");

    bool        available() const override;
    std::string name()      const override { return name_; }

    std::vector<ExtractedFact>
    extract(const RetainContent& content, int content_index) override;

private:
    std::shared_ptr<agentcpp::api::ClaudeClient> client_;
    std::string                                  model_;
    std::string                                  name_;
    HeuristicFactExtractor                       fallback_;
};

} // namespace agentcpp::memory
