#ifndef TINYVIDEOPROCESSOR_IVISIONANALYZER_H
#define TINYVIDEOPROCESSOR_IVISIONANALYZER_H

#include <QString>

#include "infrastructure/vision/visiontypes.h"

/**
 * @brief IVisionAnalyzer 视觉分析构建块
 *
 * 在 IVisionPipeline 内部使用，负责从帧上提取结构化识别结果。
 * 只读 VisionFrame，不修改帧数据。
 *
 * 典型实现：OpenCvContourAnalyzer、YoloOnnxAnalyzer、
 * OcrTesseractAnalyzer、OnnxSegAnalyzer 等。
 */
class IVisionAnalyzer {
public:
    virtual ~IVisionAnalyzer() = default;

    /**
     * @brief Name 分析器名称，用于日志和调试
     */
    virtual QString Name() const = 0;

    /**
     * @brief Analyze 从帧上分析并追加结果
     * @param frame 输入帧，只读
     * @param out 结果集合，追加当前 analyzer 的输出
     * @return true 分析成功；false 分析失败
     */
    virtual bool Analyze(const VisionFrame& frame, VisionResultSet& out) = 0;
};

#endif // TINYVIDEOPROCESSOR_IVISIONANALYZER_H
