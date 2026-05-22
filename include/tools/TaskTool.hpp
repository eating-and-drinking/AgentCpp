#pragma once
#include "Tool.hpp"

namespace agentcpp::api      { class ClaudeClient; }
namespace agentcpp::agent    { struct QueryConfig; }

namespace agentcpp::tools {

// Task tool: spawns a sub-agent that runs an independent conversation against
// the same Claude API and ToolRegistry, then returns its final text response
// to the parent agent.
//
// Sub-agent runs are silent (events are dropped — they don't reach the
// parent's TUI). Recursion is bounded by ToolContext::max_subagent_depth.
//
// All wiring (client, registry, model, max_tokens, etc.) is captured at
// construction time from the same values the top-level agent uses.
class TaskTool : public Tool {
public:
    TaskTool(std::shared_ptr<agentcpp::api::ClaudeClient> client,
             ToolRegistry&                           registry,
             std::string                             model,
             int                                     max_tokens,
             int                                     max_turns_per_subagent)
        : client_(std::move(client))
        , registry_(registry)
        , model_(std::move(model))
        , max_tokens_(max_tokens)
        , max_turns_(max_turns_per_subagent)
    {}

    std::string name()        const override { return "Task"; }
    std::string description() const override;
    json        inputSchema() const override;

    ToolCallResult execute(const json& input, const ToolContext& ctx) override;

private:
    std::shared_ptr<agentcpp::api::ClaudeClient> client_;
    ToolRegistry&                           registry_;
    std::string                             model_;
    int                                     max_tokens_;
    int                                     max_turns_;
};

} // namespace agentcpp::tools
