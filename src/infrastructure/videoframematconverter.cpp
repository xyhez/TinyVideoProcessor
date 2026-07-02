#include "videoframematconverter.h"

#include <cstdint>
#include <cstring>
#include <utility>

extern "C" {
#include <libavutil/pixdesc.h>
}

namespace {
constexpr AVPixelFormat kMatPixelFormat = AV_PIX_FMT_BGR24;	///< OpenCV 处理链使用的中间 AVFrame 像素格式
constexpr int kMatType = CV_8UC3;	///< OpenCV 处理链使用的 BGR 三通道 Mat 类型
constexpr int kBgrChannels = 3;	///< BGR24 / CV_8UC3 每个像素的通道数量
}

VideoFrameMatConverter::~VideoFrameMatConverter()
{
    if (frameToMatSwsContext_) {
        sws_freeContext(frameToMatSwsContext_);
        frameToMatSwsContext_ = nullptr;
    }
}

bool VideoFrameMatConverter::ConvertFrameToMat(const AVFrame* frame, cv::Mat& image)
{
    image.release();

    if (!IsSupportedSourceFrame(frame)) {
        return false;
    }

    const AVPixelFormat sourceFormat = static_cast<AVPixelFormat>(frame->format);
    frameToMatSwsContext_ = sws_getCachedContext(frameToMatSwsContext_,
                                                 frame->width,
                                                 frame->height,
                                                 sourceFormat,
                                                 frame->width,
                                                 frame->height,
                                                 kMatPixelFormat,
                                                 SWS_BILINEAR,
                                                 nullptr,
                                                 nullptr,
                                                 nullptr);
    if (!frameToMatSwsContext_) {
        return false;
    }

    cv::Mat convertedImage(frame->height, frame->width, kMatType);
    uint8_t* destinationData[4] = { convertedImage.data, nullptr, nullptr, nullptr };
    int destinationLinesize[4] = { static_cast<int>(convertedImage.step), 0, 0, 0 };

    const int scaledHeight = sws_scale(frameToMatSwsContext_,
                                       frame->data,
                                       frame->linesize,
                                       0,
                                       frame->height,
                                       destinationData,
                                       destinationLinesize);
    if (scaledHeight <= 0) {
        return false;
    }

    image = std::move(convertedImage);
    return true;
}

bool VideoFrameMatConverter::ConvertFrameToMat(const AVFramePtr& frame, cv::Mat& image)
{
    return ConvertFrameToMat(frame.get(), image);
}

AVFramePtr VideoFrameMatConverter::ConvertMatToFrame(const cv::Mat& image,
                                                     const AVFrame* referenceFrame)
{
    if (image.empty() || image.type() != kMatType || image.cols <= 0 || image.rows <= 0) {
        return {};
    }

    AVFramePtr frame = MakeAVFrame();
    if (!frame) {
        return {};
    }

    frame->format = kMatPixelFormat;
    frame->width = image.cols;
    frame->height = image.rows;

    if (av_frame_get_buffer(frame.get(), 32) < 0) {
        return {};
    }

    for (int y = 0; y < image.rows; ++y) {
        std::memcpy(frame->data[0] + y * frame->linesize[0],
                    image.ptr(y),
                    static_cast<size_t>(image.cols * kBgrChannels));
    }

    if (referenceFrame) {
        av_frame_copy_props(frame.get(), referenceFrame);
    }

    frame->format = kMatPixelFormat;
    frame->width = image.cols;
    frame->height = image.rows;

    return frame;
}

AVFramePtr VideoFrameMatConverter::ConvertMatToFrame(const cv::Mat& image,
                                                     const AVFramePtr& referenceFrame)
{
    return ConvertMatToFrame(image, referenceFrame.get());
}

bool VideoFrameMatConverter::IsSupportedSourceFrame(const AVFrame* frame) const
{
    if (!frame || frame->width <= 0 || frame->height <= 0) {
        return false;
    }

    const AVPixelFormat sourceFormat = static_cast<AVPixelFormat>(frame->format);
    return sourceFormat != AV_PIX_FMT_NONE && !IsHardwareFrameFormat(sourceFormat);
}

bool VideoFrameMatConverter::IsHardwareFrameFormat(AVPixelFormat format) const
{
    const AVPixFmtDescriptor* descriptor = av_pix_fmt_desc_get(format);
    return descriptor && (descriptor->flags & AV_PIX_FMT_FLAG_HWACCEL);
}
