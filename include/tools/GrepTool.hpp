#pragma once
#include "Tool.hpp"

namespace agentcpp::tools {

class GrepTool : public Tool {
public:
    std::string name()        const override { return "Grep"; }
    std::string description() const override;
    json        inputSchema() const override;

    ToolCallResult execute(const json& input, const ToolContext& ctx) override;

private:
    struct Match {
        std::filesystem::path file;
        int                   line_no;
        std::string           line;
    };

    std::vector<Match> searchFile(
        const std::filesystem::path& path,
        const std::string& pattern,
        bool case_insensitive,
        bool fixed_strings
    ) const;
};

} // namespace agentcpp::tools
