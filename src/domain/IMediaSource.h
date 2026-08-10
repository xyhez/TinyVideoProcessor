#pragma once

#include <QString>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
}

// 媒体源类型
enum class MediaSourceType {
    File,   ///< 文件
    Camera, ///< 相机
    Network ///< 网络
};

struct Fileinfo {
    QString filepath_ = "NON";	///< 媒体文件路径
    bool hasVideo_ = false;	///< 是否包含视频流
    bool hasAudio_ = false;	///< 是否包含音频流
    int videoStreamIndex_ = -1;	///< 视频流索引
    int audioStreamIndex_ = -1;	///< 音频流索引
    int width_ = 0;	///< 视频宽度
    int height_ = 0;	///< 视频高度
    AVCodecID CodecId_ = AV_CODEC_ID_NONE;	///< 兼容保留的编码器 ID
    AVCodecID videoCodecId_ = AV_CODEC_ID_NONE;	///< 视频编码器 ID
    AVCodecID audioCodecId_ = AV_CODEC_ID_NONE;	///< 音频编码器 ID
    int duration_ = 0;	///< 媒体时长，单位秒
    double FrameRate = 30.0;	///< 视频帧率
    double A_timeBase = 0.0;	///< 音频时间基
    double V_timeBase = 0.0;	///< 视频时间基

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
        , hasVideo_(true)
        , width_(width)
        , height_(height)
        , CodecId_(CodecId)
        , videoCodecId_(CodecId)
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

    /**
     * @brief LastError 获取最近一次媒体源操作失败原因
     * @return 错误信息；无错误时返回空字符串
     */
    virtual QString LastError() const
    {
        return QString();
    }

    /**
     * @brief SourceIdentity 获取媒体源稳定身份
     * @return 稳定身份；无法提供时返回空字符串
     *
     * 用于判断同一底层资源是否已被占用，例如同一个摄像头设备。
     */
    virtual QString SourceIdentity() const
    {
        return QString();
    }

    /**
     * @brief AllowsDuplicateOpen 判断媒体源是否允许重复打开同一身份
     * @return true 允许重复打开，false 不允许
     */
    virtual bool AllowsDuplicateOpen() const
    {
        return true;
    }

    /**
     * @brief RequestInterrupt 请求中断媒体源当前阻塞操作
     *
     * 默认空实现。网络流可以用它打断 FFmpeg 阻塞读，文件源和普通设备源可不处理。
     */
    virtual void RequestInterrupt()
    {
    }

    /**
     * @brief ClearInterrupt 清除媒体源中断请求
     *
     * 默认空实现。用于重新打开或恢复实时源读取前清理中断状态。
     */
    virtual void ClearInterrupt()
    {
    }

    /**
     * @brief IsSeekable 媒体源是否支持 seek
     * @return true 支持 seek，false 不支持
     *
     * 文件源通常返回 true，实时源（摄像头、网络流）通常返回 false
     */
    virtual bool IsSeekable() const = 0;

    /**
     * @brief HasDuration 媒体源是否有确定的时长
     * @return true 有时长，false 无时长
     *
     * 文件源通常返回 true，实时源（摄像头、直播流）通常返回 false
     */
    virtual bool HasDuration() const = 0;

    /**
     * @brief IsRealtime 媒体源是否为实时源
     * @return true 实时源，false 非实时源
     *
     * 摄像头、直播流等返回 true，文件源返回 false
     */
    virtual bool IsRealtime() const = 0;
};
