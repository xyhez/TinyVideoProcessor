#include "edgedetectionprocessor.h"

#include <opencv2/imgproc.hpp>

namespace {
constexpr int kRequiredMatType = CV_8UC3;	///< 处理器要求的输入输出图像类型
constexpr double kCannyLowThreshold = 80.0;	///< Canny 边缘检测低阈值
constexpr double kCannyHighThreshold = 160.0;	///< Canny 边缘检测高阈值
constexpr int kCannyApertureSize = 3;	///< Canny Sobel 算子孔径大小
}

QString EdgeDetectionProcessor::Name() const
{
    return QStringLiteral("EdgeDetection");
}

bool EdgeDetectionProcessor::Enabled() const
{
    return enabled_;
}

void EdgeDetectionProcessor::SetEnabled(bool enabled)
{
    enabled_ = enabled;
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
