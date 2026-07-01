#pragma once

#include <memory>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

#if __has_include(<QString>) && __has_include(<QByteArray>)
#include <QByteArray>
#include <QString>
#define TINY_LOG_HAS_QT_TYPES 1
#else
#define TINY_LOG_HAS_QT_TYPES 0
#endif

#if __has_include(<format>)
#include <format>
#define TINY_LOG_HAS_STD_FORMAT 1
#else
#define TINY_LOG_HAS_STD_FORMAT 0
#endif

#include "ConsoleSink.h"
#include "FileSink.h"
#include "Logger.h"
#include "RotatingFileSink.h"
#include "SourceLocation.h"
#include "detail/NullMutex.h"
#include "detail/Registry.h"

// CMake 通过 target_compile_definitions 注入项目根的绝对路径
// 没用 CMake 构建时退化为 "."（当前目录），保证总是能编过
#ifndef TINY_LOG_PROJECT_ROOT
#define TINY_LOG_PROJECT_ROOT "."
#endif

namespace logging {


/**
 * @brief 获取默认 Logger（进程内单例）
 *
 * 必须 inline，否则放在头文件里被多个 TU 包含会触发 ODR 多重定义。
 */
inline Logger* GetDefaultLogger() {
    return detail::Registry::GetInstance()->GetDefaultLogger();
}

/**
 * @brief 按名字获取 Logger，不存在则由 Registry 自动创建
 *
 * 用法：auto* net = logging::GetLogger("Network");
 */
inline Logger* GetLogger(const std::string& name) {
    return detail::Registry::GetInstance()->GetLogger(name);
}

/**
 * @brief 按名字获取 AsyncLogger，不存在则由 Registry 自动创建
 *
 * 异步 Logger 把"分发到 sinks"丢给全局共享的 ThreadPool，业务线程立刻返回。
 * 用法：auto* net = logging::GetAsyncLogger("AsyncNet");
 */
inline Logger* GetAsyncLogger(const std::string& name) {
    return detail::Registry::GetInstance()->GetAsyncLogger(name);
}

// ============================================================
// 全局便捷接口（P7.2）
// ============================================================

/**
 * @brief 一键设置全局 level（双层过滤的全局上限）
 *
 * 配合每个 Logger 自己的 level 形成双层过滤：record 必须同时
 * 通过两层才会输出。Off 立刻静音所有日志，恢复时各 Logger 自己
 * 的 level 配置不丢。
 */
inline void SetGlobalLevel(Level level) {
    detail::Registry::GetInstance()->SetGlobalLevel(level);
}

/**
 * @brief 一键 flush 所有 Logger 的所有 sink
 *
 * 程序退出 / 崩溃前调用，避免缓冲区数据丢失。
 */
inline void FlushAll() {
    detail::Registry::GetInstance()->FlushAll();
}

/**
 * @brief 一键设置所有 Logger 的 flush_on_level
 *
 * 达到这个 level 的日志写入后立即 flush，避免 crash 时丢失关键日志。
 */
inline void SetFlushOnLevel(Level level) {
    detail::Registry::GetInstance()->SetFlushOnLevel(level);
}

namespace detail {

inline SourceLocation MakeLocation(const char* file, int line, const char* func) {
    SourceLocation sourceLocation{file,line,func};
    return sourceLocation;
}

inline void WriteArg(std::ostringstream& oss, const char* value) {
    oss << (value ? value : "");
}

#if TINY_LOG_HAS_QT_TYPES
template <typename T>
inline std::enable_if_t<std::is_same_v<std::decay_t<T>, QString>, void>
WriteArg(std::ostringstream& oss, T&& value) {
    const QByteArray utf8 = value.toUtf8();
    oss << utf8.constData();
}

template <typename T>
inline std::enable_if_t<std::is_same_v<std::decay_t<T>, QByteArray>, void>
WriteArg(std::ostringstream& oss, T&& value) {
    oss << std::string(value.constData(), static_cast<std::size_t>(value.size()));
}
#endif

template <typename T>
inline std::enable_if_t<
#if TINY_LOG_HAS_QT_TYPES
    !std::is_same_v<std::decay_t<T>, QString> &&
    !std::is_same_v<std::decay_t<T>, QByteArray> &&
#endif
    !std::is_same_v<std::decay_t<T>, const char*> &&
    !std::is_same_v<std::decay_t<T>, char*>,
    void>
WriteArg(std::ostringstream& oss, T&& value) {
    oss << std::forward<T>(value);
}

inline void AppendArgs(std::ostringstream&, bool&) {}

template <typename T, typename... Args>
inline void AppendArgs(std::ostringstream& oss, bool& first, T&& value, Args&&... args) {
    if (!first) {
        oss << ' ';
    }
    first = false;
    WriteArg(oss, std::forward<T>(value));
    AppendArgs(oss, first, std::forward<Args>(args)...);
}

template <typename... Args>
inline std::string JoinArgs(Args&&... args) {
    std::ostringstream oss;
    bool first = true;
    AppendArgs(oss, first, std::forward<Args>(args)...);
    return oss.str();
}

template <typename T>
inline void AppendFormatArg(std::ostringstream& oss, T&& value) {
    WriteArg(oss, std::forward<T>(value));
}

inline void AppendFormatArgs(std::ostringstream&) {}

template <typename T, typename... Args>
inline void AppendFormatArgs(std::ostringstream& oss, T&& value, Args&&... args) {
    oss << ' ';
    AppendFormatArg(oss, std::forward<T>(value));
    AppendFormatArgs(oss, std::forward<Args>(args)...);
}

template <typename... Args>
inline std::string FormatMessage(const std::string& fmt, Args&&... args) {
#if TINY_LOG_HAS_STD_FORMAT
    return std::format(fmt, std::forward<Args>(args)...);
#else
    std::ostringstream oss;
    oss << fmt;
    AppendFormatArgs(oss, std::forward<Args>(args)...);
    return oss.str();
#endif
}

inline void PrintWithLocation(Level level,
                              const std::string& msg,
                              const char* file,
                              int line,
                              const char* func) {
    GetDefaultLogger()->Print(level, msg, MakeLocation(file, line, func));
}

class Stream {
public:
    Stream(Level level, const char* file, int line, const char* func)
        : level_(level)
        , file_(file)
        , line_(line)
        , func_(func)
    {
    }

