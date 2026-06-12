#pragma once

// #include <memory>
#include <mutex>
#include <deque>
#include <condition_variable>
#include <atomic>
#include <functional>

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
    std::deque<T> raw_queue_;                       ///< 数据队列
    std::mutex mutex_;                              ///< 锁
    std::condition_variable cv_not_empty_;          ///< 空队列条件变量
    std::condition_variable cv_not_full_;           ///< 满队列条件变量
    size_t max_size_;                               ///< 队列最大容量
    std::atomic<bool> stop_flag_;                   ///< 停止标志
    std::atomic<bool> wait_interrupted_;            ///< 非终止式等待中断标志
    std::function<void(T)> deleter_ = nullptr;      ///< 数据析构器
public:
// 核心接口
    SyncQueue(size_t max_size ,std::function<void(T)> deleter = nullptr)
    : max_size_(max_size)
    , stop_flag_(false)
    , wait_interrupted_(false)
    , deleter_(deleter){

    }
    ~SyncQueue() {
        Clear();
    }

    /**
     * @brief 阻塞入队，队列满则等待
     * @param item
     */
    bool Push(T item) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_not_full_.wait(lock, [this]{ return
                    raw_queue_.size() < max_size_ || stop_flag_ || wait_interrupted_;
            });
            if (stop_flag_ || wait_interrupted_) {
                if (deleter_) {
                    deleter_(item);
                }
                return false;
            }
            raw_queue_.push_back(item);
            cv_not_empty_.notify_one();
        }
        return true;
    }

    /**
     * @brief 阻塞出队，队列为空则等待
     * @param item
     * @return
     */
    bool Pop(T& item) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_not_empty_.wait(lock, [this]{ return
                !raw_queue_.empty() || stop_flag_ || wait_interrupted_;
            });

            if (raw_queue_.empty()) return false;
            item = raw_queue_.front();
            raw_queue_.pop_front();
        }
        cv_not_full_.notify_one();
        return item != nullptr;
    }

    /**
     * @brief 非阻塞出队，队列空直接返回false，不等待
     * @param item
     * @return
     */
    bool TryPop(T& item) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if(raw_queue_.empty()) return false;
            item = raw_queue_.front();
            raw_queue_.pop_front();
        }
        cv_not_full_.notify_one();
        return item != nullptr;
    }

    /**
     * @brief 只拿到数据，但是不从队列里取出来
     * @param item
     * @return
     */
    bool Front(T& item) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if(raw_queue_.empty()) return false;
            item = raw_queue_.front();
        }
        return item != nullptr;
    }

    void PopFront() {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if(raw_queue_.empty()) return;
            raw_queue_.pop_front();
        }
        cv_not_full_.notify_one();
    }

// 辅助接口
    size_t Size() {
        std::lock_guard<std::mutex> lock(mutex_);
        return raw_queue_.size();
    }

    bool Empty() {
        std::lock_guard<std::mutex> lock(mutex_);
        return raw_queue_.empty();
    }

    void Clear() {
        std::unique_lock<std::mutex> lock(mutex_);
        while (!raw_queue_.empty()) {
            if (deleter_)
                deleter_(raw_queue_.front());
            raw_queue_.pop_front();
        }
        cv_not_full_.notify_all();
        cv_not_empty_.notify_all();
    }

    void ClearAndWake() {
        Clear();
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

    bool IsStopRequested() const {
        return stop_flag_;
    }

    bool IsWaitInterrupted() const {
        return wait_interrupted_;
    }

    void RequestStop() {
        SetStopFlag(true);
        WakeAll();
    }

    void Resume() {
        SetStopFlag(false);
        ResumeWait();
    }

    void InterruptWait() {
        wait_interrupted_ = true;
        WakeAll();
    }

    void ResumeWait() {
        wait_interrupted_ = false;
        WakeAll();
    }
};
