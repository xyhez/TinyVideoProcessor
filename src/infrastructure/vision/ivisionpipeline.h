#ifndef TINYVIDEOPROCESSOR_IVISIONPIPELINE_H
#define TINYVIDEOPROCESSOR_IVISIONPIPELINE_H

#include <QString>

#include "infrastructure/vision/visiontypes.h"

/**
 * @brief IVisionPipeline 一种识别算法的完整闭包接口
 *
 * 一条识别管线内部自行组织 IFramePreprocessor / IVisionAnalyzer /
 * IVisionPostprocessor 构建块，对主链路暴露"输入原始帧，输出结果集合"
 * 的统一入口。
 *
 * 生命周期采用懒加载：
 * - 构造时轻量，不加载模型和 GPU 资源
 * - Startup() 由 VisionPipelineManager 在激活时调用，加载模型
 * - Shutdown() 由 VisionPipelineManager 在切走时调用，释放资源
 * - Run() 只在 IsStarted() 为 true 时调用
 */
class IVisionPipeline {
public:
    virtual ~IVisionPipeline() = default;

    /**
     * @brief Id 管线唯一标识
     * @return 例如 "yolo-onnx" / "opencv-contour"
     */
    virtual QString Id() const = 0;

    /**
     * @brief DisplayName UI 展示名
     * @return 例如 "YOLO 目标检测" / "OpenCV 轮廓检测"
     */
    virtual QString DisplayName() const = 0;

    /**
     * @brief Type 管线分类
     * @return 例如 "detection" / "ocr" / "segmentation"
     */
    virtual QString Type() const = 0;

    /**
     * @brief Startup 加载模型和分配资源
     * @return true 加载成功；false 加载失败
     *
     * 可能耗时较长（YOLO 模型加载可能几秒），第一版接受同步调用。
     * 失败时应保持 dormant 状态，Shutdown() 可安全再次调用。
     */
    virtual bool Startup() = 0;

    /**
     * @brief Shutdown 释放模型和资源
     *
     * 幂等：多次调用不应崩溃。调用后 IsStarted() 应返回 false。
     */
    virtual void Shutdown() = 0;

    /**
     * @brief IsStarted 当前是否已加载并可运行
     */
    virtual bool IsStarted() const = 0;

    /**
     * @brief Run 执行完整的预处理 → 分析 → 后处理链
     * @param input 输入帧，只读；如需修改必须先 clone 到 working frame
     * @param out 结果集合，追加当前 pipeline 的输出
     * @return true 处理成功；false 未 Startup 或处理链失败
     */
    virtual bool Run(const VisionFrame& input, VisionResultSet& out) = 0;
};

#endif // TINYVIDEOPROCESSOR_IVISIONPIPELINE_H
