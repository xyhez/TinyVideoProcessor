#include "audioplayer.h"

AudioPlayer::AudioPlayer(QObject *p)
    : QObject(p)
    , m_pcmBuffer(192000, 0)
{
    initResampler();
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

void AudioPlayer::initAudioPlay(AVCodecParameters* codecPar,
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

    initAudioOutput();

    qDebug() << "AudioPlayer initialized: Rate =" << m_dstRate
             << "Hz, Channels =" << m_dstChannels;
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

double AudioPlayer::calculateHardwareLatency()
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

void AudioPlayer::startAudioPlay()
{
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

        // if((tmp->pts * av_q2d(tmp->time_base) * 1000) < m_syncManager->getClock()) {
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

        double audioclock = tmp->pts * m_audioTimeBase * 1000;
        // qDebug()<<"audioclock："<<audioclock;
        if (m_syncManager) {
            m_syncManager->refreshClock(audioclock);
        }

        int pcmBytes = converted * m_dstChannels * m_bytesPerSample;
        int pcmIndex = 0;

        while (pcmIndex < pcmBytes) {
            if (m_stopFlag) {
                break;
            }

            if (m_pauseFlag) {
                break;
            }

            if (!m_audioSink || m_audioSink->state() == QAudio::StoppedState) {
                qWarning() << "AudioPlayer: Audio sink stopped unexpectedly!";
                break;
            }

            int chunk = qMin(pcmBytes - pcmIndex, 32*1024);
            if (chunk > 0 && m_ioDevice) {
                int written = m_ioDevice->write(m_pcmBuffer.constData() + pcmIndex, chunk);
                if (written > 0) {
                    pcmIndex += written;
                    m_totalBytesWritten += written;
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

        double latency = calculateHardwareLatency();

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
    m_stopFlag = false;
    m_pauseFlag = false;
    qDebug() << "AudioPlayer started";
}

void AudioPlayer::stop()
{
    m_stopFlag = true;

    if (m_audioFrameQueue) {
        m_audioFrameQueue->SetStopFlag(true);
        m_audioFrameQueue->WakeAll();
    }

    if (m_audioSink) {
        m_audioSink->stop();
    }

    qDebug() << "AudioPlayer stopped";
}

void AudioPlayer::pause()
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

void AudioPlayer::volumeUp() {
    qDebug()<<"增大音量";
    if(m_volume>0.95) {
        m_volume = 1.0;
    }else{
        m_volume+=0.05;
    }
    emit e_volume(m_volume);
}

void AudioPlayer::volumeDown(){
    qDebug()<<"减小音量";
    if(m_volume>0.05){
        m_volume-=0.05;
    }else{
        m_volume = 0.0;
    }
    emit e_volume(m_volume);
}
