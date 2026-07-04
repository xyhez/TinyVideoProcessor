#ifndef AUDIOPLAYER_H
#define AUDIOPLAYER_H

#include <QObject>
#include <QAudioSink>
#include <QAudioDevice>
#include <QThread>
#include <QMediaDevices>
#include <QIODevice>
#include <QAudioFormat>
#include <QMutex>
#include <QElapsedTimer>
#include <QVector>
#include <memory>
#include <QWaitCondition>
#include <atomic>

#include "core/ffmpeg_raii.h"
#include "infrastructure/sync/syncmanager.h"
#include "core/queue.h"

extern "C" {
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/samplefmt.h>
#include <libavutil/time.h>
#include <libavutil/frame.h>
#include <libavutil/rational.h>
#include <libavcodec/avcodec.h>
}

class AudioPlayer : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief AudioPlayer 构造函数
     * @param p Qt 父对象
     */
    AudioPlayer(QObject *p = nullptr);

    /**
     * @brief AudioPlayer 析构函数，释放音频输出和重采样资源
     */
    ~AudioPlayer();

    /**
     * @brief InitAudioPlay 初始化音频播放链路
     * @param codecPar 音频解码参数
     * @param frameQueue 音频帧队列
     * @param syncManager 同步时钟管理器
     * @param audioTimeBase 音频流时间基
     */
    void InitAudioPlay(AVCodecParameters* codecPar,
                       std::shared_ptr<SyncQueue<AVFramePtr>> frameQueue,
                       std::shared_ptr<SyncManager> syncManager,
                       double audioTimeBase);

    /**
     * @brief Start 启动或恢复音频播放
     */
    void Start();

    /**
     * @brief Stop 停止音频播放
     */
    void Stop();

    /**
     * @brief Pause 暂停音频播放
     */
    void Pause();

    /**
     * @brief Forward 快进3秒
     */
    void Forward();

    /**
     * @brief VolumeUp 增大音量5%
     */
    void VolumeUp();

    /**
     * @brief VolumeDown 减小音量5%
     */
    void VolumeDown();

    /**
     * @brief SetVolume 设置用户音量
     * @param volume 音量值，范围 0.0-1.0
     */
    void SetVolume(qreal volume);

    /**
     * @brief Volume 获取当前用户音量
     * @return 当前用户音量，范围 0.0-1.0
     */
    qreal Volume() const;

    /**
     * @brief SetMuted 设置静音状态
     * @param muted 是否静音
     */
    void SetMuted(bool muted);

    /**
     * @brief IsMuted 获取当前静音状态
     * @return true 已静音
     * @return false 未静音
     */
    bool IsMuted() const;

    /**
     * @brief StartAudioPlay 音频播放线程入口
     */
    void StartAudioPlay();

    /**
     * @brief SetSpeed 设置播放速度
     * @param speed 支持 0.5 / 1.0 / 1.5 / 2.0
     */
    void SetSpeed(double speed);
signals:
    /**
     * @brief VolumeChanged 返回当前的音量
     * @param volume 当前音量
     */
    void VolumeChanged(qreal volume);

    /**
     * @brief MuteChanged 静音状态变化信号
     * @param muted 是否静音
     */
    void MuteChanged(bool muted);

private:
    std::shared_ptr<SyncQueue<AVFramePtr>> m_audioFrameQueue;	///< 音频帧队列
    std::shared_ptr<SyncManager> m_syncManager;	///< 同步时钟管理器
    double m_audioTimeBase = 0.0;	///< 音频时间基
    bool m_first = true;	///< 是否为首帧音频

    SwrContext *m_swr = nullptr;	///< 音频重采样上下文
    QAudioSink *m_audioSink = nullptr;	///< Qt 音频输出对象
    QIODevice *m_ioDevice = nullptr;	///< 音频输出设备

    int m_srcRate = 44100;	///< 输入采样率
    int m_srcChannels = 2;	///< 输入声道数
    int m_dstRate = 44100;	///< 输出采样率
    int m_dstChannels = 2;	///< 输出声道数
    AVSampleFormat m_dstFormat = AV_SAMPLE_FMT_S16;	///< 输出采样格式
    int m_bytesPerSample = 2;	///< 单采样字节数

    qint64 m_totalBytesWritten = 0;	///< 已写入总字节数
    double m_lastHardwareLatency = 0.0;	///< 最近硬件延迟

    QElapsedTimer m_latencyUpdateTimer;	///< 延迟更新计时器
    static constexpr double LATENCY_SMOOTH_ALPHA = 0.1;	///< 延迟平滑系数

    AVFrame* m_frame = nullptr;	///< 当前音频帧
    QByteArray m_pcmBuffer;	///< PCM 缓冲区
    QByteArray m_speedPcmBuffer;	///< 变速 PCM 缓冲区

    qint64 m_bytesWrittenTotal = 0;	///< 累计写入字节数
    QElapsedTimer m_statusTimer;	///< 状态输出计时器

    std::atomic_bool m_stopFlag {false};	///< 停止标志
    std::atomic_bool m_pauseFlag {false};	///< 暂停标志
    QMutex m_mutex;	///< 播放互斥锁
    QWaitCondition m_queueNotEmpty;	///< 音频队列非空条件

    qreal m_volume = 1.0;	///< 用户音量
    bool m_muted = false;	///< 静音状态
    mutable QMutex m_volumeMutex;	///< 音量互斥锁
    std::atomic<double> m_speed {1.0};	///< 播放速度
    std::atomic_bool m_outputVolumeDirty {true};	///< 音频输出音量是否需要刷新

private:
    /**
     * @brief InitResampler 初始化音频重采样器
     */
    void InitResampler();

    /**
     * @brief InitAudioOutput 初始化 Qt 音频输出设备
     */
    void InitAudioOutput();

    /**
     * @brief ApplyOutputVolume 将用户音量和静音状态应用到音频输出设备
     */
    void ApplyOutputVolume();

    /**
     * @brief ReleaseAudioOutput 在音频对象所属线程释放 Qt 音频输出设备
     */
    void ReleaseAudioOutput();

    /**
     * @brief CalculateHardwareLatency 估算音频硬件缓冲延迟
     * @return 延迟时间，单位毫秒
     */
    double CalculateHardwareLatency();

    /**
     * @brief BuildSpeedAdjustedPcm 根据播放速度生成变速后的 PCM 数据
     * @param pcmBytes 原始 PCM 字节数
     * @param bytesPerSampleFrame 单个采样帧字节数
     * @param speed 播放速度
     * @return 变速后 PCM 字节数
     */
    int BuildSpeedAdjustedPcm(int pcmBytes, int bytesPerSampleFrame, double speed);

    /**
     * @brief WritePcmBuffer 分块写入 PCM 并刷新主时钟
     * @param data PCM 数据指针
     * @param bytes PCM 字节数
     * @param bytesPerSampleFrame 单个采样帧字节数
     * @param speed 当前播放速度
     * @param frameStartClockMs 当前音频帧起始时钟，单位毫秒
     * @return true 写入完成
     * @return false 写入被停止或失败
     */
    bool WritePcmBuffer(const char* data,
                        int bytes,
                        int bytesPerSampleFrame,
                        double speed,
                        double frameStartClockMs);

    /**
     * @brief Run 音频播放主循环
     */
    void Run();

signals:
};

#endif // AUDIOPLAYER_H