    Stream(Stream&& other) noexcept
        : level_(other.level_)
        , file_(other.file_)
        , line_(other.line_)
        , func_(other.func_)
        , stream_(std::move(other.stream_))
        , first_(other.first_)
        , active_(other.active_)
    {
        other.active_ = false;
    }

    Stream(const Stream&) = delete;
    Stream& operator=(const Stream&) = delete;

    ~Stream() {
        if (active_) {
            PrintWithLocation(level_, stream_.str(), file_, line_, func_);
        }
    }

    template <typename T>
    Stream& operator<<(T&& value) {
        if (!first_) {
            stream_ << ' ';
        }
        first_ = false;
        WriteArg(stream_, std::forward<T>(value));
        return *this;
    }

    Stream& noquote() {
        return *this;
    }

private:
    Level level_;
    const char* file_;
    int line_;
    const char* func_;
    std::ostringstream stream_;
    bool first_ = true;
    bool active_ = true;
};

} // namespace detail
} // namespace logging


// ============================================================
// 用法：
//     LOG_INFO("hello");
//     LOG_ERROR("something broke: " + reason);
// ============================================================

// 使用默认的Logger
#define LOG_TRACE(...) \
    ::logging::detail::PrintWithLocation(::logging::Level::Trace, \
        ::logging::detail::JoinArgs(__VA_ARGS__), __FILE__, __LINE__, __func__)

#define LOG_DEBUG(...) \
    ::logging::detail::PrintWithLocation(::logging::Level::Debug, \
        ::logging::detail::JoinArgs(__VA_ARGS__), __FILE__, __LINE__, __func__)

#define LOG_INFO(...) \
    ::logging::detail::PrintWithLocation(::logging::Level::Info, \
        ::logging::detail::JoinArgs(__VA_ARGS__), __FILE__, __LINE__, __func__)

#define LOG_WARN(...) \
    ::logging::detail::PrintWithLocation(::logging::Level::Warn, \
        ::logging::detail::JoinArgs(__VA_ARGS__), __FILE__, __LINE__, __func__)

#define LOG_ERROR(...) \
    ::logging::detail::PrintWithLocation(::logging::Level::Error, \
        ::logging::detail::JoinArgs(__VA_ARGS__), __FILE__, __LINE__, __func__)

#define LOG_CRITICAL(...) \
    ::logging::detail::PrintWithLocation(::logging::Level::Critical, \
        ::logging::detail::JoinArgs(__VA_ARGS__), __FILE__, __LINE__, __func__)

#define LOG_TRACE_STREAM() \
    ::logging::detail::Stream(::logging::Level::Trace, __FILE__, __LINE__, __func__)

#define LOG_DEBUG_STREAM() \
    ::logging::detail::Stream(::logging::Level::Debug, __FILE__, __LINE__, __func__)

#define LOG_INFO_STREAM() \
    ::logging::detail::Stream(::logging::Level::Info, __FILE__, __LINE__, __func__)

#define LOG_WARN_STREAM() \
    ::logging::detail::Stream(::logging::Level::Warn, __FILE__, __LINE__, __func__)

#define LOG_ERROR_STREAM() \
    ::logging::detail::Stream(::logging::Level::Error, __FILE__, __LINE__, __func__)

#define LOG_CRITICAL_STREAM() \
    ::logging::detail::Stream(::logging::Level::Critical, __FILE__, __LINE__, __func__)



// 使用指定的Logger
#define LOG_TRACE_TO(logger, msg) \
    (logger)->Print(::logging::Level::Trace, (msg), \
        ::logging::SourceLocation{__FILE__, __LINE__, __func__})

#define LOG_DEBUG_TO(logger, msg) \
    (logger)->Print(::logging::Level::Debug, (msg), \
        ::logging::SourceLocation{__FILE__, __LINE__, __func__})

