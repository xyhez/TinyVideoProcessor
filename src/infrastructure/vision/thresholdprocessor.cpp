#include "thresholdprocessor.h"

#include <algorithm>

#include <opencv2/imgproc.hpp>

namespace {
constexpr int kRequiredMatType = CV_8UC3;	///< 处理器要求的输入输出图像类型
constexpr int kMinThreshold = 0;	///< 二值化阈值下限
constexpr int kMaxThreshold = 255;	///< 二值化阈值上限
constexpr double kBinaryMaxValue = 255.0;	///< 二值化输出的白色像素值
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
    return enabled_;
}

void ThresholdProcessor::SetEnabled(bool enabled)
{
    enabled_ = enabled;
}

bool ThresholdProcessor::Process(cv::Mat& image)
{
    if (image.empty() || image.type() != kRequiredMatType) {
        return false;
    }

    cv::Mat grayImage;
    cv::Mat binaryImage;
    cv::cvtColor(image, grayImage, cv::COLOR_BGR2GRAY);
    cv::threshold(grayImage, binaryImage, threshold_, kBinaryMaxValue, cv::THRESH_BINARY);
    cv::cvtColor(binaryImage, image, cv::COLOR_GRAY2BGR);
    return true;
}

void ThresholdProcessor::SetThreshold(int threshold)
{
    threshold_ = std::clamp(threshold, kMinThreshold, kMaxThreshold);
}

int ThresholdProcessor::Threshold() const
{
    return threshold_;
}
