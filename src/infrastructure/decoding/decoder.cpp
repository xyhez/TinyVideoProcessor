#include "decoder.h"
#include "tiny_log/Logging.h"

#include <QMutexLocker>
#include <QtGlobal>
#include <utility>

namespace {
constexpr bool kEnableHardwareDecoder = true;	///< 是否优先尝试硬件解码，失败后回退到软件解码
}
Decoder::Decoder(QObject *parent)
    : QObject{parent}
{

}

Decoder::~Decoder() {
    ResetDecoderResources();
    LOG_DEBUG_STREAM() << decoder_type << "decoder resources released";
}

void Decoder::ResetDecoderResources()
{
    if(Ctx) {
        avcodec_free_context(&Ctx);
    }
    if (m_hwDeviceCtx) {
        av_buffer_unref(&m_hwDeviceCtx);
    }
    codec = nullptr;
    par = nullptr;
    m_isHWDecoder = false;
    m_hwPixFmt = AV_PIX_FMT_NONE;
    m_swPixFmt = AV_PIX_FMT_NONE;
    m_lastLoggedTransferPixFmt = AV_PIX_FMT_NONE;
}

void Decoder::CreateConnect() {

}

bool Decoder::InitHWDecoder()
{
    if (!kEnableHardwareDecoder) {
        LOG_DEBUG_STREAM() << "Video hardware decoding disabled, using software decoder";
        return false;
    }
    if (par->codec_type != AVMEDIA_TYPE_VIDEO) {
        LOG_DEBUG_STREAM() << "Audio stream uses software decoder";
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
        LOG_DEBUG_STREAM() << "Video codec does not support hardware decoding:" << avcodec_get_name(par->codec_id);
        return false;
    }

    // 按平台优先级尝试硬件加速器；不可用的类型会被 av_hwdevice_find_type_by_name 忽略。
    const char *hwDevices[] = {
#if defined(Q_OS_WIN)
        "d3d11va",
        "dxva2",
#elif defined(Q_OS_LINUX)
        "vaapi",
        "vdpau",
#endif
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
                    LOG_DEBUG_STREAM() << "Create hardware device context failed:" << hwDevices[i] << "-" << errBuf;
                    continue;
                }


                AVBufferRef* codecDeviceContext = av_buffer_ref(m_hwDeviceCtx);
                if (!codecDeviceContext) {
                    LOG_DEBUG_STREAM() << "Reference hardware device context failed:" << hwDevices[i];
                    av_buffer_unref(&m_hwDeviceCtx);
                    continue;
                }

                Ctx->hw_device_ctx = codecDeviceContext;
                m_hwPixFmt = config->pix_fmt;

                m_swPixFmt = AV_PIX_FMT_NONE;


                m_isHWDecoder = true;

                return true;
            }
        }
    }

    m_isHWDecoder = false;
    if (m_hwDeviceCtx) {
        av_buffer_unref(&m_hwDeviceCtx);
    }
    m_hwPixFmt = AV_PIX_FMT_NONE;
    m_swPixFmt = AV_PIX_FMT_NONE;
    LOG_DEBUG_STREAM() << "Video uses software decoder";
    return false;
}

AVFramePtr Decoder::TransferHWFrame(AVFrame *hwFrame)
{
    if (!m_isHWDecoder || !hwFrame) {
        return {};
    }


    AVFramePtr softwareFrame = MakeAVFrame();
    if (!softwareFrame) {
        LOG_DEBUG_STREAM() << "Allocate hardware transfer frame failed";
        return {};
    }

    int ret = av_hwframe_transfer_data(softwareFrame.get(), hwFrame, 0);
    if (ret < 0) {
        char errBuf[256];
        av_strerror(ret, errBuf, sizeof(errBuf));
        LOG_DEBUG_STREAM() << "Hardware frame transfer failed:" << errBuf;
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
        LOG_DEBUG_STREAM() << "Hardware transfer frame format:"
                               << av_get_pix_fmt_name(transferFormat);
    }

    return softwareFrame;
}

