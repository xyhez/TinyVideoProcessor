#pragma once

#include <QDebug>
#include <QString>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
}

enum class MediaSourceType {
    File,
    Camera,
    NetWork
};

struct Fileinfo {
    QString filepath_ = "NON";
    int width_ = 0;
    int height_ = 0;
    AVCodecID CodecId_ = AV_CODEC_ID_NONE;
    int duration_ = 0;          ///< 秒
    double FrameRate = 30.0;
    double A_timeBase = 0.0;
    double V_timeBase = 0.0;

    Fileinfo() = default;
    Fileinfo(QString filepath, int width, int height, AVCodecID CodecId, int duration)
        : filepath_(filepath)
        , width_(width)
        , height_(height)
        , CodecId_(CodecId)
        , duration_(duration)
    {
    }
};

class IMediaSource {
public:
    virtual ~IMediaSource() = default;
    virtual bool Open() = 0;
    virtual bool Close() = 0;
    virtual AVFormatContext* GetFormatContect() = 0;
    virtual MediaSourceType GetType() = 0;
    virtual void SetInfo(QString) = 0;
    virtual Fileinfo& GetSourceInfo() = 0;
};
