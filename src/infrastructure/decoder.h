#ifndef DECODER_H
#define DECODER_H

#include <QObject>
#include <QMutex>
#include <QDebug>
#include <QThread>
#include <QWaitCondition>
#include <memory>

#include "core/queue.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libavutil/pixdesc.h>
#include <libavutil/hwcontext.h>  // 硬解码需要
#include <libswscale/swscale.h>
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
    void CreateConnect();

    /**
     * @brief InitDecoder 初始化解码器
     * @param packetVec 包队列的共享指针
     * @param frameVec 帧队列的共享指针
     * @param flag 标志位; 0-音频解码器; 1-视频解码器
     * @return 0 初始化解码器成功
     * @return -1 初始化解码器失败
     */
    int InitDecoder(std::shared_ptr<SyncQueue<AVPacket*>> packetVec
                    ,std::shared_ptr<SyncQueue<AVFrame*>> frameVec
                    ,AVCodecParameters *
                    ,AVRational
                    );

    /**
     * @brief StartDecoder 从包队列中取包，开始解码，供主线程调用
     * @return 0 解码正常
     * @return -1 解码异常
     */
    int StartDecoder();

    /**
     * @brief Flush 清理解码器内部缓存
     * @return 0 成功
     * @return -1 失败
     */
    int Flush();

    /**
     * @brief SuspendAndWait 进入管线挂起安全点
     * @return 0 成功
     * @return -1 已停止
     */
    int SuspendAndWait();

    /**
     * @brief SetDiscardBefore 丢弃指定时间点之前的输出帧
     * @param seconds 目标时间，单位秒
     */
    void SetDiscardBefore(double seconds);

    /**
     * @brief ClearDiscardBefore 清除输出帧丢弃边界
     */
    void ClearDiscardBefore();

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

private:
    /**
     * @brief InitHWDecoder 初始化硬件解码器
     * @return true 成功
     * @return false 失败
     */
    bool InitHWDecoder();

    /**
     * @brief TransferHWFrame 将硬件帧转换为系统内存帧
     * @param hwFrame 硬件帧
     * @return AVFrame* 转换后的帧，失败返回nullptr
     */
    AVFrame *TransferHWFrame(AVFrame *hwFrame);

    bool ShouldDiscardFrame(const AVFrame *candidateFrame);
    bool WaitIfSuspended();
    bool IsSuspendRequested() const;

    std::shared_ptr<SyncQueue<AVPacket*>> m_PacketVec;        ///< 包队列
    std::shared_ptr<SyncQueue<AVFrame*>>  m_FrameVec;         ///< 帧队列


    AVCodecContext *Ctx = nullptr;                          ///< 解码器实例

    AVCodecParameters *par = nullptr;                       ///< 解码器参数---蓝图

    const AVCodec *codec = nullptr;                         ///< 解码器配置

    AVStream m_stream;                                      ///< 设置stream

    bool pause_flag = false;                                ///< 暂停标志

    bool stop_flag = false;                                 ///< 停止标志

    bool suspend_requested_ = false;                        ///< 管线挂起请求
    bool suspended_ = false;                                ///< 已到达管线挂起安全点
    QWaitCondition suspendCondition_;                       ///< 管线挂起条件变量

    mutable QMutex mutex;                                   ///< 互斥锁

    QString decoder_type = "";                              ///< 解码器类型

    AVFrame *frame = nullptr;                               ///< 管理解码后的帧

    AVRational m_time_base;                                 ///< 时间基础

    bool m_discardBeforeEnabled = false;                    ///< 是否丢弃指定时间前的输出帧
    double m_discardBeforeSeconds = 0.0;                    ///< 输出帧丢弃边界，单位秒

    // 硬解码相关
    bool m_isHWDecoder = false;                             ///< 是否使用硬件解码
    AVBufferRef *m_hwDeviceCtx = nullptr;                   ///< 硬件设备上下文
    enum AVPixelFormat m_hwPixFmt = AV_PIX_FMT_NONE;        ///< 硬件像素格式
    enum AVPixelFormat m_swPixFmt = AV_PIX_FMT_NONE;        ///< 软件像素格式
    SwsContext *m_swsCtx = nullptr;                         ///< 硬解帧格式转换上下文
};

#endif // DECODER_H
