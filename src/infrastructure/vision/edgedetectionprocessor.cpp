#include "edgedetectionprocessor.h"

#include <cstdint>
#include <cstring>

#include <opencv2/imgproc.hpp>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

namespace {
constexpr int kRequiredMatType = CV_8UC3;	///< 旧 BGR 路径要求的图像类型
constexpr double kCannyLowThreshold = 80.0;	///< Canny 低阈值
constexpr double kCannyHighThreshold = 160.0;	///< Canny 高阈值
constexpr int kCannyApertureSize = 3;	///< Canny Sobel 孔径
constexpr uint8_t kNeutralChroma = 128;	///< 边缘图使用的中性色度值
}

QString EdgeDetectionProcessor::Name() const
{
    return QStringLiteral("EdgeDetection");
}

bool EdgeDetectionProcessor::Enabled() const
{
    return enabled_.load();
}

void EdgeDetectionProcessor::SetEnabled(bool enabled)
{
    enabled_.store(enabled);
}

bool EdgeDetectionProcessor::Process(cv::Mat& image)
{
    if (image.empty() || image.type() != kRequiredMatType) {
        return false;
    }

    cv::Mat grayImage;
    cv::Mat edgeImage;
    cv::cvtColor(image, grayImage, cv::COLOR_BGR2GRAY);
    cv::Canny(grayImage, edgeImage, kCannyLowThreshold, kCannyHighThreshold, kCannyApertureSize);
    cv::cvtColor(edgeImage, image, cv::COLOR_GRAY2BGR);
    return true;
}

bool EdgeDetectionProcessor::SupportsYuvProcessing() const
{
    return true;
}

bool EdgeDetectionProcessor::Process(AVFrame* frame)
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

    cv::Canny(yPlane, edgeScratch_, kCannyLowThreshold, kCannyHighThreshold, kCannyApertureSize);
    if (edgeScratch_.empty()) {
        return false;
    }

    edgeScratch_.copyTo(yPlane);

    const int chromaHeight = (frame->height + 1) / 2;
    std::memset(frame->data[1], kNeutralChroma,
                static_cast<size_t>(frame->linesize[1]) * chromaHeight);
    std::memset(frame->data[2], kNeutralChroma,
                static_cast<size_t>(frame->linesize[2]) * chromaHeight);
    return true;
}
