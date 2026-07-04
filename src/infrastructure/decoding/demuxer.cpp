#include "demuxer.h"
#include "tiny_log/Logging.h"

#include <QMutexLocker>
#include <utility>

namespace {
constexpr double kSeekStepSeconds = 10.0;	///< 相对 seek 的默认步长秒数
constexpr int kReadErrorRetryDelayMs = 20;	///< 读取失败后的短暂重试等待时间，避免错误循环空转
constexpr int kMaxConsecutiveReadErrors = 100;	///< 连续读取错误上限，超过后认为媒体源异常

QString AvErrorString(int errorCode)
{
    char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(errorCode, errBuf, sizeof(errBuf));
    return QString::fromLocal8Bit(errBuf);
}
}

Demuxer::Demuxer(std::shared_ptr<IMediaSource> mediaSource,
                 std::shared_ptr<SyncQueue<AVPacketPtr>> APacket,
                 std::shared_ptr<SyncQueue<AVPacketPtr>> VPacket,
                 std::shared_ptr<SyncQueue<AVFramePtr>> AFrame,
                 std::shared_ptr<SyncQueue<AVFramePtr>> VFrame,
                 QObject *parent)
    : QObject{parent}
    , m_mediaSource(mediaSource)
    , m_APacket(APacket)
    , m_VPacket(VPacket)
    , m_AFrame(AFrame)
    , m_VFrame(VFrame)
{
    if (!m_mediaSource) {
        LOG_CRITICAL("Demuxer media source is null");
        return;
    }

    AVFormatContext* ic = m_mediaSource->GetFormatContect();
    if (!ic) {
        LOG_CRITICAL("Demuxer format context is null");
        return;
    }

    Fileinfo& info = m_mediaSource->GetSourceInfo();
    for (int i = 0; i < static_cast<int>(ic->nb_streams); i++) {
        AVStream* stream = ic->streams[i];
        AVCodecParameters* codecParameters = stream->codecpar;
        if (codecParameters->codec_type == AVMEDIA_TYPE_VIDEO) {
            LOG_DEBUG(QString("Found video stream #%1: %2x%3, codec=%4, duration=%5s")
                              .arg(i)
                              .arg(codecParameters->width)
                              .arg(codecParameters->height)
                              .arg(avcodec_get_name(codecParameters->codec_id))
                              .arg(ic->duration / 1000000));
            LOG_DEBUG(QString("frame rate: %1 fps").arg(av_q2d(stream->avg_frame_rate)));
            LOG_DEBUG(QString("frame duration: %1 ms")
                              .arg(1.0 / av_q2d(stream->avg_frame_rate) * 1000));
            LOG_DEBUG("video time_base:", av_q2d(stream->time_base));

            m_videoStreamIndex = i;
            m_V_coderPar = codecParameters;
            m_V_time_base = stream->time_base;

            info.hasVideo_ = true;
            info.videoStreamIndex_ = i;
            info.width_ = codecParameters->width;
            info.height_ = codecParameters->height;
            info.CodecId_ = codecParameters->codec_id;
            info.videoCodecId_ = codecParameters->codec_id;
            const double frameRate = av_q2d(stream->avg_frame_rate);
            if (frameRate > 0.0) {
                info.FrameRate = frameRate;
            }
            info.V_timeBase = av_q2d(stream->time_base);
            if (ic->duration > 0) {
                info.duration_ = ic->duration / 1000000;
            }
        } else if (codecParameters->codec_type == AVMEDIA_TYPE_AUDIO) {
            LOG_DEBUG(QString("Found audio stream #%1: %2x%3, codec=%4, duration=%5s")
                              .arg(i)
                              .arg(codecParameters->sample_rate)
                              .arg(codecParameters->bit_rate)
                              .arg(avcodec_get_name(codecParameters->codec_id))
                              .arg(ic->duration / 1000000));
            LOG_DEBUG("audio time_base:", av_q2d(stream->time_base));

            m_audioStreamIndex = i;
            m_A_coderPar = codecParameters;
            m_A_time_base = stream->time_base;

            info.hasAudio_ = true;
            info.audioStreamIndex_ = i;
            info.audioCodecId_ = codecParameters->codec_id;
            info.A_timeBase = av_q2d(stream->time_base);
        } else if (codecParameters->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            LOG_DEBUG("Found subtitle stream");
        }
    }
}

