#ifndef DEMUXER_H
#define DEMUXER_H

#include <QObject>
#include <QMutex>
#include <QDebug>
#include <QThread>
#include <QWaitCondition>
#include <memory>

#include "core/queue.h"
#include "domain/IMediaSource.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
}

///< 视频参数
struct videoparam {

};




class demuxer : public QObject
{
    Q_OBJECT
public:
    /**
     * @brief demuxer 构造函数
     * @param mediaSource 媒体源抽象
     * @param APacket 音频包队列
     * @param VPacket 视频包队列
     * @param AFrame 音频帧队列
     * @param VFrame 视频帧队列
     * @param parent Qt 父对象
     */
    explicit demuxer(std::shared_ptr<IMediaSource> mediaSource
                     ,std::shared_ptr<SyncQueue<AVPacket*>> APacket = nullptr
                     ,std::shared_ptr<SyncQueue<AVPacket*>> VPacket = nullptr
                     ,std::shared_ptr<SyncQueue<AVFrame*>>  AFrame  = nullptr
                     ,std::shared_ptr<SyncQueue<AVFrame*>>  VFrame  = nullptr
                     ,QObject *parent = nullptr);

    /**
     * @brief demuxer 析构函数
     */
    ~demuxer();

    /**
     * @brief ReAcoderPar
     * @return 返回音频解码器配置指针
     */
    AVCodecParameters* ReAcoderPar();

    /**
     * @brief ReVcoderPar
     * @return 返回视频解码器配置指针
     */
    AVCodecParameters* ReVcoderPar();

    /**
     * @brief SetAudioQueues 绑定可选音频队列
     * @param APacket 音频包队列
     * @param AFrame 音频帧队列
     */
    void SetAudioQueues(std::shared_ptr<SyncQueue<AVPacket*>> APacket,
                        std::shared_ptr<SyncQueue<AVFrame*>> AFrame);

    /**
     * @brief ReAtimebase
     * @return 音频流 time_base
     */
    AVRational ReAtimebase();

    /**
     * @brief ReVtimebase
     * @return 视频流 time_base
     */
    AVRational ReVtimebase();


    /**
     * @brief 解复用工作函数 待主线程调用
     *
     * @return 0 解复用正常运行
     * @return -1 解复用异常
     */
    int StartDemuxer();

    /**
     * @brief SeekTo 跳转到指定秒数
     * @return 0 成功
     * @return -1 失败
     */
    int SeekTo(double seconds);

    /**
     * @brief SeekRelative 按当前读取位置相对跳转指定秒数
     * @return 0 成功
     * @return -1 失败
     */
    int SeekRelative(double seconds);

    /**
     * @brief SuspendAndWait 进入管线挂起安全点
     * @return 0 成功
     * @return -1 已停止
     */
    int SuspendAndWait();

    /**
     * @brief Position 获取当前读取位置，单位秒
     * @return 当前读取位置
     */
    double Position() const;

    /**
     * @brief Rewind 倒退10s
     * @return  0 成功
     */
    int Rewind();

    /**
     * @brief Forward 前进10s
     * @return  0 成功
     */
    int Forward();

    /**
     * @brief Start
     * @return 0 成功
     * @return -1 失败
     */
    int Start();

    /**
     * @brief Stop
     * @return 0 成功
     * @return -1 失败
     */
    int Stop();



    /**
     * @brief Pause
     * @return 0 成功
     * @return -1 失败
     */

    int Pause();

signals:
    /**
     * @brief DemuxFinished 解复用自然读到文件尾
     */
    void DemuxFinished();


private:
    std::shared_ptr<IMediaSource> m_mediaSource;        ///< 媒体源
    std::shared_ptr<SyncQueue<AVPacket*>> m_APacket;   ///< 音频包队列
    std::shared_ptr<SyncQueue<AVPacket*>> m_VPacket;   ///< 视频包队列
    std::shared_ptr<SyncQueue<AVFrame*>>  m_AFrame;    ///< 音频帧队列
    std::shared_ptr<SyncQueue<AVFrame*>>  m_VFrame;    ///< 视频帧队列

    bool pause_flag = false;                            ///< 暂停标志

    bool stop_flag = false;                             ///< 停止标志

    bool suspend_requested_ = false;                    ///< 管线挂起请求
    bool suspended_ = false;                            ///< 已到达管线挂起安全点
    QWaitCondition suspendCondition_;                   ///< 管线挂起条件变量

    mutable QMutex mutex;                               ///< 互斥锁

    /**
     * @brief CurrentPositionSeconds 获取当前解复用读取位置
     * @return 当前读取位置，单位秒
     */
    double CurrentPositionSeconds() const;

    /**
     * @brief WaitIfSuspended 如果收到挂起请求则等待恢复
     * @return true 可以继续运行
     * @return false 已停止
     */
    bool WaitIfSuspended();

    /**
     * @brief IsSuspendRequested 判断是否请求管线挂起
     * @return true 已请求挂起
     * @return false 未请求挂起
     */
    bool IsSuspendRequested() const;

    AVPacket* packet = nullptr;                         ///< 存从文件中读取出来的包

    int m_videoStreamIndex = -1;                        ///< 视频流下标，用于区分packet类型

    int m_audioStreamIndex = -1;                        ///< 音频流下标，用于区分packet类型

    AVCodecParameters* m_A_coderPar = nullptr;          ///< 音频流解码器配置
    AVCodecParameters* m_V_coderPar = nullptr;          ///< 视频流解码器配置

    AVRational m_A_time_base;                           ///< 音频流time_base
    AVRational m_V_time_base;                           ///< 视频流time_base


    int64_t m_AcurrentPTS = 0;                          ///< 最新的音频流的pts
    int64_t m_VcurrentPTS = 0;                          ///< 最新的视频流的pts
};

#endif // DEMUXER_H
