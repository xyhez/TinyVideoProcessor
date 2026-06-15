#pragma  once
#ifndef TINYVIDEOPROCESSOR_MEDIAPLAYCONTROLLER_H
#define TINYVIDEOPROCESSOR_MEDIAPLAYCONTROLLER_H

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

constexpr size_t kPacketQueueMax = 30;
constexpr size_t kFrameQueueMax  = 30;

inline auto packetDeleter = [](AVPacket* p) { if (p) av_packet_free(&p); };
inline auto frameDeleter  = [](AVFrame*  f) { if (f) av_frame_free(&f);  };
/**
 * @brief
 * MediaPlayController是应用层的协调者，
 * 是MainWindow(UI层)和基础设施层组件(demuxer/decoder/player/render)之间的中间层。
 * MainWindow(UI) -按钮事件 -键盘事件 -显示窗口
 * MediaPlayerController(应用层) -协调所有播放组件 -管理生命周期 -处理业务逻辑
 * 基础设施层 -demuxer/decoder/audioplayer/videorender/syncmanager
 */
class MediaPlayController :public QObject{
    Q_OBJECT
public:
    enum class PlaybackState {
        Stopped,
        Playing,
        Paused
    };

    /**
     * @brief MediaPlayController 构造函数
     * @param parent Qt 父对象
     */
    explicit MediaPlayController(QObject* parent = nullptr);

    /**
     * @brief MediaPlayController 析构函数，释放播放管线资源
     */
    ~MediaPlayController();

    /**
     * @brief Initialize 初始化播放管线
     * @param mediaSource 媒体源抽象
     * @param rendererContainer 视频渲染容器
     * @return true 初始化成功
     * @return false 初始化失败
     */
    bool Initialize(std::shared_ptr<IMediaSource> mediaSource,
        QWidget* rendererContainer);

    /**
     * @brief SwitchSource 切换当前媒体源
     * @param mediaSource 新媒体源抽象
     * @param rendererContainer 视频渲染容器
     * @param autoStart 切换成功后是否立即播放
     * @return true 切换成功
     * @return false 切换失败
     */
    bool SwitchSource(std::shared_ptr<IMediaSource> mediaSource,
                      QWidget* rendererContainer,
                      bool autoStart);

    /**
     * @brief CreateConnects 创建 Controller 内部信号槽连接
     */
    void CreateConnects();

    /**
     * @brief Start 启动或恢复播放
     */
    void Start();

    /**
     * @brief Stop 停止播放并回到起始位置
     */
    void Stop();

    /**
     * @brief Pause 暂停播放
     */
    void Pause();

    /**
     * @brief VolumeUp 增大音量
     */
    void VolumeUp();

    /**
     * @brief VolumeDown 减小音量
     */
    void VolumeDown();

    /**
     * @brief SeekTo 跳转到指定播放位置
     * @param seconds 目标位置，单位秒
     */
    void SeekTo(double seconds);

    /**
     * @brief SeekRelative 相对当前位置跳转
     * @param seconds 相对跳转秒数，负数表示后退
     */
    void SeekRelative(double seconds);

    /**
     * @brief Rewind 后退固定时间
     */
    void Rewind();

    /**
     * @brief Forward 前进固定时间
     */
    void Forward();

    /**
     * @brief State 获取当前播放状态
     * @return 当前播放状态
     */
    PlaybackState State() const;

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
     * @brief SetPlaybackSpeed 设置播放速度
     * @param speed 支持的播放速度档位
     */
    void SetPlaybackSpeed(double speed);

    /**
     * @brief PlaybackSpeed 获取当前播放速度
     * @return 当前播放速度
     */
    double PlaybackSpeed() const;

signals:
    /**
     * @brief StateChanged 播放状态变化信号
     * @param state 新播放状态
     */
    void StateChanged(PlaybackState state);

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
     * @brief PlaybackSpeedChanged 播放速度变化信号
     * @param speed 当前播放速度
     */
    void PlaybackSpeedChanged(double speed);

private:
    // 队列
    std::shared_ptr<SyncQueue<AVPacket*>> APacket_ = nullptr;
    std::shared_ptr<SyncQueue<AVPacket*>> VPacket_ = nullptr;
    std::shared_ptr<SyncQueue<AVFrame*>>  AFrame_ = nullptr;
    std::shared_ptr<SyncQueue<AVFrame*>>  VFrame_ = nullptr;

    // 媒体源
    std::shared_ptr<IMediaSource> mediaSource_;

    // 同步
    std::shared_ptr<syncmanager> syncManager_;

    // 组件
    std::unique_ptr<demuxer> demuxer_;
    std::unique_ptr<decoder> Adecoder_;
    std::unique_ptr<decoder> Vdecoder_;
    std::unique_ptr<AudioPlayer> audioplay_;
    VideoRender* videoRender_ = nullptr;

    // 线程
    std::unique_ptr<QThread> demuxerTH_;
    std::unique_ptr<QThread> AdecoderTH_;
    std::unique_ptr<QThread> VdecoderTH_;
    std::unique_ptr<QThread> audioplayTH_;

    // 定时向主线程中提供当前视频播放进度
    std::unique_ptr<QTimer> positionTimer_ = nullptr;

    PlaybackState state_ = PlaybackState::Stopped;
    double playbackSpeed_ = 1.0;

    /**
     * @brief ReleaseResources 释放当前播放管线资源
     */
    void ReleaseResources();

    /**
     * @brief InitializeOpenedMediaSource 使用已打开的媒体源初始化播放管线
     * @param mediaSource 已打开的媒体源抽象
     * @param rendererContainer 视频渲染容器
     * @return true 初始化成功
     * @return false 初始化失败
     */
    bool InitializeOpenedMediaSource(std::shared_ptr<IMediaSource> mediaSource,
                                     QWidget* rendererContainer);

    /**
     * @brief SetState 设置播放状态并发出状态变化信号
     * @param state 目标播放状态
     */
    void SetState(PlaybackState state);

    /**
     * @brief SuspendPipelineAndWait 挂起 demuxer/decoder 管线并等待安全点
     * @return true 挂起成功
     * @return false 挂起失败
     */
    bool SuspendPipelineAndWait();

    /**
     * @brief RestorePlaybackState 按 Seek 前状态恢复播放管线
     * @param previousState Seek 前播放状态
     */
    void RestorePlaybackState(PlaybackState previousState);

    /**
     * @brief ResetPipelineToStart 清空缓存并把管线重置到起始位置
     */
    void ResetPipelineToStart();

    /**
     * @brief ClearQueues 清空音视频包队列和帧队列
     */
    void ClearQueues();

    /**
     * @brief FlushDecoders 清理解码器内部缓存
     */
    void FlushDecoders();
};


#endif //TINYVIDEOPROCESSOR_MEDIAPLAYCONTROLLER_H
