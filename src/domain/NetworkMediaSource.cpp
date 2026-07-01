#include "NetworkMediaSource.h"
#include "tiny_log/Logging.h"

extern "C" {
#include <libavutil/dict.h>
#include <libavutil/error.h>
}

namespace {
constexpr int kMicrosecondsPerMillisecond = 1000;

QString AvErrorString(int errorCode)
{
    char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(errorCode, errBuf, sizeof(errBuf));
    return QString::fromLocal8Bit(errBuf);
}

QString ProtocolName(NetworkStreamProtocol protocol)
{
    switch (protocol) {
    case NetworkStreamProtocol::Auto:
        return "auto";
    case NetworkStreamProtocol::Rtsp:
        return "rtsp";
    case NetworkStreamProtocol::Rtmp:
        return "rtmp";
    case NetworkStreamProtocol::HttpFlv:
        return "http-flv";
    case NetworkStreamProtocol::Mjpeg:
        return "mjpeg";
    }

    return "unknown";
}
}

NetworkMediaSource::NetworkMediaSource(QString url)
{
    options_.url = url;
    sourceInfo_.filepath_ = url;
}

NetworkMediaSource::NetworkMediaSource(const NetworkOpenOptions& options)
    : options_(options)
{
    sourceInfo_.filepath_ = options_.url;
}

NetworkMediaSource::~NetworkMediaSource()
{
    Close();
}

bool NetworkMediaSource::Open()
{
    lastError_.clear();
    if (formatContext_) {
        Close();
    }
    ClearInterrupt();

    options_.url = options_.url.trimmed();
    sourceInfo_.filepath_ = options_.url;

    if (options_.url.isEmpty()) {
        SetLastError("Network stream URL is empty");
        return false;
    }

    if (!IsProtocolSupported()) {
        SetLastError(QString("Network stream protocol is not supported: %1").arg(ProtocolName(options_.protocol)));
        return false;
    }

    AVDictionary* openOptions = nullptr;
    ConfigureOpenOptions(&openOptions);

    formatContext_ = avformat_alloc_context();
    if (!formatContext_) {
        av_dict_free(&openOptions);
        SetLastError("Failed to create network stream context");
        return false;
    }
    formatContext_->interrupt_callback.callback = &NetworkMediaSource::InterruptCallback;
    formatContext_->interrupt_callback.opaque = this;

    LOG_DEBUG("NetworkMediaSource::Open() - open url:", options_.url);
    const int openRet = avformat_open_input(&formatContext_,
                                            options_.url.toUtf8().constData(),
                                            nullptr,
                                            &openOptions);

    av_dict_free(&openOptions);

    if (openRet < 0) {
        SetLastError(QString("Open network stream failed: %1").arg(AvErrorString(openRet)));
        if (formatContext_) {
            avformat_close_input(&formatContext_);
            formatContext_ = nullptr;
        }
        return false;
    }

    const int streamInfoRet = avformat_find_stream_info(formatContext_, nullptr);
    if (streamInfoRet < 0) {
        SetLastError(QString("Failed to read network stream information: %1").arg(AvErrorString(streamInfoRet)));
        avformat_close_input(&formatContext_);
        formatContext_ = nullptr;
        return false;
    }

    sourceInfo_.filepath_ = options_.url;
    sourceInfo_.hasVideo_ = false;
    sourceInfo_.hasAudio_ = false;
    sourceInfo_.duration_ = 0;

    for (unsigned int i = 0; i < formatContext_->nb_streams; ++i) {
        AVStream* stream = formatContext_->streams[i];
        AVCodecParameters* codecParameters = stream->codecpar;
        if (codecParameters->codec_type == AVMEDIA_TYPE_VIDEO && !sourceInfo_.hasVideo_) {
            sourceInfo_.hasVideo_ = true;
            sourceInfo_.videoStreamIndex_ = static_cast<int>(i);
            sourceInfo_.width_ = codecParameters->width;
            sourceInfo_.height_ = codecParameters->height;
            sourceInfo_.CodecId_ = codecParameters->codec_id;
            sourceInfo_.videoCodecId_ = codecParameters->codec_id;
            const double frameRate = av_q2d(stream->avg_frame_rate);
            if (frameRate > 0.0) {
                sourceInfo_.FrameRate = frameRate;
            }
            sourceInfo_.V_timeBase = av_q2d(stream->time_base);
        } else if (codecParameters->codec_type == AVMEDIA_TYPE_AUDIO && !sourceInfo_.hasAudio_) {
            sourceInfo_.hasAudio_ = true;
            sourceInfo_.audioStreamIndex_ = static_cast<int>(i);
            sourceInfo_.audioCodecId_ = codecParameters->codec_id;
            sourceInfo_.A_timeBase = av_q2d(stream->time_base);
        }
    }

    if (!sourceInfo_.hasVideo_) {
        SetLastError("Network stream did not provide a usable video stream");
        avformat_close_input(&formatContext_);
        formatContext_ = nullptr;
        return false;
    }

    LOG_DEBUG("NetworkMediaSource::Open() - network stream opened");
    return true;
}

