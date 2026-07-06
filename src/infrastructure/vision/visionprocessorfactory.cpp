#include "visionprocessorfactory.h"

#include <memory>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QJsonValue>

#include "tiny_log/Logging.h"

#include "infrastructure/vision/opencvcontourvisionpipeline.h"
#include "infrastructure/vision/visionpipelineconfig.h"
#include "infrastructure/vision/visionpipelinemanager.h"
#include "infrastructure/vision/yolovisionpipeline.h"

namespace {
QString ProjectRoot()
{
#ifdef TINY_LOG_PROJECT_ROOT
    return QStringLiteral(TINY_LOG_PROJECT_ROOT);
#else
    return QCoreApplication::applicationDirPath();
#endif
}

QString ResolveConfiguredPath(const QString& path)
{
    if (path.isEmpty()) {
        return {};
    }

    QFileInfo fileInfo(path);
    if (fileInfo.isAbsolute()) {
        return QDir::cleanPath(fileInfo.absoluteFilePath());
    }

    return QDir(ProjectRoot()).absoluteFilePath(path);
}

int ReadIntParam(const QJsonObject& params, const QString& key, int defaultValue)
{
    const QJsonValue value = params.value(key);
    return value.isDouble() ? value.toInt(defaultValue) : defaultValue;
}

float ReadFloatParam(const QJsonObject& params, const QString& key, float defaultValue)
{
    const QJsonValue value = params.value(key);
    return value.isDouble() ? static_cast<float>(value.toDouble(defaultValue)) : defaultValue;
}

QString ReadPathParam(const QJsonObject& params, const QString& key, const QString& defaultValue)
{
    const QJsonValue value = params.value(key);
    const QString path = value.isString() ? value.toString().trimmed() : defaultValue;
    return ResolveConfiguredPath(path);
}

YoloPipelineOptions BuildYoloOptions(const VisionPipelineConfig& config)
{
    YoloPipelineOptions options = DefaultYoloPipelineOptions();
    const QJsonObject& params = config.params;
    options.modelPath = ReadPathParam(params, QStringLiteral("modelPath"), options.modelPath);
    options.labelsPath = ReadPathParam(params, QStringLiteral("labelsPath"), options.labelsPath);
    options.inputWidth = ReadIntParam(params, QStringLiteral("inputWidth"), options.inputWidth);
    options.inputHeight = ReadIntParam(params, QStringLiteral("inputHeight"), options.inputHeight);
    options.confidenceThreshold = ReadFloatParam(
        params,
        QStringLiteral("confidenceThreshold"),
        options.confidenceThreshold);
    options.nmsThreshold = ReadFloatParam(params, QStringLiteral("nmsThreshold"), options.nmsThreshold);
    return options;
}

bool RegisterConfiguredPipeline(const VisionPipelineConfig& config,
                                VisionPipelineManager& manager)
{
    if (!config.enabled) {
        LOG_DEBUG_STREAM() << "VisionProcessorFactory skip disabled pipeline" << config.id;
        return false;
    }

    if (config.type == QStringLiteral("opencv-contour")) {
        manager.RegisterPipeline(std::make_unique<OpenCvContourVisionPipeline>());
        return true;
    }

    if (config.type == QStringLiteral("yolo-onnx")) {
        manager.RegisterPipeline(std::make_unique<YoloVisionPipeline>(BuildYoloOptions(config)));
        return true;
    }

    LOG_WARN_STREAM() << "VisionProcessorFactory skip unknown pipeline type"
                      << config.type << "id" << config.id;
    return false;
}
}

void VisionProcessorFactory::RegisterBuiltinPipelines(VisionPipelineManager& manager)
{
    manager.RegisterPipeline(std::make_unique<OpenCvContourVisionPipeline>());
    manager.RegisterPipeline(std::make_unique<YoloVisionPipeline>());
    // 未来新增识别算法在这里追加,例如:
    //   manager.RegisterPipeline(std::make_unique<OcrVisionPipeline>());
}

void VisionProcessorFactory::RegisterConfiguredPipelines(
    VisionPipelineManager& manager,
    const VisionPipelineConfigSet& configSet)
{
    int registeredCount = 0;
    for (const VisionPipelineConfig& config : configSet.pipelines) {
        if (RegisterConfiguredPipeline(config, manager)) {
            ++registeredCount;
        }
    }

    if (registeredCount <= 0) {
        LOG_WARN_STREAM() << "VisionProcessorFactory config has no valid pipelines,"
                          << "fallback to builtin pipelines";
        RegisterBuiltinPipelines(manager);
    }
}
