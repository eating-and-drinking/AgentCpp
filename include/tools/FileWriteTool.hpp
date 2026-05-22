#pragma once
#include "Tool.hpp"

namespace agentcpp::tools {

class FileWriteTool : public Tool {
public:
    std::string name()        const override { return "Write"; }
    std::string description() const override;
    json        inputSchema() const override;

    ToolCallResult execute(const json& input, const ToolContext& ctx) override;
};

} // namespace agentcpp::tools