bool NetworkMediaSource::Close()
{
    RequestInterrupt();
    if (formatContext_) {
        LOG_DEBUG("NetworkMediaSource::Close() - close network stream");
        avformat_close_input(&formatContext_);
        formatContext_ = nullptr;
        return true;
    }

    return false;
}

AVFormatContext* NetworkMediaSource::GetFormatContect()
{
    return formatContext_;
}

MediaSourceType NetworkMediaSource::GetType()
{
    return MediaSourceType::Network;
}

void NetworkMediaSource::SetInfo(QString sourceInfo)
{
    options_.url = sourceInfo;
    sourceInfo_.filepath_ = sourceInfo;
}

Fileinfo& NetworkMediaSource::GetSourceInfo()
{
    return sourceInfo_;
}

void NetworkMediaSource::SetOpenOptions(const NetworkOpenOptions& options)
{
    options_ = options;
    sourceInfo_.filepath_ = options_.url;
}

QString NetworkMediaSource::LastError() const
{
    return lastError_;
}

QString NetworkMediaSource::SourceIdentity() const
{
    return QString("network:%1").arg(options_.url.trimmed());
}

bool NetworkMediaSource::AllowsDuplicateOpen() const
{
    return false;
}

void NetworkMediaSource::RequestInterrupt()
{
    interruptRequested_.store(true);
}

void NetworkMediaSource::ClearInterrupt()
{
    interruptRequested_.store(false);
}

bool NetworkMediaSource::IsSeekable() const
{
    return false;
}

bool NetworkMediaSource::HasDuration() const
{
    return false;
}

bool NetworkMediaSource::IsRealtime() const
{
    return true;
}

void NetworkMediaSource::SetLastError(const QString& message)
{
    lastError_ = message;
    LOG_WARN("NetworkMediaSource:", lastError_);
}

bool NetworkMediaSource::IsProtocolSupported() const
{
    if (options_.protocol == NetworkStreamProtocol::Rtsp) {
        return true;
    }

    if (options_.protocol == NetworkStreamProtocol::Auto) {
        return options_.url.startsWith("rtsp://", Qt::CaseInsensitive);
    }

    return false;
}

void NetworkMediaSource::ConfigureOpenOptions(AVDictionary** dictionary) const
{
    if (!dictionary) {
        return;
    }

    if (options_.protocol == NetworkStreamProtocol::Rtsp ||
        options_.url.startsWith("rtsp://", Qt::CaseInsensitive)) {
        av_dict_set(dictionary, "rtsp_transport", options_.rtspTransport.toUtf8().constData(), 0);
    }

    const QString openTimeoutUs = QString::number(options_.openTimeoutMs * kMicrosecondsPerMillisecond);
    const QString readTimeoutUs = QString::number(options_.readTimeoutMs * kMicrosecondsPerMillisecond);
    av_dict_set(dictionary, "stimeout", openTimeoutUs.toUtf8().constData(), 0);
    av_dict_set(dictionary, "timeout", openTimeoutUs.toUtf8().constData(), 0);
    av_dict_set(dictionary, "rw_timeout", readTimeoutUs.toUtf8().constData(), 0);
    av_dict_set(dictionary, "buffer_size", QString::number(options_.bufferSizeBytes).toUtf8().constData(), 0);
    av_dict_set(dictionary, "max_delay", QString::number(options_.maxDelayUs).toUtf8().constData(), 0);

    if (options_.lowLatency) {
        av_dict_set(dictionary, "fflags", "nobuffer", 0);
        av_dict_set(dictionary, "flags", "low_delay", 0);
    }
}

int NetworkMediaSource::InterruptCallback(void* opaque)
{
    auto* source = static_cast<NetworkMediaSource*>(opaque);
    if (!source) {
        return 0;
    }

    return source->interruptRequested_.load() ? 1 : 0;
}
