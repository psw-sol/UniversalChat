#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <memory>
#include <string>

namespace chat {

class Logger {
public:
    // Initialize the logger
    static void init(const std::string& log_file = "",
                     const std::string& level = "info",
                     bool console_output = true,
                     size_t max_file_size_mb = 100,
                     size_t max_files = 10);

    // Get the logger instance
    static std::shared_ptr<spdlog::logger>& get();

    // Set log level
    static void setLevel(const std::string& level);

    // Shutdown logger
    static void shutdown();

private:
    static std::shared_ptr<spdlog::logger> logger_;
    static bool initialized_;
};

// Convenience macros for logging
#define LOG_TRACE(...)    SPDLOG_LOGGER_TRACE(chat::Logger::get(), __VA_ARGS__)
#define LOG_DEBUG(...)    SPDLOG_LOGGER_DEBUG(chat::Logger::get(), __VA_ARGS__)
#define LOG_INFO(...)     SPDLOG_LOGGER_INFO(chat::Logger::get(), __VA_ARGS__)
#define LOG_WARN(...)     SPDLOG_LOGGER_WARN(chat::Logger::get(), __VA_ARGS__)
#define LOG_ERROR(...)    SPDLOG_LOGGER_ERROR(chat::Logger::get(), __VA_ARGS__)
#define LOG_CRITICAL(...) SPDLOG_LOGGER_CRITICAL(chat::Logger::get(), __VA_ARGS__)

} // namespace chat
