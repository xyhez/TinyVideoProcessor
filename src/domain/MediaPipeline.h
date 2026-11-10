#ifndef TINYVIDEOPROCESSOR_MEDIAPIPELINE_H
#define TINYVIDEOPROCESSOR_MEDIAPIPELINE_H

#include <QObject>
#include <QThread>
#include <QTimer>
#include <cstdint>
#include <functional>
#include <memory>

#include "core/ffmpeg_raii.h"
#include "core/queue.h"
#include "domain/IMediaSource.h"
#include "infrastructure/audio/audioplayer.h"
#include "infrastructure/decoding/decoder.h"
#include "infrastructure/decoding/demuxer.h"
#include "infrastructure/sync/syncmanager.h"
#include "infrastructure/sync/videoframescheduler.h"
#include "infrastructure/rendering/videorender.h"

class QWidget;
class IVideoFrameSink;
class ImageProcessingFrameSink;
class ImageProcessorPipeline;
class VisionAnalysisService;
class VisionPipelineManager;
class VisionProcessingFrameSink;
class VisionResultStore;

/**
 * @brief MediaPipeline 管理单路媒体源的播放管线资源
 *
 * 第一版先提供接口空壳，后续逐步迁移 MediaPlayController 中的
 * 队列、基础设施组件、线程和管线生命周期逻辑。
 */
class MediaPipeline : public QObject {
    Q_OBJECT
public:
    /**
     * @brief RuntimeMetricsSnapshot 单路管线运行指标值快照
     * @details 不暴露媒体源、调度器或输出 sink 的所有权与指针。
     */
    struct RuntimeMetricsSnapshot {
        bool initialized = false;
        int frameWidth = 0;
        int frameHeight = 0;
        AVCodecID videoCodecId = AV_CODEC_ID_NONE;
        std::uint64_t submittedFrames = 0;
        std::uint64_t schedulerDroppedFrames = 0;
        bool schedulerRunning = false;
        bool imageProcessingEnabled = false;
        bool visionAnalysisActive = false;
    };

    /**
     * @brief MediaPipeline 构造函数
     * @param visionPipelineManager 视觉管线管理器指针，nullptr 表示禁用视觉分析
     * @param visionAnalysisService 异步视觉分析服务指针，nullptr 表示禁用异步分析
     * @param visionResultStore 视觉结果仓库，与渲染器共享
     * @param parent Qt 父对象
     */
    explicit MediaPipeline(VisionPipelineManager* visionPipelineManager = nullptr,
                           VisionAnalysisService* visionAnalysisService = nullptr,
                           std::shared_ptr<VisionResultStore> visionResultStore = {},
                           QObject* parent = nullptr);

    /**
     * @brief MediaPipeline 析构函数，关闭并释放管线资源
     */
    ~MediaPipeline();

    /**
     * @brief Initialize 初始化单路播放管线
     * @param mediaSource 媒体源抽象
     * @param rendererContainer 默认视频渲染容器
     * @param videoFrameSink 外部视频帧输出目标，为空时使用默认 VideoRender 可见路径
     * @return true 初始化成功
     * @return false 初始化失败
     */
    bool Initialize(std::shared_ptr<IMediaSource> mediaSource,
                    QWidget* rendererContainer,
                    IVideoFrameSink* videoFrameSink = nullptr);

    /**
     * @brief SetStreamId 设置当前管线所属的逻辑流 ID
     * @details 用于视觉结果按流归属；必须在 Start 之前调用。
     * @param streamId 逻辑流 ID
     */
    void SetStreamId(int streamId);

    /**
     * @brief Shutdown 关闭并释放当前播放管线
     */
    void Shutdown();

    /**
     * @brief Start 启动或恢复播放管线
     */
    void Start();

    /**
     * @brief Pause 暂停播放管线
     */
    void Pause();

    /**
     * @brief Stop 停止播放管线并回到起始位置
     */
    void Stop();

    /**
     * @brief SeekTo 跳转到指定播放位置
     * @param seconds 目标位置，单位秒
     * @return true 跳转成功
     * @return false 跳转失败
     */
    bool SeekTo(double seconds);

    /**
     * @brief SeekRelative 按当前位置相对跳转
     * @param seconds 相对跳转秒数，负数表示后退
     * @return true 跳转成功
     * @return false 跳转失败
     */
    bool SeekRelative(double seconds);

    /**
     * @brief Duration 获取当前媒体总时长
     * @return 总时长，单位秒
     */
    double Duration() const;

    /**
     * @brief Position 获取当前播放位置
     * @return 当前播放位置，单位秒
     */
    double Position() const;

    /**
     * @brief IsInitialized 判断当前播放管线资源是否已初始化
     * @return true 已初始化
     * @return false 未初始化
     */
    bool IsInitialized() const;

