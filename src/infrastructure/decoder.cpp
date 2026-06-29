#include "decoder.h"
#include "core/AppLog.h"

#include <QMutexLocker>
#include <utility>

namespace {
constexpr bool kEnableHardwareDecoder = true;
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

                m_swPixFmt = AV_PIX_FMT_NONE;


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

AVFramePtr decoder::TransferHWFrame(AVFrame *hwFrame)
{
    if (!m_isHWDecoder || !hwFrame) {
        return {};
    }


    AVFramePtr softwareFrame = MakeAVFrame();
    if (!softwareFrame) {
        APP_LOG_DEBUG_STREAM() << "Allocate hardware transfer frame failed";
        return {};
    }

    int ret = av_hwframe_transfer_data(softwareFrame.get(), hwFrame, 0);
    if (ret < 0) {
        char errBuf[256];
        av_strerror(ret, errBuf, sizeof(errBuf));
        APP_LOG_DEBUG_STREAM() << "Hardware frame transfer failed:" << errBuf;
        return {};
    }

    // 澶嶅埗鎵€鏈夊睘鎬э紙鍖呮嫭pts, time_base绛夛級
    av_frame_copy_props(softwareFrame.get(), hwFrame);

    // 纭繚time_base涔熻璁剧疆锛堟湁浜涚‖浠惰В鐮佸櫒鍙兘涓嶈缃繖涓級
    if (hwFrame->time_base.den > 0) {
        softwareFrame->time_base = hwFrame->time_base;
    } else if (m_time_base.den > 0) {
        softwareFrame->time_base = m_time_base;
    }

    const AVPixelFormat transferFormat = static_cast<AVPixelFormat>(softwareFrame->format);
    if (m_lastLoggedTransferPixFmt != transferFormat) {
        m_lastLoggedTransferPixFmt = transferFormat;
        APP_LOG_DEBUG_STREAM() << "Hardware transfer frame format:"
                               << av_get_pix_fmt_name(transferFormat);
    }

    return softwareFrame;
}

int decoder::InitDecoder(std::shared_ptr<SyncQueue<AVPacketPtr>> packetVec
                         ,std::shared_ptr<SyncQueue<AVFramePtr>> framevec
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
    m_lastLoggedTransferPixFmt = AV_PIX_FMT_NONE;

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

    bool exitRequested = false;
    while (!exitRequested) {
        // ========== 鍙栧寘閮ㄥ垎 ==========
        if (!WaitIfSuspended()) {
            break;
        }

        AVPacketPtr packet;
        if (!m_PacketVec->Pop(packet)) {
            if (m_PacketVec->IsStopRequested()) {
                break;
            }
            continue;
        }

        if (!packet) continue;

        if (IsSuspendRequested()) {
            continue;
        }

        int ret = 0;
        {
            QMutexLocker locker(&mutex);
            ret = avcodec_send_packet(Ctx, packet.get());
        }

        if (ret < 0) {
            char errBuf[256];
            av_strerror(ret, errBuf, sizeof(errBuf));
            APP_LOG_DEBUG_STREAM() << "Send packet failed:" << errBuf;
            continue;
        }

        // ========== 鎺ユ敹甯ч儴鍒?==========
        while (true) {
            if (!WaitIfSuspended()) {
                exitRequested = true;
                break;
            }

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
            AVFramePtr outFrame;

            if (decoder_type == "瑙嗛" && m_isHWDecoder &&
                frame->format == m_hwPixFmt) {
                // 瑙嗛纭В甯э細闇€瑕佽浆鎹?
                outFrame = TransferHWFrame(frame);
                if (!outFrame) {
                    APP_LOG_DEBUG_STREAM() << "Hardware frame transfer failed, skipping frame";
                    continue;
                }
            } else {
                // 视频软解或音频：只引用解码输出帧，格式适配交给后续渲染边界
                outFrame = MakeAVFrame();
                if (!outFrame) {
                    APP_LOG_DEBUG_STREAM() << "Allocate output frame failed";
                    continue;
                }

                if (av_frame_ref(outFrame.get(), frame) < 0) {
                    APP_LOG_DEBUG_STREAM() << "Reference frame failed";
                    continue;
                }

                if (outFrame->time_base.den <= 0) {
                    outFrame->time_base = m_time_base;
                }
            }

            // 楠岃瘉杈撳嚭甯?
            if (decoder_type == "瑙嗛" && (outFrame->width <= 0 || outFrame->height <= 0)) {
                APP_LOG_DEBUG_STREAM() << "Invalid video frame size, dropping";
                continue;
            }

            if (ShouldDiscardFrame(outFrame.get())) {
                av_frame_unref(frame);
                continue;
            }

            // ========== 鍏ラ槦 ==========
            if (!m_FrameVec->Push(std::move(outFrame))) {
                av_frame_unref(frame);
                if (m_FrameVec->IsStopRequested()) {
                    exitRequested = true;
                    break;
                }
                if (IsSuspendRequested()) {
                    break;
                }
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
    {
        QMutexLocker locker(&mutex);
        stop_flag = true;
        suspend_requested_ = false;
        APP_LOG_DEBUG_STREAM() << decoder_type << "decoder thread stopped";
        suspendCondition_.wakeAll();
    }

    if (m_PacketVec) {
        m_PacketVec->RequestStop();
    }
    if (m_FrameVec) {
        m_FrameVec->RequestStop();
    }

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

