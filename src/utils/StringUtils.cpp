#include <utils/StringUtils.hpp>
#include <algorithm>
#include <sstream>
#include <regex>
#include <iomanip>

namespace agentcpp::utils {

std::vector<std::string> split(std::string_view s, char delim) {
    std::vector<std::string> result;
    std::string cur;
    for (char c : s) {
        if (c == delim) { result.push_back(cur); cur.clear(); }
        else cur += c;
    }
    result.push_back(cur);
    return result;
}

std::string ltrim(std::string_view s) {
    size_t b = s.find_first_not_of(" \t\r\n\v\f");
    return b == std::string_view::npos ? "" : std::string(s.substr(b));
}

std::string rtrim(std::string_view s) {
    size_t e = s.find_last_not_of(" \t\r\n\v\f");
    return e == std::string_view::npos ? "" : std::string(s.substr(0, e + 1));
}

std::string trim(std::string_view s) {
    return ltrim(rtrim(s));
}

bool startsWith(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

bool endsWith(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

std::string replaceAll(std::string s, std::string_view from, std::string_view to) {
    if (from.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

std::string join(const std::vector<std::string>& parts, std::string_view sep) {
    std::string result;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) result += sep;
        result += parts[i];
    }
    return result;
}

std::string truncate(std::string_view s, std::size_t max_len) {
    if (s.size() <= max_len) return std::string(s);
    return std::string(s.substr(0, max_len - 3)) + "...";
}

std::string addLineNumbers(std::string_view s, int start) {
    std::ostringstream out;
    std::string line;
    std::istringstream iss{std::string(s)};
    int n = start;
    while (std::getline(iss, line)) {
        out << std::setw(6) << n++ << "\t" << line << "\n";
    }
    return out.str();
}

// ANSI helpers
std::string bold(std::string_view s)    { return "\033[1m" + std::string(s) + "\033[0m"; }
std::string dim(std::string_view s)     { return "\033[2m" + std::string(s) + "\033[0m"; }
std::string cyan(std::string_view s)    { return "\033[36m" + std::string(s) + "\033[0m"; }
std::string green(std::string_view s)   { return "\033[32m" + std::string(s) + "\033[0m"; }
std::string yellow(std::string_view s)  { return "\033[33m" + std::string(s) + "\033[0m"; }
std::string red(std::string_view s)     { return "\033[31m" + std::string(s) + "\033[0m"; }
std::string magenta(std::string_view s) { return "\033[35m" + std::string(s) + "\033[0m"; }

std::string stripAnsi(std::string_view s) {
    // Remove ESC[...m sequences
    static const std::regex ansi_re("\033\\[[0-9;]*m");
    return std::regex_replace(std::string(s), ansi_re, "");
}

std::string wordWrap(std::string_view s, int width) {
    std::ostringstream out;
    std::istringstream in{std::string(s)};
    std::string para;
    while (std::getline(in, para)) {
        int col = 0;
        std::istringstream words{para};
        std::string word;
        bool first = true;
        while (words >> word) {
            int wlen = static_cast<int>(stripAnsi(word).size());
            if (!first && col + 1 + wlen > width) {
                out << "\n";
                col = 0;
                first = true;
            }
            if (!first) { out << " "; col++; }
            out << word;
            col += wlen;
            first = false;
        }
        out << "\n";
    }
    return out.str();
}

std::string formatBytes(std::size_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB"};
    double val = static_cast<double>(bytes);
    int u = 0;
    while (val >= 1024.0 && u < 3) { val /= 1024.0; u++; }
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1) << val << " " << units[u];
    return oss.str();
}

bool looksLikeJson(std::string_view s) {
    auto t = trim(s);
    return (!t.empty() && (t.front() == '{' || t.front() == '['));
}

} // namespace agentcpp::utils
