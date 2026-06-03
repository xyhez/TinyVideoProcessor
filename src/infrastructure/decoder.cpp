#include "decoder.h"

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
    qDebug() << decoder_type << "解码器资源已清理";
}

void decoder::createConnect() {

}

bool decoder::initHWDecoder()
{
    // return false;
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

AVFrame *decoder::transferHWFrame(AVFrame *hwFrame)
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

    // 创建/复用转换上下文
    static SwsContext *sws_ctx = nullptr;
    sws_ctx = sws_getCachedContext(sws_ctx,
                                   nv12Frame->width, nv12Frame->height,
                                   AV_PIX_FMT_NV12,
                                   yuv420pFrame->width, yuv420pFrame->height,
                                   AV_PIX_FMT_YUV420P,
                                   SWS_BILINEAR, nullptr, nullptr, nullptr);

    if (!sws_ctx) {
        qDebug() << "创建 sws 上下文失败";
        av_frame_free(&nv12Frame);
        av_frame_free(&yuv420pFrame);
        return nullptr;
    }


    sws_scale(sws_ctx,
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

int decoder::initDecoder(std::shared_ptr<std::deque<AVPacket*>> packetVec
                         ,std::shared_ptr<std::deque<AVFrame*>> framevec
                         , AVCodecParameters *coderPar
                         , AVRational time_base) {
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
        initHWDecoder();
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

int decoder::startDecoder() {
    qDebug() << decoder_type << "线程开始解码"
             << (m_isHWDecoder ? "[硬解]" : "[软解]");

    frame = av_frame_alloc();
    if (!frame) {
        qDebug() << "分配帧失败";
        return -1;
    }

    while (true) {
        // ========== 取包部分 ==========
        AVPacket *packet = nullptr;
        if (decoder_type == "视频") {
            std::unique_lock<std::mutex> lock(mtx_V_packet);
            cv_V_packet_NotEmpty.wait(lock, [this]{
                return !m_PacketVec->empty() || g_pause || g_stop;
            });
            if (g_pause) continue;
            if (g_stop) break;
            packet = m_PacketVec->front();
            m_PacketVec->pop_front();
            lock.unlock();
            cv_V_packet_NotFull.notify_one();
        } else {
            std::unique_lock<std::mutex> lock(mtx_A_packet);
            cv_A_packet_NotEmpty.wait(lock, [this]{
                return !m_PacketVec->empty() || g_pause || g_stop;
            });
            if (g_pause) continue;
            if (g_stop) break;
            packet = m_PacketVec->front();
            m_PacketVec->pop_front();
            lock.unlock();
            cv_A_packet_NotFull.notify_one();
        }

        if (!packet) continue;

        int ret = avcodec_send_packet(Ctx, packet);
        av_packet_free(&packet);

        if (ret < 0) {
            char errBuf[256];
            av_strerror(ret, errBuf, sizeof(errBuf));
            qDebug() << "发送包失败:" << errBuf;
            continue;
        }

        // ========== 接收帧部分 ==========
        while (true) {
            ret = avcodec_receive_frame(Ctx, frame);
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
                outFrame = transferHWFrame(frame);
                if (!outFrame) {
                    qDebug() << "硬件帧转换失败，跳过此帧";
                    continue;
                }
            } else {
                // 视频软解 或 音频：直接引用
                outFrame = av_frame_alloc();
                if (!outFrame) {
                    qDebug() << "分配输出帧失败";
                    continue;
                }

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

            // 验证输出帧
            if (decoder_type == "视频" && (outFrame->width <= 0 || outFrame->height <= 0)) {
                qDebug() << "视频帧尺寸无效，丢弃";
                av_frame_free(&outFrame);
                continue;
            }

            // ========== 入队 ==========
            if (decoder_type == "视频") {
                std::unique_lock<std::mutex> lock(mtx_V_frame);
                cv_V_frame_NotFull.wait(lock, [this]{
                    return m_FrameVec->size() < MAXSIZE || g_pause || g_stop;
                });

                if (g_pause || g_stop) {
                    av_frame_free(&outFrame);
                    if (g_pause) continue;
                    if (g_stop) break;
                }

                m_FrameVec->emplace_back(outFrame);
                lock.unlock();
                cv_V_frame_NotEmpty.notify_one();
            } else {
                std::unique_lock<std::mutex> lock(mtx_A_frame);
                cv_A_frame_NotFull.wait(lock, [this]{
                    return m_FrameVec->size() < MAXSIZE || g_pause || g_stop;
                });

                if (g_pause || g_stop) {
                    av_frame_free(&outFrame);
                    if (g_pause) continue;
                    if (g_stop) break;
                }

                m_FrameVec->emplace_back(outFrame);
                lock.unlock();
                cv_A_frame_NotEmpty.notify_one();
            }

            av_frame_unref(frame);  // 释放 codec 的引用，复用 frame
        }
    }

    qDebug() << decoder_type << "解码线程结束";
    return 0;
}

int decoder::start() {
    mutex.lock();
    pause_flag = false;
    qDebug() << decoder_type << "解码线程开始运行";
    mutex.unlock();

    return 0;
}

int decoder::stop() {
    mutex.lock();
    stop_flag = true;
    qDebug() << decoder_type << "解码线程被停止";
    mutex.unlock();

    return 0;
}

int decoder::pause(){
    mutex.lock();
    pause_flag = true;
    qDebug() << decoder_type << "解码线程被暂停";
    mutex.unlock();

    return 0;
}
