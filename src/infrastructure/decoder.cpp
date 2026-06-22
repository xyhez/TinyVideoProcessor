#include "decoder.h"
#include "core/AppLog.h"

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
    APP_LOG_DEBUG_STREAM() << decoder_type << "瑙ｇ爜鍣ㄨ祫婧愬凡娓呯悊";
}

void decoder::CreateConnect() {

}

bool decoder::InitHWDecoder()
{
    if (!kEnableHardwareDecoder) {
        APP_LOG_DEBUG_STREAM() << "Video hardware decoding disabled, using software decoder";
        return false;
    }
    if (par->codec_type != AVMEDIA_TYPE_VIDEO) {
        APP_LOG_DEBUG_STREAM() << "Audio stream uses software decoder";
        return false;
    }

    // 鍙皾璇曞父瑙佽棰戠紪鐮佺殑纭В鐮?
    if (par->codec_id != AV_CODEC_ID_H264 &&
        par->codec_id != AV_CODEC_ID_HEVC &&
        par->codec_id != AV_CODEC_ID_AV1 &&
        par->codec_id != AV_CODEC_ID_VP8 &&
        par->codec_id != AV_CODEC_ID_VP9 &&
        par->codec_id != AV_CODEC_ID_MPEG2VIDEO &&
        par->codec_id != AV_CODEC_ID_MPEG4) {
        APP_LOG_DEBUG_STREAM() << "Video codec does not support hardware decoding:" << avcodec_get_name(par->codec_id);
        return false;
    }

    // 鎸変紭鍏堢骇灏濊瘯纭欢鍔犻€熷櫒
    const char *hwDevices[] = {
        "d3d11va",      // Windows 棣栭€?
        "dxva2",        // Windows 澶囬€?
        "cuda",         // NVIDIA
        nullptr
    };

    for (int i = 0; hwDevices[i]; i++) {
        enum AVHWDeviceType type = av_hwdevice_find_type_by_name(hwDevices[i]);
        if (type == AV_HWDEVICE_TYPE_NONE) {
            continue;
        }

        // 妫€鏌ヨВ鐮佸櫒鏄惁鏀寔璇ョ‖浠?
        for (int j = 0;; j++) {
            const AVCodecHWConfig *config = avcodec_get_hw_config(codec, j);
            if (!config) break;

            if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) &&
                config->device_type == type) {

                // 鍒涘缓纭欢璁惧涓婁笅鏂?
                int ret = av_hwdevice_ctx_create(&m_hwDeviceCtx, type, nullptr, nullptr, 0);
                if (ret < 0) {
                    char errBuf[256];
                    av_strerror(ret, errBuf, sizeof(errBuf));
                    APP_LOG_DEBUG_STREAM() << "Create hardware device context failed:" << hwDevices[i] << "-" << errBuf;
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
    APP_LOG_DEBUG_STREAM() << "Video uses software decoder";
    return false;
}

AVFrame *decoder::TransferHWFrame(AVFrame *hwFrame)
{
    if (!m_isHWDecoder || !hwFrame) {
        return nullptr;
    }


    AVFrame *nv12Frame = av_frame_alloc();
    if (!nv12Frame) {
        APP_LOG_DEBUG_STREAM() << "Allocate NV12 frame failed";
        return nullptr;
    }

    int ret = av_hwframe_transfer_data(nv12Frame, hwFrame, 0);
    if (ret < 0) {
        char errBuf[256];
        av_strerror(ret, errBuf, sizeof(errBuf));
        APP_LOG_DEBUG_STREAM() << "Hardware frame transfer failed:" << errBuf;
        av_frame_free(&nv12Frame);
        return nullptr;
    }

    // 澶嶅埗鎵€鏈夊睘鎬э紙鍖呮嫭pts, time_base绛夛級
    av_frame_copy_props(nv12Frame, hwFrame);

    // 纭繚time_base涔熻璁剧疆锛堟湁浜涚‖浠惰В鐮佸櫒鍙兘涓嶈缃繖涓級
    if (hwFrame->time_base.den > 0) {
        nv12Frame->time_base = hwFrame->time_base;
    } else if (m_time_base.den > 0) {
        nv12Frame->time_base = m_time_base;
    }

    // 楠岃瘉鏍煎紡
    if (nv12Frame->format != AV_PIX_FMT_NV12) {
        APP_LOG_DEBUG_STREAM() << "Hardware decoder output is not NV12, format:"
                 << av_get_pix_fmt_name((AVPixelFormat)nv12Frame->format);
        // 濡傛灉宸茬粡鏄?YUV420P锛岀洿鎺ヨ繑鍥烇紙浣嗚纭繚灞炴€ф纭級
        if (nv12Frame->format == AV_PIX_FMT_YUV420P) {
            APP_LOG_DEBUG_STREAM() << "Frame is already YUV420P, returning directly, pts=" << nv12Frame->pts;
            return nv12Frame;
        }
    }

    // 绗簩姝ワ細NV12 -> YUV420P 杞崲
    AVFrame *yuv420pFrame = av_frame_alloc();
    if (!yuv420pFrame) {
        av_frame_free(&nv12Frame);
        return nullptr;
    }

    yuv420pFrame->format = AV_PIX_FMT_YUV420P;
    yuv420pFrame->width = nv12Frame->width;
    yuv420pFrame->height = nv12Frame->height;

    // ========== 鍏抽敭锛氬繀椤诲鍒舵墍鏈夋椂闂存埑鐩稿叧瀛楁 ==========
    yuv420pFrame->pts = nv12Frame->pts;
    yuv420pFrame->pkt_dts = nv12Frame->pkt_dts;
    yuv420pFrame->time_base = nv12Frame->time_base;
    yuv420pFrame->flags = nv12Frame->flags;
    yuv420pFrame->best_effort_timestamp = nv12Frame->best_effort_timestamp;
    yuv420pFrame->duration = nv12Frame->duration;

    // 鍒嗛厤缂撳啿鍖?
    ret = av_frame_get_buffer(yuv420pFrame, 32);
    if (ret < 0) {
        APP_LOG_DEBUG_STREAM() << "Allocate YUV420P buffer failed";
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
        APP_LOG_DEBUG_STREAM() << "Create sws context failed";
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



    // 娓呯悊涓棿甯?
    av_frame_free(&nv12Frame);

    return yuv420pFrame;
}

int decoder::InitDecoder(std::shared_ptr<SyncQueue<AVPacket*>> packetVec
                         ,std::shared_ptr<SyncQueue<AVFrame*>> framevec
                         , AVCodecParameters *coderPar
                         , AVRational time_base) {
    if (!coderPar) {
        APP_LOG_WARN_STREAM() << "decoder init failed: codec parameters are null";
        return -1;
    }
    if (!packetVec) {
        APP_LOG_WARN_STREAM() << "decoder init failed: packet queue is null";
        return -1;
    }
    if (!framevec) {
        APP_LOG_WARN_STREAM() << "decoder init failed: frame queue is null";
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

    // 鍏堢‘瀹氳В鐮佸櫒绫诲瀷
    if (par->codec_type == AVMEDIA_TYPE_VIDEO) {
        decoder_type = "瑙嗛";
        // 瑙嗛灏濊瘯纭В鐮?
        InitHWDecoder();
    } else {
        decoder_type = "闊抽";
        // 闊抽寮哄埗杞В
        m_isHWDecoder = false;
        m_hwDeviceCtx = nullptr;
        m_hwPixFmt = AV_PIX_FMT_NONE;
    }

    int ret = avcodec_open2(Ctx, codec, nullptr);
    if (ret < 0) {
        char errBuf[256];
        av_strerror(ret, errBuf, sizeof(errBuf));
        APP_LOG_DEBUG_STREAM() << "avcodec_open2 failed:" << errBuf;
        return -1;
    }

    APP_LOG_DEBUG_STREAM() << decoder_type << "decoder initialized"
             << (m_isHWDecoder ? "(hardware)" : "(software)");
    return 0;
}

int decoder::StartDecoder() {
    APP_LOG_DEBUG_STREAM() << decoder_type << "decoder thread started"
             << (m_isHWDecoder ? "[hardware]" : "[software]");

    frame = av_frame_alloc();
    if (!frame) {
        APP_LOG_DEBUG_STREAM() << "Allocate frame failed";
        return -1;
    }

    while (true) {
        // ========== 鍙栧寘閮ㄥ垎 ==========
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
            APP_LOG_DEBUG_STREAM() << "Send packet failed:" << errBuf;
            continue;
        }

        // ========== 鎺ユ敹甯ч儴鍒?==========
        while (true) {
            {
                QMutexLocker locker(&mutex);
                ret = avcodec_receive_frame(Ctx, frame);
            }
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            if (ret < 0) {
                char errBuf[256];
                av_strerror(ret, errBuf, sizeof(errBuf));
                APP_LOG_DEBUG_STREAM() << "Receive decoded frame failed:" << errBuf;
                break;
            }

            // 纭繚 time_base 鏈夋晥
            if (frame->time_base.den <= 0) {
                frame->time_base = m_time_base;
            }

            // 澶勭悊甯э紙鍖哄垎瑙嗛纭В/瑙嗛杞В/闊抽锛?
            AVFrame *outFrame = nullptr;

            if (decoder_type == "瑙嗛" && m_isHWDecoder &&
                frame->format == m_hwPixFmt) {
                // 瑙嗛纭В甯э細闇€瑕佽浆鎹?
                outFrame = TransferHWFrame(frame);
                if (!outFrame) {
                    APP_LOG_DEBUG_STREAM() << "Hardware frame transfer failed, skipping frame";
                    continue;
                }
            } else {
                // 瑙嗛杞В 鎴?闊抽锛氶渶瑕佸垽鏂槸鍚﹂渶瑕佹牸寮忚浆鎹?
                outFrame = av_frame_alloc();
                if (!outFrame) {
                    APP_LOG_DEBUG_STREAM() << "Allocate output frame failed";
                    continue;
                }

                // 濡傛灉鏄棰戜笖鏍煎紡涓嶆槸 yuv420p锛岃繘琛屾牸寮忚浆鎹?
                if (decoder_type == "瑙嗛" && frame->format != AV_PIX_FMT_YUV420P) {
                    // 鏍煎紡杞崲锛氫换鎰忔牸寮?-> yuv420p
                    outFrame->format = AV_PIX_FMT_YUV420P;
                    outFrame->width = frame->width;
                    outFrame->height = frame->height;

                    // 澶嶅埗鏃堕棿鎴冲睘鎬?
                    outFrame->pts = frame->pts;
                    outFrame->pkt_dts = frame->pkt_dts;
                    outFrame->time_base = frame->time_base.den > 0 ? frame->time_base : m_time_base;
                    outFrame->best_effort_timestamp = frame->best_effort_timestamp;
                    outFrame->duration = frame->duration;

                    // 鍒嗛厤缂撳啿鍖?
                    int ret = av_frame_get_buffer(outFrame, 32);
                    if (ret < 0) {
                        APP_LOG_DEBUG_STREAM() << "Allocate converted frame buffer failed";
                        av_frame_free(&outFrame);
                        continue;
                    }

                    // 鍒涘缓鎴栨洿鏂?sws 杞崲涓婁笅鏂?
                    m_swsCtx = sws_getCachedContext(
                        m_swsCtx,
                        frame->width, frame->height, (AVPixelFormat)frame->format,
                        outFrame->width, outFrame->height, AV_PIX_FMT_YUV420P,
                        SWS_BILINEAR, nullptr, nullptr, nullptr);

                    if (!m_swsCtx) {
                        APP_LOG_DEBUG_STREAM() << "Create format conversion context failed";
                        av_frame_free(&outFrame);
                        continue;
                    }

                    // 鎵ц鏍煎紡杞崲
                    sws_scale(m_swsCtx,
                              frame->data, frame->linesize,
                              0, frame->height,
                              outFrame->data, outFrame->linesize);

                    APP_LOG_DEBUG_STREAM() << "Video format conversion:"
                             << av_get_pix_fmt_name((AVPixelFormat)frame->format)
                             << "->" << av_get_pix_fmt_name(AV_PIX_FMT_YUV420P);
                } else {
                    // 闊抽 鎴?宸茬粡鏄?yuv420p 鐨勮棰戯細鐩存帴寮曠敤
                    if (av_frame_ref(outFrame, frame) < 0) {
                        APP_LOG_DEBUG_STREAM() << "Reference frame failed";
                        av_frame_free(&outFrame);
                        continue;
                    }

                    // 纭繚 time_base
                    if (outFrame->time_base.den <= 0) {
                        outFrame->time_base = m_time_base;
                    }
                }
            }

            // 楠岃瘉杈撳嚭甯?
            if (decoder_type == "瑙嗛" && (outFrame->width <= 0 || outFrame->height <= 0)) {
                APP_LOG_DEBUG_STREAM() << "Invalid video frame size, dropping";
                av_frame_free(&outFrame);
                continue;
            }

            if (ShouldDiscardFrame(outFrame)) {
                av_frame_free(&outFrame);
                av_frame_unref(frame);
                continue;
            }

            // ========== 鍏ラ槦 ==========
            if (!m_FrameVec->Push(outFrame)) {
                av_frame_free(&outFrame);
                av_frame_unref(frame);
                continue;
            }

            av_frame_unref(frame);  // 閲婃斁 codec 鐨勫紩鐢紝澶嶇敤 frame
        }
    }

    APP_LOG_DEBUG_STREAM() << decoder_type << "decoder thread finished";
    return 0;
}

int decoder::Flush() {
    if (!Ctx) {
        APP_LOG_WARN_STREAM() << decoder_type << "decoder flush failed: context is null";
        return -1;
    }

    {
        QMutexLocker locker(&mutex);
        avcodec_flush_buffers(Ctx);
    }

    APP_LOG_DEBUG_STREAM() << decoder_type << "decoder flushed";
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
    APP_LOG_DEBUG_STREAM() << decoder_type << "decoder thread started";
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
    APP_LOG_DEBUG_STREAM() << decoder_type << "decoder thread stopped";
    suspendCondition_.wakeAll();
    mutex.unlock();

    return 0;
}

int decoder::Pause(){
    mutex.lock();
    pause_flag = true;
    suspend_requested_ = true;
    APP_LOG_DEBUG_STREAM() << decoder_type << "decoder thread paused";
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

