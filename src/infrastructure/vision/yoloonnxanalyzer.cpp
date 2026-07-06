#include "yoloonnxanalyzer.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <utility>
#include <vector>

#include <QElapsedTimer>
#include <QFileInfo>
#include <QtGlobal>

#include <opencv2/imgproc.hpp>

namespace {
QStringList CocoLabels()
{
    return {
        QStringLiteral("person"), QStringLiteral("bicycle"), QStringLiteral("car"),
        QStringLiteral("motorcycle"), QStringLiteral("airplane"), QStringLiteral("bus"),
        QStringLiteral("train"), QStringLiteral("truck"), QStringLiteral("boat"),
        QStringLiteral("traffic light"), QStringLiteral("fire hydrant"), QStringLiteral("stop sign"),
        QStringLiteral("parking meter"), QStringLiteral("bench"), QStringLiteral("bird"),
        QStringLiteral("cat"), QStringLiteral("dog"), QStringLiteral("horse"),
        QStringLiteral("sheep"), QStringLiteral("cow"), QStringLiteral("elephant"),
        QStringLiteral("bear"), QStringLiteral("zebra"), QStringLiteral("giraffe"),
        QStringLiteral("backpack"), QStringLiteral("umbrella"), QStringLiteral("handbag"),
        QStringLiteral("tie"), QStringLiteral("suitcase"), QStringLiteral("frisbee"),
        QStringLiteral("skis"), QStringLiteral("snowboard"), QStringLiteral("sports ball"),
        QStringLiteral("kite"), QStringLiteral("baseball bat"), QStringLiteral("baseball glove"),
        QStringLiteral("skateboard"), QStringLiteral("surfboard"), QStringLiteral("tennis racket"),
        QStringLiteral("bottle"), QStringLiteral("wine glass"), QStringLiteral("cup"),
        QStringLiteral("fork"), QStringLiteral("knife"), QStringLiteral("spoon"),
        QStringLiteral("bowl"), QStringLiteral("banana"), QStringLiteral("apple"),
        QStringLiteral("sandwich"), QStringLiteral("orange"), QStringLiteral("broccoli"),
        QStringLiteral("carrot"), QStringLiteral("hot dog"), QStringLiteral("pizza"),
        QStringLiteral("donut"), QStringLiteral("cake"), QStringLiteral("chair"),
        QStringLiteral("couch"), QStringLiteral("potted plant"), QStringLiteral("bed"),
        QStringLiteral("dining table"), QStringLiteral("toilet"), QStringLiteral("tv"),
        QStringLiteral("laptop"), QStringLiteral("mouse"), QStringLiteral("remote"),
        QStringLiteral("keyboard"), QStringLiteral("cell phone"), QStringLiteral("microwave"),
        QStringLiteral("oven"), QStringLiteral("toaster"), QStringLiteral("sink"),
        QStringLiteral("refrigerator"), QStringLiteral("book"), QStringLiteral("clock"),
        QStringLiteral("vase"), QStringLiteral("scissors"), QStringLiteral("teddy bear"),
        QStringLiteral("hair drier"), QStringLiteral("toothbrush")
    };
}

cv::Mat OutputAsRows(const cv::Mat& output)
{
    if (output.empty()) {
        return {};
    }

    if (output.dims == 2) {
        return output;
    }

    if (output.dims != 3 || output.size[0] != 1) {
        return {};
    }

    const int dim1 = output.size[1];
    const int dim2 = output.size[2];
    cv::Mat raw(dim1, dim2, CV_32F, const_cast<float*>(output.ptr<float>()));

    if (dim1 < dim2 && dim1 <= 256) {
        cv::Mat transposed;
        cv::transpose(raw, transposed);
        return transposed;
    }

    return raw;
}
}

YoloOnnxAnalyzer::YoloOnnxAnalyzer(QString pipelineId,
                                   QString modelPath,
                                   float confidenceThreshold)
    : pipelineId_(std::move(pipelineId))
    , modelPath_(std::move(modelPath))
    , confidenceThreshold_(confidenceThreshold)
    , classLabels_(CocoLabels())
{
}

QString YoloOnnxAnalyzer::Name() const
{
    return QStringLiteral("yolo-onnx-analyzer");
}

