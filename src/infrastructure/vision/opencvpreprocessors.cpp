#include "opencvpreprocessors.h"

#include <algorithm>
#include <cmath>

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

QString OpenCvBgrToRgb::Name() const
{
    return QStringLiteral("opencv-bgr-to-rgb");
}

bool OpenCvBgrToRgb::Process(VisionFrame& frame)
{
    if (frame.image.empty()) {
        return false;
    }

    switch (frame.pixelFormat) {
    case VisionPixelFormat::RGB24:
        return true;
    case VisionPixelFormat::BGR24: {
        cv::Mat rgb;
        cv::cvtColor(frame.image, rgb, cv::COLOR_BGR2RGB);
        frame.image = std::move(rgb);
        frame.pixelFormat = VisionPixelFormat::RGB24;
        frame.stride = static_cast<int>(frame.image.step);
        return true;
    }
    case VisionPixelFormat::GRAY8:
        return false;
    }
    return false;
}

OpenCvLetterbox::OpenCvLetterbox(int targetWidth, int targetHeight)
    : targetWidth_(targetWidth)
    , targetHeight_(targetHeight)
{
}

QString OpenCvLetterbox::Name() const
{
    return QStringLiteral("opencv-letterbox");
}

bool OpenCvLetterbox::Process(VisionFrame& frame)
{
    hasMapping_ = false;

    if (frame.image.empty() || targetWidth_ <= 0 || targetHeight_ <= 0 ||
        frame.width <= 0 || frame.height <= 0) {
        return false;
    }

    originalWidth_ = frame.width;
    originalHeight_ = frame.height;

    const double scaleX = static_cast<double>(targetWidth_) / static_cast<double>(originalWidth_);
    const double scaleY = static_cast<double>(targetHeight_) / static_cast<double>(originalHeight_);
    scale_ = std::min(scaleX, scaleY);
    if (!std::isfinite(scale_) || scale_ <= 0.0) {
        return false;
    }

    const int resizedWidth = std::max(1, static_cast<int>(std::round(originalWidth_ * scale_)));
    const int resizedHeight = std::max(1, static_cast<int>(std::round(originalHeight_ * scale_)));
    padLeft_ = (targetWidth_ - resizedWidth) / 2;
    padTop_ = (targetHeight_ - resizedHeight) / 2;

    cv::Mat resized;
    cv::resize(frame.image, resized, cv::Size(resizedWidth, resizedHeight), 0.0, 0.0, cv::INTER_LINEAR);

    const int cvType = frame.image.type();
    cv::Mat letterboxed(targetHeight_, targetWidth_, cvType, cv::Scalar(114, 114, 114));
    resized.copyTo(letterboxed(cv::Rect(padLeft_, padTop_, resizedWidth, resizedHeight)));

    frame.image = std::move(letterboxed);
    frame.width = targetWidth_;
    frame.height = targetHeight_;
    frame.stride = static_cast<int>(frame.image.step);
    hasMapping_ = true;
    return true;
}

int OpenCvLetterbox::OriginalWidth() const
{
    return originalWidth_;
}

int OpenCvLetterbox::OriginalHeight() const
{
    return originalHeight_;
}

int OpenCvLetterbox::TargetWidth() const
{
    return targetWidth_;
}

int OpenCvLetterbox::TargetHeight() const
{
    return targetHeight_;
}

double OpenCvLetterbox::Scale() const
{
    return scale_;
}

int OpenCvLetterbox::PadLeft() const
{
    return padLeft_;
}

int OpenCvLetterbox::PadTop() const
{
    return padTop_;
}

bool OpenCvLetterbox::HasMapping() const
{
    return hasMapping_;
}
