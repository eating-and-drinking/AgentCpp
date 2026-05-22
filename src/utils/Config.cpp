#include <utils/Config.hpp>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <iostream>

namespace agentcpp::utils {

Config& Config::instance() {
    static Config inst;
    return inst;
}

void Config::load(const std::filesystem::path& explicit_path) {
    std::filesystem::path path = explicit_path;
    if (path.empty()) {
        auto found = findDotEnv();
        if (found) path = *found;
    }
    if (!path.empty() && std::filesystem::exists(path)) {
        parseDotEnvFile(path);
    }
}

std::optional<std::string> Config::get(const std::string& key) const {
    // Env var takes priority
    if (const char* v = std::getenv(key.c_str())) {
        return std::string(v);
    }
    auto it = values_.find(key);
    if (it != values_.end()) return it->second;
    return std::nullopt;
}

std::string Config::getOr(const std::string& key, std::string def) const {
    return get(key).value_or(std::move(def));
}

std::string Config::require(const std::string& key) const {
    auto v = get(key);
    if (!v) throw std::runtime_error("Required config key missing: " + key);
    return *v;
}

std::string Config::apiKey() const {
    // Support both ANTHROPIC_API_KEY and CLAUDE_API_KEY
    auto v = get("ANTHROPIC_API_KEY");
    if (v) return *v;
    return getOr("CLAUDE_API_KEY", "");
}

std::string Config::baseUrl() const {
    return getOr("ANTHROPIC_BASE_URL", "https://api.anthropic.com");
}

std::string Config::model() const {
    auto v = get("CLAUDE_MODEL");
    if (v) return *v;
    return getOr("ANTHROPIC_MODEL", "claude-opus-4-5");
}

std::string Config::apiVersion() const {
    return getOr("ANTHROPIC_API_VERSION", "2023-06-01");
}

bool Config::debugMode() const {
    auto v = get("CLAUDE_DEBUG");
    return v && (*v == "1" || *v == "true" || *v == "yes");
}

bool Config::headlessMode() const {
    auto v = get("CLAUDE_HEADLESS");
    return v && (*v == "1" || *v == "true" || *v == "yes");
}

int Config::maxTokens() const {
    auto v = get("CLAUDE_MAX_TOKENS");
    if (v) {
        try { return std::stoi(*v); } catch (...) {}
    }
    return 8096;
}

std::string Config::skillsDir() const {
    return getOr("AGENTCPP_SKILLS_DIR", "");
}

std::string Config::memoryDir() const {
    return getOr("AGENTCPP_MEMORY_DIR", "");
}

bool Config::memoryEnabled() const {
    auto v = get("AGENTCPP_MEMORY");
    if (!v) return true;
    // Treat 0/false/no/off as disabled
    std::string s = *v;
    for (auto& c : s) c = static_cast<char>(::tolower(c));
    return !(s == "0" || s == "false" || s == "no" || s == "off");
}

// ── Private helpers ───────────────────────────────────────────────────────────

std::optional<std::filesystem::path> Config::findDotEnv() {
    // 1. Current working directory
    auto cwd_env = std::filesystem::current_path() / ".env";
    if (std::filesystem::exists(cwd_env)) return cwd_env;

    // 2. Home directory
    if (const char* home = std::getenv("HOME")) {
        auto home_env = std::filesystem::path(home) / ".env";
        if (std::filesystem::exists(home_env)) return home_env;
    }
    return std::nullopt;
}

void Config::parseDotEnvFile(const std::filesystem::path& path) {
    std::ifstream f(path);
    if (!f) return;

    std::string line;
    while (std::getline(f, line)) {
        // Strip comments and blank lines
        auto comment = line.find('#');
        if (comment != std::string::npos) line = line.substr(0, comment);

        // Trim whitespace
        auto trim = [](std::string s) {
            size_t b = s.find_first_not_of(" \t\r\n");
            size_t e = s.find_last_not_of(" \t\r\n");
            return (b == std::string::npos) ? "" : s.substr(b, e - b + 1);
        };
        line = trim(line);
        if (line.empty()) continue;

        // Strip optional "export " prefix
        if (line.rfind("export ", 0) == 0) line = line.substr(7);

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key   = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));

        // Strip surrounding quotes
        if (value.size() >= 2) {
            char q = value.front();
            if ((q == '"' || q == '\'') && value.back() == q)
                value = value.substr(1, value.size() - 2);
        }

        if (!key.empty() && values_.find(key) == values_.end()) {
            // Don't overwrite env vars already in environment
            if (!std::getenv(key.c_str()))
                values_[key] = value;
        }
    }
}

} // namespace agentcpp::utils
        // Strip surrounding quotes
        if (value.size() >= 2) {
            char q = value.front();
            if ((q == '"' || q == '\'') && value.back() == q)
                value = value.substr(1, value.size() - 2);
        }

        if (!key.empty() && values_.find(key) == values_.end()) {
            // Don't overwrite env vars already in environment
            if (!std::getenv(key.c_str()))
                values_[key] = value;
        }
    }
}

} // namespace agentcpp::utils
