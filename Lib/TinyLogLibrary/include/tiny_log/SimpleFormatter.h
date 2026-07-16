#pragma once

#include <filesystem>
#include <sstream>
#include <string>

#include "Formatter.h"
#include "Level.h"

namespace logging {

/**
 * @brief 简单格式：[Level] [location] message
 *
 * 适合控制台输出。
 */
class SimpleFormatter : public Formatter {
public:
    std::string Format(const Record& record) override {
        const auto file = std::filesystem::path(record.GetSourceLocation().file).filename().string();
        std::ostringstream oss;
        oss << "[" << ToString(record.GetLevel()) << "] "
            << "[" << file << ":" << record.GetSourceLocation().line << "] "
            << record.GetMessage();
        return oss.str();
    }
};

} // namespace logging
