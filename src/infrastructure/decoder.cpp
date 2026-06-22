#include "decoder.h"

#include <QMutexLocker>

namespace {
constexpr bool kEnableHardwareDecoder = false;
}
decoder::decoder(QObject *parent)
    : QObject{parent}
{

}

decoder::~decoder() {
    if(Ctx) {
        avcodec_free_context(&Ctx);
    }
    if (m_hwDeviceCtx) {
        av_buffer_unref(&m_hwDeviceCtx);
    }
    if (m_swsCtx) {
        sws_freeContext(m_swsCtx);
        m_swsCtx = nullptr;
    }
    qDebug() << decoder_type << "解码器资源已清理";
}

void decoder::CreateConnect() {

}

bool decoder::InitHWDecoder()
{
    if (!kEnableHardwareDecoder) {
        qDebug() << "视频硬解已禁用，使用软件解码";
        return false;
    }
    if (par->codec_type != AVMEDIA_TYPE_VIDEO) {
        qDebug() << "音频流强制使用软解";
        return false;
    }

    // 只尝试常见视频编码的硬解码
    if (par->codec_id != AV_CODEC_ID_H264 &&
        par->codec_id != AV_CODEC_ID_HEVC &&
        par->codec_id != AV_CODEC_ID_AV1 &&
        par->codec_id != AV_CODEC_ID_VP8 &&
        par->codec_id != AV_CODEC_ID_VP9 &&
        par->codec_id != AV_CODEC_ID_MPEG2VIDEO &&
        par->codec_id != AV_CODEC_ID_MPEG4) {
        qDebug() << "视频编码格式不支持硬解码:" << avcodec_get_name(par->codec_id);
        return false;
    }

    // 按优先级尝试硬件加速器
    const char *hwDevices[] = {
        "d3d11va",      // Windows 首选
        "dxva2",        // Windows 备选
        "cuda",         // NVIDIA
        nullptr
    };

    for (int i = 0; hwDevices[i]; i++) {
        enum AVHWDeviceType type = av_hwdevice_find_type_by_name(hwDevices[i]);
        if (type == AV_HWDEVICE_TYPE_NONE) {
            continue;
        }

        // 检查解码器是否支持该硬件
        for (int j = 0;; j++) {
            const AVCodecHWConfig *config = avcodec_get_hw_config(codec, j);
            if (!config) break;

            if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) &&
                config->device_type == type) {

                // 创建硬件设备上下文
                int ret = av_hwdevice_ctx_create(&m_hwDeviceCtx, type, nullptr, nullptr, 0);
                if (ret < 0) {
                    char errBuf[256];
                    av_strerror(ret, errBuf, sizeof(errBuf));
                    qDebug() << "创建硬件设备上下文失败:" << hwDevices[i] << "-" << errBuf;
                    continue;
                }


                Ctx->hw_device_ctx = av_buffer_ref(m_hwDeviceCtx);
                m_hwPixFmt = config->pix_fmt;

                m_swPixFmt = AV_PIX_FMT_NV12;


                m_isHWDecoder = true;

                return true;
            }
        }
    }

    m_isHWDecoder = false;
    m_hwDeviceCtx = nullptr;
    m_hwPixFmt = AV_PIX_FMT_NONE;
    m_swPixFmt = AV_PIX_FMT_NONE;
    qDebug() << "视频使用软件解码";
    return false;
}

