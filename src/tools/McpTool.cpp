#include <tools/McpTool.hpp>

namespace agentcpp::tools {

ToolCallResult McpTool::execute(const json& input, const ToolContext& ctx) {
    if (ctx.read_only) {
        // MCP tools are external; we can't tell whether they write. Block to be safe.
        return ToolCallResult::error(
            "read-only mode: MCP tool '" + name_ + "' is disabled");
    }
    if (!rt_.client) return ToolCallResult::error("MCP tool has no client");

    bool is_error = false;
    std::string out = rt_.client->callTool(rt_.info.name, input, is_error);
    if (is_error) return ToolCallResult::error(out);
    return ToolCallResult::ok(std::move(out));
}

} // namespace agentcpp::tools
