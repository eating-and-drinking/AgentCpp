#include <tools/GrepTool.hpp>
#include <utils/StringUtils.hpp>
#include <utils/Logger.hpp>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <regex>
#include <algorithm>

namespace agentcpp::tools {

std::string GrepTool::description() const {
    return
        "Search for a pattern in files using regular expressions. "
        "Returns matching lines with file path and line number. "
        "Searches files in the given path recursively. "
        "Use fixed_strings=true to search for literal text without regex.";
}

json GrepTool::inputSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"pattern", {
                {"type", "string"},
                {"description", "Regular expression (or literal string if fixed_strings=true) to search for."}
            }},
            {"path", {
                {"type", "string"},
                {"description", "File or directory to search. Defaults to current working directory."}
            }},
            {"include", {
                {"type", "string"},
                {"description", "File glob pattern to include (e.g. '*.ts', '*.cpp')."}
            }},
            {"case_insensitive", {
                {"type", "boolean"},
                {"description", "Case-insensitive matching (default false)."},
                {"default", false}
            }},
            {"fixed_strings", {
                {"type", "boolean"},
                {"description", "Treat pattern as a literal string, not a regex (default false)."},
                {"default", false}
            }},
            {"limit", {
                {"type", "integer"},
                {"description", "Maximum number of matches to return (default 100)."},
                {"default", 100}
            }}
        }},
        {"required", json::array({"pattern"})}
    };
}

ToolCallResult GrepTool::execute(const json& input, const ToolContext& ctx) {
    std::string pattern         = input.value("pattern", "");
    std::string path_str        = input.value("path", "");
    std::string include_glob    = input.value("include", "");
    bool case_insensitive       = input.value("case_insensitive", false);
    bool fixed_strings          = input.value("fixed_strings", false);
    int limit                   = std::min(input.value("limit", 100), 1000);

    if (pattern.empty()) return ToolCallResult::error("pattern is required");

    std::filesystem::path search_path = path_str.empty() ? ctx.cwd : std::filesystem::path(path_str);
    if (search_path.is_relative()) search_path = ctx.cwd / search_path;

    if (!std::filesystem::exists(search_path)) {
        return ToolCallResult::error("Path does not exist: " + search_path.string());
    }

    std::vector<Match> all_matches;

    auto processFile = [&](const std::filesystem::path& file) {
        if (static_cast<int>(all_matches.size()) >= limit) return;
        auto file_matches = searchFile(file, pattern, case_insensitive, fixed_strings);
        for (auto& m : file_matches) {
            all_matches.push_back(std::move(m));
            if (static_cast<int>(all_matches.size()) >= limit) break;
        }
    };

    // Determine if include_glob filter is needed
    auto matchesInclude = [&](const std::filesystem::path& p) {
        if (include_glob.empty()) return true;
        std::string fname = p.filename().string();
        // Simple glob: support *.ext
        if (include_glob[0] == '*' && include_glob[1] == '.') {
            std::string ext = include_glob.substr(1); // ".ext"
            return p.extension().string() == ext;
        }
        return fname == include_glob;
    };

    if (std::filesystem::is_regular_file(search_path)) {
        processFile(search_path);
    } else {
        try {
            for (auto it = std::filesystem::recursive_directory_iterator(
                    search_path,
                    std::filesystem::directory_options::skip_permission_denied
                ); it != std::filesystem::recursive_directory_iterator(); ++it)
            {
                if (static_cast<int>(all_matches.size()) >= limit) break;
                const auto& p = it->path();
                std::string fname = p.filename().string();

                // Skip hidden and common non-source dirs
                if (!fname.empty() && fname[0] == '.') {
                    if (it->is_directory()) it.disable_recursion_pending();
                    continue;
                }
                if (fname == "node_modules" || fname == ".git" || fname == "build" || fname == "dist") {
                    if (it->is_directory()) it.disable_recursion_pending();
                    continue;
                }

                if (it->is_regular_file() && matchesInclude(p)) {
                    processFile(p);
                }
            }
        } catch (const std::filesystem::filesystem_error&) {}
    }

    if (all_matches.empty()) {
        return ToolCallResult::ok("No matches found for: " + pattern);
    }

    // Group by file for readability
    std::ostringstream out;
    out << all_matches.size() << " match(es) for \"" << pattern << "\":\n\n";

    std::string last_file;
    for (const auto& m : all_matches) {
        std::string fstr = m.file.string();
        if (fstr != last_file) {
            out << "\n" << fstr << ":\n";
            last_file = fstr;
        }
        out << std::setw(5) << m.line_no << ": " << m.line << "\n";
    }

    if (static_cast<int>(all_matches.size()) >= limit) {
        out << "\n(Results capped at " << limit << ". Use a more specific pattern or path.)";
    }

    return ToolCallResult::ok(out.str());
}

std::vector<GrepTool::Match> GrepTool::searchFile(
    const std::filesystem::path& path,
    const std::string& pattern,
    bool case_insensitive,
    bool fixed_strings
) const {
    std::vector<Match> results;

    std::ifstream f(path);
    if (!f) return results;

    try {
        std::regex re;
        std::string literal;

        if (fixed_strings) {
            literal = pattern;
        } else {
            auto flags = std::regex::ECMAScript;
            if (case_insensitive) flags |= std::regex::icase;
            re = std::regex(pattern, flags);
        }

        std::string line;
        int line_no = 0;
        while (std::getline(f, line)) {
            ++line_no;
            bool found = false;
            if (fixed_strings) {
                if (case_insensitive) {
                    std::string lo_line = line, lo_pat = literal;
                    std::transform(lo_line.begin(), lo_line.end(), lo_line.begin(), ::tolower);
                    std::transform(lo_pat.begin(), lo_pat.end(), lo_pat.begin(), ::tolower);
                    found = lo_line.find(lo_pat) != std::string::npos;
                } else {
                    found = line.find(literal) != std::string::npos;
                }
            } else {
                found = std::regex_search(line, re);
            }

            if (found) {
                std::string trimmed = line;
                if (trimmed.size() > 200) trimmed = trimmed.substr(0, 200) + "...";
                results.push_back({ path, line_no, trimmed });
            }
        }
    } catch (const std::regex_error&) {
        // Invalid regex — treat as fixed string fallback
        std::string line;
        int line_no = 0;
        f.clear(); f.seekg(0);
        while (std::getline(f, line)) {
            ++line_no;
            if (line.find(pattern) != std::string::npos) {
                results.push_back({ path, line_no, utils::truncate(line, 200) });
            }
        }
    }

    return results;
}

} // namespace agentcpp::tools
