#include "audioplayer.h"

#include <cmath>

namespace {
constexpr double kSpeedEpsilon = 0.001;

bool IsSupportedSpeed(double speed)
{
    return std::fabs(speed - 0.5) < kSpeedEpsilon
        || std::fabs(speed - 1.0) < kSpeedEpsilon
        || std::fabs(speed - 1.5) < kSpeedEpsilon
        || std::fabs(speed - 2.0) < kSpeedEpsilon;
}
}

AudioPlayer::AudioPlayer(QObject *p)
    : QObject(p)
    , m_pcmBuffer(192000, 0)
{
    InitResampler();
    m_statusTimer.start();
    m_latencyUpdateTimer.start();
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

void AudioPlayer::InitAudioPlay(AVCodecParameters* codecPar,
                                std::shared_ptr<SyncQueue<AVFrame*>> frameQueue,
                                std::shared_ptr<syncmanager> syncManager,
                                double audioTimeBase)
{
    m_audioFrameQueue = frameQueue;
    m_syncManager = syncManager;
    m_audioTimeBase = audioTimeBase;

    if (codecPar) {
        m_dstRate = codecPar->sample_rate;
        m_dstChannels = codecPar->ch_layout.nb_channels;
    }

    InitAudioOutput();

    qDebug() << "AudioPlayer initialized: Rate =" << m_dstRate
             << "Hz, Channels =" << m_dstChannels;
}

void AudioPlayer::InitResampler() {
    m_swr = swr_alloc();
}

void AudioPlayer::InitAudioOutput()
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

double AudioPlayer::CalculateHardwareLatency()
{
    if (!m_audioSink || !m_ioDevice) {
        return m_lastHardwareLatency;
    }

    qint64 processedUs = m_audioSink->processedUSecs();
    double processedSeconds = processedUs / 1000000.0;

    int bytesPerSecond = m_dstRate * m_dstChannels * m_bytesPerSample;
    double expectedSeconds = (double)m_totalBytesWritten / bytesPerSecond;

    double latency = expectedSeconds - processedSeconds;

    const double SMOOTH_ALPHA = 0.1;
    if (m_lastHardwareLatency > 0) {
        latency = m_lastHardwareLatency * (1.0 - SMOOTH_ALPHA) + latency * SMOOTH_ALPHA;
    }

    m_lastHardwareLatency = qMax(0.0, latency);
    return m_lastHardwareLatency;
}

int AudioPlayer::BuildSpeedAdjustedPcm(int pcmBytes, int bytesPerSampleFrame, double speed)
{
    if (pcmBytes <= 0 || bytesPerSampleFrame <= 0) {
        return 0;
    }

    const int sourceFrames = pcmBytes / bytesPerSampleFrame;
    const int alignedBytes = sourceFrames * bytesPerSampleFrame;
    const char* source = m_pcmBuffer.constData();

    m_speedPcmBuffer.clear();
    if (std::fabs(speed - 1.0) < kSpeedEpsilon) {
        m_speedPcmBuffer.append(source, alignedBytes);
        return m_speedPcmBuffer.size();
    }

    int reserveBytes = alignedBytes;
    if (std::fabs(speed - 0.5) < kSpeedEpsilon) {
        reserveBytes = alignedBytes * 2;
    } else if (std::fabs(speed - 1.5) < kSpeedEpsilon) {
        reserveBytes = alignedBytes * 2 / 3 + bytesPerSampleFrame;
    } else if (std::fabs(speed - 2.0) < kSpeedEpsilon) {
        reserveBytes = alignedBytes / 2 + bytesPerSampleFrame;
    }
    m_speedPcmBuffer.reserve(reserveBytes);

    for (int frameIndex = 0; frameIndex < sourceFrames; ++frameIndex) {
        const char* sampleFrame = source + frameIndex * bytesPerSampleFrame;

        if (std::fabs(speed - 0.5) < kSpeedEpsilon) {
            m_speedPcmBuffer.append(sampleFrame, bytesPerSampleFrame);
            m_speedPcmBuffer.append(sampleFrame, bytesPerSampleFrame);
        } else if (std::fabs(speed - 1.5) < kSpeedEpsilon) {
            if (frameIndex % 3 != 2) {
                m_speedPcmBuffer.append(sampleFrame, bytesPerSampleFrame);
            }
        } else if (std::fabs(speed - 2.0) < kSpeedEpsilon) {
            if (frameIndex % 2 == 0) {
                m_speedPcmBuffer.append(sampleFrame, bytesPerSampleFrame);
            }
        }
    }

    return m_speedPcmBuffer.size();
}

bool AudioPlayer::WritePcmBuffer(const char* data,
                                 int bytes,
                                 int bytesPerSampleFrame,
                                 double speed,
                                 double frameStartClockMs)
{
    if (!data || bytes <= 0 || bytesPerSampleFrame <= 0) {
        return true;
    }

    int pcmIndex = 0;
    qint64 outputSampleFramesWritten = 0;

    while (pcmIndex < bytes) {
        if (m_stopFlag || m_pauseFlag) {
            break;
        }

        if (!m_audioSink || m_audioSink->state() == QAudio::StoppedState) {
            qWarning() << "AudioPlayer: Audio sink stopped unexpectedly!";
            return false;
        }

        int chunk = qMin(bytes - pcmIndex, 32 * 1024);
        chunk -= chunk % bytesPerSampleFrame;
        if (chunk <= 0) {
            break;
        }

        if (!m_ioDevice) {
            QThread::usleep(5 * 1000);
            continue;
        }

        const qint64 written = m_ioDevice->write(data + pcmIndex, chunk);
        if (written > 0) {
            pcmIndex += static_cast<int>(written);
            m_totalBytesWritten += written;
            m_bytesWrittenTotal += written;

            const qint64 writtenFrames = written / bytesPerSampleFrame;
            outputSampleFramesWritten += writtenFrames;
            if (m_syncManager && writtenFrames > 0 && m_dstRate > 0) {
                const double mediaElapsedMs =
                    static_cast<double>(outputSampleFramesWritten) * speed / m_dstRate * 1000.0;
                m_syncManager->RefreshClock(frameStartClockMs + mediaElapsedMs);
            }
        } else if (written < 0) {
            qWarning() << "AudioPlayer: Write error!";
            return false;
        } else {
            QThread::usleep(5 * 1000);
        }
    }

    return true;
}

void AudioPlayer::StartAudioPlay()
{
    if (m_audioSink) {
        m_ioDevice = m_audioSink->start();
        if (!m_ioDevice) {
            qCritical() << "AudioPlayer: Failed to start audio sink!";
            return;
        }
    }

    Run();
}

void AudioPlayer::Run()
{
    m_stopFlag = false;
    m_pauseFlag = false;

    if (!m_frame) {
        m_frame = av_frame_alloc();
    }

    const int statusInterval = 5000;

    while (true) {
        if (m_stopFlag) {
            break;
        }

        if (m_pauseFlag) {
            m_audioSink->suspend();
            while (m_pauseFlag && !m_stopFlag) {
                QThread::msleep(10);
            }
            if (!m_stopFlag) {
                m_audioSink->resume();
            }
            continue;
        }

        AVFrame *tmp = nullptr;
        if (!m_audioFrameQueue) {
            break;
        }
        if (!m_audioFrameQueue->Pop(tmp)) {
            // 队列被停止
            break;
        }

        if (!tmp) {
            continue;
        }

        // if((tmp->pts * av_q2d(tmp->time_base) * 1000) < m_syncManager->GetClock()) {
        //     av_frame_free(&tmp);
        //     continue;
        // }

        if (m_first) {
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

        int pcmBytes = converted * m_dstChannels * m_bytesPerSample;
        const int bytesPerSampleFrame = m_dstChannels * m_bytesPerSample;
        const double speedSnapshot = m_speed.load();
        const int speedAdjustedBytes = BuildSpeedAdjustedPcm(pcmBytes, bytesPerSampleFrame, speedSnapshot);

        double frameStartClockMs = 0.0;
        if (tmp->pts != AV_NOPTS_VALUE) {
            frameStartClockMs = tmp->pts * m_audioTimeBase * 1000.0;
        } else if (m_syncManager) {
            frameStartClockMs = m_syncManager->GetClock();
        }

        if (m_syncManager) {
            m_syncManager->RefreshClock(frameStartClockMs);
        }

        WritePcmBuffer(m_speedPcmBuffer.constData(),
                       speedAdjustedBytes,
                       bytesPerSampleFrame,
                       speedSnapshot,
                       frameStartClockMs);

        av_frame_unref(tmp);
        av_frame_free(&tmp);

        double latency = CalculateHardwareLatency();

        if (m_statusTimer.elapsed() > statusInterval) {
            qDebug() << "AudioPlayer: Status -"
                     << "Written:" << (m_bytesWrittenTotal / 1024 / 1024) << "MB"
                     << "Clock:" << (m_syncManager ? m_syncManager->GetClock() : 0.0)
                     << "Latency:" << QString::number(m_lastHardwareLatency, 'f', 3) << "s";
            m_statusTimer.restart();
        }
    }

    qDebug() << "AudioPlayer: Playback thread finished";
}

void AudioPlayer::Start()
{
    m_stopFlag = false;
    m_pauseFlag = false;
    qDebug() << "AudioPlayer started";
}

void AudioPlayer::SetSpeed(double speed)
{
    if (!std::isfinite(speed) || !IsSupportedSpeed(speed)) {
        qWarning() << "AudioPlayer rejected unsupported speed:" << speed;
        return;
    }

    m_speed.store(speed);
    qDebug() << "AudioPlayer speed set to" << speed;
}

void AudioPlayer::Stop()
{
    m_stopFlag = true;

    if (m_audioFrameQueue) {
        m_audioFrameQueue->RequestStop();
    }

    if (m_audioSink) {
        m_audioSink->stop();
    }

    qDebug() << "AudioPlayer stopped";
}

void AudioPlayer::Pause()
{
    m_pauseFlag = true;

    if (m_audioFrameQueue) {
        m_audioFrameQueue->WakeAll();
    }

    qDebug() << "AudioPlayer paused";
}

void AudioPlayer::Forward() {
    m_audioSink->suspend();
}

void AudioPlayer::VolumeUp() {
    qDebug()<<"增大音量";
    if(m_volume>0.95) {
        m_volume = 1.0;
    }else{
        m_volume+=0.05;
    }
    emit VolumeChanged(m_volume);
}

void AudioPlayer::VolumeDown(){
    qDebug()<<"减小音量";
    if(m_volume>0.05){
        m_volume-=0.05;
    }else{
        m_volume = 0.0;
    }
    emit VolumeChanged(m_volume);
}