Demuxer::~Demuxer()
{
    LOG_DEBUG("Demuxer resources released");
}

AVCodecParameters* Demuxer::ReAcoderPar()
{
    return m_A_coderPar;
}

AVCodecParameters* Demuxer::ReVcoderPar()
{
    return m_V_coderPar;
}

void Demuxer::SetAudioQueues(std::shared_ptr<SyncQueue<AVPacketPtr>> APacket,
                             std::shared_ptr<SyncQueue<AVFramePtr>> AFrame)
{
    QMutexLocker locker(&mutex);
    m_APacket = APacket;
    m_AFrame = AFrame;
}

AVRational Demuxer::ReAtimebase()
{
    return m_A_time_base;
}

AVRational Demuxer::ReVtimebase()
{
    return m_V_time_base;
}

int Demuxer::StartDemuxer()
{
    LOG_DEBUG("Demuxer started");

    if (!m_mediaSource) {
        LOG_WARN("Demuxer media source is not initialized");
        return -1;
    }

    AVFormatContext* ic = m_mediaSource->GetFormatContect();
    if (!ic) {
        LOG_WARN("Demuxer format context is not initialized");
        return -1;
    }

    bool reachedEof = false;
    bool failed = false;
    QString errorMessage;
    int consecutiveReadErrors = 0;
    while (true) {
        if (!WaitIfSuspended()) {
            break;
        }

        AVPacketPtr packet = MakeAVPacket();
        if (!packet) {
            errorMessage = "Allocate packet failed";
            failed = true;
            break;
        }
        int ret = 0;

        {
            QMutexLocker locker(&mutex);
            if (stop_flag) {
                break;
            }
            ret = av_read_frame(ic, packet.get());
        }

        if (ret < 0) {
            {
                QMutexLocker locker(&mutex);
                if (stop_flag) {
                    break;
                }
            }

            if (IsSuspendRequested()) {
                continue;
            }

            if (ret == AVERROR_EOF) {
                LOG_DEBUG("Demuxing finished (EOF)");
                reachedEof = true;
                break;
            }

            if (ret == AVERROR(EAGAIN)) {
                QThread::msleep(kReadErrorRetryDelayMs);
                continue;
            }

            ++consecutiveReadErrors;
            const QString ffmpegError = AvErrorString(ret);
            LOG_WARN("Demuxer read error:", ffmpegError,
                         "consecutive errors:", consecutiveReadErrors);

            if (consecutiveReadErrors >= kMaxConsecutiveReadErrors) {
                if (m_mediaSource && m_mediaSource->IsRealtime()) {
                    errorMessage = QString("Realtime media source read failed: %1").arg(ffmpegError);
                } else {
                    errorMessage = QString("Media stream read failed: %1").arg(ffmpegError);
                }
                failed = true;
                break;
            }

            QThread::msleep(kReadErrorRetryDelayMs);
            continue;
        }

        consecutiveReadErrors = 0;

        if (IsSuspendRequested()) {
            continue;
        }

        if (packet->stream_index != m_audioStreamIndex &&
            packet->stream_index != m_videoStreamIndex) {
            continue;
        }

        if (packet->stream_index == m_audioStreamIndex) {
            {
                QMutexLocker locker(&mutex);
                if (stop_flag) {
                    break;
                }
                m_AcurrentPTS = packet->pts;
            }
            if (!m_APacket->Push(std::move(packet))) {
                continue;
            }
        } else {
            {
                QMutexLocker locker(&mutex);
                if (stop_flag) {
                    break;
                }
                m_VcurrentPTS = packet->pts;
            }
            if (!m_VPacket->Push(std::move(packet))) {
                continue;
            }
        }
    }

    if (reachedEof) {
        if (m_APacket) {
            m_APacket->RequestStop();
        }
        if (m_VPacket) {
            m_VPacket->RequestStop();
        }
        emit DemuxFinished();
    }

    if (failed) {
        if (m_APacket) {
            m_APacket->RequestStop();
        }
        if (m_VPacket) {
            m_VPacket->RequestStop();
        }
        emit DemuxErrorOccurred(errorMessage);
    }

    return 0;
}

