#include "visionprocessorfactory.h"

#include <memory>

#include "tiny_log/Logging.h"

#include "infrastructure/vision/opencvcontourvisionpipeline.h"
#include "infrastructure/vision/visionpipelineconfig.h"
#include "infrastructure/vision/visionpipelinemanager.h"
#include "infrastructure/vision/yolovisionpipeline.h"

namespace {
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
        manager.RegisterPipeline(std::make_unique<YoloVisionPipeline>());
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
