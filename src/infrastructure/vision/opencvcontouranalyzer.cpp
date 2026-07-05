#include "opencvcontouranalyzer.h"

#include <utility>
#include <vector>

#include <QElapsedTimer>

#include <opencv2/imgproc.hpp>

OpenCvContourAnalyzer::OpenCvContourAnalyzer(QString pipelineId,
                                             double minArea,
                                             double maxArea)
    : pipelineId_(std::move(pipelineId))
    , minArea_(minArea)
    , maxArea_(maxArea)
{
}

QString OpenCvContourAnalyzer::Name() const
{
    return QStringLiteral("opencv-contour");
}

bool OpenCvContourAnalyzer::Analyze(const VisionFrame& frame, VisionResultSet& out)
{
    if (frame.image.empty()) {
        return false;
    }
    if (frame.pixelFormat != VisionPixelFormat::GRAY8) {
        return false;
    }

    QElapsedTimer timer;
    timer.start();

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(frame.image, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    VisionResult result;
    result.pipelineId = pipelineId_;
    result.type = VisionResultType::DetectionBox;

    for (const auto& contour : contours) {
        const double area = cv::contourArea(contour);
        if (area < minArea_) {
            continue;
        }
        if (maxArea_ > 0.0 && area > maxArea_) {
            continue;
        }

        const cv::Rect rect = cv::boundingRect(contour);
        DetectionBox box;
        box.box = QRectF(rect.x, rect.y, rect.width, rect.height);
        box.classId = -1;
        box.confidence = 1.0F;	// 传统检测无置信度概念，标记为 100%
        result.boxes.append(box);
    }

    result.elapsedMs = static_cast<double>(timer.nsecsElapsed()) / 1.0e6;
    out.pts = frame.pts;
    out.results.append(result);
    return true;
}

void OpenCvContourAnalyzer::SetMinArea(double minArea)
{
    minArea_ = minArea;
}

void OpenCvContourAnalyzer::SetMaxArea(double maxArea)
{
    maxArea_ = maxArea;
}