int Decoder::InitDecoder(std::shared_ptr<SyncQueue<AVPacketPtr>> packetVec
                         ,std::shared_ptr<SyncQueue<AVFramePtr>> framevec
                         , AVCodecParameters *coderPar
                         , AVRational time_base) {
    ResetDecoderResources();
    decoder_type.clear();

    if (!coderPar) {
        LOG_WARN_STREAM() << "decoder init failed: codec parameters are null";
        return -1;
    }
    if (!packetVec) {
        LOG_WARN_STREAM() << "decoder init failed: packet queue is null";
        return -1;
    }
    if (!framevec) {
        LOG_WARN_STREAM() << "decoder init failed: frame queue is null";
        return -1;
    }

    m_PacketVec = packetVec;
    m_FrameVec = framevec;
    m_time_base = time_base;

    par = coderPar;

    codec = avcodec_find_decoder(par->codec_id);
    if (!codec) {
        LOG_WARN_STREAM() << "decoder init failed: decoder was not found for codec"
                          << avcodec_get_name(par->codec_id);
        ResetDecoderResources();
        return -1;
    }

    Ctx = avcodec_alloc_context3(codec);
    if (!Ctx) {
        LOG_WARN_STREAM() << "decoder init failed: allocate codec context failed";
        ResetDecoderResources();
        return -1;
    }

    int ret = avcodec_parameters_to_context(Ctx, par);
    if (ret < 0) {
        char errBuf[256];
        av_strerror(ret, errBuf, sizeof(errBuf));
        LOG_WARN_STREAM() << "decoder init failed: copy codec parameters failed:" << errBuf;
        ResetDecoderResources();
        return -1;
    }

    // 鍏堢‘瀹氳В鐮佸櫒绫诲瀷
    if (par->codec_type == AVMEDIA_TYPE_VIDEO) {
        decoder_type = "Video";
        // 瑙嗛灏濊瘯纭В鐮?
        InitHWDecoder();
    } else {
        decoder_type = "Audio";
        // 闊抽寮哄埗杞В
        m_isHWDecoder = false;
        m_hwDeviceCtx = nullptr;
        m_hwPixFmt = AV_PIX_FMT_NONE;
    }

    ret = avcodec_open2(Ctx, codec, nullptr);
    if (ret < 0) {
        char errBuf[256];
        av_strerror(ret, errBuf, sizeof(errBuf));
        LOG_WARN_STREAM() << "decoder init failed: avcodec_open2 failed:" << errBuf;
        ResetDecoderResources();
        return -1;
    }

    LOG_DEBUG_STREAM() << decoder_type << "decoder initialized"
             << (m_isHWDecoder ? "(hardware)" : "(software)");
    return 0;
}

int Decoder::StartDecoder() {
    if (!Ctx || !m_PacketVec || !m_FrameVec) {
        LOG_WARN_STREAM() << decoder_type << "decoder start failed: decoder is not initialized";
        emit DecoderErrorOccurred(QString("%1 decoder is not initialized").arg(decoder_type));
        return -1;
    }

    LOG_DEBUG_STREAM() << decoder_type << "decoder thread started"
             << (m_isHWDecoder ? "[hardware]" : "[software]");

    SetWorkerActive(true);

    AVFramePtr decodeFrame = MakeAVFrame();
    if (!decodeFrame) {
        LOG_DEBUG_STREAM() << "Allocate frame failed";
        SetWorkerActive(false);
        emit DecoderErrorOccurred(QString("%1 decoder frame allocation failed").arg(decoder_type));
        return -1;
    }

    bool exitRequested = false;
    while (!exitRequested) {
        if (!WaitIfSuspended()) {
            break;
        }

        AVPacketPtr packet;
        const SyncQueuePopResult popResult = m_PacketVec->PopWithStatus(packet);
        if (popResult == SyncQueuePopResult::Interrupted) {
            continue;
        }
        if (popResult == SyncQueuePopResult::Stopped) {
            break;
        }
        if (popResult == SyncQueuePopResult::EndOfStream) {
            const bool drained = DrainDecoder(decodeFrame.get());
            if (!drained) {
                if (m_PacketVec->IsStopRequested() || m_FrameVec->IsStopRequested()) {
                    break;
                }
                LOG_WARN_STREAM() << decoder_type << "decoder drain failed";
                SetWorkerActive(false);
                emit DecoderErrorOccurred(QString("%1 decoder drain failed").arg(decoder_type));
                return -1;
            }

            m_FrameVec->SignalEndOfStream();
            SetWorkerActive(false);
            emit DecoderFinished();
            LOG_DEBUG_STREAM() << decoder_type << "decoder reached end of stream";
            return 0;
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
            LOG_DEBUG_STREAM() << "Send packet failed:" << errBuf;
            continue;
        }

        const ReceiveFramesResult receiveResult = ReceiveAvailableFrames(decodeFrame.get());
        if (receiveResult == ReceiveFramesResult::Stopped) {
            exitRequested = true;
        } else if (receiveResult == ReceiveFramesResult::Failed) {
            LOG_WARN_STREAM() << decoder_type << "decoder receive failed";
            SetWorkerActive(false);
            emit DecoderErrorOccurred(QString("%1 decoder receive failed").arg(decoder_type));
            return -1;
        } else if (receiveResult == ReceiveFramesResult::EndOfStream) {
            m_FrameVec->SignalEndOfStream();
            SetWorkerActive(false);
            emit DecoderFinished();
            return 0;
        }
    }

    SetWorkerActive(false);
    LOG_DEBUG_STREAM() << decoder_type << "decoder thread finished";
    return 0;
}

