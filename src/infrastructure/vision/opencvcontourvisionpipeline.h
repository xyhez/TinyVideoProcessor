#ifndef TINYVIDEOPROCESSOR_OPENCVCONTOURVISIONPIPELINE_H
#define TINYVIDEOPROCESSOR_OPENCVCONTOURVISIONPIPELINE_H

#include <memory>
#include <vector>

#include <QString>

#include "infrastructure/vision/iframepreprocessor.h"
#include "infrastructure/vision/ivisionanalyzer.h"
#include "infrastructure/vision/ivisionpipeline.h"
#include "infrastructure/vision/ivisionpostprocessor.h"

/**
 * @brief OpenCvContourPipelineOptions OpenCV 轮廓检测管线构造参数
 */
struct OpenCvContourPipelineOptions {
    double threshold = 128.0;	///< 二值化阈值
    double maxValue = 255.0;	///< 二值化输出最大值
};

OpenCvContourPipelineOptions DefaultOpenCvContourPipelineOptions();

/**
 * @brief OpenCvContourVisionPipeline 基于 OpenCV 传统视觉的目标轮廓检测管线
 *
 * 内部组合：
 *   OpenCvGrayscale → OpenCvThreshold → OpenCvContourAnalyzer → OpenCvMeasurement
 *
 * 输入：任意 VisionPixelFormat 的 VisionFrame
 * 输出：每个足够大的连通区域生成一个 DetectionBox，label 附加面积信息
 *
 * 适用场景：光照稳定、目标与背景对比明显的固定相机位工业场景。
 */
class OpenCvContourVisionPipeline : public IVisionPipeline {
public:
    explicit OpenCvContourVisionPipeline(OpenCvContourPipelineOptions options = {});
    ~OpenCvContourVisionPipeline() override;

    QString Id() const override;
    QString DisplayName() const override;
    QString Type() const override;

    bool Startup() override;
    void Shutdown() override;
    bool IsStarted() const override;

    bool Run(const VisionFrame& input, VisionResultSet& out) override;

private:
    OpenCvContourPipelineOptions options_;	///< OpenCV 轮廓检测构造参数
    std::vector<std::unique_ptr<IFramePreprocessor>> preprocessors_;	///< 预处理链
    std::unique_ptr<IVisionAnalyzer> analyzer_;	///< 轮廓分析器
    std::vector<std::unique_ptr<IVisionPostprocessor>> postprocessors_;	///< 后处理链
    bool started_ = false;	///< 是否已 Startup
};

#endif // TINYVIDEOPROCESSOR_OPENCVCONTOURVISIONPIPELINE_H
