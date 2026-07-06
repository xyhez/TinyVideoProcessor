#include "visionprocessorfactory.h"

#include <memory>

#include "infrastructure/vision/opencvcontourvisionpipeline.h"
#include "infrastructure/vision/visionpipelinemanager.h"
#include "infrastructure/vision/yolovisionpipeline.h"

void VisionProcessorFactory::RegisterBuiltinPipelines(VisionPipelineManager& manager)
{
    manager.RegisterPipeline(std::make_unique<OpenCvContourVisionPipeline>());
    manager.RegisterPipeline(std::make_unique<YoloVisionPipeline>());
    // 未来新增识别算法在这里追加,例如:
    //   manager.RegisterPipeline(std::make_unique<OcrVisionPipeline>());
}
