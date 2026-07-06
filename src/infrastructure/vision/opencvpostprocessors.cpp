#include "opencvpostprocessors.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>
#include <vector>

#include <QRectF>

#include <opencv2/dnn.hpp>

#include "infrastructure/vision/opencvpreprocessors.h"

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

YoloNms::YoloNms(QString pipelineId, float scoreThreshold, float nmsThreshold)
    : pipelineId_(std::move(pipelineId))
    , scoreThreshold_(scoreThreshold)
    , nmsThreshold_(nmsThreshold)
{
}

QString YoloNms::Name() const
{
    return QStringLiteral("yolo-nms");
}

bool YoloNms::Process(const VisionFrame& /*frame*/, VisionResultSet& results)
{
    for (VisionResult& result : results.results) {
        if (result.pipelineId != pipelineId_ || result.boxes.isEmpty()) {
            continue;
        }

        std::vector<cv::Rect> boxes;
        std::vector<float> scores;
        boxes.reserve(static_cast<size_t>(result.boxes.size()));
        scores.reserve(static_cast<size_t>(result.boxes.size()));

        for (const DetectionBox& box : result.boxes) {
            boxes.emplace_back(static_cast<int>(std::round(box.box.x())),
                               static_cast<int>(std::round(box.box.y())),
                               static_cast<int>(std::round(box.box.width())),
                               static_cast<int>(std::round(box.box.height())));
            scores.push_back(box.confidence);
        }

        std::map<int, std::vector<int>> indicesByClass;
        for (int index = 0; index < result.boxes.size(); ++index) {
            indicesByClass[result.boxes[index].classId].push_back(index);
        }

        std::vector<int> keptIndices;
        for (const auto& entry : indicesByClass) {
            const std::vector<int>& sourceIndices = entry.second;
            std::vector<cv::Rect> classBoxes;
            std::vector<float> classScores;
            classBoxes.reserve(sourceIndices.size());
            classScores.reserve(sourceIndices.size());
            for (int sourceIndex : sourceIndices) {
                classBoxes.push_back(boxes[sourceIndex]);
                classScores.push_back(scores[sourceIndex]);
            }

            std::vector<int> classKeptIndices;
            cv::dnn::NMSBoxes(classBoxes,
                              classScores,
                              scoreThreshold_,
                              nmsThreshold_,
                              classKeptIndices);
            for (int classIndex : classKeptIndices) {
                if (classIndex >= 0 && classIndex < static_cast<int>(sourceIndices.size())) {
                    keptIndices.push_back(sourceIndices[static_cast<size_t>(classIndex)]);
                }
            }
        }
        std::sort(keptIndices.begin(), keptIndices.end());

        QVector<DetectionBox> keptBoxes;
        keptBoxes.reserve(static_cast<int>(keptIndices.size()));
        for (int index : keptIndices) {
            if (index >= 0 && index < result.boxes.size()) {
                keptBoxes.append(result.boxes[index]);
            }
        }
        result.boxes = std::move(keptBoxes);
    }
    return true;
}

CoordinateMapper::CoordinateMapper(QString pipelineId, const OpenCvLetterbox* letterbox)
    : pipelineId_(std::move(pipelineId))
    , letterbox_(letterbox)
{
}

QString CoordinateMapper::Name() const
{
    return QStringLiteral("coordinate-mapper");
}

bool CoordinateMapper::Process(const VisionFrame& /*frame*/, VisionResultSet& results)
{
    if (!letterbox_ || !letterbox_->HasMapping() || letterbox_->Scale() <= 0.0) {
        return false;
    }

    const double scale = letterbox_->Scale();
    const double padLeft = static_cast<double>(letterbox_->PadLeft());
    const double padTop = static_cast<double>(letterbox_->PadTop());
    const double originalWidth = static_cast<double>(letterbox_->OriginalWidth());
    const double originalHeight = static_cast<double>(letterbox_->OriginalHeight());

    for (VisionResult& result : results.results) {
        if (result.pipelineId != pipelineId_) {
            continue;
        }

        for (DetectionBox& box : result.boxes) {
            double x = (box.box.x() - padLeft) / scale;
            double y = (box.box.y() - padTop) / scale;
            double w = box.box.width() / scale;
            double h = box.box.height() / scale;

            const double right = std::clamp(x + w, 0.0, originalWidth);
            const double bottom = std::clamp(y + h, 0.0, originalHeight);
            x = std::clamp(x, 0.0, originalWidth);
            y = std::clamp(y, 0.0, originalHeight);
            w = std::max(0.0, right - x);
            h = std::max(0.0, bottom - y);

            box.box = QRectF(x, y, w, h);
        }
    }
    return true;
}
