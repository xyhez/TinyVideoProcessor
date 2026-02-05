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
#include <deque>  // 添加 deque 支持

#include "syncmanager.h"
#include "global.h"

extern "C" {
#include <libswresample/swresample.h>
#include <libavutil/time.h>
#include <libavutil/frame.h>
#include <libavcodec/avcodec.h>
}

class AudioPlayer : public QObject
{
    Q_OBJECT

public:
    AudioPlayer(QObject *p = nullptr);
    ~AudioPlayer();

    void initAudioPlay(AVCodecParameters* codecPar,
                       std::shared_ptr<std::deque<AVFrame*>> frameQueue,
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
    std::shared_ptr<std::deque<AVFrame*>> m_audioFrameQueue;
    std::shared_ptr<syncmanager> m_syncManager;
    AVRational m_audioTimeBase;
    bool m_first = true;

    SwrContext *m_swr = nullptr;
    QAudioSink *m_audioSink = nullptr;
    QIODevice *m_ioDevice = nullptr;

    int m_dstRate = 44100;
    int m_dstChannels = 2;
    const AVSampleFormat m_dstFormat = AV_SAMPLE_FMT_S16;
    int m_bytesPerSample = 2;

    qint64 m_totalBytesWritten = 0;
    double m_lastHardwareLatency = 0.0;

    QElapsedTimer m_latencyUpdateTimer;
    static constexpr double LATENCY_SMOOTH_ALPHA = 0.1;

    AVFrame* m_frame = nullptr;
    QByteArray m_pcmBuffer;

    qint64 m_bytesWrittenTotal = 0;
    QElapsedTimer m_statusTimer;

    bool m_stopFlag = false;
    bool m_pauseFlag = false;
    QMutex m_mutex;
    QWaitCondition m_queueNotEmpty;

    qreal m_volume = 1.0;

private:
    void initResampler();
    void initAudioOutput();
    double calculateHardwareLatency();
    void run();

signals:
};

#endif // AUDIOPLAYER_H
