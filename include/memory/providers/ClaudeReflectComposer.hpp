#pragma once
//
// ClaudeReflectComposer — IReflectComposer backed by the Anthropic Messages
// API. Builds a prompt from the bank's disposition + mission and the facts
// returned by the recall pipeline, then returns the LLM's free-form answer.
//
// Falls back to TemplateReflectComposer on API error.
//
#include <memory/providers/IReflectComposer.hpp>
#include <memory/providers/TemplateReflectComposer.hpp>

#include <memory>
#include <string>

namespace agentcpp::api { class ClaudeClient; }

namespace agentcpp::memory {

class ClaudeReflectComposer final : public IReflectComposer {
public:
    ClaudeReflectComposer(std::shared_ptr<agentcpp::api::ClaudeClient> client,
                          std::string model = "");

    bool        available() const override;
    std::string name()      const override { return name_; }

    std::string compose(const Bank& bank,
                        const ReflectQuery& query,
                        const std::map<std::string, std::vector<MemoryUnit>>& based_on,
                        const std::vector<MentalModel>& mental_models) override;

private:
    std::shared_ptr<agentcpp::api::ClaudeClient> client_;
    std::string                                  model_;
    std::string                                  name_;
    TemplateReflectComposer                      fallback_;
};

} // namespace agentcpp::memory
