#pragma once
#include "Tool.hpp"
#include <mcp/McpManager.hpp>

namespace agentcpp::tools {

// Tool subclass that proxies calls to an MCP server. One McpTool per
// (server, server-side tool) pair. Created and registered from the resolved
// tool list produced by McpManager.
class McpTool : public Tool {
public:
    McpTool(std::string prefixed_name, agentcpp::mcp::ResolvedTool rt)
        : name_(std::move(prefixed_name)), rt_(std::move(rt)) {}

    std::string name()        const override { return name_; }
    std::string description() const override { return rt_.info.description; }
    json        inputSchema() const override { return rt_.info.input_schema; }

    ToolCallResult execute(const json& input, const ToolContext& ctx) override;

private:
    std::string                 name_;
    agentcpp::mcp::ResolvedTool rt_;
};

} // namespace agentcpp::tools
