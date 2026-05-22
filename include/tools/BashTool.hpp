#pragma once
#include "Tool.hpp"

namespace agentcpp::tools {

class BashTool : public Tool {
public:
    std::string name()        const override { return "Bash"; }
    std::string category()    const override { return "code"; }
    std::string description() const override;
    json        inputSchema() const override;

    ToolCallResult execute(const json& input, const ToolContext& ctx) override;

private:
    struct ExecResult {
        std::string stdout_output;
        std::string stderr_output;
        int         exit_code = 0;
        bool        timed_out = false;
    };

    ExecResult runCommand(const std::string& command, int timeout_ms, const std::filesystem::path& cwd);
    bool       isDestructiveCommand(const std::string& command) const;
    bool       requiresPermission(const std::string& command) const;
};

} // namespace agentcpp::tools
