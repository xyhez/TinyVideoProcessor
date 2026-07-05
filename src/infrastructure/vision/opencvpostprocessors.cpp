#include "opencvpostprocessors.h"

#include <utility>

OpenCvMeasurement::OpenCvMeasurement(QString pipelineId)
    : pipelineId_(std::move(pipelineId))
{
}

QString OpenCvMeasurement::Name() const
{
    return QStringLiteral("opencv-measurement");
}

bool OpenCvMeasurement::Process(const VisionFrame& /*frame*/, VisionResultSet& results)
{
    for (VisionResult& result : results.results) {
        if (result.pipelineId != pipelineId_) {
            continue;
        }

        for (DetectionBox& box : result.boxes) {
            const double area = box.box.width() * box.box.height();
            const QString areaText = QString("area:%1").arg(static_cast<qint64>(area));
            if (box.label.isEmpty()) {
                box.label = areaText;
            } else {
                box.label += " " + areaText;
            }
        }
    }
    return true;
}
