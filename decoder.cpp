#include "decoder.h"

decoder::decoder(QObject *parent)
    : QObject{parent}
{

}

decoder::~decoder() {
    if(Ctx) {
        avcodec_free_context(&Ctx);
    }
    qDebug() << "解码器资源已清理";
}

void decoder::createConnect() {

}

int decoder::initDecoder(std::shared_ptr<QVector<AVPacket*>> packetVec
                         ,std::shared_ptr<QVector<AVFrame*>> framevec
                         , AVCodecParameters* coderPar
                         , AVRational time_base) {
    m_PacketVec = packetVec;
    m_FrameVec = framevec;
    m_time_base = time_base;

    par = coderPar;

    codec = avcodec_find_decoder(par->codec_id);
    if (!codec) return -1;

    Ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(Ctx, par);

    if (par->codec_type == AVMEDIA_TYPE_VIDEO)
    {
        decoder_type = "视频";
    } else {
        decoder_type = "音频";
    }

    int ret = avcodec_open2(Ctx, codec, nullptr);
    if (ret < 0)
        qDebug()<< "avcodec_open2 error:" << ret;


    return 0;
}

int decoder::startDecoder() {
    qDebug()<<decoder_type<<"线程开始解码";

    frame   = av_frame_alloc();

    while(true) {
        if(stop_flag == true) {
            break;
        }
        if(pause_flag == true) {
            QThread::msleep(10);
            continue;
        }
        if(m_FrameVec->size()>=30){
            // qDebug()<<decoder_type<<"帧队列已满，等待中...";
            QThread::msleep(100);
            continue;
        }

        if(m_PacketVec->isEmpty()){
            QThread::msleep(100);
            continue;
        }
        mutex.lock();
        AVPacket* packet = m_PacketVec->takeFirst();
        mutex.unlock();
        if (!packet) {
            qDebug() << "取到空包";
            continue;
        }

        int ret = avcodec_send_packet(Ctx, packet);
        av_packet_free(&packet);

        ///< 处理返回值
        if(ret < 0) {
            qDebug()<<"解码失败...";
            continue;
        }

        while(true) {
            ///< 接收原始帧
            ret = avcodec_receive_frame(Ctx,frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            } else if (ret < 0) {
                qDebug() << QString("接收解码帧失败: %1").arg(ret);
                break;
            }

            frame->time_base = m_time_base;

            // if (decoder_type == "视频") {
            //     const char* pix = av_get_pix_fmt_name(static_cast<AVPixelFormat>(frame->format));
            //     QString pixName = pix ? QString::fromUtf8(pix) : QStringLiteral("unknown");
            //     qInfo().nospace() << " pts=" << frame->pts
            //                       << " ms=" << frame->pts * av_q2d(m_time_base)*1000
            //                       << " time_base=" << av_q2d(m_time_base)
            //                       << " type=Video "
            //                       << frame->width << "x" << frame->height
            //                       << " pix_fmt=" << pixName;
            // } else {
            //     // frame->time_base = Ctx->time_base;
            //     qInfo().nospace() << " pts=" << frame->pts
            //                       << " ms=" <<frame->pts * av_q2d(m_time_base)*1000
            //                       << " time_base="<<av_q2d(m_time_base)
            //                       << " nb_samples="<<frame->nb_samples
            //                       << " sample_rate=" << par->sample_rate
            //                       << " type=zAudio samples=" << frame->nb_samples
            //                       << " frame->duration=" << frame->nb_samples / (double)frame->sample_rate * 1000.0;
            // }

            AVFrame* vFrame = av_frame_alloc();
            av_frame_ref(vFrame, frame);
            mutex.lock();
            if (!m_FrameVec->emplace_back(vFrame)) {
                qDebug() << decoder_type << "帧入队失败!";
            }
            mutex.unlock();

            av_frame_unref(frame);   // 关键：复用 frame
        }

    }

    return 0;
}

int decoder::start() {
    mutex.lock();
    pause_flag = false;
    qDebug()<<"解码线程开始运行";
    mutex.unlock();

    return 0;
}

int decoder::stop() {
    mutex.lock();
    stop_flag = true;
    qDebug()<<"解码线程被停止";
    mutex.unlock();

    return 0;
}

int decoder::pause(){
    mutex.lock();
    pause_flag = true;
    qDebug()<<decoder_type<<"解码线程被暂停";
    mutex.unlock();

    return 0;
}


