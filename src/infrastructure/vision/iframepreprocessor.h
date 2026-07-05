#ifndef TINYVIDEOPROCESSOR_IFRAMEPREPROCESSOR_H
#define TINYVIDEOPROCESSOR_IFRAMEPREPROCESSOR_H

#include <QString>

#include "infrastructure/vision/visiontypes.h"

/**
 * @brief IFramePreprocessor 图像预处理构建块
 *
 * 在 IVisionPipeline 内部使用，负责对 working frame 做 in-place 变换。
 * 可以修改 VisionFrame 的图像内容和元数据（如 resize 后的宽高、
 * 色彩转换后的 pixelFormat）。
 *
 * 典型实现：OpenCvResize、OpenCvLetterbox、OpenCvBgrToRgb、
 * OpenCvGrayscale、OpenCvThreshold 等。
 */
class IFramePreprocessor {
public:
    virtual ~IFramePreprocessor() = default;

    /**
     * @brief Name 处理器名称，用于日志和调试
     */
    virtual QString Name() const = 0;

    /**
     * @brief Process 对帧做 in-place 预处理
     * @param frame 需要处理的帧，允许修改图像和元数据
     * @return true 处理成功；false 处理失败
     */
    virtual bool Process(VisionFrame& frame) = 0;
};

#endif // TINYVIDEOPROCESSOR_IFRAMEPREPROCESSOR_H
