#ifndef TINYVIDEOPROCESSOR_MULTISTREAMMANAGER_H
#define TINYVIDEOPROCESSOR_MULTISTREAMMANAGER_H

#include <QList>
#include <QObject>
#include <QString>

/**
 * @brief MultiStreamManager 管理多路播放管线的外层身份和状态
 *
 * Phase 3.2.2 先建立多路管理空壳，只管理 StreamId、active stream
 * 和 active audio stream，不创建也不持有真实 MediaPipeline。
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
     * @brief AddStream 添加一路逻辑流
     * @return 新分配的 StreamId
     */
    StreamId AddStream();

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

private:
    /**
     * @brief SelectFallbackStream 选择可用的兜底流
     * @return 可用 StreamId；无流时返回 InvalidStreamId
     */
    StreamId SelectFallbackStream() const;

    QList<StreamId> streamIds_;
    StreamId nextStreamId_ = 1;
    StreamId activeStreamId_ = InvalidStreamId;
    StreamId activeAudioStreamId_ = InvalidStreamId;
};

#endif // TINYVIDEOPROCESSOR_MULTISTREAMMANAGER_H
