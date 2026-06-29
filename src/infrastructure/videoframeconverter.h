#ifndef VIDEOFRAMECONVERTER_H
#define VIDEOFRAMECONVERTER_H

#include "core/ffmpeg_raii.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

/**
 * @brief VideoFrameConverter 可显示帧格式转换器
 * @details 负责把软件内存视频帧转换为当前渲染器可绘制的 YUV420P 帧。
 *          不负责硬件帧 transfer，不参与取帧、同步判断或多路生命周期管理。
 */
class VideoFrameConverter
{
public:
    /**
     * @brief VideoFrameConverter 构造函数
     */
    VideoFrameConverter() = default;

    /**
     * @brief VideoFrameConverter 析构函数，释放 swscale 转换上下文
     */
    ~VideoFrameConverter();

    VideoFrameConverter(const VideoFrameConverter&) = delete;
    VideoFrameConverter& operator=(const VideoFrameConverter&) = delete;

    /**
     * @brief ConvertToDisplayFrame 转换为当前渲染器可显示帧
     * @details 输入 frame 的所有权会转移给转换器。若无需转换则原样返回；
     *          若转换成功则返回新帧；转换失败返回空指针。
     * @param frame 输入软件内存帧
     * @return AVFramePtr YUV420P 可显示帧，失败时为空
     */
    AVFramePtr ConvertToDisplayFrame(AVFramePtr frame);

private:
    /**
     * @brief IsDisplayFormat 判断是否已经是目标可显示格式
     * @param frame 待判断帧
     * @return true 已经是目标格式
     */
    bool IsDisplayFormat(const AVFrame* frame) const;

    /**
     * @brief HasDisplayColorProfile 判断是否已经符合当前渲染器显示色彩约束
     * @param frame 待判断帧
     * @return true 已经符合显示色彩约束
     */
    bool HasDisplayColorProfile(const AVFrame* frame) const;

    /**
     * @brief IsHardwareFrameFormat 判断是否为硬件像素格式
     * @param format 像素格式
     * @return true 硬件像素格式
     */
    bool IsHardwareFrameFormat(AVPixelFormat format) const;

    /**
     * @brief ConvertSoftwareFrame 转换软件内存帧到 YUV420P
     * @param frame 输入帧
     * @return AVFramePtr 转换后的帧，失败时为空
     */
    AVFramePtr ConvertSoftwareFrame(AVFramePtr frame);

    /**
     * @brief LogConversionIfNeeded 输出格式转换诊断日志
     * @param sourceFormat 输入像素格式
     * @param sourceRange 输入色彩范围
     * @param sourceColorSpace 输入色彩空间
     * @param width 输入宽度
     * @param height 输入高度
     * @param success 是否转换成功
     * @param usedFallback 是否使用默认色彩策略
     * @param usedBt2020Fallback 是否发生 BT.2020 降级
     */
    void LogConversionIfNeeded(AVPixelFormat sourceFormat,
                               AVColorRange sourceRange,
                               AVColorSpace sourceColorSpace,
                               int width,
                               int height,
                               bool success,
                               bool usedFallback,
                               bool usedBt2020Fallback);

    SwsContext* swsContext_ = nullptr;	///< swscale 转换上下文
    AVPixelFormat lastLoggedSourceFormat_ = AV_PIX_FMT_NONE;	///< 上次记录的输入格式
    AVColorRange lastLoggedSourceRange_ = AVCOL_RANGE_UNSPECIFIED;	///< 上次记录的输入色彩范围
    AVColorSpace lastLoggedSourceColorSpace_ = AVCOL_SPC_UNSPECIFIED;	///< 上次记录的输入色彩空间
    int lastLoggedWidth_ = 0;	///< 上次记录的宽度
    int lastLoggedHeight_ = 0;	///< 上次记录的高度
    bool lastLoggedSuccess_ = false;	///< 上次记录的转换结果
    bool lastLoggedUsedFallback_ = false;	///< 上次记录是否使用默认色彩策略
    bool lastLoggedUsedBt2020Fallback_ = false;	///< 上次记录是否使用 BT.2020 降级
    bool hasLoggedConversion_ = false;	///< 是否已经输出过转换日志
};

#endif // VIDEOFRAMECONVERTER_H
