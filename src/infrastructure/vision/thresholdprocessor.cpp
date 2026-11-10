#include "thresholdprocessor.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

#include <opencv2/imgproc.hpp>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

namespace {
constexpr int kRequiredMatType = CV_8UC3;	///< 处理器要求的输入输出图像类型
constexpr int kMinThreshold = 0;	///< 二值化阈值下限
constexpr int kMaxThreshold = 255;	///< 二值化阈值上限
constexpr double kBinaryMaxValue = 255.0;	///< 二值化输出的白色像素值
constexpr uint8_t kNeutralChroma = 128;	///< 二值化时使用的中性色度值
}

ThresholdProcessor::ThresholdProcessor(int threshold)
{
    SetThreshold(threshold);
}

QString ThresholdProcessor::Name() const
{
    return QStringLiteral("Threshold");
}

bool ThresholdProcessor::Enabled() const
{
    return enabled_.load();
}

void ThresholdProcessor::SetEnabled(bool enabled)
{
    enabled_.store(enabled);
}

bool ThresholdProcessor::Process(cv::Mat& image)
{
    if (image.empty() || image.type() != kRequiredMatType) {
        return false;
    }

    cv::Mat grayImage;
    cv::Mat binaryImage;
    cv::cvtColor(image, grayImage, cv::COLOR_BGR2GRAY);
    cv::threshold(grayImage, binaryImage, threshold_.load(), kBinaryMaxValue, cv::THRESH_BINARY);
    cv::cvtColor(binaryImage, image, cv::COLOR_GRAY2BGR);
    return true;
}

bool ThresholdProcessor::SupportsYuvProcessing() const
{
    return true;
}

bool ThresholdProcessor::Process(AVFrame* frame)
{
    if (!frame || frame->format != AV_PIX_FMT_YUV420P
        || !frame->data[0] || !frame->data[1] || !frame->data[2]) {
        return false;
    }

    cv::Mat yPlane(frame->height, frame->width, CV_8UC1,
                   frame->data[0], static_cast<size_t>(frame->linesize[0]));
    if (yPlane.empty()) {
        return false;
    }

    cv::threshold(yPlane, yPlane, threshold_.load(), kBinaryMaxValue, cv::THRESH_BINARY);

    const int chromaHeight = (frame->height + 1) / 2;
    std::memset(frame->data[1], kNeutralChroma,
                static_cast<size_t>(frame->linesize[1]) * chromaHeight);
    std::memset(frame->data[2], kNeutralChroma,
                static_cast<size_t>(frame->linesize[2]) * chromaHeight);
    return true;
}

void ThresholdProcessor::SetThreshold(int threshold)
{
    threshold_.store(std::clamp(threshold, kMinThreshold, kMaxThreshold));
}

int ThresholdProcessor::Threshold() const
{
    return threshold_.load();
}