void Decoder::SetWorkerActive(bool active)
{
    QMutexLocker locker(&mutex);
    worker_active_ = active;
    if (!active) {
        suspended_ = false;
        suspendCondition_.wakeAll();
    }
}

Decoder::ReceiveFramesResult Decoder::ReceiveAvailableFrames(AVFrame* decodeFrame)
{
    if (!decodeFrame) {
        return ReceiveFramesResult::Failed;
    }

    while (true) {
        if (!WaitIfSuspended()) {
            return ReceiveFramesResult::Stopped;
        }

        int ret = 0;
        {
            QMutexLocker locker(&mutex);
            ret = avcodec_receive_frame(Ctx, decodeFrame);
        }

        if (ret == AVERROR(EAGAIN)) {
            return ReceiveFramesResult::NeedMoreInput;
        }
        if (ret == AVERROR_EOF) {
            return ReceiveFramesResult::EndOfStream;
        }
        if (ret < 0) {
            char errBuf[256];
            av_strerror(ret, errBuf, sizeof(errBuf));
            LOG_DEBUG_STREAM() << "Receive decoded frame failed:" << errBuf;
            av_frame_unref(decodeFrame);
            return ReceiveFramesResult::Failed;
        }

        if (!ProcessDecodedFrame(decodeFrame)) {
            return ReceiveFramesResult::Stopped;
        }
    }
}

bool Decoder::ProcessDecodedFrame(AVFrame* decodeFrame)
{
    if (!decodeFrame) {
        return true;
    }

    if (decodeFrame->time_base.den <= 0) {
        decodeFrame->time_base = m_time_base;
    }

    AVFramePtr outFrame;
    if (decoder_type == "Video" && m_isHWDecoder &&
        decodeFrame->format == m_hwPixFmt) {
        outFrame = TransferHWFrame(decodeFrame);
        if (!outFrame) {
            LOG_DEBUG_STREAM() << "Hardware frame transfer failed, skipping frame";
        }
    } else {
        outFrame = MakeAVFrame();
        if (!outFrame) {
            LOG_DEBUG_STREAM() << "Allocate output frame failed";
        } else if (av_frame_ref(outFrame.get(), decodeFrame) < 0) {
            LOG_DEBUG_STREAM() << "Reference frame failed";
            outFrame.reset();
        } else if (outFrame->time_base.den <= 0) {
            outFrame->time_base = m_time_base;
        }
    }

    bool shouldSubmit = static_cast<bool>(outFrame);
    if (shouldSubmit && decoder_type == "Video" &&
        (outFrame->width <= 0 || outFrame->height <= 0)) {
        LOG_DEBUG_STREAM() << "Invalid video frame size, dropping";
        shouldSubmit = false;
    }
    if (shouldSubmit && ShouldDiscardFrame(outFrame.get())) {
        shouldSubmit = false;
    }

    av_frame_unref(decodeFrame);

    if (!shouldSubmit) {
        return true;
    }

    if (!m_FrameVec->Push(std::move(outFrame))) {
        return !m_FrameVec->IsStopRequested();
    }

    return true;
}

