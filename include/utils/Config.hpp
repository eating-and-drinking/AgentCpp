#pragma once
#include <string>
#include <optional>
#include <unordered_map>
#include <filesystem>

namespace agentcpp::utils {

// Loads configuration from environment variables and .env files.
// Priority: process env > .env in cwd > .env in home dir
class Config {
public:
    static Config& instance();

    // Load .env file (call once at startup)
    void load(const std::filesystem::path& dotenv_path = "");

    // Get a config value (checks env vars first, then loaded .env)
    std::optional<std::string> get(const std::string& key) const;

    // Get with default
    std::string getOr(const std::string& key, std::string def) const;

    // Required - throws if missing
    std::string require(const std::string& key) const;

    // Convenience accessors
    std::string apiKey()     const;
    std::string baseUrl()    const;
    std::string model()      const;
    std::string apiVersion() const;
    bool        debugMode()  const;
    bool        headlessMode() const;
    int         maxTokens()  const;

    // Skills / memory directories (empty string means "not configured").
    std::string skillsDir()     const;   // AGENTCPP_SKILLS_DIR
    std::string memoryDir()     const;   // AGENTCPP_MEMORY_DIR
    bool        memoryEnabled() const;   // AGENTCPP_MEMORY=0 disables

private:
    Config() = default;
    std::unordered_map<std::string, std::string> values_;

    static std::optional<std::filesystem::path> findDotEnv();
    void parseDotEnvFile(const std::filesystem::path& path);
};

} // namespace agentcpp::utils
