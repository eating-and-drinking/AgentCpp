#pragma once
#include <string>
#include <string_view>
#include <mutex>
#include <fstream>
#include <filesystem>

namespace agentcpp::utils {

enum class LogLevel { Debug, Info, Warn, Error };

class Logger {
public:
    static Logger& instance();

    void setLevel(LogLevel lvl);
    void setLogFile(const std::filesystem::path& path);
    void setSilent(bool silent);  // suppress stderr output (headless mode)

    void debug(std::string_view msg);
    void info(std::string_view msg);
    void warn(std::string_view msg);
    void error(std::string_view msg);

    // Formatted variants
    template<typename... Args>
    void debugf(std::string_view fmt, Args&&... args);
    template<typename... Args>
    void infof(std::string_view fmt, Args&&... args);
    template<typename... Args>
    void warnf(std::string_view fmt, Args&&... args);
    template<typename... Args>
    void errorf(std::string_view fmt, Args&&... args);

private:
    Logger() = default;
    void log(LogLevel lvl, std::string_view msg);

    LogLevel           level_  = LogLevel::Info;
    bool               silent_ = false;
    std::mutex         mu_;
    std::ofstream      file_;
};

// Convenience macros
#define LOG_DEBUG(msg) agentcpp::utils::Logger::instance().debug(msg)
#define LOG_INFO(msg)  agentcpp::utils::Logger::instance().info(msg)
#define LOG_WARN(msg)  agentcpp::utils::Logger::instance().warn(msg)
#define LOG_ERROR(msg) agentcpp::utils::Logger::instance().error(msg)

} // namespace agentcpp::utils
