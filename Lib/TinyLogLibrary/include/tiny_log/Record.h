#pragma once

#include <chrono>
#include <string>
#include <utility>

#include "Level.h"
#include "SourceLocation.h"

namespace logging {

/**
 * @brief 一条日志的数据包
 */
class Record {
public:
    Record(Level level,
           std::string message,
           std::string loggerName,
           std::chrono::system_clock::time_point time,
           SourceLocation sourceLocation)
        : level_(level)
        , message_(std::move(message))
        , logger_name_(std::move(loggerName))
        , time_(time)
        , source_location_(sourceLocation) {}

    Record() = default;
    ~Record() = default;

    Level GetLevel() const { return level_; }
    std::chrono::system_clock::time_point GetTime() const { return time_; }
    const std::string& GetMessage() const { return message_; }
    const std::string& GetLoggerName() const { return logger_name_; }
    const SourceLocation& GetSourceLocation() const { return source_location_; }

private:
    Level level_ = Level::Info;
    std::string message_;
    std::string logger_name_ = "default";
    std::chrono::system_clock::time_point time_ = std::chrono::system_clock::now();
    SourceLocation source_location_;
};

} // namespace logging
