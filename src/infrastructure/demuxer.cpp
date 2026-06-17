#include "demuxer.h"

#include <QMutexLocker>

namespace {
constexpr double kSeekStepSeconds = 10.0;
}

demuxer::demuxer(std::shared_ptr<IMediaSource> mediaSource
                 ,std::shared_ptr<SyncQueue<AVPacket*>> APacket
                 ,std::shared_ptr<SyncQueue<AVPacket*>> VPacket
                 ,std::shared_ptr<SyncQueue<AVFrame*>>  AFrame
                 ,std::shared_ptr<SyncQueue<AVFrame*>>  VFrame
                 ,QObject *parent)
    : QObject{parent}
    , m_mediaSource(mediaSource)
    , m_APacket(APacket)
    , m_VPacket(VPacket)
    , m_AFrame(AFrame)
    , m_VFrame(VFrame)
{
    if (!m_mediaSource) {
        qCritical() << "媒体源为空";
        return;
    }

    // 从媒体源获取 AVFormatContext
    AVFormatContext* ic = m_mediaSource->GetFormatContect();

    if (!ic) {
        qCritical() << "AVFormatContext 为空，媒体源未正确打开";
        return;
    }

    // 获取媒体源信息的引用
    Fileinfo& info = m_mediaSource->GetSourceInfo();

    // 遍历流，找音视频流
    for(int i = 0; i < ic->nb_streams; i++) {
        ///< 找到视频流
        if(ic->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            qDebug() << QString("找到视频流 #%1: %2x%3, 编码=%4, 时长=%5秒")
                            .arg(i)
                            .arg(ic->streams[i]->codecpar->width)
                            .arg(ic->streams[i]->codecpar->height)
                            .arg(avcodec_get_name(ic->streams[i]->codecpar->codec_id))
                            .arg(ic->duration/1000000);
            qDebug() << QString("  帧率: %1 fps").arg(av_q2d(ic->streams[i]->avg_frame_rate));
            qDebug() << QString("  帧持续时间: %1 ms").arg(1.0/ av_q2d(ic->streams[i]->avg_frame_rate)*1000);
            qDebug() << "视频时间基(time_base):" << av_q2d(ic->streams[i]->time_base);

            m_videoStreamIndex = i;
            m_V_coderPar = ic->streams[i]->codecpar;
            m_V_time_base = ic->streams[i]->time_base;

            // 填充媒体源信息
            info.hasVideo_ = true;
            info.videoStreamIndex_ = i;
            info.width_ = ic->streams[i]->codecpar->width;
            info.height_ = ic->streams[i]->codecpar->height;
            info.CodecId_ = ic->streams[i]->codecpar->codec_id;
            info.videoCodecId_ = ic->streams[i]->codecpar->codec_id;
            const double frameRate = av_q2d(ic->streams[i]->avg_frame_rate);
            if (frameRate > 0.0) {
                info.FrameRate = frameRate;
            }
            info.V_timeBase = av_q2d(ic->streams[i]->time_base);
            if (ic->duration > 0) {
                info.duration_ = ic->duration/1000000; // info拿到的duration单位是s
            }

        }
        else if(ic->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            ///< 找到音频流
            qDebug() << QString("找到音频流 #%1: %2x%3, 编码=%4, 时长=%5秒")
                            .arg(i)
                            .arg(ic->streams[i]->codecpar->sample_rate)
                            .arg(ic->streams[i]->codecpar->bit_rate)
                            .arg(avcodec_get_name(ic->streams[i]->codecpar->codec_id))
                            .arg(ic->duration/1000000);
            qDebug() << "音频时间基(time_base):" << av_q2d(ic->streams[i]->time_base);

            m_audioStreamIndex = i;
            m_A_coderPar = ic->streams[i]->codecpar;
            m_A_time_base = ic->streams[i]->time_base;

            // 填充媒体源信息
            info.hasAudio_ = true;
            info.audioStreamIndex_ = i;
            info.audioCodecId_ = ic->streams[i]->codecpar->codec_id;
            info.A_timeBase = av_q2d(ic->streams[i]->time_base);

        }
        else if(ic->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            ///< 找到字幕流
            qDebug() << "找到字幕流";
        }
    }
}

demuxer::~demuxer(){
    // 不再需要关闭 AVFormatContext，由 IMediaSource 管理
    qDebug() << "解复用器资源已清理";
}

AVCodecParameters* demuxer::ReAcoderPar(){
    return m_A_coderPar;
}

AVCodecParameters* demuxer::ReVcoderPar(){
    return m_V_coderPar;
}

void demuxer::SetAudioQueues(std::shared_ptr<SyncQueue<AVPacket*>> APacket,
                             std::shared_ptr<SyncQueue<AVFrame*>> AFrame) {
    QMutexLocker locker(&mutex);
    m_APacket = APacket;
    m_AFrame = AFrame;
}