AVFrame *decoder::TransferHWFrame(AVFrame *hwFrame)
{
    if (!m_isHWDecoder || !hwFrame) {
        return nullptr;
    }


    AVFrame *nv12Frame = av_frame_alloc();
    if (!nv12Frame) {
        qDebug() << "分配 NV12 帧失败";
        return nullptr;
    }

    int ret = av_hwframe_transfer_data(nv12Frame, hwFrame, 0);
    if (ret < 0) {
        char errBuf[256];
        av_strerror(ret, errBuf, sizeof(errBuf));
        qDebug() << "硬件帧转换失败:" << errBuf;
        av_frame_free(&nv12Frame);
        return nullptr;
    }

    // 复制所有属性（包括pts, time_base等）
    av_frame_copy_props(nv12Frame, hwFrame);

    // 确保time_base也被设置（有些硬件解码器可能不设置这个）
    if (hwFrame->time_base.den > 0) {
        nv12Frame->time_base = hwFrame->time_base;
    } else if (m_time_base.den > 0) {
        nv12Frame->time_base = m_time_base;
    }

    // 验证格式
    if (nv12Frame->format != AV_PIX_FMT_NV12) {
        qDebug() << "警告：硬件解码输出不是 NV12，是:"
                 << av_get_pix_fmt_name((AVPixelFormat)nv12Frame->format);
        // 如果已经是 YUV420P，直接返回（但要确保属性正确）
        if (nv12Frame->format == AV_PIX_FMT_YUV420P) {
            qDebug() << "已经是YUV420P，直接返回，pts=" << nv12Frame->pts;
            return nv12Frame;
        }
    }

    // 第二步：NV12 -> YUV420P 转换
    AVFrame *yuv420pFrame = av_frame_alloc();
    if (!yuv420pFrame) {
        av_frame_free(&nv12Frame);
        return nullptr;
    }

    yuv420pFrame->format = AV_PIX_FMT_YUV420P;
    yuv420pFrame->width = nv12Frame->width;
    yuv420pFrame->height = nv12Frame->height;

    // ========== 关键：必须复制所有时间戳相关字段 ==========
    yuv420pFrame->pts = nv12Frame->pts;
    yuv420pFrame->pkt_dts = nv12Frame->pkt_dts;
    yuv420pFrame->time_base = nv12Frame->time_base;
    yuv420pFrame->flags = nv12Frame->flags;
    yuv420pFrame->best_effort_timestamp = nv12Frame->best_effort_timestamp;
    yuv420pFrame->duration = nv12Frame->duration;

    // 分配缓冲区
    ret = av_frame_get_buffer(yuv420pFrame, 32);
    if (ret < 0) {
        qDebug() << "分配 YUV420P 缓冲区失败";
        av_frame_free(&nv12Frame);
        av_frame_free(&yuv420pFrame);
        return nullptr;
    }

    m_swsCtx = sws_getCachedContext(m_swsCtx,
                                   nv12Frame->width, nv12Frame->height,
                                   AV_PIX_FMT_NV12,
                                   yuv420pFrame->width, yuv420pFrame->height,
                                   AV_PIX_FMT_YUV420P,
                                   SWS_BILINEAR, nullptr, nullptr, nullptr);

    if (!m_swsCtx) {
        qDebug() << "创建 sws 上下文失败";
        av_frame_free(&nv12Frame);
        av_frame_free(&yuv420pFrame);
        return nullptr;
    }


    sws_scale(m_swsCtx,
              nv12Frame->data, nv12Frame->linesize,
              0, nv12Frame->height,
              yuv420pFrame->data, yuv420pFrame->linesize);

    yuv420pFrame->pts = nv12Frame->pts;
    yuv420pFrame->time_base = nv12Frame->time_base;
    yuv420pFrame->best_effort_timestamp = nv12Frame->best_effort_timestamp;



    // 清理中间帧
    av_frame_free(&nv12Frame);

    return yuv420pFrame;
}

int decoder::InitDecoder(std::shared_ptr<SyncQueue<AVPacket*>> packetVec
                         ,std::shared_ptr<SyncQueue<AVFrame*>> framevec
                         , AVCodecParameters *coderPar
                         , AVRational time_base) {
    if (!coderPar) {
        qWarning() << "decoder init failed: codec parameters are null";
        return -1;
    }
    if (!packetVec) {
        qWarning() << "decoder init failed: packet queue is null";
        return -1;
    }
    if (!framevec) {
        qWarning() << "decoder init failed: frame queue is null";
        return -1;
    }

    m_PacketVec = packetVec;
    m_FrameVec = framevec;
    m_time_base = time_base;

    par = coderPar;

    codec = avcodec_find_decoder(par->codec_id);
    if (!codec) return -1;

    Ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(Ctx, par);

    // 先确定解码器类型
    if (par->codec_type == AVMEDIA_TYPE_VIDEO) {
        decoder_type = "视频";
        // 视频尝试硬解码
        InitHWDecoder();
    } else {
        decoder_type = "音频";
        // 音频强制软解
        m_isHWDecoder = false;
        m_hwDeviceCtx = nullptr;
        m_hwPixFmt = AV_PIX_FMT_NONE;
    }

    int ret = avcodec_open2(Ctx, codec, nullptr);
    if (ret < 0) {
        char errBuf[256];
        av_strerror(ret, errBuf, sizeof(errBuf));
        qDebug() << "avcodec_open2 错误:" << errBuf;
        return -1;
    }

    qDebug() << decoder_type << "解码器初始化完成"
             << (m_isHWDecoder ? "(硬解)" : "(软解)");
    return 0;
}

