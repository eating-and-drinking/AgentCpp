#include <tools/FileEditTool.hpp>
#include <utils/Logger.hpp>
#include <utils/StringUtils.hpp>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace agentcpp::tools {

std::string FileEditTool::description() const {
    return
        "Make targeted edits to an existing file using exact string replacement. "
        "Finds old_string in the file and replaces it with new_string. "
        "old_string must appear exactly once unless replace_all is true. "
        "The file must already exist; use Write to create new files. "
        "Preserves all existing whitespace and formatting outside the replaced section.";
}

json FileEditTool::inputSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"file_path", {
                {"type", "string"},
                {"description", "Path to the file to edit."}
            }},
            {"old_string", {
                {"type", "string"},
                {"description", "The exact string to find and replace. Must be unique in the file unless replace_all is true."}
            }},
            {"new_string", {
                {"type", "string"},
                {"description", "The replacement string."}
            }},
            {"replace_all", {
                {"type", "boolean"},
                {"description", "If true, replace all occurrences. Default false."},
                {"default", false}
            }}
        }},
        {"required", json::array({"file_path", "old_string", "new_string"})}
    };
}

ToolCallResult FileEditTool::execute(const json& input, const ToolContext& ctx) {
    std::string path_str   = input.value("file_path", "");
    std::string old_string = input.value("old_string", "");
    std::string new_string = input.value("new_string", "");
    bool replace_all       = input.value("replace_all", false);

    if (path_str.empty())   return ToolCallResult::error("file_path is required");
    if (old_string.empty()) return ToolCallResult::error("old_string is required");
    if (old_string == new_string) return ToolCallResult::error("old_string and new_string are identical — nothing to change");

    if (ctx.read_only) return ToolCallResult::error("Cannot edit files in read-only mode");

    std::filesystem::path path = path_str;
    if (path.is_relative()) path = ctx.cwd / path;

    if (!std::filesystem::exists(path)) {
        return ToolCallResult::error("File not found: " + path.string() +
            ". Use the Write tool to create new files.");
    }

    return applyEdit(path, old_string, new_string, replace_all);
}

ToolCallResult FileEditTool::applyEdit(
    const std::filesystem::path& path,
    const std::string& old_string,
    const std::string& new_string,
    bool replace_all
) {
    // Read file
    std::ifstream f(path, std::ios::binary);
    if (!f) return ToolCallResult::error("Cannot read file: " + path.string());
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    f.close();

    // Count occurrences
    size_t count = 0;
    size_t pos   = 0;
    while ((pos = content.find(old_string, pos)) != std::string::npos) {
        ++count;
        pos += old_string.size();
    }

    if (count == 0) {
        return ToolCallResult::error(
            "old_string not found in " + path.string() + ".\n"
            "Make sure it matches exactly including whitespace and indentation."
        );
    }

    if (!replace_all && count > 1) {
        return ToolCallResult::error(
            "old_string appears " + std::to_string(count) + " times in " + path.string() + ". "
            "Provide more surrounding context to make it unique, or set replace_all=true."
        );
    }

    // Apply replacement
    std::string result;
    result.reserve(content.size() + (new_string.size() > old_string.size()
        ? (new_string.size() - old_string.size()) * count : 0));

    pos = 0;
    size_t replacements = 0;
    while (true) {
        size_t found = content.find(old_string, pos);
        if (found == std::string::npos) { result += content.substr(pos); break; }
        result += content.substr(pos, found - pos);
        result += new_string;
        pos = found + old_string.size();
        ++replacements;
        if (!replace_all) break;
    }
    if (!replace_all) result += content.substr(pos);

    // Write back
    std::ofstream out(path, std::ios::binary);
    if (!out) return ToolCallResult::error("Cannot write file: " + path.string());
    out << result;
    out.close();

    LOG_INFO("FileEditTool: edited " + path.string());

    return ToolCallResult::ok(
        "Edited " + path.string() +
        " (" + std::to_string(replacements) + " replacement" +
        (replacements > 1 ? "s" : "") + ")"
    );
}

} // namespace agentcpp::tools
