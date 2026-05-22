#pragma once
#include "Tool.hpp"

namespace agentcpp::tools {

class ComputerTool : public Tool {
public:
    std::string name()        const override { return "Computer"; }
    std::string category()    const override { return "computer"; }
    std::string description() const override;
    json        inputSchema() const override;

    ToolCallResult execute(const json& input, const ToolContext& ctx) override;
};

} // namespace agentcpp::tools
