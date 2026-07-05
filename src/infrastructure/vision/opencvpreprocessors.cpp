#include "opencvpreprocessors.h"

#include <opencv2/imgproc.hpp>

QString OpenCvGrayscale::Name() const
{
    return QStringLiteral("opencv-grayscale");
}

bool OpenCvGrayscale::Process(VisionFrame& frame)
{
    if (frame.image.empty()) {
        return false;
    }

    switch (frame.pixelFormat) {
    case VisionPixelFormat::GRAY8:
        // 已经是灰度，无需处理
        return true;
    case VisionPixelFormat::BGR24: {
        cv::Mat gray;
        cv::cvtColor(frame.image, gray, cv::COLOR_BGR2GRAY);
        frame.image = std::move(gray);
        frame.pixelFormat = VisionPixelFormat::GRAY8;
        frame.stride = static_cast<int>(frame.image.step);
        return true;
    }
    case VisionPixelFormat::RGB24: {
        cv::Mat gray;
        cv::cvtColor(frame.image, gray, cv::COLOR_RGB2GRAY);
        frame.image = std::move(gray);
        frame.pixelFormat = VisionPixelFormat::GRAY8;
        frame.stride = static_cast<int>(frame.image.step);
        return true;
    }
    }
    return false;
}

OpenCvThreshold::OpenCvThreshold(double threshold, double maxValue)
    : threshold_(threshold)
    , maxValue_(maxValue)
{
}

QString OpenCvThreshold::Name() const
{
    return QStringLiteral("opencv-threshold");
}

bool OpenCvThreshold::Process(VisionFrame& frame)
{
    if (frame.image.empty()) {
        return false;
    }
    if (frame.pixelFormat != VisionPixelFormat::GRAY8) {
        // 严格要求上游先做灰度化，避免隐式颜色空间转换
        return false;
    }

    cv::threshold(frame.image, frame.image, threshold_, maxValue_, cv::THRESH_BINARY);
    frame.stride = static_cast<int>(frame.image.step);
    return true;
}

void OpenCvThreshold::SetThreshold(double threshold)
{
    threshold_ = threshold;
}

double OpenCvThreshold::Threshold() const
{
    return threshold_;
}
