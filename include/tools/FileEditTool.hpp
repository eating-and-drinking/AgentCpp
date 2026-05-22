#pragma once
#include "Tool.hpp"

namespace agentcpp::tools {

class FileEditTool : public Tool {
public:
    std::string name()        const override { return "Edit"; }
    std::string category()    const override { return "files"; }
    std::string description() const override;
    json        inputSchema() const override;

    ToolCallResult execute(const json& input, const ToolContext& ctx) override;

private:
    ToolCallResult applyEdit(
        const std::filesystem::path& path,
        const std::string& old_string,
        const std::string& new_string,
        bool replace_all
    );
};

} // namespace agentcpp::tools