int decoder::StartDecoder() {
    qDebug() << decoder_type << "线程开始解码"
             << (m_isHWDecoder ? "[硬解]" : "[软解]");

    frame = av_frame_alloc();
    if (!frame) {
        qDebug() << "分配帧失败";
        return -1;
    }

    while (true) {
        // ========== 取包部分 ==========
        if (!WaitIfSuspended()) {
            break;
        }

        AVPacket *packet = nullptr;
        if (!m_PacketVec->Pop(packet)) {
            if (m_PacketVec->IsStopRequested()) {
                break;
            }
            continue;
        }

        if (!packet) continue;

        if (IsSuspendRequested()) {
            av_packet_free(&packet);
            continue;
        }

        int ret = 0;
        {
            QMutexLocker locker(&mutex);
            ret = avcodec_send_packet(Ctx, packet);
        }
        av_packet_free(&packet);

        if (ret < 0) {
            char errBuf[256];
            av_strerror(ret, errBuf, sizeof(errBuf));
            qDebug() << "发送包失败:" << errBuf;
            continue;
        }

        // ========== 接收帧部分 ==========
        while (true) {
            {
                QMutexLocker locker(&mutex);
                ret = avcodec_receive_frame(Ctx, frame);
            }
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            if (ret < 0) {
                char errBuf[256];
                av_strerror(ret, errBuf, sizeof(errBuf));
                qDebug() << "接收解码帧失败:" << errBuf;
                break;
            }

            // 确保 time_base 有效
            if (frame->time_base.den <= 0) {
                frame->time_base = m_time_base;
            }

            // 处理帧（区分视频硬解/视频软解/音频）
            AVFrame *outFrame = nullptr;

            if (decoder_type == "视频" && m_isHWDecoder &&
                frame->format == m_hwPixFmt) {
                // 视频硬解帧：需要转换
                outFrame = TransferHWFrame(frame);
                if (!outFrame) {
                    qDebug() << "硬件帧转换失败，跳过此帧";
                    continue;
                }
            } else {
                // 视频软解 或 音频：需要判断是否需要格式转换
                outFrame = av_frame_alloc();
                if (!outFrame) {
                    qDebug() << "分配输出帧失败";
                    continue;
                }

                // 如果是视频且格式不是 yuv420p，进行格式转换
                if (decoder_type == "视频" && frame->format != AV_PIX_FMT_YUV420P) {
                    // 格式转换：任意格式 -> yuv420p
                    outFrame->format = AV_PIX_FMT_YUV420P;
                    outFrame->width = frame->width;
                    outFrame->height = frame->height;

                    // 复制时间戳属性
                    outFrame->pts = frame->pts;
                    outFrame->pkt_dts = frame->pkt_dts;
                    outFrame->time_base = frame->time_base.den > 0 ? frame->time_base : m_time_base;
                    outFrame->best_effort_timestamp = frame->best_effort_timestamp;
                    outFrame->duration = frame->duration;

                    // 分配缓冲区
                    int ret = av_frame_get_buffer(outFrame, 32);
                    if (ret < 0) {
                        qDebug() << "分配转换后帧缓冲区失败";
                        av_frame_free(&outFrame);
                        continue;
                    }

                    // 创建或更新 sws 转换上下文
                    m_swsCtx = sws_getCachedContext(
                        m_swsCtx,
                        frame->width, frame->height, (AVPixelFormat)frame->format,
                        outFrame->width, outFrame->height, AV_PIX_FMT_YUV420P,
                        SWS_BILINEAR, nullptr, nullptr, nullptr);

                    if (!m_swsCtx) {
                        qDebug() << "创建格式转换上下文失败";
                        av_frame_free(&outFrame);
                        continue;
                    }

                    // 执行格式转换
                    sws_scale(m_swsCtx,
                              frame->data, frame->linesize,
                              0, frame->height,
                              outFrame->data, outFrame->linesize);

                    qDebug() << "视频格式转换:"
                             << av_get_pix_fmt_name((AVPixelFormat)frame->format)
                             << "->" << av_get_pix_fmt_name(AV_PIX_FMT_YUV420P);
                } else {
                    // 音频 或 已经是 yuv420p 的视频：直接引用
                    if (av_frame_ref(outFrame, frame) < 0) {
                        qDebug() << "引用帧失败";
                        av_frame_free(&outFrame);
                        continue;
                    }

                    // 确保 time_base
                    if (outFrame->time_base.den <= 0) {
                        outFrame->time_base = m_time_base;
                    }
                }
            }

            // 验证输出帧
            if (decoder_type == "视频" && (outFrame->width <= 0 || outFrame->height <= 0)) {
                qDebug() << "视频帧尺寸无效，丢弃";
                av_frame_free(&outFrame);
                continue;
            }

            if (ShouldDiscardFrame(outFrame)) {
                av_frame_free(&outFrame);
                av_frame_unref(frame);
                continue;
            }

            // ========== 入队 ==========
            if (!m_FrameVec->Push(outFrame)) {
                av_frame_free(&outFrame);
                av_frame_unref(frame);
                continue;
            }

            av_frame_unref(frame);  // 释放 codec 的引用，复用 frame
        }
    }

    qDebug() << decoder_type << "解码线程结束";
    return 0;
}

