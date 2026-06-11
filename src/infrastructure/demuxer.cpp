#include "demuxer.h"

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
            info.width_ = ic->streams[i]->codecpar->width;
            info.height_ = ic->streams[i]->codecpar->height;
            info.CodecId_ = ic->streams[i]->codecpar->codec_id;
            const double frameRate = av_q2d(ic->streams[i]->avg_frame_rate);
            if (frameRate > 0.0) {
                info.FrameRate = frameRate;
            }
            info.V_timeBase = av_q2d(ic->streams[i]->time_base);
            if (ic->duration > 0) {
                info.duration_ = ic->duration/1000000;
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

    while (true) {
        AVPacket* packet = av_packet_alloc();
        int ret = av_read_frame(ic, packet);

        if (ret < 0) {
            av_packet_free(&packet);
            if (ret == AVERROR_EOF) {
                qDebug() << "Demuxing finished (EOF)";
                break;
            }
            qDebug() << "读取错误，重试...";
            QThread::msleep(20);
            continue;
        }

        // 提前过滤非音视频流
        if (packet->stream_index != m_audioStreamIndex &&
            packet->stream_index != m_videoStreamIndex) {
            av_packet_free(&packet);
            continue;
        }

        // ========== 音频流 ==========
        if (packet->stream_index == m_audioStreamIndex) {
            if (stop_flag) {
                av_packet_free(&packet);
                break;
            }
            m_AcurrentPTS = packet->pts;
            m_APacket->Push(packet);
        }
        // ========== 视频流 ==========
        else {
            if (stop_flag) {
                av_packet_free(&packet);
                break;
            }
            m_VcurrentPTS = packet->pts;
            m_VPacket->Push(packet);
        }
    }

    return 0;
}

int demuxer::Rewind() {
    return 0;
}

int demuxer::Forward() {
    return 0;
}

int demuxer::Start() {
    mutex.lock();
    pause_flag = false;
    qDebug() << "解复用线程开始运行";
    mutex.unlock();

    return 0;
}

int demuxer::Stop() {
    mutex.lock();
    stop_flag = true;
    qDebug() << "解复用线程被停止";
    mutex.unlock();

    return 0;
}

int demuxer::Pause(){
    mutex.lock();
    pause_flag = true;
    qDebug() << "解复用线程被暂停";
    mutex.unlock();

    return 0;
}
