#include "grayscaleprocessor.h"

#include <cstdint>
#include <cstring>

#include <opencv2/imgproc.hpp>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

namespace {
constexpr int kRequiredMatType = CV_8UC3;	///< 处理器要求的输入输出图像类型
constexpr uint8_t kNeutralChroma = 128;	///< 灰度化时使用的中性色度值
}

QString GrayscaleProcessor::Name() const
{
    return QStringLiteral("Grayscale");
}

bool GrayscaleProcessor::Enabled() const
{
    return enabled_.load();
}

void GrayscaleProcessor::SetEnabled(bool enabled)
{
    enabled_.store(enabled);
}

bool GrayscaleProcessor::Process(cv::Mat& image)
{
    if (image.empty() || image.type() != kRequiredMatType) {
        return false;
    }

    cv::Mat grayImage;
    cv::cvtColor(image, grayImage, cv::COLOR_BGR2GRAY);
    cv::cvtColor(grayImage, image, cv::COLOR_GRAY2BGR);
    return true;
}

bool GrayscaleProcessor::SupportsYuvProcessing() const
{
    return true;
}

bool GrayscaleProcessor::Process(AVFrame* frame)
{
    if (!frame || frame->format != AV_PIX_FMT_YUV420P
        || !frame->data[0] || !frame->data[1] || !frame->data[2]) {
        return false;
    }

    // Y 平面保持不变，把 U/V 置为中性色度即可显示为灰度。
    const int chromaHeight = (frame->height + 1) / 2;
    std::memset(frame->data[1], kNeutralChroma,
                static_cast<size_t>(frame->linesize[1]) * chromaHeight);
    std::memset(frame->data[2], kNeutralChroma,
                static_cast<size_t>(frame->linesize[2]) * chromaHeight);
    return true;
}
