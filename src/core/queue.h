#pragma once

// #include <memory>
#include <mutex>
#include <deque>

/**
 * @brief 线程同步队列
 * 使用方法:
 * std::shared_ptr<SyncQueue<AVPacket*>>
 * 代替之前的
 * std::shared_ptr<deque<AVPakcet*>>
 */
template<typename T>
class SyncQueue {
private:
    std::deque<T*> raw_queue_;
    std::mutex mutex_;
    std::condition_variable cv_not_empty_;
    std::condition_variable cv_not_full_;
    size_t max_size_;
    bool stop_flag_;
public:
// 核心接口
    /**
     * @brief 阻塞入队，队列满则等待
     * @param item
     */
    void Push(T item) {

    }

    /**
     * @brief 阻塞出队，队列为空则等待
     * @param item
     * @return
     */
    bool Pop(T*& item) {

    }

    /**
     * @brief 非阻塞出队，队列空直接返回false，不等待
     * @param item
     * @return
     */
    bool tryPop(T*& item) {

    }

// 辅助接口
    size_t size() const {
        return raw_queue_.size();
    }

    bool empty() const {
        return raw_queue_.empty();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        raw_queue_.clear();
    }

    void SetMaxSize(size_t max_size) {
        max_size_ = max_size;
    }

    void WakeAll() {
        cv_not_empty_.notify_all();
        cv_not_full_.notify_all();
    }

    void SetStopFlag(bool flag) {
        stop_flag_ = flag;
    }



};
