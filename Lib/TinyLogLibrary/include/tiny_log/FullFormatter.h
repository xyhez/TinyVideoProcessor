#pragma once

#include <filesystem>
#include <sstream>
#include <string>

#include "Formatter.h"
#include "Level.h"
#include "detail/TimeUtils.h"

namespace logging {

/**
 * @brief 完整格式：[time] [location] [Level] message
 *
 * 适合文件输出。
 */
class FullFormatter : public Formatter {
public:
    std::string Format(const Record& record) override {
        const auto file = std::filesystem::path(record.GetSourceLocation().file).filename().string();
        std::ostringstream oss;
        oss << "[" << detail::TimeToString(record.GetTime()) << "] "
            << "[" << record.GetLoggerName() << "] "
            << "[" << ToString(record.GetLevel()) << "] "
            << "[" << file << ":" << record.GetSourceLocation().line << "] "
            << record.GetMessage();
        return oss.str();
    }
};

} // namespace logging
