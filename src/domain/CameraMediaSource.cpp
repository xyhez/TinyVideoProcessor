#include "CameraMediaSource.h"

extern "C" {
#include <libavdevice/avdevice.h>
}

CameraMediaSource::CameraMediaSource(QString deviceName)
    : deviceName_(deviceName)
{
    sourceInfo_.filepath_ = deviceName; // 对摄像头，filepath_ 存储设备名
}

CameraMediaSource::CameraMediaSource(const CameraOpenOptions& options)
    : deviceName_(options.deviceName)
    , width_(options.width)
    , height_(options.height)
    , frameRate_(options.frameRate)
    , rtbufsize_(options.rtbufsize)
    , pixelFormat_(options.pixelFormat)
    , codec_(options.codec)
{
    sourceInfo_.filepath_ = options.deviceName;
}

CameraMediaSource::~CameraMediaSource()
{
    if (formatContext_) {
        avformat_close_input(&formatContext_);
        formatContext_ = nullptr;
    }
}

bool CameraMediaSource::Open()
{
    lastError_.clear();
    if (formatContext_) {
        Close();
    }

    if (deviceName_.trimmed().isEmpty()) {
        SetLastError("摄像头设备名为空");
        return false;
    }

    // 注册 FFmpeg 设备（libavdevice）
    static bool devicesRegistered = false;
    if (!devicesRegistered) {
        avdevice_register_all();
        devicesRegistered = true;
    }

    // 查找 dshow input format
    const AVInputFormat* inputFormat = av_find_input_format("dshow");
    if (!inputFormat) {
        SetLastError("无法找到 FFmpeg dshow input format");
        return false;
    }

    // 构造 dshow 设备 URL: "video=设备名"
    QString deviceUrl = QString("video=%1").arg(deviceName_);
    qDebug() << "CameraMediaSource::Open() - 打开摄像头:" << deviceUrl;

    // 设置摄像头参数
    AVDictionary* options = nullptr;
    av_dict_set(&options, "video_size", QString("%1x%2").arg(width_).arg(height_).toUtf8().constData(), 0);
    av_dict_set(&options, "framerate", QString::number(frameRate_).toUtf8().constData(), 0);

    // 设置实时缓冲区大小
    QString rtbufsizeStr = QString("%1M").arg(rtbufsize_);
    av_dict_set(&options, "rtbufsize", rtbufsizeStr.toUtf8().constData(), 0);

    // 如果指定了像素格式，设置 pixel_format
    if (!pixelFormat_.isEmpty()) {
        av_dict_set(&options, "pixel_format", pixelFormat_.toUtf8().constData(), 0);
    }

    // 如果指定了编码格式，设置 vcodec
    if (!codec_.isEmpty()) {
        av_dict_set(&options, "vcodec", codec_.toUtf8().constData(), 0);
    }

    // 打开摄像头设备
    int ret = avformat_open_input(&formatContext_,
                                   deviceUrl.toUtf8().constData(),
                                   inputFormat,
                                   &options);

    av_dict_free(&options);

    if (ret != 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        SetLastError(QString("打开摄像头失败：%1。设备可能不存在、被占用，或所选参数不受支持。")
                         .arg(QString::fromLocal8Bit(errbuf)));
        if (formatContext_) {
            avformat_close_input(&formatContext_);
        }
        return false;
    }

    // 获取流信息
    if (avformat_find_stream_info(formatContext_, nullptr) < 0) {
        SetLastError("无法获取摄像头流信息");
        avformat_close_input(&formatContext_);
        return false;
    }

    // 填充 sourceInfo_（摄像头特性）
    sourceInfo_.filepath_ = deviceName_;
    sourceInfo_.hasVideo_ = false;
    sourceInfo_.hasAudio_ = false; // 第一版不接音频
    sourceInfo_.duration_ = 0;     // 实时源无 duration

    // 查找视频流
    for (unsigned int i = 0; i < formatContext_->nb_streams; i++) {
        AVStream* stream = formatContext_->streams[i];
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            sourceInfo_.hasVideo_ = true;
            sourceInfo_.videoStreamIndex_ = i;
            sourceInfo_.width_ = stream->codecpar->width;
            sourceInfo_.height_ = stream->codecpar->height;
            sourceInfo_.videoCodecId_ = stream->codecpar->codec_id;

            // 计算帧率
            if (stream->avg_frame_rate.den > 0) {
                sourceInfo_.FrameRate = av_q2d(stream->avg_frame_rate);
            } else {
                sourceInfo_.FrameRate = frameRate_; // 使用设置的帧率
            }

            // 计算 time_base
            sourceInfo_.V_timeBase = av_q2d(stream->time_base);

            qDebug() << "CameraMediaSource::Open() - 视频流信息:";
            qDebug() << "  分辨率:" << sourceInfo_.width_ << "x" << sourceInfo_.height_;
            qDebug() << "  帧率:" << sourceInfo_.FrameRate;
            qDebug() << "  编码:" << avcodec_get_name(sourceInfo_.videoCodecId_);

            break;
        }
    }

    if (!sourceInfo_.hasVideo_) {
        SetLastError("摄像头未找到可用视频流");
        avformat_close_input(&formatContext_);
        return false;
    }

    qDebug() << "CameraMediaSource::Open() - 摄像头打开成功";
    return true;
}

bool CameraMediaSource::Close()
{
    if (formatContext_) {
        qDebug() << "CameraMediaSource::Close() - 关闭摄像头";
        avformat_close_input(&formatContext_);
        formatContext_ = nullptr;
        return true;
    }
    return false;
}

AVFormatContext* CameraMediaSource::GetFormatContect()
{
    return formatContext_;
}

MediaSourceType CameraMediaSource::GetType()
{
    return MediaSourceType::Camera;
}

Fileinfo& CameraMediaSource::GetSourceInfo()
{
    return sourceInfo_;
}

void CameraMediaSource::SetInfo(QString deviceName)
{
    deviceName_ = deviceName;
    sourceInfo_.filepath_ = deviceName;
}

void CameraMediaSource::SetResolution(int width, int height)
{
    width_ = width;
    height_ = height;
}

void CameraMediaSource::SetFrameRate(int frameRate)
{
    frameRate_ = frameRate;
}

void CameraMediaSource::SetOpenOptions(const CameraOpenOptions& options)
{
    deviceName_ = options.deviceName;
    width_ = options.width;
    height_ = options.height;
    frameRate_ = options.frameRate;
    rtbufsize_ = options.rtbufsize;
    pixelFormat_ = options.pixelFormat;
    codec_ = options.codec;
    sourceInfo_.filepath_ = options.deviceName;
}

QString CameraMediaSource::LastError() const
{
    return lastError_;
}

QString CameraMediaSource::SourceIdentity() const
{
    return QString("camera:%1").arg(deviceName_.trimmed());
}

bool CameraMediaSource::AllowsDuplicateOpen() const
{
    return false;
}

bool CameraMediaSource::IsSeekable() const {
    return false; // 摄像头不支持 seek
}

bool CameraMediaSource::HasDuration() const {
    return false; // 摄像头没有确定的时长
}

bool CameraMediaSource::IsRealtime() const {
    return true; // 摄像头是实时源
}

void CameraMediaSource::SetLastError(const QString& message)
{
    lastError_ = message;
    qWarning() << "CameraMediaSource:" << lastError_;
}
