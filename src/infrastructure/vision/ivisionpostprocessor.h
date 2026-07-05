#ifndef TINYVIDEOPROCESSOR_IVISIONPOSTPROCESSOR_H
#define TINYVIDEOPROCESSOR_IVISIONPOSTPROCESSOR_H

#include <QString>

#include "infrastructure/vision/visiontypes.h"

/**
 * @brief IVisionPostprocessor 结果后处理构建块
 *
 * 在 IVisionPipeline 内部使用，负责加工 VisionResultSet 里的识别结果。
 * 不重新执行推理，不修改 VisionFrame。
 *
 * 典型实现：YoloNms、CoordinateMapper、OpenCvMaskContour、
 * OpenCvMeasurement 等。
 */
class IVisionPostprocessor {
public:
    virtual ~IVisionPostprocessor() = default;

    /**
     * @brief Name 后处理器名称，用于日志和调试
     */
    virtual QString Name() const = 0;

    /**
     * @brief Process 加工识别结果
     * @param frame 输入帧，只读（用于坐标映射等参考）
     * @param results 结果集合，允许修改（过滤、合并、坐标映射等）
     * @return true 处理成功；false 处理失败
     */
    virtual bool Process(const VisionFrame& frame, VisionResultSet& results) = 0;
};

#endif // TINYVIDEOPROCESSOR_IVISIONPOSTPROCESSOR_H
