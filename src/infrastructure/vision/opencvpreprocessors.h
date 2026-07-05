#ifndef TINYVIDEOPROCESSOR_OPENCVPREPROCESSORS_H
#define TINYVIDEOPROCESSOR_OPENCVPREPROCESSORS_H

#include <QString>

#include "infrastructure/vision/iframepreprocessor.h"

/**
 * @brief OpenCvGrayscale BGR24 / RGB24 → GRAY8 灰度转换
 *
 * 输入已经是 GRAY8 时直接返回 true，不重新计算。
 * 变换后会同步更新 VisionFrame::pixelFormat 和 stride。
 */
class OpenCvGrayscale : public IFramePreprocessor {
public:
    QString Name() const override;
    bool Process(VisionFrame& frame) override;
};

/**
 * @brief OpenCvThreshold GRAY8 → GRAY8 二值化
 *
 * 输入必须是 GRAY8（要求上游先做 OpenCvGrayscale），否则返回 false。
 * 第一版使用固定的 THRESH_BINARY，参数化阈值和最大值。
 */
class OpenCvThreshold : public IFramePreprocessor {
public:
    /**
     * @param threshold 阈值，范围 0-255
     * @param maxValue 高于阈值的像素输出值，默认 255
     */
    explicit OpenCvThreshold(double threshold = 128.0, double maxValue = 255.0);

    QString Name() const override;
    bool Process(VisionFrame& frame) override;

    void SetThreshold(double threshold);
    double Threshold() const;

private:
    double threshold_;	///< 二值化阈值
    double maxValue_;	///< 高于阈值的输出值
};

#endif // TINYVIDEOPROCESSOR_OPENCVPREPROCESSORS_H