bool Decoder::DrainDecoder(AVFrame* decodeFrame)
{
    bool drainPacketAccepted = false;
    while (true) {
        if (!WaitIfSuspended()) {
            return false;
        }

        if (!drainPacketAccepted) {
            int sendRet = 0;
            {
                QMutexLocker locker(&mutex);
                sendRet = avcodec_send_packet(Ctx, nullptr);
            }

            if (sendRet == AVERROR_EOF) {
                return true;
            }
            if (sendRet == AVERROR(EAGAIN)) {
                const ReceiveFramesResult receiveResult = ReceiveAvailableFrames(decodeFrame);
                if (receiveResult == ReceiveFramesResult::Stopped ||
                    receiveResult == ReceiveFramesResult::Failed) {
                    return false;
                }
                if (receiveResult == ReceiveFramesResult::EndOfStream) {
                    return true;
                }
                continue;
            }
            if (sendRet < 0) {
                char errBuf[256];
                av_strerror(sendRet, errBuf, sizeof(errBuf));
                LOG_WARN_STREAM() << "Send decoder drain packet failed:" << errBuf;
                return false;
            }

            drainPacketAccepted = true;
        }

        const ReceiveFramesResult receiveResult = ReceiveAvailableFrames(decodeFrame);
        if (receiveResult == ReceiveFramesResult::EndOfStream) {
            return true;
        }
        if (receiveResult == ReceiveFramesResult::NeedMoreInput) {
            LOG_WARN_STREAM() << decoder_type
                              << "decoder requested more input after accepting drain packet";
        }
        return false;
    }
}

int Decoder::Flush() {
    if (!Ctx) {
        LOG_WARN_STREAM() << decoder_type << "decoder flush failed: context is null";
        return -1;
    }

    {
        QMutexLocker locker(&mutex);
        avcodec_flush_buffers(Ctx);
    }

    LOG_DEBUG_STREAM() << decoder_type << "decoder flushed";
    return 0;
}

int Decoder::SuspendAndWait() {
    {
        QMutexLocker locker(&mutex);
        if (stop_flag) {
            return -1;
        }
        pause_flag = true;
        suspend_requested_ = true;
        if (!worker_active_) {
            return 0;
        }
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
    while (worker_active_ && !suspended_ && !stop_flag) {
        suspendCondition_.wait(&mutex);
    }

    return stop_flag ? -1 : 0;
}

void Decoder::SetDiscardBefore(double seconds) {
    QMutexLocker locker(&mutex);
    m_discardBeforeEnabled = true;
    m_discardBeforeSeconds = seconds < 0.0 ? 0.0 : seconds;
}

void Decoder::ClearDiscardBefore() {
    QMutexLocker locker(&mutex);
    m_discardBeforeEnabled = false;
    m_discardBeforeSeconds = 0.0;
}

bool Decoder::ShouldDiscardFrame(const AVFrame *candidateFrame) {
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

int Decoder::Start() {
    mutex.lock();
    pause_flag = false;
    suspend_requested_ = false;
    LOG_DEBUG_STREAM() << decoder_type << "decoder thread started";
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

int Decoder::Stop() {
    {
        QMutexLocker locker(&mutex);
        stop_flag = true;
        suspend_requested_ = false;
        LOG_DEBUG_STREAM() << decoder_type << "decoder thread stopped";
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

int Decoder::Pause(){
    {
        QMutexLocker locker(&mutex);
        pause_flag = true;
        suspend_requested_ = true;
        LOG_DEBUG_STREAM() << decoder_type << "decoder thread paused";
        suspendCondition_.wakeAll();
    }

    if (m_PacketVec) {
        m_PacketVec->InterruptWait();
    }
    if (m_FrameVec) {
        m_FrameVec->InterruptWait();
    }

    return 0;
}
bool Decoder::WaitIfSuspended() {
    QMutexLocker locker(&mutex);

    while (suspend_requested_ && !stop_flag) {
        suspended_ = true;
        suspendCondition_.wakeAll();
        suspendCondition_.wait(&mutex);
    }

    suspended_ = false;
    return !stop_flag;
}

bool Decoder::IsSuspendRequested() const {
    QMutexLocker locker(&mutex);
    return suspend_requested_;
}

