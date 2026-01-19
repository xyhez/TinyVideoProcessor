#include "audioplayer.h"

AudioPlayer::AudioPlayer(QObject *p)
    : QObject(p)
    , m_pcmBuffer(192000, 0)
{
    initResampler();
    m_statusTimer.start();
    m_latencyUpdateTimer.start();  // 初始化延迟更新定时器
}

AudioPlayer::~AudioPlayer() {
    if (m_swr) {
        swr_free(&m_swr);
    }
    if (m_frame) {
        av_frame_free(&m_frame);
    }
    if (m_audioSink) {
        m_audioSink->stop();
        delete m_audioSink;
        m_audioSink = nullptr;
    }
}

void AudioPlayer::initAudioPlay(AVCodecParameters* codecPar,
                                std::shared_ptr<QVector<AVFrame*>> frameQueue,
                                std::shared_ptr<syncmanager> syncManager)
{
    m_audioFrameQueue = frameQueue;
    m_syncManager = syncManager;

    ///< 设置音频参数
    if (codecPar) {
        m_dstRate = codecPar->sample_rate;
        m_dstChannels = codecPar->ch_layout.nb_channels;
    }

    ///< 初始化音频输出
    initAudioOutput();

    qDebug() << "AudioPlayer initialized: Rate =" << m_dstRate
             << "Hz, Channels =" << m_dstChannels;

    ///< 获取音量
    m_volume = m_audioSink->volume();
}

void AudioPlayer::initResampler() {
    m_swr = swr_alloc();
}

void AudioPlayer::initAudioOutput()
{
    QAudioFormat fmt;
    fmt.setSampleRate(m_dstRate);
    fmt.setChannelCount(m_dstChannels);
    fmt.setSampleFormat(QAudioFormat::Int16);

    QAudioDevice dev = QMediaDevices::defaultAudioOutput();
    if (!dev.isFormatSupported(fmt)) {
        qWarning() << "AudioPlayer: Default format not supported, using preferred format";
        fmt = dev.preferredFormat();
        m_dstRate = fmt.sampleRate();
        m_dstChannels = fmt.channelCount();
    }

    m_audioSink = new QAudioSink(dev, fmt, this);

    qDebug() << "AudioPlayer: Audio output initialized -"
             << m_dstRate << "Hz," << m_dstChannels << "channels";
}

/**
 * @brief 计算音频硬件缓冲区延迟（单位：秒）
 *
 * 优化：使用QAudioSink的内部状态，更准确的延迟估算
 */
double AudioPlayer::calculateHardwareLatency()
{
    if (!m_audioSink || !m_ioDevice) {
        return m_lastHardwareLatency;  // 返回上次的平滑值
    }

    // 方法1：使用processedUSecs()（推荐，跨平台）
    qint64 processedUs = m_audioSink->processedUSecs(); // 返回自调用以来处理的音频数据量（微秒）
    double processedSeconds = processedUs / 1000000.0;  // 转换成 秒

    int bytesPerSecond = m_dstRate * m_dstChannels * m_bytesPerSample; // 每秒的字节数
    double expectedSeconds = (double)m_totalBytesWritten / bytesPerSecond; // 已经写入音频硬件的数据量（秒）

    double latency = expectedSeconds - processedSeconds;    // 已写入但是没有播放的数据量（秒）---差值

    // ==================== 延迟平滑（核心修复点3） ====================
    // 硬件延迟可能抖动，需要低通滤波
    const double SMOOTH_ALPHA = 0.1;
    if (m_lastHardwareLatency > 0) {
        latency = m_lastHardwareLatency * (1.0 - SMOOTH_ALPHA) + latency * SMOOTH_ALPHA;
    }

    m_lastHardwareLatency = qMax(0.0, latency);
    return m_lastHardwareLatency;
}

void AudioPlayer::startAudioPlay()
{
    // 启动音频设备
    if (m_audioSink) {
        m_ioDevice = m_audioSink->start();
        if (!m_ioDevice) {
            qCritical() << "AudioPlayer: Failed to start audio sink!";
            return;
        }
    }

    run();
}

