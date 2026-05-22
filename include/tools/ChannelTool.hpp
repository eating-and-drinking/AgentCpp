#pragma once
#include "Tool.hpp"

namespace agentcpp::tools {

class ChannelPublishTool : public Tool {
public:
    std::string name()        const override { return "ChannelPublish"; }
    std::string category()    const override { return "core"; }
    std::string description() const override;
    json        inputSchema() const override;
    ToolCallResult execute(const json& input, const ToolContext& ctx) override;
};

class ChannelReadTool : public Tool {
public:
    std::string name()        const override { return "ChannelRead"; }
    std::string category()    const override { return "core"; }
    std::string description() const override;
    json        inputSchema() const override;
    ToolCallResult execute(const json& input, const ToolContext& ctx) override;
};

class ChannelListTool : public Tool {
public:
    std::string name()        const override { return "ChannelList"; }
    std::string category()    const override { return "core"; }
    std::string description() const override;
    json        inputSchema() const override;
    ToolCallResult execute(const json& input, const ToolContext& ctx) override;
};

} // namespace agentcpp::tools
