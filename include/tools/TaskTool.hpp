#pragma once
#include "Tool.hpp"

namespace agentcpp::api      { class ClaudeClient; }
namespace agentcpp::agent    { struct QueryConfig; }

namespace agentcpp::tools {

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
    std::string category()    const override { return "core"; }
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
