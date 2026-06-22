#include "tiny_log/detail/Registry.h"

#include "tiny_log/AsyncLogger.h"
#include "tiny_log/ConsoleSink.h"
#include "tiny_log/QtSink.h"

namespace logging::detail {

Registry::Registry() {
    thread_pool_ = std::make_shared<ThreadPool>(ThreadPoolConfig{});

    auto def = std::make_unique<Logger>("default");
    def->AddSink(std::make_shared<QtSinkMT>());
    def->AddSink(std::make_shared<RotatingFileSinkMT>(
        std::string(TINY_LOG_PROJECT_ROOT) + "/logfile/log.txt", 50000, 3));
    loggers_["default"] = std::move(def);
}

Registry::~Registry() {
    if (thread_pool_) {
        thread_pool_->Shutdown();
    }
}

Logger* Registry::GetLogger(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = loggers_.find(name);
    if (it != loggers_.end())
        return it->second.get();

    auto logger = std::make_unique<Logger>(name);
    logger->AddSink(std::make_shared<QtSinkST>());
    Logger* ptr = logger.get();
    loggers_[name] = std::move(logger);
    return ptr;
}

Logger* Registry::GetAsyncLogger(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = loggers_.find(name);
    if (it != loggers_.end())
        return it->second.get();

    auto logger = std::make_unique<AsyncLogger>(name, thread_pool_);
    logger->AddSink(std::make_shared<QtSinkMT>());
    Logger* ptr = logger.get();
    loggers_[name] = std::move(logger);
    return ptr;
}

Logger* Registry::GetDefaultLogger() {
    return GetLogger("default");
}

void Registry::FlushAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [name, logger] : loggers_) {
        logger->Flush();
    }
}

void Registry::SetGlobalLevel(Level level) {
    global_level_.store(level, std::memory_order_relaxed);
}

void Registry::SetFlushOnLevel(Level level) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [name, logger] : loggers_) {
        logger->SetFlushOnLevel(level);
    }
}

Registry* Registry::registry_ = new Registry();

} // namespace logging::detail
