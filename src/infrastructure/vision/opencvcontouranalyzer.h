#ifndef TINYVIDEOPROCESSOR_OPENCVCONTOURANALYZER_H
#define TINYVIDEOPROCESSOR_OPENCVCONTOURANALYZER_H

#include <QString>

#include "infrastructure/vision/ivisionanalyzer.h"

/**
 * @brief OpenCvContourAnalyzer 从二值化图中提取轮廓 → DetectionBox
 *
 * 输入必须是 GRAY8（已二值化）。内部使用 cv::findContours 提取外部轮廓，
 * 按最小 / 最大面积过滤后用 cv::boundingRect 输出 DetectionBox。
 *
 * DetectionBox 的 classId = -1、confidence = 1.0、label 为空
 * （面积等测量信息由后续 OpenCvMeasurement 补充到 label）。
 *
 * elapsedMs 记录本 analyzer 的分析耗时，作为整条 pipeline 耗时的近似
 * （前后处理耗时相对可忽略）。
 */
class OpenCvContourAnalyzer : public IVisionAnalyzer {
public:
    /**
     * @param pipelineId 结果归属的 pipeline id，写入 VisionResult::pipelineId
     * @param minArea 最小轮廓面积过滤（像素²），默认 100
     * @param maxArea 最大轮廓面积过滤（像素²），<=0 表示不限制
     */
    explicit OpenCvContourAnalyzer(QString pipelineId,
                                   double minArea = 100.0,
                                   double maxArea = -1.0);

    QString Name() const override;
    bool Analyze(const VisionFrame& frame, VisionResultSet& out) override;

    void SetMinArea(double minArea);
    void SetMaxArea(double maxArea);

private:
    QString pipelineId_;	///< 结果归属的 pipeline id
    double minArea_;	///< 最小面积过滤
    double maxArea_;	///< 最大面积过滤，<=0 表示不限制
};

#endif // TINYVIDEOPROCESSOR_OPENCVCONTOURANALYZER_H
