#pragma once

#include <memory>
#include <mutex>

/**
 * @brief 线程同步队列
 * 使用方法:
 * std::shared_ptr<SyncQueue<AVPacket*>>
 * 代替之前的
 * std::shared_ptr<deque<AVPakcet*>>
 */
template<typename T>
class SyncQueue {
public:

private:
    std::shared_ptr<T> raw_queue_;

    std::mutex
};