AVRational demuxer::ReAtimebase() {
    return m_A_time_base;
}

AVRational demuxer::ReVtimebase() {
    return m_V_time_base;
}

int demuxer::StartDemuxer() {
    qDebug() << "开始解复用";

    if (!m_mediaSource) {
        qDebug() << "媒体源未初始化";
        return -1;
    }

    AVFormatContext* ic = m_mediaSource->GetFormatContect();

    if (!ic) {
        qDebug() << "上下文未初始化";
        return -1;
    }

    bool reachedEof = false;
    while (true) {
        if (!WaitIfSuspended()) {
            break;
        }

        AVPacket* packet = av_packet_alloc();
        int ret = 0;

        {
            QMutexLocker locker(&mutex);
            if (stop_flag) {
                av_packet_free(&packet);
                break;
            }
            ret = av_read_frame(ic, packet);
        }

        if (ret < 0) {
            av_packet_free(&packet);
            if (ret == AVERROR_EOF) {
                qDebug() << "Demuxing finished (EOF)";
                reachedEof = true;
                break;
            }
            qDebug() << "读取错误，重试...";
            QThread::msleep(20);
            continue;
        }

        // 提前过滤非音视频流
        if (IsSuspendRequested()) {
            av_packet_free(&packet);
            continue;
        }

        if (packet->stream_index != m_audioStreamIndex &&
            packet->stream_index != m_videoStreamIndex) {
            av_packet_free(&packet);
            continue;
        }

        // ========== 音频流 ==========
        if (packet->stream_index == m_audioStreamIndex) {
            {
                QMutexLocker locker(&mutex);
                if (stop_flag) {
                    av_packet_free(&packet);
                    break;
                }
                m_AcurrentPTS = packet->pts;
            }
            if (!m_APacket->Push(packet)) {
                continue;
            }
        }
        // ========== 视频流 ==========
        else {
            {
                QMutexLocker locker(&mutex);
                if (stop_flag) {
                    av_packet_free(&packet);
                    break;
                }
                m_VcurrentPTS = packet->pts;
            }
            if (!m_VPacket->Push(packet)) {
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

    return 0;
}

int demuxer::Rewind() {
    return SeekRelative(-kSeekStepSeconds);
}

int demuxer::Forward() {
    return SeekRelative(kSeekStepSeconds);
}

int demuxer::SeekTo(double seconds) {
    if (!m_mediaSource) {
        qWarning() << "Seek failed: media source is null";
        return -1;
    }

    AVFormatContext* ic = m_mediaSource->GetFormatContect();
    if (!ic) {
        qWarning() << "Seek failed: format context is null";
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
        qWarning() << "Seek failed:" << errBuf;
        return -1;
    }

    m_AcurrentPTS = 0;
    m_VcurrentPTS = 0;

    qDebug() << "Demuxer seek to" << seconds << "seconds";
    return 0;
}

int demuxer::SeekRelative(double seconds) {
    return SeekTo(CurrentPositionSeconds() + seconds);
}

int demuxer::SuspendAndWait() {
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

double demuxer::Position() const {
    return CurrentPositionSeconds();
}

double demuxer::CurrentPositionSeconds() const {
    QMutexLocker locker(&mutex);

    if (m_VcurrentPTS > 0 && m_V_time_base.den > 0) {
        return m_VcurrentPTS * av_q2d(m_V_time_base);
    }

    if (m_AcurrentPTS > 0 && m_A_time_base.den > 0) {
        return m_AcurrentPTS * av_q2d(m_A_time_base);
    }

    return 0.0;
}

int demuxer::Start() {
    mutex.lock();
    pause_flag = false;
    suspend_requested_ = false;
    qDebug() << "解复用线程开始运行";
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

int demuxer::Stop() {
    mutex.lock();
    stop_flag = true;
    suspend_requested_ = false;
    qDebug() << "解复用线程被停止";
    suspendCondition_.wakeAll();
    mutex.unlock();

    return 0;
}

int demuxer::Pause(){
    mutex.lock();
    pause_flag = true;
    suspend_requested_ = true;
    qDebug() << "解复用线程被暂停";
    suspendCondition_.wakeAll();
    mutex.unlock();

    return 0;
}
bool demuxer::WaitIfSuspended() {
    QMutexLocker locker(&mutex);

    while (suspend_requested_ && !stop_flag) {
        suspended_ = true;
        suspendCondition_.wakeAll();
        suspendCondition_.wait(&mutex);
    }

    suspended_ = false;
    return !stop_flag;
}

bool demuxer::IsSuspendRequested() const {
    QMutexLocker locker(&mutex);
    return suspend_requested_;
}
