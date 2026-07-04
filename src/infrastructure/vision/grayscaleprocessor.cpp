#include "grayscaleprocessor.h"

#include <opencv2/imgproc.hpp>

namespace {
constexpr int kRequiredMatType = CV_8UC3;	///< 处理器要求的输入输出图像类型
}

QString GrayscaleProcessor::Name() const
{
    return QStringLiteral("Grayscale");
}

bool GrayscaleProcessor::Enabled() const
{
    return enabled_;
}

void GrayscaleProcessor::SetEnabled(bool enabled)
{
    enabled_ = enabled;
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
