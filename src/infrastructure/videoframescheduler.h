#ifndef VIDEOFRAMESCHEDULER_H
#define VIDEOFRAMESCHEDULER_H

#include <QObject>
#include <atomic>
#include <cstdint>
#include <memory>

#include "core/ffmpeg_raii.h"
#include "core/queue.h"
#include "infrastructure/ivideoframesink.h"
#include "infrastructure/syncmanager.h"

extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/frame.h>
}

/**
 * @brief VideoFrameScheduler 单路视频帧调度器
 * @details 负责从视频帧队列取帧、执行同步判断并把可显示帧交给 IVideoFrameSink。
 *          不负责 OpenGL 绘制、QWidget 显示、多路布局或 StreamId 管理。
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
    void Initialize(std::shared_ptr<SyncQueue<AVFramePtr>> frameQueue,
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
     * @brief SetPreviewFrameRateReductionEnabled 设置预览降帧策略
     * @param enabled 是否启用预览降帧
     * @param targetFps 启用后目标预览帧率
     */
    void SetPreviewFrameRateReductionEnabled(bool enabled, int targetFps);

    /**
     * @brief ClearOutputFrame 清理当前输出 sink 持有的显示帧
     * @details 只清理当前显示帧，不清空队列、不 flush decoder、不重置同步时钟。
     */
    void ClearOutputFrame();

private slots:
    /**
     * @brief Tick 自调度入口
     * @details 从帧队列查看队首帧，完成同步判断、丢帧追赶、可显示帧转交和下一轮调度。
     */
    void Tick();

private:
    /**
     * @brief CanScheduleFrame 判断当前是否具备调度条件
     * @return true 可以调度
     * @return false 当前缺少运行状态、帧队列、同步管理器或输出 sink
     */
    bool CanScheduleFrame() const;

    /**
     * @brief FrameTimingDecision 表示当前队首帧的调度判断结果
     */
    enum class FrameTimingDecision {
        Display,
        Wait,
        Drop
    };

    /**
     * @brief FrameTimingSnapshot 队首帧同步判断快照
     */
    struct FrameTimingSnapshot {
        int64_t pts = AV_NOPTS_VALUE;	///< 队首帧 PTS
    };

    /**
     * @brief CalculateScaledFrameInterval 计算播放速度修正后的帧间隔
     * @param speed 当前播放速度快照
     * @return 修正后的帧间隔，单位毫秒
     */
    int CalculateScaledFrameInterval(double speed) const;

    /**
     * @brief PeekNextFrameTiming 查看队首帧同步判断快照但不弹出队列
     * @param snapshot 输出队首帧同步判断快照
     * @return true 成功取得队首帧
     * @return false 队列为空或队首帧无效
     */
    bool PeekNextFrameTiming(FrameTimingSnapshot& snapshot);

    /**
     * @brief EvaluateFrameTiming 根据同步时钟判断队首帧应显示、等待或丢弃
     * @param snapshot 队首帧同步判断快照
     * @param syncType 当前同步类型
     * @param waitMs 输出等待时间，只有返回 Wait 时有效
     * @return 当前帧调度结果
     */
    FrameTimingDecision EvaluateFrameTiming(const FrameTimingSnapshot& snapshot,
                                            SYNCTYPE syncType,
                                            int& waitMs) const;

    /**
     * @brief DropLateFrame 弹出并释放过晚帧
     * @return true 成功弹出并释放过晚帧
     * @return false 队列中没有可丢弃帧
     */
    bool DropLateFrame();

    /**
     * @brief ShouldDropForPreviewFrameRateLimit 判断当前帧是否应因预览降帧被丢弃
     * @param snapshot 队首帧同步判断快照
     * @param speed 当前播放速度快照
     * @return true 应丢弃当前帧
     */
    bool ShouldDropForPreviewFrameRateLimit(const FrameTimingSnapshot& snapshot,
                                            double speed) const;

    /**
     * @brief DropPreviewRateLimitedFrame 弹出并释放被预览降帧策略跳过的帧
     * @param syncType 当前同步类型
     * @return true 成功丢弃一帧
     */
    bool DropPreviewRateLimitedFrame(SYNCTYPE syncType);

    /**
     * @brief PopDisplayableFrame 弹出可显示帧并转交给调用方
     * @param frame 输出可显示帧
     * @return true 成功弹出可显示帧
     * @return false 没有可显示帧
     */
    bool PopDisplayableFrame(AVFramePtr& frame);

    /**
     * @brief RefreshVideoClockIfNeeded 在 VIDEO sync 模式下刷新视频主时钟
     * @param frame 当前已显示帧
     * @param syncType 当前同步类型
     */
    void RefreshVideoClockIfNeeded(const AVFrame* frame, SYNCTYPE syncType);

    /**
     * @brief RememberPreviewPresentedFrame 记录最近一次真正提交到渲染器的帧时间
     * @param frame 已提交显示的帧
     */
    void RememberPreviewPresentedFrame(const AVFrame* frame);

    /**
     * @brief PresentDisplayableFrame 将可显示帧提交给当前输出 sink
     * @details 调用后 frame 所有权转移给目标 sink，调用方不能再访问该指针。
     * @param frame 已完成调度判断的可显示帧
     */
    void PresentDisplayableFrame(AVFramePtr frame);

    /**
     * @brief ScheduleNextFrame 安排下一次调度
     * @param delayMs 延迟时间，单位毫秒
     */
    void ScheduleNextFrame(int delayMs);

    std::shared_ptr<SyncQueue<AVFramePtr>> frameQueue_;	///< 视频帧队列
    std::shared_ptr<syncmanager> syncManager_;	///< 同步时钟管理器
    IVideoFrameSink* frameSink_ = nullptr;	///< 视频帧输出目标

    double videoTimeBase_ = 0.0;	///< 视频时间基
    int frameIntervalMs_ = 33;	///< 默认帧间隔，单位毫秒
    std::atomic<double> speed_{1.0};	///< 当前播放速度
    std::atomic<bool> previewFrameRateReductionEnabled_{false};	///< 是否启用预览降帧
    std::atomic<int> previewFrameIntervalMs_{67};	///< 预览降帧目标帧间隔
    bool hasPreviewPresentedPts_ = false;	///< 是否已有预览显示时间戳
    double lastPreviewPresentedPtsMs_ = 0.0;	///< 最近一次预览显示帧 PTS
    std::atomic<bool> running_{false};	///< 是否正在调度
    std::atomic<int> dropped_{0};	///< 丢帧计数
    std::atomic<int> rendered_{0};	///< 显示帧计数
};

#endif // VIDEOFRAMESCHEDULER_H
