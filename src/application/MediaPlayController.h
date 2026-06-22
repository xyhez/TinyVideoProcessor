#pragma  once
#ifndef TINYVIDEOPROCESSOR_MEDIAPLAYCONTROLLER_H
#define TINYVIDEOPROCESSOR_MEDIAPLAYCONTROLLER_H

#include <QList>
#include <QObject>
#include <QString>
#include <memory>

#include "domain/IMediaSource.h"

class MultiStreamManager;
class QWidget;

/**
 * @brief
 * MediaPlayController是应用层的协调者，
 * 是 MainWindow(UI 层) 和 MultiStreamManager(多路播放管理器) 之间的中间层。
 * MainWindow(UI) 负责按钮事件、键盘事件和显示窗口。
 * MediaPlayController(应用层) 负责播放状态编排和用户级参数保存。
 * MultiStreamManager(领域层) 负责播放管线集合和 active stream 管理。
 */
class MediaPlayController :public QObject{
    Q_OBJECT
public:
    enum class PlaybackState {
        Stopped,
        Playing,
        Paused
    };

    using StreamId = int;
    static constexpr StreamId InvalidStreamId = -1;

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
     * @brief AddStream 添加一路媒体源
     * @param mediaSource 媒体源抽象
     * @param rendererContainer 视频渲染容器
     * @return 新增流 ID，失败返回 InvalidStreamId
     */
    StreamId AddStream(std::shared_ptr<IMediaSource> mediaSource,
                       QWidget* rendererContainer);

    /**
     * @brief RemoveStream 移除一路媒体源
     * @param id 目标流 ID
     * @return true 移除成功
     */
    bool RemoveStream(StreamId id);

    /**
     * @brief StreamIds 获取当前全部流 ID
     * @return 流 ID 列表
     */
    QList<StreamId> StreamIds() const;

    /**
     * @brief ActiveStream 获取当前活动显示流
     * @return 当前活动显示流 ID
     */
    StreamId ActiveStream() const;

    /**
     * @brief SetActiveStream 设置当前活动显示流
     * @param id 目标流 ID
     * @return true 设置成功
     */
    bool SetActiveStream(StreamId id);

    /**
     * @brief ActiveAudioStream 获取当前活动音频流
     * @return 当前活动音频流 ID
     */
    StreamId ActiveAudioStream() const;

    /**
     * @brief SetActiveAudioStream 设置当前活动音频流
     * @param id 目标流 ID
     * @return true 设置成功
     */
    bool SetActiveAudioStream(StreamId id);

    /**
     * @brief StreamHasAudio 判断指定媒体流是否有音频
     * @param id 目标流 ID
     * @return true 有音频
     * @return false 不存在或没有音频
     */
    bool StreamHasAudio(StreamId id) const;

    /**
     * @brief IsActiveStreamSeekable 判断当前活动流是否支持 seek
     * @return true 支持 seek
     * @return false 不支持 seek 或没有活动流
     */
    bool IsActiveStreamSeekable() const;

    /**
     * @brief HasActiveStreamDuration 判断当前活动流是否有确定时长
     * @return true 有确定时长
     * @return false 无确定时长或没有活动流
     */
    bool HasActiveStreamDuration() const;

    /**
     * @brief IsActiveStreamRealtime 判断当前活动流是否为实时源
     * @return true 实时源
     * @return false 非实时源或没有活动流
     */
    bool IsActiveStreamRealtime() const;

    /**
     * @brief StartAll 启动或恢复全部媒体流
     */
    void StartAll();

    /**
     * @brief PauseAll 暂停全部媒体流
     */
    void PauseAll();

    /**
     * @brief StopAll 停止全部媒体流
     */
    void StopAll();

    /**
     * @brief Start 启动或恢复播放
     */
    void Start();

    /**
     * @brief Stop 停止播放并回到起始位置
     */
    void Stop();

    /**
     * @brief Shutdown 关闭并释放当前播放管线
     */
    void Shutdown();

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
     * @brief SetVolume 设置音量
     * @param volume 音量值，范围 0.0-1.0
     */
    void SetVolume(double volume);

    /**
     * @brief Volume 获取当前音量
     * @return 当前音量，范围 0.0-1.0
     */
    double Volume() const;

    /**
     * @brief SetMuted 设置静音状态
     * @param muted 是否静音
     */
    void SetMuted(bool muted);

    /**
     * @brief IsMuted 获取当前静音状态
     * @return true 已静音
     * @return false 未静音
     */
    bool IsMuted() const;

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
     * @brief StreamAdded 媒体流添加信号
     * @param id 新增流 ID
     */
    void StreamAdded(MediaPlayController::StreamId id);

    /**
     * @brief StreamRemoved 媒体流移除信号
     * @param id 被移除流 ID
     */
    void StreamRemoved(MediaPlayController::StreamId id);

    /**
     * @brief ActiveStreamChanged 活动显示流变化信号
     * @param id 新活动显示流 ID
     */
    void ActiveStreamChanged(MediaPlayController::StreamId id);

    /**
     * @brief ActiveAudioStreamChanged 活动音频流变化信号
     * @param id 新活动音频流 ID
     */
    void ActiveAudioStreamChanged(MediaPlayController::StreamId id);

    /**
     * @brief StreamErrorOccurred 媒体流错误信号
     * @param id 发生错误的流 ID
     * @param message 错误信息
     */
    void StreamErrorOccurred(MediaPlayController::StreamId id, QString message);

    /**
     * @brief StreamPositionChanged 指定媒体流播放位置变化信号
     * @param id 目标流 ID
     * @param seconds 当前播放位置，单位秒
     */
    void StreamPositionChanged(MediaPlayController::StreamId id, double seconds);

    /**
     * @brief StreamDurationChanged 指定媒体流时长变化信号
     * @param id 目标流 ID
     * @param seconds 媒体总时长，单位秒
     */
    void StreamDurationChanged(MediaPlayController::StreamId id, double seconds);

    /**
     * @brief StreamPlaybackFinished 指定媒体流自然播放结束信号
     * @param id 目标流 ID
     */
    void StreamPlaybackFinished(MediaPlayController::StreamId id);

    /**
     * @brief StreamVolumeChanged 指定媒体流音量变化信号
     * @param id 目标流 ID
     * @param volume 当前音量，范围 0.0-1.0
     */
    void StreamVolumeChanged(MediaPlayController::StreamId id, double volume);

    /**
     * @brief StreamMuteChanged 指定媒体流静音状态变化信号
     * @param id 目标流 ID
     * @param muted 是否静音
     */
    void StreamMuteChanged(MediaPlayController::StreamId id, bool muted);

private:
    std::unique_ptr<MultiStreamManager> multiStreamManager_;

    PlaybackState state_ = PlaybackState::Stopped;
    double playbackSpeed_ = 1.0;
    double volume_ = 1.0;
    bool muted_ = false;

    /**
     * @brief ConnectManagerSignals 创建 Controller 与 MultiStreamManager 的信号转发连接
     */
    void ConnectManagerSignals();

    /**
     * @brief ApplyUserSettingsToActiveStream 将当前用户级播放参数下发给 active stream
     */
    void ApplyUserSettingsToActiveStream();

    /**
     * @brief SetState 设置播放状态并发出状态变化信号
     * @param state 目标播放状态
     */
    void SetState(PlaybackState state);

    /**
     * @brief RestorePlaybackState 按 Seek 前状态恢复播放管线
     * @param previousState Seek 前播放状态
     */
    void RestorePlaybackState(PlaybackState previousState);
};


#endif //TINYVIDEOPROCESSOR_MEDIAPLAYCONTROLLER_H
