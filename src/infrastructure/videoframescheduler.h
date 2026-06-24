#ifndef VIDEOFRAMESCHEDULER_H
#define VIDEOFRAMESCHEDULER_H

#include <QObject>
#include <atomic>
#include <memory>

#include "core/queue.h"
#include "infrastructure/ivideoframesink.h"
#include "infrastructure/syncmanager.h"

extern "C" {
#include <libavutil/frame.h>
}

/**
 * @brief VideoFrameScheduler 单路视频帧调度器
 * @details 负责从视频帧队列取帧、执行同步判断并把可显示帧交给 IVideoFrameSink。
 *          第一版仅建立调度器边界和生命周期接口，暂不接管 VideoRender 现有调度逻辑。
 */
class VideoFrameScheduler : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief VideoFrameScheduler 构造函数
     * @param parent Qt 父对象
     */
    explicit VideoFrameScheduler(QObject* parent = nullptr);

    /**
     * @brief VideoFrameScheduler 析构函数，停止未完成的调度循环
     */
    ~VideoFrameScheduler() override;

    /**
     * @brief Initialize 初始化调度器输入
     * @param frameQueue 视频帧队列
     * @param syncManager 同步时钟管理器
     * @param frameRate 视频帧率
     * @param videoTimeBase 视频流时间基
     */
    void Initialize(std::shared_ptr<SyncQueue<AVFrame*>> frameQueue,
                    std::shared_ptr<syncmanager> syncManager,
                    double frameRate,
                    double videoTimeBase);

    /**
     * @brief SetFrameSink 设置可显示帧输出目标
     * @details 不转移 sink 所有权，调用方必须保证 sink 生命周期覆盖调度使用期。
     * @param frameSink 可显示帧消费者
     */
    void SetFrameSink(IVideoFrameSink* frameSink);

    /**
     * @brief Start 启动视频帧调度
     */
    void Start();

    /**
     * @brief Stop 停止视频帧调度
     */
    void Stop();

    /**
     * @brief Pause 暂停视频帧调度
     */
    void Pause();

    /**
     * @brief SetSpeed 设置调度器感知的播放速度
     * @param speed 支持的播放速度档位
     */
    void SetSpeed(double speed);

    /**
     * @brief ClearOutputFrame 清理当前输出 sink 持有的显示帧
     * @details 只清理当前显示帧，不清空队列、不 flush decoder、不重置同步时钟。
     */
    void ClearOutputFrame();

private slots:
    /**
     * @brief Tick 自调度入口
     * @details 后续迁移 VideoRender 调度逻辑时由该函数替代 RenderFrame。
     */
    void Tick();

private:
    /**
     * @brief CanScheduleFrame 判断当前是否具备调度条件
     * @return true 可以调度
     * @return false 当前缺少运行状态、帧队列、同步管理器或输出 sink
     */
    bool CanScheduleFrame() const;

    std::shared_ptr<SyncQueue<AVFrame*>> frameQueue_;
    std::shared_ptr<syncmanager> syncManager_;
    IVideoFrameSink* frameSink_ = nullptr;

    double videoTimeBase_ = 0.0;
    int frameIntervalMs_ = 40;
    std::atomic<double> speed_{1.0};
    std::atomic<bool> running_{false};
    std::atomic<int> dropped_{0};
    std::atomic<int> rendered_{0};
};

#endif // VIDEOFRAMESCHEDULER_H
