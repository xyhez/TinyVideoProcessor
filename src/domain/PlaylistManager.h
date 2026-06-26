#pragma once

#include <QObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

struct PlaylistItem {
    QString filePath;	///< 文件路径
    QString displayName;	///< 显示名称
    double durationSeconds = 0.0;	///< 媒体时长，单位秒
};

class PlaylistManager : public QObject {
    Q_OBJECT

public:
    enum class PlayMode {
        Sequential,
        ListLoop,
        SingleLoop,
        Random
    };

    /**
     * @brief PlaylistManager 构造函数
     * @param parent Qt 父对象
     */
    explicit PlaylistManager(QObject* parent = nullptr);

    /**
     * @brief Count 获取播放列表条目数量
     * @return 条目数量
     */
    int Count() const;

    /**
     * @brief CurrentIndex 获取当前条目索引
     * @return 当前索引，空列表时为 -1
     */
    int CurrentIndex() const;

    /**
     * @brief Items 获取播放列表条目副本
     * @return 播放列表条目集合
     */
    QList<PlaylistItem> Items() const;

    /**
     * @brief CurrentItem 获取当前播放条目
     * @return 当前条目；无有效当前项时返回 std::nullopt
     */
    std::optional<PlaylistItem> CurrentItem() const;

    /**
     * @brief AddFile 添加单个文件
     * @param filePath 文件路径
     */
    void AddFile(const QString& filePath);

    /**
     * @brief AddFiles 批量添加文件
     * @param filePaths 文件路径集合
     */
    void AddFiles(const QStringList& filePaths);

    /**
     * @brief RemoveAt 删除指定索引的条目
     * @param index 待删除条目索引
     */
    void RemoveAt(int index);

    /**
     * @brief Clear 清空播放列表
     */
    void Clear();

    /**
     * @brief SetCurrentIndex 设置当前条目索引
     * @param index 目标索引
     * @return true 设置成功
     * @return false 索引无效
     */
    bool SetCurrentIndex(int index);

    /**
     * @brief MoveNext 根据播放模式移动到下一项
     * @return true 移动或保持当前项成功
     * @return false 空列表或顺序模式已到末尾
     */
    bool MoveNext();

    /**
     * @brief MovePrevious 根据播放模式移动到上一项
     * @return true 移动或保持当前项成功
     * @return false 空列表或顺序模式已到开头
     */
    bool MovePrevious();

    /**
     * @brief SetPlayMode 设置播放模式
     * @param mode 播放模式
     */
    void SetPlayMode(PlayMode mode);

    /**
     * @brief GetPlayMode 获取当前播放模式
     * @return 当前播放模式
     */
    PlayMode GetPlayMode() const;

signals:
    /**
     * @brief PlaylistChanged 播放列表内容变化信号
     */
    void PlaylistChanged();

    /**
     * @brief CurrentIndexChanged 当前索引变化信号
     * @param index 新当前索引
     */
    void CurrentIndexChanged(int index);

    /**
     * @brief PlayModeChanged 播放模式变化信号
     * @param mode 新播放模式
     */
    void PlayModeChanged(PlayMode mode);

private:
    /**
     * @brief AddFileInternal 添加文件的内部实现，不发信号
     * @param filePath 文件路径
     * @return true 列表发生变化
     * @return false 文件无效或已存在
     */
    bool AddFileInternal(const QString& filePath);

    /**
     * @brief NormalizePath 规范化文件路径用于去重
     * @param filePath 原始文件路径
     * @return 规范化后的路径，空路径返回空字符串
     */
    QString NormalizePath(const QString& filePath) const;

    QList<PlaylistItem> items_;	///< 播放列表条目
    int currentIndex_ = -1;	///< 当前播放索引
    PlayMode playMode_ = PlayMode::Sequential;	///< 当前播放模式
};

