#pragma once
#include "Tool.hpp"
#include <skills/SkillRegistry.hpp>

namespace agentcpp::tools {

// Skill tool: looks up a skill by name and returns its full SKILL.md body
// to the model. The model then follows the instructions inside.
//
// Input: { "name": "<skill-name>" }
//
// Discovery happens before the tool is registered. A list of available skill
// names + descriptions is also injected into the system prompt by QueryEngine,
// so the model knows what's available without invoking the tool.
class SkillTool : public Tool {
public:
    explicit SkillTool(const agentcpp::skills::SkillRegistry& registry)
        : registry_(registry) {}

    std::string name() const override { return "Skill"; }
    std::string description() const override;
    json        inputSchema() const override;

    ToolCallResult execute(const json& input, const ToolContext& ctx) override;

private:
    const agentcpp::skills::SkillRegistry& registry_;
};

} // namespace agentcpp::tools
