#ifndef DECODER_H
#define DECODER_H

#include <QObject>
#include <QMutex>
#include <QDebug>
#include <QThread>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libavutil/pixdesc.h>

}


class decoder : public QObject
{
    Q_OBJECT
public:
    explicit decoder(QObject *parent = nullptr);
    ~decoder();

    /**
     * @brief 创建信号槽
     */
    void createConnect();

    /**
     * @brief initDecoder 初始化解码器
     * @param packetVec 包队列的共享指针
     * @param frameVec 帧队列的共享指针
     * @param flag 标志位; 0-音频解码器; 1-视频解码器
     * @return 0 初始化解码器成功
     * @return -1 初始化解码器失败
     */
    int initDecoder(std::shared_ptr<QVector<AVPacket*>> packetVec
                    ,std::shared_ptr<QVector<AVFrame*>> frameVec
                    ,AVCodecParameters*
                    ,AVRational
        );

    /**
     * @brief startDecoder 从包队列中取包，开始解码，供主线程调用
     * @return 0 解码正常
     * @return -1 解码异常
     */
    int startDecoder();

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

    std::shared_ptr<QVector<AVPacket*>> m_PacketVec;        ///< 包队列
    std::shared_ptr<QVector<AVFrame*>> m_FrameVec;          ///< 帧队列


    AVCodecContext* Ctx = nullptr;                          ///< 解码器实例

    AVCodecParameters *par = nullptr;                       ///< 解码器参数---蓝图

    const AVCodec *codec = nullptr;                         ///< 解码器配置

    AVStream m_stream;                                      ///< 设置stream

    bool pause_flag = false;                                ///< 暂停标志

    bool stop_flag = false;                                 ///< 停止标志

    QMutex mutex;                                           ///< 互斥锁

    QString decoder_type = "";                              ///< 解码器类型

    AVFrame* frame = nullptr;                               ///< 管理解码后的帧

    AVRational m_time_base;                                 ///< 时间基础
};

#endif // DECODER_H
