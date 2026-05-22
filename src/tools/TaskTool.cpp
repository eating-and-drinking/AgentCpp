#include <tools/TaskTool.hpp>
#include <agent/QueryEngine.hpp>
#include <api/ClaudeClient.hpp>
#include <sstream>
#include <vector>

namespace agentcpp::tools {

std::string TaskTool::description() const {
    return
        "Spawn a sub-agent to handle a self-contained task. The sub-agent has "
        "its own independent conversation and runs to completion (up to its "
        "own max_turns), then returns its final text response. Use this to "
        "delegate work that would otherwise pollute the main conversation, "
        "such as broad research or parallel investigations. Sub-agents have "
        "access to the same tools as the parent. Recursion is bounded.";
}

json TaskTool::inputSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"description", {
                {"type", "string"},
                {"description", "Short 3-5 word description of the task (used for logging)."}
            }},
            {"prompt", {
                {"type", "string"},
                {"description", "The full instruction the sub-agent should receive. "
                                "Should be self-contained — the sub-agent has no memory of the parent's context."}
            }}
        }},
        {"required", json::array({"prompt"})}
    };
}

ToolCallResult TaskTool::execute(const json& input, const ToolContext& ctx) {
    std::string prompt = input.value("prompt", "");
    if (prompt.empty()) return ToolCallResult::error("'prompt' is required");

    if (ctx.subagent_depth >= ctx.max_subagent_depth) {
        return ToolCallResult::error(
            "Sub-agent recursion limit reached (depth=" +
            std::to_string(ctx.subagent_depth) + "/" +
            std::to_string(ctx.max_subagent_depth) +
            "). The current agent must complete this task directly.");
    }

    // Sub-agent gets its own QueryConfig: inherit ctx but bump depth.
    agentcpp::agent::QueryConfig sub_cfg;
    sub_cfg.model         = model_;
    sub_cfg.max_tokens    = max_tokens_;
    sub_cfg.max_turns     = max_turns_;
    sub_cfg.system_prompt = "";  // QueryEngine builds default
    sub_cfg.auto_approve  = ctx.auto_approve;
    sub_cfg.headless      = true;
    sub_cfg.tool_ctx               = ctx;
    sub_cfg.tool_ctx.subagent_depth = ctx.subagent_depth + 1;

    // Disallow recursive Task spawning past the limit by removing it from
    // the allowed_tools set when we're already at max-1. We keep the rest
    // of the tools accessible to the sub-agent unconditionally.
    // (Left implicit — depth check in execute() handles it.)

    agentcpp::agent::QueryEngine sub_engine(client_, registry_);
    // No event callback set: sub-agent runs silently.

    std::vector<agentcpp::api::Message> sub_conv;
    std::string result;
    try {
        result = sub_engine.runTurn(sub_conv, prompt, sub_cfg);
    } catch (const std::exception& e) {
        return ToolCallResult::error(
            std::string("sub-agent threw: ") + e.what());
    }

    if (result.empty()) {
        return ToolCallResult::error(
            "Sub-agent returned no text (it may have exhausted max_turns or "
            "failed silently).");
    }
    return ToolCallResult::ok(std::move(result));
}

} // namespace agentcpp::tools
