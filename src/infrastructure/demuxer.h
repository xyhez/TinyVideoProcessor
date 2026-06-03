#ifndef DEMUXER_H
#define DEMUXER_H

#include <QObject>
#include <QMutex>
#include <QDebug>
#include <QThread>

#include "global.h"
#include "core/queue.h"

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
    explicit demuxer(std::shared_ptr<std::deque<AVPacket*>> = nullptr
                     ,std::shared_ptr<std::deque<AVPacket*>> = nullptr
                     ,std::shared_ptr<std::deque<AVFrame*>> = nullptr
                     ,std::shared_ptr<std::deque<AVFrame*>> = nullptr
                     ,QObject *parent = nullptr);
    ~demuxer();

    /**
     * @brief 初始化解复用器
     *
     * @param filename 待播放视频的文件名
     * @return 0 初始化
     * @return -1 初始化失败
     */
    int initDemuxer(QString filename);

    /**
     * @brief reAcoderPar
     * @return 返回音频解码器配置指针
     */
    AVCodecParameters* reAcoderPar();

    /**
     * @brief reVcoderPar
     * @return 返回视频解码器配置指针
     */
    AVCodecParameters* reVcoderPar();

    /**
     * @brief reAtimebase
     * @return
     */
    AVRational reAtimebase();

    /**
     * @brief reVtimebase
     * @return
     */
    AVRational reVtimebase();


    /**
     * @brief 解复用工作函数 待主线程调用
     *
     * @return 0 解复用正常运行
     * @return -1 解复用异常
     */
    int startDemuxer();

    /**
     * @brief Rewind 倒退3s
     * @return  0 成功
     */
    int Rewind();

    /**
     * @brief Forward 前进3s---300
     * @return  0 成功
     */
    int Forward();

    /**
     * @brief start
     * @return 0 成功
     * @return -1 失败
     */
    int start();

    /**
     * @brief stop
     * @return 0 成功
     * @return -1 失败
     */
    int stop();



    /**
     * @brief pause
     * @return 0 成功
     * @return -1 失败
     */

    int pause();

signals:


private:
    std::shared_ptr<std::deque<AVPacket*>> m_APacket;       ///< 音频包队列
    std::shared_ptr<std::deque<AVPacket*>> m_VPacket;       ///< 视频包队列
    std::shared_ptr<std::deque<AVFrame*>> m_AFrame;         ///< 音频帧队列
    std::shared_ptr<std::deque<AVFrame*>> m_VFrame;         ///< 视频帧队列

    QString m_videoFileName;                            ///< 视频的文件名

    AVFormatContext* ic = nullptr;                      ///< 文件上下文

    bool pause_flag = false;                            ///< 暂停标志

    bool stop_flag = false;                             ///< 停止标志

    QMutex mutex;                                       ///< 互斥锁

    AVPacket* packet = nullptr;                         ///< 存从文件中读取出来的包

    int m_videoStreamIndex = -1;                        ///< 视频流下标，用于区分packet类型

    int m_audioStreamIndex = -1;                        ///< 音频流下标，用于区分packet类型

    AVCodecParameters* m_A_coderPar = nullptr;          ///< 音频流解码器配置
    AVCodecParameters* m_V_coderPar = nullptr;          ///< 视频流解码器配置

    AVRational m_A_time_base;                           ///< 音频流time_base
    AVRational m_V_time_base;                           ///< 视频流time_base


    int m_AcurrentPTS = 0;                              ///< 最新的音频流的pts
    int m_VcurrentPTS = 0;                              ///< 最新的视频流的pts
};

#endif // DEMUXER_H