    /**
     * @brief RuntimeMetrics 获取单路管线运行指标
     * @return 初始化源信息、累计调度统计和处理链状态的只读值快照
     */
    RuntimeMetricsSnapshot RuntimeMetrics() const;

    /**
     * @brief SetPlaybackSpeed 设置播放速度
     * @param speed 播放速度档位
     */
    void SetPlaybackSpeed(double speed);

    /**
     * @brief SetPreviewFrameRateReductionEnabled 设置预览降帧策略
     * @param enabled 是否启用预览降帧
     * @param targetFps 启用后目标预览帧率
     */
    void SetPreviewFrameRateReductionEnabled(bool enabled, int targetFps);

    /**
     * @brief SetImageProcessingEnabled 设置当前管线图像处理开关
     * @param enabled 是否启用图像处理
     */
    void SetImageProcessingEnabled(bool enabled);

    /**
     * @brief IsImageProcessingEnabled 获取当前管线图像处理开关
     * @return true 已启用
     * @return false 未启用
     */
    bool IsImageProcessingEnabled() const;

    /**
     * @brief ImageProcessingPipeline 获取当前管线图像处理器管线
     * @return 图像处理器管线指针
     */
    ImageProcessorPipeline* ImageProcessingPipeline();

    /**
     * @brief ImageProcessingPipeline 获取当前管线只读图像处理器管线
     * @return 图像处理器管线指针
     */
    const ImageProcessorPipeline* ImageProcessingPipeline() const;

    /**
     * @brief SetVisionActive 切换本管线视觉分析 sink 的激活状态
     * @param active true 表示本管线属于 active stream，触发实际分析
     */
    void SetVisionActive(bool active);

    /**
     * @brief IsVisionActive 获取视觉分析 sink 激活状态
     */
    bool IsVisionActive() const;

    /**
     * @brief SetVisionOverlayEnabled 切换视觉结果是否绘制到画面
     */
    void SetVisionOverlayEnabled(bool enabled);

    /**
     * @brief IsVisionOverlayEnabled 获取视觉结果绘制开关
     */
    bool IsVisionOverlayEnabled() const;

    /**
     * @brief SetVolume 设置音量
     * @param volume 音量值，范围 0.0-1.0
     */
    void SetVolume(double volume);

    /**
     * @brief SetMuted 设置静音状态
     * @param muted 是否静音
     */
    void SetMuted(bool muted);

signals:
    /**
     * @brief PositionChanged 播放位置变化信号
     * @param seconds 当前播放位置，单位秒
     */
    void PositionChanged(double seconds);

    /**
     * @brief DurationChanged 媒体总时长变化信号
     * @param seconds 总时长，单位秒
     */
    void DurationChanged(double seconds);

    /**
     * @brief PlaybackFinished 当前媒体源自然播放结束信号
     */
    void PlaybackFinished();

    /**
     * @brief VolumeChanged 音量变化信号
     * @param volume 当前音量，范围 0.0-1.0
     */
    void VolumeChanged(double volume);

    /**
     * @brief MuteChanged 静音状态变化信号
     * @param muted 是否静音
     */
    void MuteChanged(bool muted);

    /**
     * @brief ErrorOccurred 播放管线错误信号
     * @param message 错误信息
     */
    void ErrorOccurred(QString message);

private:
    /**
     * @brief CreatePipelineResources 创建当前媒体源对应的单路播放资源
     * @param mediaSource 已打开或待接入的媒体源抽象
     * @param rendererContainer 默认视频渲染容器
     * @param videoFrameSink 外部视频帧输出目标，为空时把 VideoRender 加入默认容器
     * @return true 创建成功
     * @return false 创建失败
     */
    bool CreatePipelineResources(std::shared_ptr<IMediaSource> mediaSource,
                                 QWidget* rendererContainer,
                                 IVideoFrameSink* videoFrameSink);

    /**
     * @brief CreateConnects 创建 Pipeline 内部信号槽连接
     */
    void CreateConnects();

    /**
     * @brief ReleaseResources 释放当前 Pipeline 持有的播放资源
     */
    void ReleaseResources();

    /**
     * @brief HandleDemuxFinished 处理媒体源自然播放结束
     */
    void HandleDemuxFinished();

    /**
     * @brief HandleAudioPlaybackDrained 记录音频消费者已排空自然结束数据
     */
    void HandleAudioPlaybackDrained();

    /**
     * @brief HandleVideoPlaybackDrained 记录视频消费者已排空自然结束数据
     */
    void HandleVideoPlaybackDrained();

    /**
     * @brief TryFinalizePlayback 在全部自然结束条件满足后广播播放完成
     */
    void TryFinalizePlayback();

    /**
     * @brief ResetPlaybackCompletionState 重置本轮播放的自然结束汇合状态
     */
    void ResetPlaybackCompletionState();

