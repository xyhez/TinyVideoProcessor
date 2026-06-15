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

    /**
     * @brief Fileinfo 默认构造函数
     */
    Fileinfo() = default;

    /**
     * @brief Fileinfo 构造媒体基础信息
     * @param filepath 文件路径
     * @param width 视频宽度
     * @param height 视频高度
     * @param CodecId 编码器 ID
     * @param duration 媒体总时长，单位秒
     */
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
    /**
     * @brief IMediaSource 虚析构函数
     */
    virtual ~IMediaSource() = default;

    /**
     * @brief Open 打开媒体源
     * @return true 打开成功
     * @return false 打开失败
     */
    virtual bool Open() = 0;

    /**
     * @brief Close 关闭媒体源并释放资源
     * @return true 关闭成功
     * @return false 关闭失败
     */
    virtual bool Close() = 0;

    /**
     * @brief GetFormatContect 获取 FFmpeg 格式上下文
     * @return AVFormatContext 指针
     */
    virtual AVFormatContext* GetFormatContect() = 0;

    /**
     * @brief GetType 获取媒体源类型
     * @return 媒体源类型
     */
    virtual MediaSourceType GetType() = 0;

    /**
     * @brief SetInfo 设置媒体源路径或描述信息
     * @param sourceInfo 媒体源信息
     */
    virtual void SetInfo(QString sourceInfo) = 0;

    /**
     * @brief GetSourceInfo 获取媒体源基础信息
     * @return 媒体源信息引用
     */
    virtual Fileinfo& GetSourceInfo() = 0;
};
