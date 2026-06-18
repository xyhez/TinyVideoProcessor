#ifndef TINYVIDEOPROCESSOR_MULTISTREAMMANAGER_H
#define TINYVIDEOPROCESSOR_MULTISTREAMMANAGER_H

#include <QList>
#include <QObject>
#include <QString>

#include <map>
#include <memory>

#include "domain/IMediaSource.h"
#include "domain/MediaPipeline.h"

class QWidget;

/**
 * @brief MultiStreamManager 管理多路播放管线的外层身份和状态
 *
 * MultiStreamManager 只在外层管理 StreamId 和多个单路 MediaPipeline，
 * MediaPipeline 本身不感知 id，也不承担多路调度职责。
 */
class MultiStreamManager : public QObject {
    Q_OBJECT

public:
    using StreamId = int;
    static constexpr StreamId InvalidStreamId = -1;

    /**
     * @brief MultiStreamManager 构造函数
     * @param parent Qt 父对象
     */
    explicit MultiStreamManager(QObject* parent = nullptr);

    /**
     * @brief AddStream 添加并初始化一路播放管线
     * @param mediaSource 媒体源抽象
     * @param rendererContainer 视频渲染容器
     * @return 新分配的 StreamId
     */
    StreamId AddStream(std::shared_ptr<IMediaSource> mediaSource,
                       QWidget* rendererContainer);

    /**
     * @brief RemoveStream 移除指定逻辑流
     * @param id 待移除的 StreamId
     * @return true 移除成功
     * @return false id 不存在
     */
    bool RemoveStream(StreamId id);

    /**
     * @brief HasStream 判断指定逻辑流是否存在
     * @param id 待检查的 StreamId
     * @return true 存在
     * @return false 不存在
     */
    bool HasStream(StreamId id) const;

    /**
     * @brief StreamIds 获取当前所有逻辑流 id
     * @return StreamId 列表
     */
    QList<StreamId> StreamIds() const;

    /**
     * @brief ActiveStream 获取当前活动显示流
     * @return 当前活动 StreamId；无活动流时返回 InvalidStreamId
     */
    StreamId ActiveStream() const;

    /**
     * @brief SetActiveStream 设置当前活动显示流
     * @param id 目标 StreamId，允许 InvalidStreamId 清空状态
     * @return true 设置成功
     * @return false id 不存在
     */
    bool SetActiveStream(StreamId id);

    /**
     * @brief ActiveAudioStream 获取当前活动音频流
     * @return 当前活动音频 StreamId；无活动音频流时返回 InvalidStreamId
     */
    StreamId ActiveAudioStream() const;

    /**
     * @brief SetActiveAudioStream 设置当前活动音频流
     * @param id 目标 StreamId，允许 InvalidStreamId 清空状态
     * @return true 设置成功
     * @return false id 不存在
     */
    bool SetActiveAudioStream(StreamId id);

    /**
     * @brief ShutdownAll 清空全部逻辑流状态
     */
    void ShutdownAll();

    /**
     * @brief StartAll 启动或恢复全部逻辑流
     */
    void StartAll();

    /**
     * @brief PauseAll 暂停全部逻辑流
     */
    void PauseAll();

    /**
     * @brief StopAll 停止全部逻辑流
     */
    void StopAll();

    /**
     * @brief StartStream 启动或恢复指定逻辑流
     * @param id 目标 StreamId
     * @return true 操作已下发
     * @return false id 不存在
     */
    bool StartStream(StreamId id);

    /**
     * @brief PauseStream 暂停指定逻辑流
     * @param id 目标 StreamId
     * @return true 操作已下发
     * @return false id 不存在
     */
    bool PauseStream(StreamId id);

    /**
     * @brief StopStream 停止指定逻辑流
     * @param id 目标 StreamId
     * @return true 操作已下发
     * @return false id 不存在
     */
    bool StopStream(StreamId id);

    /**
     * @brief SeekStreamTo 跳转指定逻辑流到目标位置
     * @param id 目标 StreamId
     * @param seconds 目标位置，单位秒
     * @return true 跳转成功
     * @return false id 不存在或跳转失败
     */
    bool SeekStreamTo(StreamId id, double seconds);

    /**
     * @brief SeekStreamRelative 按当前位置相对跳转指定逻辑流
     * @param id 目标 StreamId
     * @param seconds 相对跳转秒数，负数表示后退
     * @return true 跳转成功
     * @return false id 不存在或跳转失败
     */
    bool SeekStreamRelative(StreamId id, double seconds);