bool YoloOnnxAnalyzer::Load()
{
    lastError_.clear();
    loaded_ = false;

    if (modelPath_.isEmpty() || !QFileInfo::exists(modelPath_)) {
        lastError_ = QString("YOLO model not found: %1").arg(modelPath_);
        return false;
    }

    try {
        net_ = cv::dnn::readNetFromONNX(modelPath_.toLocal8Bit().constData());
        if (net_.empty()) {
            lastError_ = QString("YOLO model loaded as empty net: %1").arg(modelPath_);
            return false;
        }

        net_.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
        net_.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
        loaded_ = true;
        return true;
    } catch (const cv::Exception& ex) {
        lastError_ = QString("OpenCV DNN load failed: %1").arg(QString::fromLocal8Bit(ex.what()));
        return false;
    } catch (const std::exception& ex) {
        lastError_ = QString("YOLO model load failed: %1").arg(QString::fromLocal8Bit(ex.what()));
        return false;
    }
}

bool YoloOnnxAnalyzer::Analyze(const VisionFrame& frame, VisionResultSet& out)
{
    if (!loaded_ || net_.empty()) {
        lastError_ = QStringLiteral("YOLO analyzer is not loaded");
        return false;
    }
    if (frame.image.empty() || frame.pixelFormat != VisionPixelFormat::RGB24 ||
        frame.image.channels() != 3) {
        lastError_ = QStringLiteral("YOLO analyzer requires RGB24 3-channel input");
        return false;
    }

    try {
        QElapsedTimer timer;
        timer.start();

        cv::Mat blob;
        cv::dnn::blobFromImage(frame.image,
                               blob,
                               1.0 / 255.0,
                               cv::Size(frame.width, frame.height),
                               cv::Scalar(),
                               false,
                               false,
                               CV_32F);
        net_.setInput(blob);

        std::vector<cv::Mat> outputs;
        net_.forward(outputs, net_.getUnconnectedOutLayersNames());
        if (outputs.empty()) {
            lastError_ = QStringLiteral("YOLO model produced no output");
            return false;
        }

        VisionResult result;
        result.pipelineId = pipelineId_;
        result.type = VisionResultType::DetectionBox;
        if (!ParseOutput(outputs.front(), frame, result)) {
            return false;
        }

        result.elapsedMs = static_cast<double>(timer.nsecsElapsed()) / 1.0e6;
        out.pts = frame.pts;
        out.results.append(result);
        return true;
    } catch (const cv::Exception& ex) {
        lastError_ = QString("OpenCV DNN inference failed: %1").arg(QString::fromLocal8Bit(ex.what()));
        return false;
    } catch (const std::exception& ex) {
        lastError_ = QString("YOLO inference failed: %1").arg(QString::fromLocal8Bit(ex.what()));
        return false;
    }
}

bool YoloOnnxAnalyzer::ParseOutput(const cv::Mat& output,
                                   const VisionFrame& frame,
                                   VisionResult& result) const
{
    const cv::Mat rows = OutputAsRows(output);
    if (rows.empty() || rows.cols < 6) {
        return false;
    }

    const int attributes = rows.cols;
    const bool hasObjectness = attributes != classLabels_.size() + 4;
    const int classOffset = hasObjectness ? 5 : 4;
    const int classCount = attributes - classOffset;
    if (classCount <= 0) {
        return false;
    }

    for (int i = 0; i < rows.rows; ++i) {
        const float* data = rows.ptr<float>(i);
        const float objectness = hasObjectness ? data[4] : 1.0F;
        if (objectness < confidenceThreshold_) {
            continue;
        }

        int bestClassId = -1;
        float bestClassScore = 0.0F;
        for (int classIndex = 0; classIndex < classCount; ++classIndex) {
            const float score = data[classOffset + classIndex];
            if (score > bestClassScore) {
                bestClassScore = score;
                bestClassId = classIndex;
            }
        }

        const float confidence = objectness * bestClassScore;
        if (confidence < confidenceThreshold_ || bestClassId < 0) {
            continue;
        }

        const float centerX = data[0];
        const float centerY = data[1];
        const float width = data[2];
        const float height = data[3];

        if (!std::isfinite(centerX) || !std::isfinite(centerY) ||
            !std::isfinite(width) || !std::isfinite(height) ||
            width <= 0.0F || height <= 0.0F) {
            continue;
        }

        DetectionBox box;
        box.box = QRectF(centerX - width / 2.0F,
                         centerY - height / 2.0F,
                         width,
                         height);
        box.label = ClassLabel(bestClassId);
        box.confidence = confidence;
        box.classId = bestClassId;
        result.boxes.append(box);
    }

    Q_UNUSED(frame);
    return true;
}

QString YoloOnnxAnalyzer::ClassLabel(int classId) const
{
    if (classId >= 0 && classId < classLabels_.size()) {
        return classLabels_.at(classId);
    }
    return QString("class:%1").arg(classId);
}

bool YoloOnnxAnalyzer::IsLoaded() const
{
    return loaded_;
}

QString YoloOnnxAnalyzer::LastError() const
{
    return lastError_;
}
