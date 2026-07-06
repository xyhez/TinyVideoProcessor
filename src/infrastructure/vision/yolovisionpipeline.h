#ifndef TINYVIDEOPROCESSOR_YOLOVISIONPIPELINE_H
#define TINYVIDEOPROCESSOR_YOLOVISIONPIPELINE_H

#include <memory>
#include <vector>

#include <QString>

#include "infrastructure/vision/iframepreprocessor.h"
#include "infrastructure/vision/ivisionpipeline.h"
#include "infrastructure/vision/ivisionpostprocessor.h"

class OpenCvLetterbox;
class YoloOnnxAnalyzer;

/**
 * @brief YoloPipelineOptions YOLO 管线构造参数
 */
struct YoloPipelineOptions {
    QString modelPath;	///< ONNX 模型路径
    QString labelsPath;	///< 类别名文件路径，一行一个类别名；空则使用内置 COCO
    int inputWidth = 640;	///< 模型输入宽度
    int inputHeight = 640;	///< 模型输入高度
    float confidenceThreshold = 0.35F;	///< 检测置信度阈值
    float nmsThreshold = 0.45F;	///< NMS IoU 阈值
};

YoloPipelineOptions DefaultYoloPipelineOptions();

/**
 * @brief YoloVisionPipeline 基于 OpenCV DNN + ONNX 的 YOLO 目标检测管线
 *
 * 默认模型路径为 models/yolo.onnx。模型文件不进入 git，由用户自行放置。
 */
class YoloVisionPipeline : public IVisionPipeline {
public:
    explicit YoloVisionPipeline(YoloPipelineOptions options = {});
    ~YoloVisionPipeline() override;

    QString Id() const override;
    QString DisplayName() const override;
    QString Type() const override;

    bool Startup() override;
    void Shutdown() override;
    bool IsStarted() const override;

    bool Run(const VisionFrame& input, VisionResultSet& out) override;

private:
    YoloPipelineOptions options_;	///< YOLO 管线构造参数
    std::vector<std::unique_ptr<IFramePreprocessor>> preprocessors_;	///< 预处理链
    OpenCvLetterbox* letterbox_ = nullptr;	///< preprocessors_ 内 letterbox 的借用指针
    std::unique_ptr<YoloOnnxAnalyzer> analyzer_;	///< YOLO ONNX 分析器
    std::vector<std::unique_ptr<IVisionPostprocessor>> postprocessors_;	///< 后处理链
    bool started_ = false;	///< 是否已 Startup
};

#endif // TINYVIDEOPROCESSOR_YOLOVISIONPIPELINE_H