int decoder::Flush() {
    if (!Ctx) {
        qWarning() << decoder_type << "decoder flush failed: context is null";
        return -1;
    }

    {
        QMutexLocker locker(&mutex);
        avcodec_flush_buffers(Ctx);
    }

    qDebug() << decoder_type << "decoder flushed";
    return 0;
}

int decoder::SuspendAndWait() {
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

    if (m_PacketVec) {
        m_PacketVec->InterruptWait();
    }
    if (m_FrameVec) {
        m_FrameVec->InterruptWait();
    }

    QMutexLocker locker(&mutex);
    while (!suspended_ && !stop_flag) {
        suspendCondition_.wait(&mutex);
    }

    return stop_flag ? -1 : 0;
}

void decoder::SetDiscardBefore(double seconds) {
    QMutexLocker locker(&mutex);
    m_discardBeforeEnabled = true;
    m_discardBeforeSeconds = seconds < 0.0 ? 0.0 : seconds;
}

void decoder::ClearDiscardBefore() {
    QMutexLocker locker(&mutex);
    m_discardBeforeEnabled = false;
    m_discardBeforeSeconds = 0.0;
}

bool decoder::ShouldDiscardFrame(const AVFrame *candidateFrame) {
    if (!candidateFrame || candidateFrame->pts == AV_NOPTS_VALUE) {
        return false;
    }

    QMutexLocker locker(&mutex);
    if (!m_discardBeforeEnabled || m_time_base.num <= 0 || m_time_base.den <= 0) {
        return false;
    }

    const double frameSeconds = candidateFrame->pts * av_q2d(m_time_base);
    if (frameSeconds < m_discardBeforeSeconds) {
        return true;
    }

    m_discardBeforeEnabled = false;
    return false;
}

int decoder::Start() {
    mutex.lock();
    pause_flag = false;
    suspend_requested_ = false;
    qDebug() << decoder_type << "解码线程开始运行";
    suspendCondition_.wakeAll();
    mutex.unlock();

    if (m_PacketVec) {
        m_PacketVec->ResumeWait();
    }
    if (m_FrameVec) {
        m_FrameVec->ResumeWait();
    }

    return 0;
}

int decoder::Stop() {
    mutex.lock();
    stop_flag = true;
    suspend_requested_ = false;
    qDebug() << decoder_type << "解码线程被停止";
    suspendCondition_.wakeAll();
    mutex.unlock();

    return 0;
}

int decoder::Pause(){
    mutex.lock();
    pause_flag = true;
    suspend_requested_ = true;
    qDebug() << decoder_type << "解码线程被暂停";
    suspendCondition_.wakeAll();
    mutex.unlock();

    return 0;
}
bool decoder::WaitIfSuspended() {
    QMutexLocker locker(&mutex);

    while (suspend_requested_ && !stop_flag) {
        suspended_ = true;
        suspendCondition_.wakeAll();
        suspendCondition_.wait(&mutex);
    }

    suspended_ = false;
    return !stop_flag;
}

bool decoder::IsSuspendRequested() const {
    QMutexLocker locker(&mutex);
    return suspend_requested_;
}
