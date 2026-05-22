#include <tools/FileWriteTool.hpp>
#include <utils/Logger.hpp>
#include <utils/StringUtils.hpp>
#include <fstream>
#include <filesystem>

namespace agentcpp::tools {

std::string FileWriteTool::description() const {
    return
        "Write content to a file, creating it and any parent directories if needed. "
        "Overwrites existing files. For targeted edits to existing files, prefer the Edit tool.";
}

json FileWriteTool::inputSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"file_path", {
                {"type", "string"},
                {"description", "Absolute or relative path to the file to write."}
            }},
            {"content", {
                {"type", "string"},
                {"description", "The content to write to the file."}
            }}
        }},
        {"required", json::array({"file_path", "content"})}
    };
}

ToolCallResult FileWriteTool::execute(const json& input, const ToolContext& ctx) {
    std::string path_str = input.value("file_path", "");
    std::string content  = input.value("content", "");

    if (path_str.empty()) return ToolCallResult::error("file_path is required");

    if (ctx.read_only) {
        return ToolCallResult::error("Cannot write files in read-only mode");
    }

    std::filesystem::path path = path_str;
    if (path.is_relative()) path = ctx.cwd / path;

    // Create parent directories
    auto parent = path.parent_path();
    if (!parent.empty() && !std::filesystem::exists(parent)) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            return ToolCallResult::error("Failed to create directories: " + ec.message());
        }
    }

    bool exists = std::filesystem::exists(path);

    std::ofstream f(path, std::ios::binary);
    if (!f) {
        return ToolCallResult::error("Cannot open file for writing: " + path.string());
    }
    f << content;
    if (!f) {
        return ToolCallResult::error("Write failed for: " + path.string());
    }
    f.close();

    LOG_INFO("FileWriteTool: wrote " + utils::formatBytes(content.size()) + " to " + path.string());

    std::string action = exists ? "Updated" : "Created";
    return ToolCallResult::ok(action + " " + path.string() +
        " (" + utils::formatBytes(content.size()) + ")");
}

} // namespace agentcpp::tools
