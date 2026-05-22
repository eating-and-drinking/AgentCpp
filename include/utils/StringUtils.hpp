#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace agentcpp::utils {

// Split string by delimiter
std::vector<std::string> split(std::string_view s, char delim);

// Trim whitespace from both ends
std::string trim(std::string_view s);
std::string ltrim(std::string_view s);
std::string rtrim(std::string_view s);

// Check if string starts/ends with prefix/suffix
bool startsWith(std::string_view s, std::string_view prefix);
bool endsWith(std::string_view s, std::string_view suffix);

// Replace all occurrences of `from` with `to` in `s`
std::string replaceAll(std::string s, std::string_view from, std::string_view to);

// Join vector of strings with separator
std::string join(const std::vector<std::string>& parts, std::string_view sep);

// Truncate string to max_len, adding "..." if truncated
std::string truncate(std::string_view s, std::size_t max_len);

// Simple line-number annotation: prepend "N | " to each line
std::string addLineNumbers(std::string_view s, int start = 1);

// ANSI colour helpers (for headless output)
std::string bold(std::string_view s);
std::string dim(std::string_view s);
std::string cyan(std::string_view s);
std::string green(std::string_view s);
std::string yellow(std::string_view s);
std::string red(std::string_view s);
std::string magenta(std::string_view s);

// Word-wrap to width columns (respects ANSI escape codes approximately)
std::string wordWrap(std::string_view s, int width);

// Strip ANSI escape codes
std::string stripAnsi(std::string_view s);

// Format byte size human-readably (e.g. "1.2 MB")
std::string formatBytes(std::size_t bytes);

// Check whether a string looks like valid JSON
bool looksLikeJson(std::string_view s);

} // namespace agentcpp::utils