int Demuxer::Rewind()
{
    return SeekRelative(-kSeekStepSeconds);
}

int Demuxer::Forward()
{
    return SeekRelative(kSeekStepSeconds);
}

int Demuxer::SeekTo(double seconds)
{
    if (!m_mediaSource) {
        LOG_WARN("Seek failed: media source is null");
        return -1;
    }

    AVFormatContext* ic = m_mediaSource->GetFormatContect();
    if (!ic) {
        LOG_WARN("Seek failed: format context is null");
        return -1;
    }

    if (seconds < 0.0) {
        seconds = 0.0;
    }

    if (ic->duration > 0) {
        const double durationSeconds = ic->duration / 1000000.0;
        if (seconds > durationSeconds) {
            seconds = durationSeconds;
        }
    }

    const int64_t targetTs = static_cast<int64_t>(seconds * AV_TIME_BASE);

    QMutexLocker locker(&mutex);
    const int ret = av_seek_frame(ic, -1, targetTs, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        char errBuf[256];
        av_strerror(ret, errBuf, sizeof(errBuf));
        LOG_WARN("Seek failed:", errBuf);
        return -1;
    }

    m_AcurrentPTS = 0;
    m_VcurrentPTS = 0;

    LOG_DEBUG("Demuxer seek to", seconds, "seconds");
    return 0;
}

int Demuxer::SeekRelative(double seconds)
{
    return SeekTo(CurrentPositionSeconds() + seconds);
}

int Demuxer::SuspendAndWait()
{
    {
        QMutexLocker locker(&mutex);
        if (stop_flag) {
            return -1;
        }
        pause_flag = true;
        suspend_requested_ = true;
        if (suspended_) {
            return 0;
        }
        suspended_ = false;
    }

    if (m_APacket) {
        m_APacket->InterruptWait();
    }
    if (m_VPacket) {
        m_VPacket->InterruptWait();
    }

    QMutexLocker locker(&mutex);
    while (!suspended_ && !stop_flag) {
        suspendCondition_.wait(&mutex);
    }

    return stop_flag ? -1 : 0;
}

double Demuxer::Position() const
{
    return CurrentPositionSeconds();
}

double Demuxer::CurrentPositionSeconds() const
{
    QMutexLocker locker(&mutex);

    if (m_VcurrentPTS > 0 && m_V_time_base.den > 0) {
        return m_VcurrentPTS * av_q2d(m_V_time_base);
    }

    if (m_AcurrentPTS > 0 && m_A_time_base.den > 0) {
        return m_AcurrentPTS * av_q2d(m_A_time_base);
    }

    return 0.0;
}

int Demuxer::Start()
{
    mutex.lock();
    pause_flag = false;
    suspend_requested_ = false;
    LOG_DEBUG("Demuxer thread started");
    suspendCondition_.wakeAll();
    mutex.unlock();

    if (m_APacket) {
        m_APacket->ResumeWait();
    }
    if (m_VPacket) {
        m_VPacket->ResumeWait();
    }

    return 0;
}

int Demuxer::Stop()
{
    mutex.lock();
    stop_flag = true;
    suspend_requested_ = false;
    LOG_DEBUG("Demuxer thread stopped");
    suspendCondition_.wakeAll();
    mutex.unlock();

    return 0;
}

int Demuxer::Pause()
{
    mutex.lock();
    pause_flag = true;
    suspend_requested_ = true;
    LOG_DEBUG("Demuxer thread paused");
    suspendCondition_.wakeAll();
    mutex.unlock();

    return 0;
}

bool Demuxer::WaitIfSuspended()
{
    QMutexLocker locker(&mutex);

    while (suspend_requested_ && !stop_flag) {
        suspended_ = true;
        suspendCondition_.wakeAll();
        suspendCondition_.wait(&mutex);
    }

    suspended_ = false;
    return !stop_flag;
}

bool Demuxer::IsSuspendRequested() const
{
    QMutexLocker locker(&mutex);
    return suspend_requested_;
}
