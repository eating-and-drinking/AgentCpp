#pragma once
#include "Tool.hpp"

namespace agentcpp::tools {

class GlobTool : public Tool {
public:
    std::string name()        const override { return "Glob"; }
    std::string category()    const override { return "files"; }
    std::string description() const override;
    json        inputSchema() const override;

    ToolCallResult execute(const json& input, const ToolContext& ctx) override;

private:
    bool matchGlob(const std::string& pattern, const std::string& str) const;
    std::vector<std::filesystem::path> glob(
        const std::filesystem::path& base,
        const std::string& pattern,
        int limit
    ) const;
};

} // namespace agentcpp::tools
