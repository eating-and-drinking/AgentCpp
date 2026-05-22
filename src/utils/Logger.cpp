#include <utils/Logger.hpp>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace agentcpp::utils {

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

void Logger::setLevel(LogLevel lvl) { level_ = lvl; }
void Logger::setSilent(bool s)      { silent_ = s; }

void Logger::setLogFile(const std::filesystem::path& path) {
    std::lock_guard<std::mutex> lk(mu_);
    if (file_.is_open()) file_.close();
    file_.open(path, std::ios::app);
}

void Logger::debug(std::string_view msg) { log(LogLevel::Debug, msg); }
void Logger::info(std::string_view msg)  { log(LogLevel::Info,  msg); }
void Logger::warn(std::string_view msg)  { log(LogLevel::Warn,  msg); }
void Logger::error(std::string_view msg) { log(LogLevel::Error, msg); }

void Logger::log(LogLevel lvl, std::string_view msg) {
    if (lvl < level_) return;

    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif

    const char* level_str = "INFO ";
    switch (lvl) {
        case LogLevel::Debug: level_str = "DEBUG"; break;
        case LogLevel::Info:  level_str = "INFO "; break;
        case LogLevel::Warn:  level_str = "WARN "; break;
        case LogLevel::Error: level_str = "ERROR"; break;
    }

    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%H:%M:%S")
        << " [" << level_str << "] "
        << msg << "\n";

    std::string line = oss.str();

    std::lock_guard<std::mutex> lk(mu_);

    // Write to file if configured
    if (file_.is_open()) {
        file_ << line;
        file_.flush();
    }

    // Write to stderr (unless silent or just debug)
    if (!silent_ && lvl >= LogLevel::Warn) {
        std::cerr << line;
    } else if (!silent_ && lvl == LogLevel::Debug) {
        // Only print debug messages if level is set to Debug explicitly
        if (level_ == LogLevel::Debug) {
            std::cerr << line;
        }
    }
}

} // namespace agentcpp::utils
