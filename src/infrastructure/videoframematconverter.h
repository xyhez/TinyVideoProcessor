#ifndef VIDEOFRAMEMATCONVERTER_H
#define VIDEOFRAMEMATCONVERTER_H

#include "core/ffmpeg_raii.h"

#include <opencv2/core/mat.hpp>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

/**
 * @brief VideoFrameMatConverter 视频帧与 OpenCV Mat 转换器
 * @details 负责在 AVFrame 和 cv::Mat 之间转换，不访问队列、同步器、
 *          播放管线、UI 或 OpenGL 资源。
 */
class VideoFrameMatConverter
{
public:
    /**
     * @brief VideoFrameMatConverter 构造函数
     */
    VideoFrameMatConverter() = default;

    /**
     * @brief VideoFrameMatConverter 析构函数，释放 swscale 转换上下文
     */
    ~VideoFrameMatConverter();

    VideoFrameMatConverter(const VideoFrameMatConverter&) = delete;
    VideoFrameMatConverter& operator=(const VideoFrameMatConverter&) = delete;

    /**
     * @brief ConvertFrameToMat 将视频帧转换为 OpenCV 图像
     * @details 输出 image 持有独立数据，调用方可以安全处理；转换器不会保存 frame 指针。
     * @param frame 输入视频帧
     * @param image 输出 BGR 图像
     * @return true 转换成功
     */
    bool ConvertFrameToMat(const AVFrame* frame, cv::Mat& image);

    /**
     * @brief ConvertFrameToMat 将 RAII 视频帧转换为 OpenCV 图像
     * @param frame 输入视频帧
     * @param image 输出 BGR 图像
     * @return true 转换成功
     */
    bool ConvertFrameToMat(const AVFramePtr& frame, cv::Mat& image);

    /**
     * @brief ConvertMatToFrame 将 OpenCV 图像转换为视频帧
     * @details 第一版只接受 CV_8UC3 BGR 图像，输出 AV_PIX_FMT_BGR24 帧；
     *          referenceFrame 只用于复制时间戳和色彩等元数据。
     * @param image 输入 BGR 图像
     * @param referenceFrame 参考视频帧，可为空
     * @return AVFramePtr 转换后的视频帧，失败时为空
     */
    AVFramePtr ConvertMatToFrame(const cv::Mat& image, const AVFrame* referenceFrame);

    /**
     * @brief ConvertMatToFrame 将 OpenCV 图像转换为视频帧
     * @param image 输入 BGR 图像
     * @param referenceFrame 参考视频帧，可为空
     * @return AVFramePtr 转换后的视频帧，失败时为空
     */
    AVFramePtr ConvertMatToFrame(const cv::Mat& image, const AVFramePtr& referenceFrame);

private:
    /**
     * @brief IsSupportedSourceFrame 判断输入帧是否可转换
     * @param frame 输入视频帧
     * @return true 可转换
     */
    bool IsSupportedSourceFrame(const AVFrame* frame) const;

    /**
     * @brief IsHardwareFrameFormat 判断是否为硬件像素格式
     * @param format 像素格式
     * @return true 硬件像素格式
     */
    bool IsHardwareFrameFormat(AVPixelFormat format) const;

    SwsContext* frameToMatSwsContext_ = nullptr;	///< AVFrame 到 cv::Mat 的 swscale 上下文
};

#endif // VIDEOFRAMEMATCONVERTER_H
