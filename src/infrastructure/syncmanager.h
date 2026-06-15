#ifndef SYNCMANAGER_H
#define SYNCMANAGER_H

#include <QObject>
#include <QMutex>

/*@brief 时钟同步模块，支持音频主时钟、视频主时钟、外部时钟
 *
 */

enum class SYNCTYPE {
    AUDIO,      ///< 音频主时钟
    VIDEO,      ///< 视频主时钟
    EXTERNAL    ///< 外部时钟
};

class syncmanager : public QObject
{
    Q_OBJECT
public:
    /**
     * @brief syncmanager 构造函数
     * @param parent Qt 父对象
     */
    explicit syncmanager(QObject *parent = nullptr);

    /**
     * @brief syncmanager 析构函数
     */
    ~syncmanager();

    /**
     * @brief RefreshClock
     * @param clockMs 新的主时钟，单位毫秒(ms)
     * @return 0 成功
     * @return -1 失败
     */
    int RefreshClock(double clockMs);

    /**
     * @brief GetClock 获取主时钟
     * @return double 返回主时钟副本
     */
    double GetClock() const;

    /**
     * @brief SeekTo 将主时钟重置到指定时间
     * @param clockMs 目标时钟，单位毫秒(ms)
     * @return 0 成功
     * @return -1 失败
     */
    int SeekTo(double clockMs);

    /**
     * @brief Forward 快进3s
     * @return 0 成功
     * @return -1 失败
     */
    int Forward();

signals:

private:
    /**
     * @brief mainclock 主时钟，单位毫秒(ms)
     */
    double mainclock = 0.0;

    SYNCTYPE currentSYNCTYPE = SYNCTYPE::AUDIO; // 默认同步方式为 音频主时钟

    mutable QMutex mutex;                           ///< 互斥锁（mutable 以允许 const 方法加锁）
};

#endif // SYNCMANAGER_H
