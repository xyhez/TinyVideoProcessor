#ifndef TINYVIDEOPROCESSOR_VISIONPROCESSORFACTORY_H
#define TINYVIDEOPROCESSOR_VISIONPROCESSORFACTORY_H

class VisionPipelineManager;
struct VisionPipelineConfigSet;

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

    /**
     * @brief RegisterConfiguredPipelines 按配置注册识别管线到 Manager
     * @param manager 目标 Manager
     * @param configSet 视觉管线配置集合
     *
     * Phase 8.2 只按 enabled / type 决定注册哪些内置 pipeline。
     * 具体 params 会在后续 YOLO / OpenCV 参数配置化阶段消费。
     * 如果没有任何有效 pipeline，则回退 RegisterBuiltinPipelines。
     */
    static void RegisterConfiguredPipelines(VisionPipelineManager& manager,
                                            const VisionPipelineConfigSet& configSet);
};

#endif // TINYVIDEOPROCESSOR_VISIONPROCESSORFACTORY_H
