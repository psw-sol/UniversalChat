#include "Logger.hpp"
#include <spdlog/sinks/basic_file_sink.h>
#include <iostream>
#include <vector>

namespace chat {

std::shared_ptr<spdlog::logger> Logger::logger_ = nullptr;
bool Logger::initialized_ = false;

void Logger::init(const std::string& log_file,
                  const std::string& level,
                  bool console_output,
                  size_t max_file_size_mb,
                  size_t max_files) {
    if (initialized_) {
        return;
    }

    try {
        std::vector<spdlog::sink_ptr> sinks;

        // Console sink
        if (console_output) {
            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");
            sinks.push_back(console_sink);
        }

        // File sink
        if (!log_file.empty()) {
            auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                log_file,
                max_file_size_mb * 1024 * 1024,  // Convert MB to bytes
                max_files
            );
            file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] %v");
            sinks.push_back(file_sink);
        }

        // Create logger
        logger_ = std::make_shared<spdlog::logger>("chat_server", sinks.begin(), sinks.end());

        // Set level
        setLevel(level);

        // Set as default logger
        spdlog::set_default_logger(logger_);

        // Flush on error
        logger_->flush_on(spdlog::level::err);

        initialized_ = true;

        LOG_INFO("Logger initialized");

    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "Logger initialization failed: " << ex.what() << std::endl;
        throw;
    }
}

std::shared_ptr<spdlog::logger>& Logger::get() {
    if (!initialized_) {
        // Create a default console logger if not initialized
        logger_ = spdlog::stdout_color_mt("chat_server");
        logger_->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");
        initialized_ = true;
    }
    return logger_;
}

void Logger::setLevel(const std::string& level) {
    if (!logger_) return;

    if (level == "trace") {
        logger_->set_level(spdlog::level::trace);
    } else if (level == "debug") {
        logger_->set_level(spdlog::level::debug);
    } else if (level == "info") {
        logger_->set_level(spdlog::level::info);
    } else if (level == "warn" || level == "warning") {
        logger_->set_level(spdlog::level::warn);
    } else if (level == "error") {
        logger_->set_level(spdlog::level::err);
    } else if (level == "critical") {
        logger_->set_level(spdlog::level::critical);
    } else if (level == "off") {
        logger_->set_level(spdlog::level::off);
    } else {
        logger_->set_level(spdlog::level::info);
    }
}

void Logger::shutdown() {
    if (logger_) {
        logger_->flush();
    }
    spdlog::shutdown();
    initialized_ = false;
}

} // namespace chat