void AudioPlayer::run()
{
    QMutexLocker locker(&m_mutex);
    m_stopFlag = false;
    m_pauseFlag = false;
    locker.unlock();

    if (!m_frame) {
        m_frame = av_frame_alloc();
    }

    const int statusInterval = 5000;  // 5秒打印一次状态

    while (true) {
        ///< 检查音量
        if(m_volume!=m_audioSink->volume()) {
            m_audioSink->setVolume(m_volume);
        }
        ///< 检查停止标志
        locker.relock();
        if (m_stopFlag) {
            locker.unlock();
            break;
        }

        ///< 检查暂停标志
        if (m_pauseFlag) {
            locker.unlock();
            m_audioSink->suspend(); ///< 排空硬件缓存
            continue;
        }
        locker.unlock();

        ///< 检查帧队列
        if (m_audioFrameQueue->isEmpty()) {
            QThread::msleep(100);
            continue;
        }

        ///< 获取一帧
        AVFrame *tmp = nullptr;
        locker.relock();
        if (!m_audioFrameQueue->isEmpty()) {
            tmp = m_audioFrameQueue->takeFirst();
        }
        locker.unlock();

        if (!tmp) {
            continue;
        }
        ///< 快进直接修改主时钟，所以需要校验一下
        if((tmp->pts * av_q2d(tmp->time_base) * 1000) < m_syncManager->getClock()) {
            av_frame_free(&tmp);
            continue;
        }

        // ==================== 首次初始化 ====================
        if (m_first) {
            m_audioTimeBase = tmp->time_base;

            AVChannelLayout inLayout = tmp->ch_layout;
            int inRate = tmp->sample_rate;
            AVSampleFormat inFmt = (AVSampleFormat)tmp->format;

            AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_STEREO;
            int outRate = m_dstRate;
            AVSampleFormat outFmt = m_dstFormat;

            swr_alloc_set_opts2(&m_swr,
                                &outLayout, outFmt, outRate,
                                &inLayout, inFmt, inRate,
                                0, nullptr);
            swr_init(m_swr);

            m_first = false;
            qDebug() << "AudioPlayer: Resampler initialized - Input:" << inRate << "Hz -> Output:" << outRate << "Hz";
        }

        // ==================== 重采样 ====================
        int dstSamples = av_rescale_rnd(tmp->nb_samples,
                                        m_dstRate,
                                        tmp->sample_rate,
                                        AV_ROUND_UP);

        int neededSize = dstSamples * m_dstChannels * m_bytesPerSample;
        if (m_pcmBuffer.size() < neededSize) {
            m_pcmBuffer.resize(neededSize);
        }

        uint8_t *outPtr = (uint8_t *)m_pcmBuffer.data();
        int converted = swr_convert(m_swr,
                                    &outPtr, dstSamples,
                                    (const uint8_t **)tmp->data, tmp->nb_samples);

        if (converted <= 0) {
            qWarning() << "AudioPlayer: Resampling failed, skipping frame";
            av_frame_unref(tmp);
            av_frame_free(&tmp);
            continue;
        }

        // 更新时钟（毫秒）
        double audioclock = tmp->pts * av_q2d(tmp->time_base) * 1000;
        if (m_syncManager) {
            m_syncManager->refreshClock(audioclock);
        }

        // ==================== 写入音频设备 ====================
        int pcmBytes = converted * m_dstChannels * m_bytesPerSample;
        int pcmIndex = 0;

        while (pcmIndex < pcmBytes) {
            // 检查停止标志
            locker.relock();
            if (m_stopFlag) {
                locker.unlock();
                break;
            }
            locker.unlock();

            if (!m_audioSink || m_audioSink->state() == QAudio::StoppedState) {
                qWarning() << "AudioPlayer: Audio sink stopped unexpectedly!";
                break;
            }

            int chunk = qMin(pcmBytes - pcmIndex, 32*1024);
            if (chunk > 0 && m_ioDevice) {
                int written = m_ioDevice->write(m_pcmBuffer.constData() + pcmIndex, chunk);
                if (written > 0) {
                    pcmIndex += written;
                    m_totalBytesWritten += written;  // **核心：累加写入字节数**
                    m_bytesWrittenTotal += written;
                } else if (written < 0) {
                    qWarning() << "AudioPlayer: Write error!";
                    break;
                }
            } else {
                QThread::usleep(5 * 1000);
            }
        }

        av_frame_unref(tmp);
        av_frame_free(&tmp);

        ///< 每帧更新延迟计算
        double latency = calculateHardwareLatency();

        ///< 定期打印状态
        if (m_statusTimer.elapsed() > statusInterval) {
            qDebug() << "AudioPlayer: Status -"
                     << "Written:" << (m_bytesWrittenTotal / 1024 / 1024) << "MB"
                     << "Clock:" << (m_syncManager ? m_syncManager->getClock() : 0.0)
                     << "Latency:" << QString::number(m_lastHardwareLatency, 'f', 3) << "s";
            m_statusTimer.restart();
        }
    }

    qDebug() << "AudioPlayer: Playback thread finished";
}

void AudioPlayer::start()
{
    QMutexLocker locker(&m_mutex);
    m_stopFlag = false;
    m_pauseFlag = false;
    qDebug() << "AudioPlayer started";
}

void AudioPlayer::stop()
{
    QMutexLocker locker(&m_mutex);
    m_stopFlag = true;

    if (m_audioSink) {
        m_audioSink->stop();
    }

    qDebug() << "AudioPlayer stopped";
}

void AudioPlayer::pause()
{
    QMutexLocker locker(&m_mutex);
    m_pauseFlag = true;
    qDebug() << "AudioPlayer paused";
}

void AudioPlayer::Forward() {
    m_mutex.lock();
    m_audioSink->suspend();
    m_mutex.unlock();
}

void AudioPlayer::volumeUp() {
    qDebug()<<"增大音量";
    m_mutex.lock();
    if(m_volume>0.95) {
        m_volume = 1.0;
    }else{
        m_volume+=0.05;
    }
    m_mutex.lock();
    emit e_volume(m_volume);
}

void AudioPlayer::volumeDown(){
    qDebug()<<"减小音量";
    m_mutex.lock();
    if(m_volume>0.05){
        m_volume-=0.05;
    }else{
        m_volume = 0.0;
    }
    m_mutex.lock();
    emit e_volume(m_volume);
}
