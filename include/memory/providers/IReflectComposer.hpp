#pragma once
//
// IReflectComposer — pluggable answer-composition for reflect().
//
// hindsight uses a multi-turn LLM agent (hindsight_api/engine/reflect/agent.py)
// to synthesize an answer that respects the bank's disposition + mission
// + retrieved facts. We expose the slot in C++:
//
//   * TemplateReflectComposer — deterministic markdown-style template that
//     prints world / experience / observation groups and any mental models.
//     No model required.
//   * ClaudeReflectComposer   — feeds the same context to the Anthropic
//     Messages API and returns the LLM's free-form answer.
//
// The composer is responsible only for the `text` field of ReflectResult;
// `based_on` and `mental_models_applied` are populated by MemoryEngine and
// passed in.
//
#include <memory/MemoryTypes.hpp>

#include <map>
#include <string>
#include <vector>

namespace agentcpp::memory {

class IReflectComposer {
public:
    virtual ~IReflectComposer() = default;

    virtual bool available() const = 0;
    virtual std::string name() const = 0;

    // Build the natural-language answer text for ReflectResult.
    virtual std::string compose(
        const Bank&                                            bank,
        const ReflectQuery&                                    query,
        const std::map<std::string, std::vector<MemoryUnit>>&  based_on,
        const std::vector<MentalModel>&                        mental_models) = 0;
};

} // namespace agentcpp::memory
