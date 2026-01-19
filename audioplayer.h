#ifndef AUDIOPLAYER_H
#define AUDIOPLAYER_H

#include <QObject>
#include <QAudioSink>
#include <QAudioDevice>
#include <QThread>
#include <QMediaDevices>
#include <QDebug>
#include <QIODevice>
#include <QAudioFormat>
#include <QMutex>
#include <QElapsedTimer>
#include <QVector>
#include <memory>
#include <QWaitCondition>

#include "syncmanager.h"

extern "C" {
#include <libswresample/swresample.h>
#include <libavutil/time.h>
#include <libavutil/frame.h>  // 添加frame.h
#include <libavcodec/avcodec.h>
}

class AudioPlayer : public QObject
{
    Q_OBJECT

public:
    // 修改构造函数：移除参数，添加初始化函数
    AudioPlayer(QObject *p = nullptr);
    ~AudioPlayer();

    // 添加初始化函数以适配项目
    void initAudioPlay(AVCodecParameters* codecPar,
                       std::shared_ptr<QVector<AVFrame*>> frameQueue,
                       std::shared_ptr<syncmanager> syncManager);



    /**
     * @brief start
     */
    void start();

    /**
     * @brief stop
     */
    void stop();

    /**
     * @brief pause
     */
    void pause();

    /**
     * @brief Forward 快进3秒
     */
    void Forward();

    /**
     * @brief volumeUp 增大音量5%
     */
    void volumeUp();

    /**
     * @brief volumeDown 减小音量5%
     */
    void volumeDown();

    /**
     * @brief startAudioPlay
     */
    void startAudioPlay();
signals:
    /**
     * @brief e_volume 返回当前的音量
     */
    void e_volume(qreal);

private:
    // 修改帧队列类型以适配项目
    std::shared_ptr<QVector<AVFrame*>> m_audioFrameQueue;
    std::shared_ptr<syncmanager> m_syncManager;
    AVRational m_audioTimeBase;
    bool m_first = true;

    // 重采样器
    SwrContext *m_swr = nullptr;
    QAudioSink *m_audioSink = nullptr;
    QIODevice *m_ioDevice = nullptr;

    // 目标音频格式
    int m_dstRate = 44100;
    int m_dstChannels = 2;
    const AVSampleFormat m_dstFormat = AV_SAMPLE_FMT_S16;
    int m_bytesPerSample = 2; // 每采样点的字节数

    // 音频时钟追踪
    qint64 m_totalBytesWritten = 0;     // 已写入的字节数
    double m_lastHardwareLatency = 0.0; // 硬件延迟

    // 硬件延迟平滑（新增）
    QElapsedTimer m_latencyUpdateTimer;  // 控制延迟更新频率
    static constexpr double LATENCY_SMOOTH_ALPHA = 0.1;  // 平滑系数

    // 临时帧和缓冲区
    AVFrame* m_frame = nullptr;
    QByteArray m_pcmBuffer;  // 缓冲区

    // 统计信息
    qint64 m_bytesWrittenTotal = 0;
    QElapsedTimer m_statusTimer;

    // 控制标志
    bool m_stopFlag = false;
    bool m_pauseFlag = false;
    QMutex m_mutex;
    QWaitCondition m_queueNotEmpty;

    qreal m_volume = 0.0;

private:
    void initResampler();
    void initAudioOutput();
    double calculateHardwareLatency();
    void run();

signals:
         // 可以添加一些信号，如播放状态改变
};

#endif // AUDIOPLAYER_H
