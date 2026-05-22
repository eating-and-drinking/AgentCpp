#pragma once
#include "Tool.hpp"

namespace agentcpp::tools {

class FileReadTool : public Tool {
public:
    std::string name()        const override { return "Read"; }
    std::string category()    const override { return "files"; }
    std::string description() const override;
    json        inputSchema() const override;

    ToolCallResult execute(const json& input, const ToolContext& ctx) override;

private:
    static constexpr int kMaxFileSizeBytes = 1 * 1024 * 1024;
    static constexpr int kDefaultMaxLines  = 2000;

    ToolCallResult readTextFile(const std::filesystem::path& path, int offset, int limit);
    ToolCallResult readBinaryFile(const std::filesystem::path& path);
    bool           isBinaryFile(const std::filesystem::path& path) const;
};

} // namespace agentcpp::tools
