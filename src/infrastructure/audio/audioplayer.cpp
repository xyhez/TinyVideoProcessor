#include "audioplayer.h"
#include "tiny_log/Logging.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <QMutexLocker>

namespace {
constexpr double kSpeedEpsilon = 0.001;	///< 音频播放速度浮点比较容差，避免档位判断受精度误差影响
constexpr qreal kVolumeStep = 0.05;	///< 音频音量增减步长，按 0.0-1.0 归一化音量计算
constexpr qreal kVolumeEpsilon = 0.0001;	///< 音频音量浮点比较容差，避免微小误差触发重复更新
constexpr int kMaxResamplerDrainIterations = 16;	///< 限制重采样尾部排空轮次，避免异常状态无限循环
constexpr qint64 kResamplerWriteTimeoutMs = 250;	///< 单轮重采样尾部写入超时
constexpr qint64 kAudioOutputDrainTimeoutMs = 2000;	///< 音频设备缓冲尽力排空超时
constexpr unsigned long kAudioOutputDrainPollMs = 5;	///< 音频设备排空状态轮询间隔

bool IsSupportedSpeed(double speed)
{
    return std::fabs(speed - 0.5) < kSpeedEpsilon
        || std::fabs(speed - 1.0) < kSpeedEpsilon
        || std::fabs(speed - 1.5) < kSpeedEpsilon
        || std::fabs(speed - 2.0) < kSpeedEpsilon;
}

QString AvErrorString(int errorCode)
{
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(errorCode, buffer, sizeof(buffer));
    return QString::fromUtf8(buffer);
}

QString ChannelLayoutName(const AVChannelLayout& layout)
{
    if (layout.nb_channels <= 0) {
        return "invalid";
    }

    char buffer[128] = {0};
    if (av_channel_layout_describe(&layout, buffer, sizeof(buffer)) < 0) {
        return QString("%1 channels").arg(layout.nb_channels);
    }

    return QString::fromUtf8(buffer);
}

QString QtSampleFormatName(QAudioFormat::SampleFormat format)
{
    switch (format) {
    case QAudioFormat::UInt8:
        return "UInt8";
    case QAudioFormat::Int16:
        return "Int16";
    case QAudioFormat::Int32:
        return "Int32";
    case QAudioFormat::Float:
        return "Float";
    case QAudioFormat::Unknown:
        return "Unknown";
    }

    return "Unknown";
}

AVSampleFormat ToAvSampleFormat(QAudioFormat::SampleFormat format)
{
    switch (format) {
    case QAudioFormat::UInt8:
        return AV_SAMPLE_FMT_U8;
    case QAudioFormat::Int16:
        return AV_SAMPLE_FMT_S16;
    case QAudioFormat::Int32:
        return AV_SAMPLE_FMT_S32;
    case QAudioFormat::Float:
        return AV_SAMPLE_FMT_FLT;
    case QAudioFormat::Unknown:
        return AV_SAMPLE_FMT_S16;
    }

    return AV_SAMPLE_FMT_S16;
}

bool CopyOrDefaultChannelLayout(AVChannelLayout* dst,
                                const AVChannelLayout& src,
                                int fallbackChannels)
{
    if (!dst) {
        return false;
    }

    std::memset(dst, 0, sizeof(*dst));
    if (src.nb_channels > 0 && av_channel_layout_copy(dst, &src) >= 0) {
        return true;
    }

    av_channel_layout_default(dst, std::max(1, fallbackChannels));
    return dst->nb_channels > 0;
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
    ReleaseAudioOutput();
}

void AudioPlayer::InitAudioPlay(AVCodecParameters* codecPar,
                                std::shared_ptr<SyncQueue<AVFramePtr>> frameQueue,
                                std::shared_ptr<SyncManager> syncManager,
                                double audioTimeBase)
{
    m_audioFrameQueue = frameQueue;
    m_syncManager = syncManager;
    m_audioTimeBase = audioTimeBase;

    if (codecPar) {
        m_srcRate = codecPar->sample_rate > 0 ? codecPar->sample_rate : m_srcRate;
        m_srcChannels = codecPar->ch_layout.nb_channels > 0
            ? codecPar->ch_layout.nb_channels
            : m_srcChannels;
    }
    m_dstRate = m_srcRate;
    m_dstChannels = 2;

    LOG_DEBUG("AudioPlayer initialized: source =", m_srcRate,
                  "Hz,", m_srcChannels, "channels");
}

void AudioPlayer::InitResampler() {
    if (m_swr) {
        swr_free(&m_swr);
    }
    m_bytesPerSample = std::max(1, av_get_bytes_per_sample(m_dstFormat));
}

void AudioPlayer::InitAudioOutput()
{
    QAudioFormat fmt;
    fmt.setSampleRate(m_dstRate);
    fmt.setChannelCount(m_dstChannels);
    fmt.setSampleFormat(QAudioFormat::Int16);

    QAudioDevice dev = QMediaDevices::defaultAudioOutput();
    if (!dev.isFormatSupported(fmt)) {
        LOG_WARN("AudioPlayer: Requested format not supported, trying preferred output format");
        QAudioFormat preferred = dev.preferredFormat();
        QAudioFormat preferredInt16 = preferred;
        preferredInt16.setSampleFormat(QAudioFormat::Int16);
        fmt = dev.isFormatSupported(preferredInt16) ? preferredInt16 : preferred;
    }

    m_dstRate = fmt.sampleRate() > 0 ? fmt.sampleRate() : m_dstRate;
    m_dstChannels = fmt.channelCount() > 0 ? fmt.channelCount() : 2;
    m_dstFormat = ToAvSampleFormat(fmt.sampleFormat());
    m_bytesPerSample = std::max(1, av_get_bytes_per_sample(m_dstFormat));

    m_audioSink = new QAudioSink(dev, fmt, this);
    ApplyOutputVolume();

    LOG_DEBUG("AudioPlayer: Audio output initialized -",
                  m_dstRate, "Hz,", m_dstChannels, "channels,",
                  QtSampleFormatName(fmt.sampleFormat()));
}

void AudioPlayer::ApplyOutputVolume()
{
    qreal outputVolume = 1.0;
    {
        QMutexLocker locker(&m_volumeMutex);
        outputVolume = m_muted ? 0.0 : m_volume;
    }

    if (m_audioSink) {
        m_audioSink->setVolume(outputVolume);
    }
    m_outputVolumeDirty.store(false);
}

void AudioPlayer::ReleaseAudioOutput()
{
    if (!m_audioSink) {
        m_ioDevice = nullptr;
        return;
    }

    m_audioSink->stop();
    delete m_audioSink;
    m_audioSink = nullptr;
    m_ioDevice = nullptr;
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
                                 double frameStartClockMs,
                                 qint64 timeoutMs,
                                 bool stopOnPause)
{
    if (!data || bytes <= 0 || bytesPerSampleFrame <= 0) {
        return true;
    }

    int pcmIndex = 0;
    qint64 outputSampleFramesWritten = 0;
    QElapsedTimer writeTimer;
    if (timeoutMs >= 0) {
        writeTimer.start();
    }

    while (pcmIndex < bytes) {
        if (m_outputVolumeDirty.load()) {
            ApplyOutputVolume();
        }

        if (m_stopFlag.load() || (stopOnPause && m_pauseFlag.load())) {
            return false;
        }

        if (timeoutMs >= 0 && writeTimer.elapsed() >= timeoutMs) {
            return false;
        }

        if (!m_audioSink || m_audioSink->state() == QAudio::StoppedState) {
            LOG_WARN("AudioPlayer: Audio sink stopped unexpectedly!");
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
            LOG_WARN("AudioPlayer: Write error!");
            return false;
        } else {
            QThread::usleep(5 * 1000);
        }
    }

    return true;
}

bool AudioPlayer::DrainEndOfStream()
{
    if (m_stopFlag.load() || m_pauseFlag.load()) {
        return false;
    }

    const int bytesPerSampleFrame = m_dstChannels * m_bytesPerSample;
    int drainIteration = 0;
    if (m_swr && bytesPerSampleFrame > 0) {
        for (; drainIteration < kMaxResamplerDrainIterations; ++drainIteration) {
            if (m_stopFlag.load() || m_pauseFlag.load()) {
                return false;
            }

            const int pendingSamples = swr_get_out_samples(m_swr, 0);
            if (pendingSamples <= 0) {
                break;
            }

            const int drainSamples = std::min(pendingSamples, std::max(1, m_dstRate));
            const int neededSize = drainSamples * bytesPerSampleFrame;
            if (m_pcmBuffer.size() < neededSize) {
                m_pcmBuffer.resize(neededSize);
            }

            uint8_t* outPtr = reinterpret_cast<uint8_t*>(m_pcmBuffer.data());
            const int converted = swr_convert(m_swr, &outPtr, drainSamples, nullptr, 0);
            if (converted < 0) {
                LOG_WARN("AudioPlayer: Failed to drain resampler tail:", AvErrorString(converted));
                break;
            }
            if (converted == 0) {
                break;
            }

            const int pcmBytes = converted * bytesPerSampleFrame;
            const double speedSnapshot = m_speed.load();
            const int speedAdjustedBytes =
                BuildSpeedAdjustedPcm(pcmBytes, bytesPerSampleFrame, speedSnapshot);
            const double frameStartClockMs = m_syncManager ? m_syncManager->GetClock() : 0.0;

            if (speedAdjustedBytes > 0 &&
                !WritePcmBuffer(m_speedPcmBuffer.constData(),
                                speedAdjustedBytes,
                                bytesPerSampleFrame,
                                speedSnapshot,
                                frameStartClockMs,
                                kResamplerWriteTimeoutMs,
                                false)) {
                if (m_stopFlag.load()) {
                    return false;
                }
                LOG_WARN("AudioPlayer: Resampler tail write reached its safety bound");
                break;
            }
        }

        if (drainIteration == kMaxResamplerDrainIterations &&
            swr_get_out_samples(m_swr, 0) > 0) {
            LOG_WARN("AudioPlayer: Resampler tail drain reached its iteration bound");
        }
    }

    if (m_stopFlag.load() || m_pauseFlag.load()) {
        return false;
    }
    if (!m_audioSink) {
        return true;
    }

    QElapsedTimer drainTimer;
    drainTimer.start();
    while (drainTimer.elapsed() < kAudioOutputDrainTimeoutMs) {
        if (m_stopFlag.load() || m_pauseFlag.load()) {
            return false;
        }

        const QAudio::State state = m_audioSink->state();
        if (state == QAudio::IdleState) {
            return true;
        }
        if (state == QAudio::StoppedState) {
            LOG_WARN("AudioPlayer: Audio sink stopped before drain completed");
            return true;
        }

        const qsizetype bufferSize = m_audioSink->bufferSize();
        if (bufferSize > 0 && m_audioSink->bytesFree() >= bufferSize) {
            return true;
        }
        QThread::msleep(kAudioOutputDrainPollMs);
    }

    LOG_WARN("AudioPlayer: Audio output drain reached its safety timeout");
    return true;
}

void AudioPlayer::StartAudioPlay()
{
    if (!m_audioSink) {
        InitAudioOutput();
    }

    if (m_audioSink) {
        m_ioDevice = m_audioSink->start();
        if (!m_ioDevice) {
            LOG_CRITICAL("AudioPlayer: Failed to start audio sink!");
            ReleaseAudioOutput();
            emit AudioPlaybackError("Failed to start audio output");
            return;
        }
    }

    Run();
    ReleaseAudioOutput();
}

void AudioPlayer::Run()
{
    const int statusInterval = 5000;

    while (true) {
        if (m_stopFlag.load()) {
            break;
        }

        if (m_outputVolumeDirty.load()) {
            ApplyOutputVolume();
        }

        if (m_pauseFlag.load()) {
            if (m_audioSink) {
                m_audioSink->suspend();
            }
            while (m_pauseFlag.load() && !m_stopFlag.load()) {
                if (m_outputVolumeDirty.load()) {
                    ApplyOutputVolume();
                }
                QThread::msleep(10);
            }
            if (!m_stopFlag.load() && m_audioSink) {
                m_audioSink->resume();
            }
            continue;
        }

        AVFramePtr tmp;
        if (!m_audioFrameQueue) {
            break;
        }

        const SyncQueuePopResult popResult = m_audioFrameQueue->PopWithStatus(tmp);
        if (popResult == SyncQueuePopResult::Stopped) {
            break;
        }
        if (popResult == SyncQueuePopResult::Interrupted) {
            QThread::msleep(1);
            continue;
        }
        if (popResult == SyncQueuePopResult::EndOfStream) {
            if (!DrainEndOfStream()) {
                continue;
            }
            if (m_stopFlag.load()) {
                break;
            }
            if (m_pauseFlag.load() || !m_audioFrameQueue->IsEndOfStream()) {
                continue;
            }
            emit AudioPlaybackDrained();
            break;
        }

        if (!tmp) {
            continue;
        }

        // if((tmp->pts * av_q2d(tmp->time_base) * 1000) < m_syncManager->GetClock()) {
        //     continue;
        // }

        if (m_first) {
            AVChannelLayout inLayout = {};
            const int fallbackInputChannels = tmp->ch_layout.nb_channels > 0
                ? tmp->ch_layout.nb_channels
                : m_srcChannels;
            if (!CopyOrDefaultChannelLayout(&inLayout, tmp->ch_layout, fallbackInputChannels)) {
                LOG_WARN("AudioPlayer: Invalid input channel layout, skipping audio frame");
                continue;
            }

            AVChannelLayout outLayout = {};
            av_channel_layout_default(&outLayout, std::max(1, m_dstChannels));

            int inRate = tmp->sample_rate > 0 ? tmp->sample_rate : m_srcRate;
            if (inRate <= 0) {
                inRate = m_dstRate;
            }
            AVSampleFormat inFmt = (AVSampleFormat)tmp->format;
            int outRate = m_dstRate;
            AVSampleFormat outFmt = m_dstFormat;

            if (m_swr) {
                swr_free(&m_swr);
            }

            int ret = swr_alloc_set_opts2(&m_swr,
                                          &outLayout, outFmt, outRate,
                                          &inLayout, inFmt, inRate,
                                          0, nullptr);
            if (ret < 0 || !m_swr) {
                LOG_WARN("AudioPlayer: Resampler allocation failed:", AvErrorString(ret));
                av_channel_layout_uninit(&inLayout);
                av_channel_layout_uninit(&outLayout);
                emit AudioPlaybackError("Audio resampler allocation failed");
                break;
            }

            ret = swr_init(m_swr);
            if (ret < 0) {
                LOG_WARN("AudioPlayer: Resampler init failed:", AvErrorString(ret));
                swr_free(&m_swr);
                av_channel_layout_uninit(&inLayout);
                av_channel_layout_uninit(&outLayout);
                emit AudioPlaybackError("Audio resampler initialization failed");
                break;
            }

            m_first = false;
            LOG_DEBUG("AudioPlayer: Resampler initialized - input:",
                          inRate, "Hz,", ChannelLayoutName(inLayout), ",",
                          av_get_sample_fmt_name(inFmt),
                          "-> output:", outRate, "Hz,",
                          ChannelLayoutName(outLayout), ",",
                          av_get_sample_fmt_name(outFmt));

            av_channel_layout_uninit(&inLayout);
            av_channel_layout_uninit(&outLayout);
        }

        if (!m_swr) {
            LOG_WARN("AudioPlayer: Resampler is not initialized, stopping audio playback");
            emit AudioPlaybackError("Audio resampler is not initialized");
            break;
        }

        const int inputRate = tmp->sample_rate > 0 ? tmp->sample_rate : m_srcRate;
        int dstSamples = av_rescale_rnd(tmp->nb_samples,
                                        m_dstRate,
                                        inputRate > 0 ? inputRate : m_dstRate,
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
            LOG_WARN("AudioPlayer: Resampling failed, skipping frame");
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

        if (!WritePcmBuffer(m_speedPcmBuffer.constData(),
                            speedAdjustedBytes,
                            bytesPerSampleFrame,
                            speedSnapshot,
                            frameStartClockMs)) {
            if (!m_pauseFlag.load() && !m_stopFlag.load()) {
                emit AudioPlaybackError("Audio output write failed");
                break;
            }
        }

        double latency = CalculateHardwareLatency();

        if (m_statusTimer.elapsed() > statusInterval) {
            LOG_DEBUG("AudioPlayer: Status -",
                          "Written:", (m_bytesWrittenTotal / 1024 / 1024), "MB",
                          "Clock:", (m_syncManager ? m_syncManager->GetClock() : 0.0),
                          "Latency:", QString::number(m_lastHardwareLatency, 'f', 3), "s");
            m_statusTimer.restart();
        }
    }

    LOG_DEBUG("AudioPlayer: Playback thread finished");
}

void AudioPlayer::Start()
{
    m_stopFlag.store(false);
    m_pauseFlag.store(false);
    LOG_DEBUG("AudioPlayer started");
}

void AudioPlayer::SetSpeed(double speed)
{
    if (!std::isfinite(speed) || !IsSupportedSpeed(speed)) {
        LOG_WARN("AudioPlayer rejected unsupported speed:", speed);
        return;
    }

    m_speed.store(speed);
    LOG_DEBUG("AudioPlayer speed set to", speed);
}

void AudioPlayer::Stop()
{
    m_stopFlag.store(true);

    if (m_audioFrameQueue) {
        m_audioFrameQueue->RequestStop();
    }

    LOG_DEBUG("AudioPlayer stopped");
}

void AudioPlayer::Pause()
{
    m_pauseFlag.store(true);

    if (m_audioFrameQueue) {
        m_audioFrameQueue->WakeAll();
    }

    LOG_DEBUG("AudioPlayer paused");
}

void AudioPlayer::Forward() {
    m_pauseFlag.store(true);
}

void AudioPlayer::VolumeUp() {
    SetVolume(Volume() + kVolumeStep);
}

void AudioPlayer::VolumeDown(){
    SetVolume(Volume() - kVolumeStep);
}

void AudioPlayer::SetVolume(qreal volume)
{
    const qreal clampedVolume = std::clamp(volume, 0.0, 1.0);
    {
        QMutexLocker locker(&m_volumeMutex);
        if (std::fabs(m_volume - clampedVolume) < kVolumeEpsilon) {
            return;
        }

        m_volume = clampedVolume;
    }

    m_outputVolumeDirty.store(true);
    emit VolumeChanged(clampedVolume);
}

qreal AudioPlayer::Volume() const
{
    QMutexLocker locker(&m_volumeMutex);
    return m_volume;
}

void AudioPlayer::SetMuted(bool muted)
{
    {
        QMutexLocker locker(&m_volumeMutex);
        if (m_muted == muted) {
            return;
        }

        m_muted = muted;
    }

    m_outputVolumeDirty.store(true);
    emit MuteChanged(muted);
}

bool AudioPlayer::IsMuted() const
{
    QMutexLocker locker(&m_volumeMutex);
    return m_muted;
}


