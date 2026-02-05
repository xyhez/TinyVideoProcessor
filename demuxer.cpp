#include "demuxer.h"

demuxer::demuxer(std::shared_ptr<std::deque<AVPacket*>> APacket
                 ,std::shared_ptr<std::deque<AVPacket*>> VPacket
                 ,std::shared_ptr<std::deque<AVFrame*>>  AFrame
                 ,std::shared_ptr<std::deque<AVFrame*>>  VFrame
                 ,QObject *parent)
    : QObject{parent}
    , m_APacket(APacket)
    , m_VPacket(VPacket)
    , m_AFrame(AFrame)
    , m_VFrame(VFrame)
{

}


demuxer::~demuxer(){
    if (ic) {
        avformat_close_input(&ic);
        ic = nullptr;
    }

    qDebug() << "解复用器资源已清理";
}

int demuxer::initDemuxer(QString filename) {
    m_videoFileName = filename;
    qDebug() <<m_videoFileName.toUtf8().constData();

    ///< 初始化ic，获取文件格式
    if(avformat_open_input(&ic,m_videoFileName.toUtf8().constData(),nullptr,nullptr)) {
        qDebug()<<"文件打开失败~";
        return -1;
    }

    ///< 获取流信息
    if (avformat_find_stream_info(ic, nullptr) < 0) {
        qDebug()<<"无法获取流信息";
        avformat_close_input(&ic);
        ic = nullptr;
        return -1;
    }

    for(int i=0;i<ic->nb_streams;i++) {
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
            qDebug()<<"视频时间基(time_base):"<<av_q2d(ic->streams[i]->time_base);
            m_videoStreamIndex = i;
            m_V_coderPar = ic->streams[i]->codecpar;
            m_V_time_base = ic->streams[i]->time_base;
            g_videoinfo.videoname = m_videoFileName.toStdString();
            g_videoinfo.FrameRate = av_q2d(ic->streams[i]->avg_frame_rate);
            g_videoinfo.V_timeBase = av_q2d(ic->streams[i]->time_base);
            g_videoinfo.duration = ic->duration/1000000;
        }else

        ///< 找到音频流
        if(ic->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            qDebug() << QString("找到音频流 #%1: %2x%3, 编码=%4, 时长=%5秒")
                            .arg(i)
                            .arg(ic->streams[i]->codecpar->sample_rate)
                            .arg(ic->streams[i]->codecpar->bit_rate)
                            .arg(avcodec_get_name(ic->streams[i]->codecpar->codec_id))
                            .arg(ic->duration/1000000);
            qDebug()<<"音频时间基(time_base):"<<av_q2d(ic->streams[i]->time_base);
            m_audioStreamIndex = i;
            m_A_coderPar = ic->streams[i]->codecpar;
            m_A_time_base = ic->streams[i]->time_base;
            g_videoinfo.A_timeBase = av_q2d(ic->streams[i]->time_base);
        }else
            ///< 找到字幕流
            if(ic->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
                qDebug()<<"找到字幕流";
            }
    }

    return 0;
}

AVCodecParameters* demuxer::reAcoderPar(){
    return m_A_coderPar;
}

AVCodecParameters* demuxer::reVcoderPar(){
    return m_V_coderPar;
}

AVRational demuxer::reAtimebase() {
    return m_A_time_base;
}

AVRational demuxer::reVtimebase() {
    return m_V_time_base;
}

int demuxer::startDemuxer() {
    qDebug() << "开始解复用";

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
            std::unique_lock<std::mutex> lock(mtx_A_packet);

            cv_A_packet_NotFull.wait(lock, [this]{
                return m_APacket->size() < A_PACKETMAXSIZE || g_pause || g_stop;
            });

            if (g_pause) {
                av_packet_free(&packet);
                continue;
            }
            if (g_stop) {
                av_packet_free(&packet);
                break;
            }

            m_APacket->emplace_back(packet);
            m_AcurrentPTS = packet->pts;
            lock.unlock();
            cv_A_packet_NotEmpty.notify_one();
        }
        // ========== 视频流 ==========
        else {
            std::unique_lock<std::mutex> lock(mtx_V_packet);

            // cv_V_packet_NotFull.wait(lock, [this]{
            //     return m_VPacket->size() < MAXSIZE || g_pause || g_stop;
            // });

            // if (g_pause) {
            //     av_packet_free(&packet);
            //     continue;
            // }
            // if (g_stop) {
            //     av_packet_free(&packet);
            //     break;
            // }

            m_VPacket->emplace_back(packet);
            m_VcurrentPTS = packet->pts;
            lock.unlock();
            cv_V_packet_NotEmpty.notify_one();
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

int demuxer::start() {
    mutex.lock();
    pause_flag = false;
    qDebug()<<"解复用线程开始运行";
    mutex.unlock();

    return 0;
}

int demuxer::stop() {
    mutex.lock();
    stop_flag = true;
    qDebug()<<"解复用线程被停止";
    mutex.unlock();

    return 0;
}

int demuxer::pause(){
    mutex.lock();
    pause_flag = true;
    qDebug()<<"解复用线程被暂停";
    mutex.unlock();

    return 0;
}
