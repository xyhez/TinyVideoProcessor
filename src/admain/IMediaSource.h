#pragma once
#include <QString>
#include <QDebug>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
}

///< 文件类型枚举
enum class MediaSourceType {
    File,
    Camera,
    NetWork
};

///< 视频信息struct
struct Fileinfo{
    QString filepath_ = "NON";
    int width_;
    int height_;
    AVCodecID CodecId_;
    int duration_;   // 视频时长
    double FrameRate = 30;              ///< 刷新率(fps)
    double A_timeBase;                  ///< 音频时间基
    double V_timeBase;                  ///< 视频时间基

    Fileinfo() = default;
    Fileinfo(QString filepath,int width,int height,AVCodecID CodecId,int duration)
        : filepath_(filepath)
        , width_(width)
        , height_(height)
        , CodecId_(CodecId)
        , duration_(duration){}
};

class IMediaSource {
public:
    virtual ~IMediaSource() = default;
    virtual bool open() = 0;
    virtual bool close() = 0;
    virtual AVFormatContext* GetFormatContect() = 0;
    virtual MediaSourceType GetType() = 0;
    virtual void SetInfo(QString) = 0;
    virtual Fileinfo& GetSourceInfo() = 0;
};
