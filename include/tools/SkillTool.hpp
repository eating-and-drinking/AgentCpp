#pragma once
#include "Tool.hpp"
#include <skills/SkillRegistry.hpp>

namespace agentcpp::tools {

class SkillTool : public Tool {
public:
    explicit SkillTool(const agentcpp::skills::SkillRegistry& registry)
        : registry_(registry) {}

    std::string name()        const override { return "Skill"; }
    std::string category()    const override { return "core"; }
    std::string description() const override;
    json        inputSchema() const override;

    ToolCallResult execute(const json& input, const ToolContext& ctx) override;

private:
    const agentcpp::skills::SkillRegistry& registry_;
};

} // namespace agentcpp::tools
