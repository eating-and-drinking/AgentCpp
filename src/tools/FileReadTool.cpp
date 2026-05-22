#include <tools/FileReadTool.hpp>
#include <utils/StringUtils.hpp>
#include <utils/Logger.hpp>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>

namespace agentcpp::tools {

std::string FileReadTool::description() const {
    return
        "Read the contents of a file. Returns the file content with line numbers. "
        "Use 'offset' and 'limit' to read a specific range of lines (1-indexed). "
        "Files larger than 1 MB are rejected; use 'limit' to read in chunks.";
}

json FileReadTool::inputSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"file_path", {
                {"type", "string"},
                {"description", "Absolute or relative path to the file to read."}
            }},
            {"offset", {
                {"type", "integer"},
                {"description", "1-indexed line number to start reading from."},
                {"default", 1}
            }},
            {"limit", {
                {"type", "integer"},
                {"description", "Maximum number of lines to read (default 2000)."},
                {"default", 2000}
            }}
        }},
        {"required", json::array({"file_path"})}
    };
}

ToolCallResult FileReadTool::execute(const json& input, const ToolContext& ctx) {
    std::string path_str = input.value("file_path", "");
    int offset           = std::max(1, input.value("offset", 1));
    int limit            = std::min(input.value("limit", kDefaultMaxLines), kDefaultMaxLines);

    if (path_str.empty()) return ToolCallResult::error("file_path is required");

    // Resolve relative paths against cwd
    std::filesystem::path path = path_str;
    if (path.is_relative()) path = ctx.cwd / path;
    path = std::filesystem::weakly_canonical(path);

    if (!std::filesystem::exists(path)) {
        return ToolCallResult::error("File not found: " + path.string());
    }
    if (std::filesystem::is_directory(path)) {
        // List directory
        std::ostringstream out;
        out << "Directory listing: " << path.string() << "\n\n";
        std::vector<std::filesystem::directory_entry> entries;
        for (auto& e : std::filesystem::directory_iterator(path)) entries.push_back(e);
        std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
            return a.path().filename() < b.path().filename();
        });
        for (const auto& e : entries) {
            out << (e.is_directory() ? "d " : "f ");
            out << e.path().filename().string();
            if (!e.is_directory()) out << " (" << utils::formatBytes(e.file_size()) << ")";
            out << "\n";
        }
        return ToolCallResult::ok(out.str());
    }

    auto file_size = std::filesystem::file_size(path);
    if (file_size > static_cast<uintmax_t>(kMaxFileSizeBytes)) {
        return ToolCallResult::error(
            "File too large (" + utils::formatBytes(file_size) +
            "). Use offset/limit to read in chunks, or use Bash(head/tail/sed)."
        );
    }

    if (isBinaryFile(path)) {
        return readBinaryFile(path);
    }
    return readTextFile(path, offset, limit);
}

ToolCallResult FileReadTool::readTextFile(
    const std::filesystem::path& path,
    int offset,
    int limit
) {
    std::ifstream f(path);
    if (!f) return ToolCallResult::error("Cannot open file: " + path.string());

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(f, line)) lines.push_back(line);

    int total = static_cast<int>(lines.size());
    int start = std::max(0, offset - 1);           // convert 1-indexed to 0-indexed
    int end   = std::min(start + limit, total);

    if (start >= total) {
        return ToolCallResult::error(
            "Offset " + std::to_string(offset) +
            " exceeds file length (" + std::to_string(total) + " lines)"
        );
    }

    std::ostringstream out;
    out << path.string() << " (" << total << " lines total)\n\n";

    for (int i = start; i < end; ++i) {
        out << std::setw(6) << (i + 1) << "\t" << lines[i] << "\n";
    }

    if (end < total) {
        out << "\n... (" << (total - end) << " more lines, use offset=" << (end + 1) << " to continue)";
    }

    return ToolCallResult::ok(out.str());
}

ToolCallResult FileReadTool::readBinaryFile(const std::filesystem::path& path) {
    return ToolCallResult::error(
        "Binary file: " + path.string() +
        ". Use Bash(xxd/od) to inspect binary content."
    );
}

bool FileReadTool::isBinaryFile(const std::filesystem::path& path) const {
    static const std::vector<std::string> binary_exts = {
        ".png", ".jpg", ".jpeg", ".gif", ".bmp", ".ico", ".webp",
        ".pdf", ".zip", ".tar", ".gz", ".bz2", ".xz", ".7z",
        ".exe", ".dll", ".so", ".dylib", ".a", ".o",
        ".mp3", ".mp4", ".avi", ".mov", ".wav", ".flac",
        ".wasm", ".bin"
    };
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    for (const auto& e : binary_exts) if (ext == e) return true;

    // Sniff first 512 bytes for null bytes
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    char buf[512];
    f.read(buf, sizeof(buf));
    size_t n = static_cast<size_t>(f.gcount());
    for (size_t i = 0; i < n; ++i) {
        if (buf[i] == '\0') return true;
    }
    return false;
}

} // namespace agentcpp::tools