    /**
     * @brief SetStreamPlaybackSpeed 设置指定逻辑流播放速度
     * @param id 目标 StreamId
     * @param speed 播放速度
     * @return true 操作已下发
     * @return false id 不存在
     */
    bool SetStreamPlaybackSpeed(StreamId id, double speed);

    /**
     * @brief SetStreamVolume 设置指定逻辑流音量
     * @param id 目标 StreamId
     * @param volume 音量值，范围 0.0-1.0
     * @return true 操作已下发
     * @return false id 不存在
     */
    bool SetStreamVolume(StreamId id, double volume);

    /**
     * @brief SetStreamMuted 设置指定逻辑流静音状态
     * @param id 目标 StreamId
     * @param muted 是否静音
     * @return true 操作已下发
     * @return false id 不存在
     */
    bool SetStreamMuted(StreamId id, bool muted);

signals:
    /**
     * @brief StreamAdded 逻辑流添加信号
     * @param id 新增的 StreamId
     */
    void StreamAdded(MultiStreamManager::StreamId id);

    /**
     * @brief StreamRemoved 逻辑流移除信号
     * @param id 被移除的 StreamId
     */
    void StreamRemoved(MultiStreamManager::StreamId id);

    /**
     * @brief ActiveStreamChanged 活动显示流变化信号
     * @param id 新活动 StreamId
     */
    void ActiveStreamChanged(MultiStreamManager::StreamId id);

    /**
     * @brief ActiveAudioStreamChanged 活动音频流变化信号
     * @param id 新活动音频 StreamId
     */
    void ActiveAudioStreamChanged(MultiStreamManager::StreamId id);

    /**
     * @brief StreamErrorOccurred 指定逻辑流错误信号
     * @param id 发生错误的 StreamId
     * @param message 错误信息
     */
    void StreamErrorOccurred(MultiStreamManager::StreamId id, QString message);

    /**
     * @brief StreamPositionChanged 指定逻辑流播放位置变化信号
     * @param id 发生变化的 StreamId
     * @param seconds 当前播放位置，单位秒
     */
    void StreamPositionChanged(MultiStreamManager::StreamId id, double seconds);

    /**
     * @brief StreamDurationChanged 指定逻辑流媒体时长变化信号
     * @param id 发生变化的 StreamId
     * @param seconds 媒体总时长，单位秒
     */
    void StreamDurationChanged(MultiStreamManager::StreamId id, double seconds);

    /**
     * @brief StreamPlaybackFinished 指定逻辑流自然播放结束信号
     * @param id 播放结束的 StreamId
     */
    void StreamPlaybackFinished(MultiStreamManager::StreamId id);

    /**
     * @brief StreamVolumeChanged 指定逻辑流音量变化信号
     * @param id 发生变化的 StreamId
     * @param volume 当前音量，范围 0.0-1.0
     */
    void StreamVolumeChanged(MultiStreamManager::StreamId id, double volume);

    /**
     * @brief StreamMuteChanged 指定逻辑流静音状态变化信号
     * @param id 发生变化的 StreamId
     * @param muted 是否静音
     */
    void StreamMuteChanged(MultiStreamManager::StreamId id, bool muted);

private:
    struct StreamSlot {
        std::shared_ptr<IMediaSource> mediaSource;
        QWidget* rendererContainer = nullptr;
        std::unique_ptr<MediaPipeline> pipeline;
    };

    /**
     * @brief SelectFallbackStream 选择可用的兜底流
     * @return 可用 StreamId；无流时返回 InvalidStreamId
     */
    StreamId SelectFallbackStream() const;

    /**
     * @brief FindPipeline 查找指定逻辑流的播放管线
     * @param id 目标 StreamId
     * @return MediaPipeline 指针；id 不存在时返回 nullptr
     */
    MediaPipeline* FindPipeline(StreamId id);

    /**
     * @brief ConnectPipelineSignals 转发指定 Pipeline 的播放信号
     * @param id Pipeline 对应的 StreamId
     * @param pipeline 单路播放管线
     */
    void ConnectPipelineSignals(StreamId id, MediaPipeline* pipeline);

    std::map<StreamId, StreamSlot> streams_;
    StreamId nextStreamId_ = 1;
    StreamId activeStreamId_ = InvalidStreamId;
    StreamId activeAudioStreamId_ = InvalidStreamId;
};

#endif // TINYVIDEOPROCESSOR_MULTISTREAMMANAGER_H
