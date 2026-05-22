#include <tools/GlobTool.hpp>
#include <utils/StringUtils.hpp>
#include <utils/Logger.hpp>
#include <algorithm>
#include <sstream>

namespace agentcpp::tools {

std::string GlobTool::description() const {
    return
        "Find files matching a glob pattern. Searches recursively under the given path. "
        "Pattern supports * (any chars except /), ** (any path), ? (single char), "
        "and [] character classes. Returns up to 100 matching paths.";
}

json GlobTool::inputSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"pattern", {
                {"type", "string"},
                {"description", "Glob pattern to match (e.g. '**/*.ts', 'src/*.cpp')."}
            }},
            {"path", {
                {"type", "string"},
                {"description", "Directory to search in. Defaults to current working directory."}
            }},
            {"limit", {
                {"type", "integer"},
                {"description", "Maximum number of results (default 100)."},
                {"default", 100}
            }}
        }},
        {"required", json::array({"pattern"})}
    };
}

ToolCallResult GlobTool::execute(const json& input, const ToolContext& ctx) {
    std::string pattern  = input.value("pattern", "");
    std::string base_str = input.value("path", "");
    int limit            = std::min(input.value("limit", 100), 1000);

    if (pattern.empty()) return ToolCallResult::error("pattern is required");

    std::filesystem::path base = base_str.empty() ? ctx.cwd : std::filesystem::path(base_str);
    if (base.is_relative()) base = ctx.cwd / base;
    if (!std::filesystem::exists(base)) {
        return ToolCallResult::error("Path does not exist: " + base.string());
    }

    auto matches = glob(base, pattern, limit);

    if (matches.empty()) {
        return ToolCallResult::ok("No files found matching: " + pattern);
    }

    // Sort results
    std::sort(matches.begin(), matches.end());

    std::ostringstream out;
    out << matches.size() << " file(s) matching \"" << pattern << "\":\n\n";
    for (const auto& p : matches) {
        // Show path relative to the search base
        std::error_code ec;
        auto rel = std::filesystem::relative(p, base, ec);
        out << (ec ? p.string() : rel.string()) << "\n";
    }
    if (static_cast<int>(matches.size()) >= limit) {
        out << "\n(Results capped at " << limit << ". Use a more specific pattern.)";
    }

    return ToolCallResult::ok(out.str());
}

// ── Glob matching ─────────────────────────────────────────────────────────────

// Match a single path component against a pattern component
// Supports *, ?, [abc], [!abc]
static bool matchComponent(std::string_view pattern, std::string_view str) {
    if (pattern == "*") return str.find('/') == std::string_view::npos;

    size_t pi = 0, si = 0;
    while (pi < pattern.size() && si < str.size()) {
        char p = pattern[pi];
        if (p == '*') {
            // Match zero or more characters (not /)
            ++pi;
            if (pi == pattern.size()) return str.find('/', si) == std::string_view::npos;
            while (si < str.size()) {
                if (str[si] != '/' && matchComponent(pattern.substr(pi), str.substr(si)))
                    return true;
                ++si;
            }
            return false;
        } else if (p == '?') {
            if (str[si] == '/') return false;
            ++pi; ++si;
        } else if (p == '[') {
            size_t close = pattern.find(']', pi + 1);
            if (close == std::string_view::npos) {
                if (p != str[si]) return false;
                ++pi; ++si;
                continue;
            }
            bool negate = (pi + 1 < pattern.size() && pattern[pi + 1] == '!');
            size_t start = pi + 1 + (negate ? 1 : 0);
            std::string_view cls = pattern.substr(start, close - start);
            bool found = cls.find(str[si]) != std::string_view::npos;
            if (negate ? found : !found) return false;
            pi = close + 1; ++si;
        } else {
            if (p != str[si]) return false;
            ++pi; ++si;
        }
    }
    while (pi < pattern.size() && pattern[pi] == '*') ++pi;
    return pi == pattern.size() && si == str.size();
}

// Match a full relative path against a glob pattern (supporting **)
static bool globMatch(std::string_view pattern, std::string_view rel_path) {
    // Split both into components
    auto splitPath = [](std::string_view sv) {
        std::vector<std::string_view> parts;
        size_t pos = 0;
        while (pos < sv.size()) {
            size_t slash = sv.find('/', pos);
            if (slash == std::string_view::npos) {
                parts.push_back(sv.substr(pos));
                break;
            }
            parts.push_back(sv.substr(pos, slash - pos));
            pos = slash + 1;
        }
        return parts;
    };

    auto ppat  = splitPath(pattern);
    auto ppath = splitPath(rel_path);

    // DP matching with ** support
    // Simple recursive implementation
    std::function<bool(size_t, size_t)> match = [&](size_t pi, size_t si) -> bool {
        if (pi == ppat.size()) return si == ppath.size();
        if (ppat[pi] == "**") {
            // ** matches zero or more path components
            if (match(pi + 1, si)) return true;
            for (size_t k = si; k < ppath.size(); ++k) {
                if (match(pi + 1, k + 1)) return true;
            }
            return false;
        }
        if (si == ppath.size()) return false;
        if (!matchComponent(ppat[pi], ppath[si])) return false;
        return match(pi + 1, si + 1);
    };

    return match(0, 0);
}

std::vector<std::filesystem::path> GlobTool::glob(
    const std::filesystem::path& base,
    const std::string& pattern,
    int limit
) const {
    std::vector<std::filesystem::path> results;

    try {
        for (auto it = std::filesystem::recursive_directory_iterator(
                base,
                std::filesystem::directory_options::skip_permission_denied
            ); it != std::filesystem::recursive_directory_iterator(); ++it)
        {
            if (static_cast<int>(results.size()) >= limit) break;

            const auto& p = it->path();

            // Skip hidden dirs
            std::string fname = p.filename().string();
            if (!fname.empty() && fname[0] == '.') {
                if (it->is_directory()) it.disable_recursion_pending();
                continue;
            }

            // Skip node_modules and similar
            if (fname == "node_modules" || fname == ".git" || fname == "build" || fname == "dist") {
                if (it->is_directory()) it.disable_recursion_pending();
                continue;
            }

            // Get relative path from base
            std::error_code ec;
            auto rel = std::filesystem::relative(p, base, ec);
            if (ec) continue;

            std::string rel_str = rel.string();
            // Normalise separators
            std::replace(rel_str.begin(), rel_str.end(), '\\', '/');

            if (globMatch(pattern, rel_str)) {
                results.push_back(p);
            }
        }
    } catch (const std::filesystem::filesystem_error&) {}

    return results;
}

bool GlobTool::matchGlob(const std::string& pattern, const std::string& str) const {
    return globMatch(pattern, str);
}

} // namespace agentcpp::tools