#define LOG_INFO_TO(logger, msg) \
    (logger)->Print(::logging::Level::Info, (msg), \
        ::logging::SourceLocation{__FILE__, __LINE__, __func__})

#define LOG_WARN_TO(logger, msg) \
    (logger)->Print(::logging::Level::Warn, (msg), \
        ::logging::SourceLocation{__FILE__, __LINE__, __func__})

#define LOG_ERROR_TO(logger, msg) \
    (logger)->Print(::logging::Level::Error, (msg), \
        ::logging::SourceLocation{__FILE__, __LINE__, __func__})

#define LOG_CRITICAL_TO(logger, msg) \
    (logger)->Print(::logging::Level::Critical, (msg), \
        ::logging::SourceLocation{__FILE__, __LINE__, __func__})


// ============================================================
// 格式化宏（C++20 std::format）
//
// 用法：
//     LOG_INFO_FMT("user={} age={}", name, age);
//     LOG_ERROR_FMT("conn {} failed: {:.2f}s", ip, elapsed);
//     LOG_INFO_FMT_TO(my_logger, "value = {:#x}", n);
//
// 与 LOG_XXX 相比：自动 to_string、支持格式说明符、C++20 编译期类型校验。
// 与 LOG_XXX 不同点：每次调用都会构造一个 std::string（格式化的固有成本）。
//
// __VA_OPT__(,) 是 C++20 标准——args 为空时不展开逗号，
// 让 LOG_INFO_FMT("no args") 也合法。
// ============================================================

// ---- 默认 Logger ----
#define LOG_TRACE_FMT(fmt, ...) \
    ::logging::detail::PrintWithLocation(::logging::Level::Trace, \
        ::logging::detail::FormatMessage(fmt __VA_OPT__(,) __VA_ARGS__), \
        __FILE__, __LINE__, __func__)

#define LOG_DEBUG_FMT(fmt, ...) \
    ::logging::detail::PrintWithLocation(::logging::Level::Debug, \
        ::logging::detail::FormatMessage(fmt __VA_OPT__(,) __VA_ARGS__), \
        __FILE__, __LINE__, __func__)

#define LOG_INFO_FMT(fmt, ...) \
    ::logging::detail::PrintWithLocation(::logging::Level::Info, \
        ::logging::detail::FormatMessage(fmt __VA_OPT__(,) __VA_ARGS__), \
        __FILE__, __LINE__, __func__)

#define LOG_WARN_FMT(fmt, ...) \
    ::logging::detail::PrintWithLocation(::logging::Level::Warn, \
        ::logging::detail::FormatMessage(fmt __VA_OPT__(,) __VA_ARGS__), \
        __FILE__, __LINE__, __func__)

#define LOG_ERROR_FMT(fmt, ...) \
    ::logging::detail::PrintWithLocation(::logging::Level::Error, \
        ::logging::detail::FormatMessage(fmt __VA_OPT__(,) __VA_ARGS__), \
        __FILE__, __LINE__, __func__)

#define LOG_CRITICAL_FMT(fmt, ...) \
    ::logging::detail::PrintWithLocation(::logging::Level::Critical, \
        ::logging::detail::FormatMessage(fmt __VA_OPT__(,) __VA_ARGS__), \
        __FILE__, __LINE__, __func__)

// ---- 指定 Logger ----
#define LOG_TRACE_FMT_TO(logger, fmt, ...) \
    (logger)->Print(::logging::Level::Trace, \
        ::logging::detail::FormatMessage(fmt __VA_OPT__(,) __VA_ARGS__), \
        ::logging::SourceLocation{__FILE__, __LINE__, __func__})

#define LOG_DEBUG_FMT_TO(logger, fmt, ...) \
    (logger)->Print(::logging::Level::Debug, \
        ::logging::detail::FormatMessage(fmt __VA_OPT__(,) __VA_ARGS__), \
        ::logging::SourceLocation{__FILE__, __LINE__, __func__})

#define LOG_INFO_FMT_TO(logger, fmt, ...) \
    (logger)->Print(::logging::Level::Info, \
        ::logging::detail::FormatMessage(fmt __VA_OPT__(,) __VA_ARGS__), \
        ::logging::SourceLocation{__FILE__, __LINE__, __func__})

#define LOG_WARN_FMT_TO(logger, fmt, ...) \
    (logger)->Print(::logging::Level::Warn, \
        ::logging::detail::FormatMessage(fmt __VA_OPT__(,) __VA_ARGS__), \
        ::logging::SourceLocation{__FILE__, __LINE__, __func__})

#define LOG_ERROR_FMT_TO(logger, fmt, ...) \
    (logger)->Print(::logging::Level::Error, \
        ::logging::detail::FormatMessage(fmt __VA_OPT__(,) __VA_ARGS__), \
        ::logging::SourceLocation{__FILE__, __LINE__, __func__})

#define LOG_CRITICAL_FMT_TO(logger, fmt, ...) \
    (logger)->Print(::logging::Level::Critical, \
        ::logging::detail::FormatMessage(fmt __VA_OPT__(,) __VA_ARGS__), \
        ::logging::SourceLocation{__FILE__, __LINE__, __func__})

