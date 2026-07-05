#ifndef TINYVIDEOPROCESSOR_VISIONPROCESSORFACTORY_H
#define TINYVIDEOPROCESSOR_VISIONPROCESSORFACTORY_H

class VisionPipelineManager;

/**
 * @brief VisionProcessorFactory 内置识别管线的注册工厂
 *
 * 由 MultiStreamManager 在构造 VisionPipelineManager 后调用一次，
 * 把所有内置 pipeline 注册到 Manager。新增识别算法时只需要在
 * RegisterBuiltinPipelines 里添加一行 manager.RegisterPipeline(...)。
 *
 * 注册后的 pipeline 都是 dormant 状态，只有被 SetActivePipeline
 * 选中的那条才会实际 Startup 加载模型和分配资源。
 */
class VisionProcessorFactory {
public:
    /**
     * @brief RegisterBuiltinPipelines 注册所有内置识别管线到 Manager
     * @param manager 目标 Manager
     */
    static void RegisterBuiltinPipelines(VisionPipelineManager& manager);
};

#endif // TINYVIDEOPROCESSOR_VISIONPROCESSORFACTORY_H
