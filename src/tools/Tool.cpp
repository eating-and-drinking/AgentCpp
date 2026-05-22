#include <tools/Tool.hpp>
#include <algorithm>
#include <set>

namespace agentcpp::tools {

ToolDefinition Tool::definition() const {
    return { name(), description(), inputSchema() };
}

std::string Tool::validateInput(const json& input) const {
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

std::vector<std::string> ToolRegistry::listGroups() const {
    std::set<std::string> seen;
    for (const auto& t : tools_) seen.insert(t->category());
    return {seen.begin(), seen.end()};
}

std::vector<std::string> ToolRegistry::toolsInGroup(const std::string& group) const {
    std::vector<std::string> out;
    for (const auto& t : tools_) {
        if (t->category() == group) out.push_back(t->name());
    }
    return out;
}

std::vector<ToolDefinition> ToolRegistry::definitionsForPersona(
    const std::vector<std::string>& allowed_groups,
    const std::vector<std::string>& extra_enable,
    const std::vector<std::string>& extra_disable,
    const std::vector<std::string>& explicit_tool_whitelist) const
{
    std::set<std::string> active;
    if (allowed_groups.empty()) {
        for (const auto& g : listGroups()) active.insert(g);
    } else {
        active.insert(allowed_groups.begin(), allowed_groups.end());
    }
    for (const auto& g : extra_enable)  active.insert(g);
    for (const auto& g : extra_disable) active.erase(g);

    std::set<std::string> name_filter(explicit_tool_whitelist.begin(),
                                      explicit_tool_whitelist.end());
    const bool use_name_filter = !name_filter.empty();

    std::vector<ToolDefinition> defs;
    defs.reserve(tools_.size());
    for (const auto& t : tools_) {
        if (!active.count(t->category())) continue;
        if (use_name_filter && !name_filter.count(t->name())) continue;
        defs.push_back(t->definition());
    }
    return defs;
}

} // namespace agentcpp::tools