    /**
     * @brief HandleDemuxError 处理解复用运行期错误
     * @param message 错误信息
     */
    void HandleDemuxError(const QString& message);

    /**
     * @brief SuspendPipelineAndWait 挂起 demuxer/decoder 管线并等待安全点
     * @return true 挂起成功
     * @return false 挂起失败
     */
    bool SuspendPipelineAndWait();

    /**
     * @brief ResetPipelineToStart 清空缓存并把管线重置到起始位置
     */
    void ResetPipelineToStart();

    /**
     * @brief ResetRealtimePipelineStop 停止实时源管线但不执行 seek 回零
     */
    void ResetRealtimePipelineStop();

    /**
     * @brief ClearQueues 清空音视频包队列和帧队列
     */
    void ClearQueues();

    /**
     * @brief FlushDecoders 清理解码器内部缓存
     */
    void FlushDecoders();

    /**
     * @brief ResumeQueues 清除队列的停止、中断和 EOS 状态
     */
    void ResumeQueues();

    /**
     * @brief StartFrameScheduler 在独立线程启动视频帧调度
     */
    void StartFrameScheduler();

    /**
     * @brief StopFrameScheduler 停止视频帧调度线程并等待退出
     */
    void StopFrameScheduler();

    /**
     * @brief InvokeOnFrameScheduler 把操作投递到调度线程
     * @param operation 待执行操作
     * @param blocking 是否阻塞等待执行完成
     */
    void InvokeOnFrameScheduler(std::function<void()> operation, bool blocking = false);

    std::shared_ptr<SyncQueue<AVPacketPtr>> APacket_ = nullptr;	///< 音频包队列
    std::shared_ptr<SyncQueue<AVPacketPtr>> VPacket_ = nullptr;	///< 视频包队列
    std::shared_ptr<SyncQueue<AVFramePtr>> AFrame_ = nullptr;	///< 音频帧队列
    std::shared_ptr<SyncQueue<AVFramePtr>> VFrame_ = nullptr;	///< 视频帧队列

    std::shared_ptr<IMediaSource> mediaSource_;	///< 当前媒体源
    std::shared_ptr<SyncManager> syncManager_;	///< 同步时钟管理器
    int sourceVideoWidth_ = 0;	///< 初始化时复制的源视频宽度
    int sourceVideoHeight_ = 0;	///< 初始化时复制的源视频高度
    AVCodecID sourceVideoCodecId_ = AV_CODEC_ID_NONE;	///< 初始化时复制的源视频编码

    std::unique_ptr<Demuxer> demuxer_;	///< 解复用器
    std::unique_ptr<Decoder> Adecoder_;	///< 音频解码器
    std::unique_ptr<Decoder> Vdecoder_;	///< 视频解码器
    std::unique_ptr<AudioPlayer> audioplay_;	///< 音频播放器
    std::unique_ptr<VideoFrameScheduler> videoFrameScheduler_;	///< 视频帧调度器
    VideoRender* videoRender_ = nullptr;	///< 默认视频渲染器
    IVideoFrameSink* activeVideoFrameSink_ = nullptr;	///< 当前视频帧输出目标
    std::unique_ptr<ImageProcessingFrameSink> imageProcessingFrameSink_;	///< 图像处理帧 sink
    VisionPipelineManager* visionPipelineManager_ = nullptr;	///< 视觉管线管理器，借用不拥有
    VisionAnalysisService* visionAnalysisService_ = nullptr;	///< 异步视觉分析服务，借用不拥有
    std::shared_ptr<VisionResultStore> visionResultStore_;	///< 视觉结果仓库
    std::unique_ptr<VisionProcessingFrameSink> visionProcessingFrameSink_;	///< 视觉分析帧 sink
    int streamId_ = -1;	///< 当前逻辑流 ID

    std::unique_ptr<QThread> frameThread_;	///< 视频帧调度线程
    std::unique_ptr<QThread> demuxerTH_;	///< 解复用线程
    std::unique_ptr<QThread> AdecoderTH_;	///< 音频解码线程
    std::unique_ptr<QThread> VdecoderTH_;	///< 视频解码线程
    std::unique_ptr<QThread> audioplayTH_;	///< 音频播放线程

    std::unique_ptr<QTimer> positionTimer_;	///< 播放位置刷新定时器
    bool playbackFinished_ = false;	///< 当前管线是否已自然播放结束
    bool demuxEndOfStream_ = false;	///< 解复用器是否已自然读到 EOS
    bool audioPlaybackDrained_ = true;	///< 音频消费者是否已排空，无码流时恒为 true
    bool videoPlaybackDrained_ = false;	///< 视频消费者是否已排空
    bool shuttingDown_ = false;	///< 资源释放期间抑制迟到的自然结束信号
};

#endif // TINYVIDEOPROCESSOR_MEDIAPIPELINE_H
