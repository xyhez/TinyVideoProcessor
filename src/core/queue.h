#pragma once

// #include <memory>
#include <mutex>
#include <deque>
#include <condition_variable>
#include <atomic>
#include <type_traits>
#include <utility>

/**
 * @brief 线程同步队列
 * 使用方法:
 * std::shared_ptr<SyncQueue<T>>
 * 代替之前的
 * std::shared_ptr<deque<T>>
 */
template<typename T>
class SyncQueue {
private:
    std::deque<T> raw_queue_;	///< 数据队列
    std::mutex mutex_;	///< 互斥锁
    std::condition_variable cv_not_empty_;	///< 非空条件变量
    std::condition_variable cv_not_full_;	///< 未满条件变量
    size_t max_size_;	///< 队列最大容量
    std::atomic<bool> stop_flag_;	///< 停止标志
    std::atomic<bool> wait_interrupted_;	///< 等待中断标志

    /**
     * @brief RawPointer 获取裸指针观察视图
     * @param item 队列元素
     * @return 裸指针观察视图
     */
    static auto RawPointer(const T& item) {
        if constexpr (std::is_pointer_v<T>) {
            return item;
        } else {
            return item.get();
        }
    }
public:
// 核心接口
    /**
     * @brief SyncQueue 构造函数
     * @param max_size 队列最大容量
     */
    explicit SyncQueue(size_t max_size)
    : max_size_(max_size)
    , stop_flag_(false)
    , wait_interrupted_(false) {

    }

    /**
     * @brief SyncQueue 析构函数，清空队列并释放剩余元素
     */
    ~SyncQueue() {
        Clear();
    }

    /**
     * @brief 阻塞入队，队列满则等待
     * @param item 待入队元素
     * @return true 入队成功
     * @return false 队列停止或等待被中断
     */
    bool Push(T item) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_not_full_.wait(lock, [this]{ return
                    raw_queue_.size() < max_size_ || stop_flag_ || wait_interrupted_;
            });
            if (stop_flag_ || wait_interrupted_) {
                return false;
            }
            raw_queue_.push_back(std::move(item));
            cv_not_empty_.notify_one();
        }
        return true;
    }

    /**
     * @brief 阻塞出队，队列为空则等待
     * @param item 输出的队首元素
     * @return true 出队成功
     * @return false 队列停止、等待被中断或队列为空
     */
    bool Pop(T& item) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_not_empty_.wait(lock, [this]{ return
                !raw_queue_.empty() || stop_flag_ || wait_interrupted_;
            });

            if (raw_queue_.empty()) return false;
            item = std::move(raw_queue_.front());
            raw_queue_.pop_front();
        }
        cv_not_full_.notify_one();
        return item != nullptr;
    }

    /**
     * @brief 非阻塞出队，队列空直接返回false，不等待
     * @param item 输出的队首元素
     * @return true 出队成功
     * @return false 队列为空
     */
    bool TryPop(T& item) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if(raw_queue_.empty()) return false;
            item = std::move(raw_queue_.front());
            raw_queue_.pop_front();
        }
        cv_not_full_.notify_one();
        return item != nullptr;
    }

    /**
     * @brief 只拿到数据，但是不从队列里取出来
     * @param item 输出的队首元素
     * @return true 获取成功
     * @return false 队列为空
     */
    bool Front(T& item) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if(raw_queue_.empty()) return false;
            item = raw_queue_.front();
        }
        return item != nullptr;
    }

    /**
     * @brief VisitFront 在锁内只读访问队首元素，不转移所有权
     * @param visitor 队首元素访问函数
     * @return true 访问成功
     * @return false 队列为空
     */
    template<typename Visitor>
    bool VisitFront(Visitor&& visitor) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (raw_queue_.empty()) return false;
        std::forward<Visitor>(visitor)(static_cast<const T&>(raw_queue_.front()));
        return true;
    }

    /**
     * @brief FrontRaw 获取队首元素的裸指针观察视图，不转移所有权
     * @return 队首元素裸指针，队列为空时返回 nullptr
     */
    auto FrontRaw() {
        std::unique_lock<std::mutex> lock(mutex_);
        if (raw_queue_.empty()) return decltype(RawPointer(raw_queue_.front()))(nullptr);
        return RawPointer(raw_queue_.front());
    }

    /**
     * @brief PopFront 移除队首元素但不返回
     */
    void PopFront() {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if(raw_queue_.empty()) return;
            raw_queue_.pop_front();
        }
        cv_not_full_.notify_one();
    }

// 辅助接口
    /**
     * @brief Size 获取当前队列长度
     * @return 队列元素数量
     */
    size_t Size() {
        std::lock_guard<std::mutex> lock(mutex_);
        return raw_queue_.size();
    }

    /**
     * @brief Empty 判断队列是否为空
     * @return true 队列为空
     * @return false 队列非空
     */
    bool Empty() {
        std::lock_guard<std::mutex> lock(mutex_);
        return raw_queue_.empty();
    }

    /**
     * @brief Clear 清空队列并释放内部元素
     */
    void Clear() {
        std::unique_lock<std::mutex> lock(mutex_);
        raw_queue_.clear();
        cv_not_full_.notify_all();
        cv_not_empty_.notify_all();
    }

    /**
     * @brief ClearAndWake 清空队列并唤醒等待线程
     */
    void ClearAndWake() {
        Clear();
    }

    /**
     * @brief SetMaxSize 设置队列最大容量
     * @param max_size 新最大容量
     */
    void SetMaxSize(size_t max_size) {
        max_size_ = max_size;
    }

    /**
     * @brief WakeAll 唤醒所有等待入队或出队的线程
     */
    void WakeAll() {
        cv_not_empty_.notify_all();
        cv_not_full_.notify_all();
    }

    /**
     * @brief SetStopFlag 设置停止标志
     * @param flag 是否请求停止
     */
    void SetStopFlag(bool flag) {
        stop_flag_ = flag;
    }

    /**
     * @brief IsStopRequested 判断是否请求停止
     * @return true 已请求停止
     * @return false 未请求停止
     */
    bool IsStopRequested() const {
        return stop_flag_;
    }

    /**
     * @brief IsWaitInterrupted 判断等待是否被非终止式中断
     * @return true 等待被中断
     * @return false 等待未被中断
     */
    bool IsWaitInterrupted() const {
        return wait_interrupted_;
    }

    /**
     * @brief RequestStop 请求停止队列并唤醒等待线程
     */
    void RequestStop() {
        SetStopFlag(true);
        WakeAll();
    }

    /**
     * @brief Resume 恢复队列运行并恢复等待
     */
    void Resume() {
        SetStopFlag(false);
        ResumeWait();
    }

    /**
     * @brief InterruptWait 非终止式中断当前等待
     */
    void InterruptWait() {
        wait_interrupted_ = true;
        WakeAll();
    }

    /**
     * @brief ResumeWait 恢复被中断的等待状态
     */
    void ResumeWait() {
        wait_interrupted_ = false;
        WakeAll();
    }
};
