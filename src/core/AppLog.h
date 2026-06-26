#pragma once

#include "tiny_log/Logging.h"

#include <QByteArray>
#include <QString>

#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

namespace app::log {

template <typename T>
inline std::enable_if_t<std::is_same_v<std::decay_t<T>, QString>, void>
WriteArg(std::ostringstream& stream, T&& value)
{
    stream << value.toUtf8().constData();
}

template <typename T>
inline std::enable_if_t<std::is_same_v<std::decay_t<T>, QByteArray>, void>
WriteArg(std::ostringstream& stream, T&& value)
{
    stream << std::string(value.constData(), static_cast<std::size_t>(value.size()));
}

inline void WriteArg(std::ostringstream& stream, const char* value)
{
    stream << (value ? value : "");
}

template <typename T>
inline std::enable_if_t<!std::is_same_v<std::decay_t<T>, QString>
                            && !std::is_same_v<std::decay_t<T>, QByteArray>,
                        void>
WriteArg(std::ostringstream& stream, T&& value)
{
    stream << std::forward<T>(value);
}

inline void AppendArgs(std::ostringstream&, bool&)
{
}

template <typename T, typename... Args>
inline void AppendArgs(std::ostringstream& stream, bool& first, T&& value, Args&&... args)
{
    if (!first) {
        stream << ' ';
    }
    first = false;
    WriteArg(stream, std::forward<T>(value));
    AppendArgs(stream, first, std::forward<Args>(args)...);
}

template <typename... Args>
inline std::string Join(Args&&... args)
{
    std::ostringstream stream;
    bool first = true;
    AppendArgs(stream, first, std::forward<Args>(args)...);
    return stream.str();
}

template <typename... Args>
inline void Log(logging::Level level,
                const char* file,
                int line,
                const char* func,
                Args&&... args)
{
    logging::detail::PrintWithLocation(level, Join(std::forward<Args>(args)...), file, line, func);
}

class Stream {
public:
    Stream(logging::Level level, const char* file, int line, const char* func)
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

    ~Stream()
    {
        if (active_) {
            logging::detail::PrintWithLocation(level_, stream_.str(), file_, line_, func_);
        }
    }

    template <typename T>
    Stream& operator<<(T&& value)
    {
        if (!first_) {
            stream_ << ' ';
        }
        first_ = false;
        WriteArg(stream_, std::forward<T>(value));
        return *this;
    }

    Stream& noquote()
    {
        return *this;
    }

private:
    logging::Level level_;	///< 日志级别
    const char* file_;	///< 源文件路径
    int line_;	///< 源代码行号
    const char* func_;	///< 函数名
    std::ostringstream stream_;	///< 日志内容流
    bool first_ = true;	///< 是否为首个参数
    bool active_ = true;	///< 是否输出日志
};

} // namespace app::log

#define APP_LOG_TRACE(...) \
    ::app::log::Log(::logging::Level::Trace, __FILE__, __LINE__, __func__, __VA_ARGS__)

#define APP_LOG_DEBUG(...) \
    ::app::log::Log(::logging::Level::Debug, __FILE__, __LINE__, __func__, __VA_ARGS__)

#define APP_LOG_INFO(...) \
    ::app::log::Log(::logging::Level::Info, __FILE__, __LINE__, __func__, __VA_ARGS__)

#define APP_LOG_WARN(...) \
    ::app::log::Log(::logging::Level::Warn, __FILE__, __LINE__, __func__, __VA_ARGS__)

#define APP_LOG_ERROR(...) \
    ::app::log::Log(::logging::Level::Error, __FILE__, __LINE__, __func__, __VA_ARGS__)

#define APP_LOG_CRITICAL(...) \
    ::app::log::Log(::logging::Level::Critical, __FILE__, __LINE__, __func__, __VA_ARGS__)

#define APP_LOG_DEBUG_STREAM() \
    ::app::log::Stream(::logging::Level::Debug, __FILE__, __LINE__, __func__)

#define APP_LOG_INFO_STREAM() \
    ::app::log::Stream(::logging::Level::Info, __FILE__, __LINE__, __func__)

#define APP_LOG_WARN_STREAM() \
    ::app::log::Stream(::logging::Level::Warn, __FILE__, __LINE__, __func__)

#define APP_LOG_CRITICAL_STREAM() \
    ::app::log::Stream(::logging::Level::Critical, __FILE__, __LINE__, __func__)
