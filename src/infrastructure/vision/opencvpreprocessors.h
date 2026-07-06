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

/**
 * @brief OpenCvBgrToRgb BGR24 → RGB24 色彩转换
 *
 * 输入已经是 RGB24 时直接返回 true。YOLO / OCR 等模型如果要求 RGB，
 * 应在所属 IVisionPipeline 内显式放入该预处理器。
 */
class OpenCvBgrToRgb : public IFramePreprocessor {
public:
    QString Name() const override;
    bool Process(VisionFrame& frame) override;
};

/**
 * @brief OpenCvLetterbox 按等比例缩放并补边到目标尺寸
 *
 * 记录最近一次处理的缩放比例和补边，用于 CoordinateMapper 把模型输入
 * 坐标映射回原始视频坐标。该对象只应在单条 pipeline 内部复用。
 */
class OpenCvLetterbox : public IFramePreprocessor {
public:
    OpenCvLetterbox(int targetWidth, int targetHeight);

    QString Name() const override;
    bool Process(VisionFrame& frame) override;

    int OriginalWidth() const;
    int OriginalHeight() const;
    int TargetWidth() const;
    int TargetHeight() const;
    double Scale() const;
    int PadLeft() const;
    int PadTop() const;
    bool HasMapping() const;

private:
    int targetWidth_ = 0;	///< 目标宽度
    int targetHeight_ = 0;	///< 目标高度
    int originalWidth_ = 0;	///< 最近一次处理前的原始宽度
    int originalHeight_ = 0;	///< 最近一次处理前的原始高度
    double scale_ = 1.0;	///< 最近一次 letterbox 缩放比例
    int padLeft_ = 0;	///< 最近一次左侧补边
    int padTop_ = 0;	///< 最近一次顶部补边
    bool hasMapping_ = false;	///< 是否已有有效映射信息
};

#endif // TINYVIDEOPROCESSOR_OPENCVPREPROCESSORS_H
