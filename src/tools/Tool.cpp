#include <tools/Tool.hpp>

namespace agentcpp::tools {

ToolDefinition Tool::definition() const {
    return { name(), description(), inputSchema() };
}

std::string Tool::validateInput(const json& input) const {
    // Basic validation: check that required fields in "required" array are present
    json schema = inputSchema();
    if (!schema.contains("required")) return "";

    for (const auto& field : schema["required"]) {
        std::string key = field.get<std::string>();
        if (!input.contains(key)) {
            return "Missing required field: " + key;
        }
    }
    return "";
}

// ── ToolRegistry ──────────────────────────────────────────────────────────────
ToolRegistry& ToolRegistry::instance() {
    static ToolRegistry reg;
    return reg;
}

void ToolRegistry::registerTool(ToolPtr tool) {
    tools_.push_back(std::move(tool));
}

ToolPtr ToolRegistry::findTool(const std::string& tool_name) const {
    for (const auto& t : tools_) {
        if (t->name() == tool_name) return t;
    }
    return nullptr;
}

std::vector<ToolDefinition> ToolRegistry::definitions() const {
    std::vector<ToolDefinition> defs;
    defs.reserve(tools_.size());
    for (const auto& t : tools_) defs.push_back(t->definition());
    return defs;
}

} // namespace agentcpp::tools
