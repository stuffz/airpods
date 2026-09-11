#pragma once

#include "core/local_time.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>

enum class LogLevel
{
    Info,
    Debug,
    Warn,
    Error
};

// Diagnostics go to stderr so the battery lines on stdout stay pipeable.
class Logger
{
public:
    static Logger &Instance()
    {
        static Logger instance;
        return instance;
    }

    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;

    void SetDebugMode(bool enabled) { debugMode = enabled; }

    void Log(LogLevel level, const std::string &message)
    {
        if (level == LogLevel::Debug && !debugMode)
        {
            return;
        }

        std::lock_guard<std::mutex> lock(logMutex);
        // endl, not "\n": diagnostics must appear immediately even when piped.
        std::cerr << "[" << Timestamp() << "] [" << LevelToString(level) << "] " << message
                  << std::endl;
    }

private:
    Logger() = default;

    ~Logger() = default;

    static std::string Timestamp()
    {
        const auto now = std::chrono::system_clock::now();
        const auto timeT = std::chrono::system_clock::to_time_t(now);

        const std::tm tm = core::LocalTime(timeT);

        std::ostringstream oss;
        oss << std::put_time(&tm, "%H:%M:%S");
        return oss.str();
    }

    static constexpr std::string_view LevelToString(LogLevel level)
    {
        constexpr std::array<std::string_view, 4> levelNames = {"INFO", "DEBUG", "WARN", "ERROR"};
        return levelNames[static_cast<size_t>(level)];
    }

    bool debugMode = false;
    std::mutex logMutex;
};

#define LOG_INFO(msg) Logger::Instance().Log(LogLevel::Info, msg)
#define LOG_DEBUG(msg) Logger::Instance().Log(LogLevel::Debug, msg)
#define LOG_WARN(msg) Logger::Instance().Log(LogLevel::Warn, msg)
#define LOG_ERROR(msg) Logger::Instance().Log(LogLevel::Error, msg)
