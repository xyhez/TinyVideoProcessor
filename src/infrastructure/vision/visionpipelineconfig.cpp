#include "visionpipelineconfig.h"

namespace {
QJsonObject OpenCvContourDefaultParams()
{
    QJsonObject params;
    params.insert(QStringLiteral("threshold"), 128);
    params.insert(QStringLiteral("maxValue"), 255);
    return params;
}

QJsonObject YoloDefaultParams()
{
    QJsonObject params;
    params.insert(QStringLiteral("modelPath"), QStringLiteral("models/yolo.onnx"));
    params.insert(QStringLiteral("labelsPath"), QString());
    params.insert(QStringLiteral("inputWidth"), 640);
    params.insert(QStringLiteral("inputHeight"), 640);
    params.insert(QStringLiteral("confidenceThreshold"), 0.35);
    params.insert(QStringLiteral("nmsThreshold"), 0.45);
    params.insert(QStringLiteral("backend"), QStringLiteral("opencv-dnn"));
    params.insert(QStringLiteral("target"), QStringLiteral("cpu"));
    return params;
}
}

bool VisionPipelineConfigSet::HasEnabledPipelines() const
{
    for (const VisionPipelineConfig& pipeline : pipelines) {
        if (pipeline.enabled) {
            return true;
        }
    }
    return false;
}

VisionPipelineConfigSet DefaultVisionPipelineConfigSet()
{
    VisionPipelineConfigSet configSet;
    configSet.usingDefault = true;

    VisionPipelineConfig openCv;
    openCv.id = QStringLiteral("opencv-contour");
    openCv.type = QStringLiteral("opencv-contour");
    openCv.displayName = QStringLiteral("OpenCV Contour Detection");
    openCv.enabled = true;
    openCv.params = OpenCvContourDefaultParams();
    configSet.pipelines.append(openCv);

    VisionPipelineConfig yolo;
    yolo.id = QStringLiteral("yolo-onnx");
    yolo.type = QStringLiteral("yolo-onnx");
    yolo.displayName = QStringLiteral("YOLO ONNX Detection");
    yolo.enabled = true;
    yolo.params = YoloDefaultParams();
    configSet.pipelines.append(yolo);

    return configSet;
}
