#ifndef TINYVIDEOPROCESSOR_MEDIAPIPELINE_H
#define TINYVIDEOPROCESSOR_MEDIAPIPELINE_H

#include <QObject>
#include <QThread>
#include <QTimer>
#include <memory>

#include "core/queue.h"
#include "domain/IMediaSource.h"
#include "infrastructure/audioplayer.h"
#include "infrastructure/decoder.h"
#include "infrastructure/demuxer.h"
#include "infrastructure/syncmanager.h"
#include "infrastructure/videorender.h"

class QWidget;
class IVideoFrameSink;

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
     * @brief MediaPipeline 构造函数
     * @param parent Qt 父对象
     */
    explicit MediaPipeline(QObject* parent = nullptr);

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
     * @brief SetPlaybackSpeed 设置播放速度
     * @param speed 播放速度档位
     */
    void SetPlaybackSpeed(double speed);

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

    std::shared_ptr<SyncQueue<AVPacket*>> APacket_ = nullptr;
    std::shared_ptr<SyncQueue<AVPacket*>> VPacket_ = nullptr;
    std::shared_ptr<SyncQueue<AVFrame*>> AFrame_ = nullptr;
    std::shared_ptr<SyncQueue<AVFrame*>> VFrame_ = nullptr;

    std::shared_ptr<IMediaSource> mediaSource_;
    std::shared_ptr<syncmanager> syncManager_;

    std::unique_ptr<demuxer> demuxer_;
    std::unique_ptr<decoder> Adecoder_;
    std::unique_ptr<decoder> Vdecoder_;
    std::unique_ptr<AudioPlayer> audioplay_;
    VideoRender* videoRender_ = nullptr;

    std::unique_ptr<QThread> demuxerTH_;
    std::unique_ptr<QThread> AdecoderTH_;
    std::unique_ptr<QThread> VdecoderTH_;
    std::unique_ptr<QThread> audioplayTH_;

    std::unique_ptr<QTimer> positionTimer_;
};

#endif // TINYVIDEOPROCESSOR_MEDIAPIPELINE_H
