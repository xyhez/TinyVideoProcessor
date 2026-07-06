#ifndef TINYVIDEOPROCESSOR_OPENCVPOSTPROCESSORS_H
#define TINYVIDEOPROCESSOR_OPENCVPOSTPROCESSORS_H

#include <QString>

#include "infrastructure/vision/ivisionpostprocessor.h"

class OpenCvLetterbox;

/**
 * @brief OpenCvMeasurement 在 DetectionBox 的 label 上补充面积信息
 *
 * 第一版实现：如果 label 为空，填 "area:<value>"；如果 label 非空，
 * 追加 " area:<value>"。面积用 box 宽 × 高近似（未做 contour 二次计算，
 * 简化实现）。
 *
 * 只处理 pipelineId 匹配的 VisionResult，避免影响其他 pipeline 的结果。
 */
class OpenCvMeasurement : public IVisionPostprocessor {
public:
    /**
     * @param pipelineId 只处理归属该 pipeline 的结果
     */
    explicit OpenCvMeasurement(QString pipelineId);

    QString Name() const override;
    bool Process(const VisionFrame& frame, VisionResultSet& results) override;

private:
    QString pipelineId_;	///< 只加工归属该 pipeline 的结果
};

/**
 * @brief YoloNms 对指定 pipeline 的 DetectionBox 执行非极大值抑制
 */
class YoloNms : public IVisionPostprocessor {
public:
    YoloNms(QString pipelineId, float scoreThreshold = 0.35F, float nmsThreshold = 0.45F);

    QString Name() const override;
    bool Process(const VisionFrame& frame, VisionResultSet& results) override;

private:
    QString pipelineId_;	///< 只处理归属该 pipeline 的结果
    float scoreThreshold_ = 0.35F;	///< 置信度阈值
    float nmsThreshold_ = 0.45F;	///< NMS IoU 阈值
};

/**
 * @brief CoordinateMapper 将 letterbox 输入坐标映射回原始视频坐标
 */
class CoordinateMapper : public IVisionPostprocessor {
public:
    CoordinateMapper(QString pipelineId, const OpenCvLetterbox* letterbox);

    QString Name() const override;
    bool Process(const VisionFrame& frame, VisionResultSet& results) override;

private:
    QString pipelineId_;	///< 只处理归属该 pipeline 的结果
    const OpenCvLetterbox* letterbox_ = nullptr;	///< letterbox 映射信息来源，借用不拥有
};

#endif // TINYVIDEOPROCESSOR_OPENCVPOSTPROCESSORS_H
