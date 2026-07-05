#ifndef TINYVIDEOPROCESSOR_VISIONTYPES_H
#define TINYVIDEOPROCESSOR_VISIONTYPES_H

#include <cstdint>

#include <QRectF>
#include <QString>
#include <QVector>

#include <opencv2/core/mat.hpp>

/**
 * @brief VisionPixelFormat 视觉分析帧的像素格式
 *
 * 显式声明帧内 cv::Mat 的颜色空间，避免识别管线私下假设 BGR 或 RGB。
 */
enum class VisionPixelFormat {
    BGR24,	///< 8-bit 三通道 BGR，OpenCV 默认布局
    RGB24,	///< 8-bit 三通道 RGB，多数深度学习模型输入格式
    GRAY8	///< 8-bit 单通道灰度
};

/**
 * @brief VisionFrame 视觉分析的统一输入帧
 *
 * 主链路把 AVFrame 转成 VisionFrame 后传入 IVisionPipeline::Run。
 * IVisionPipeline 内部若需要修改帧数据，必须先 clone 出 working frame，
 * 不能直接污染上游传入的 const VisionFrame。
 */
struct VisionFrame {
    int width = 0;	///< 图像宽度，单位像素
    int height = 0;	///< 图像高度，单位像素
    int stride = 0;	///< 单行字节数，用于外部内存布局校验
    int64_t pts = 0;	///< 显示时间戳，单位与上游一致
    VisionPixelFormat pixelFormat = VisionPixelFormat::BGR24;	///< 当前 image 的颜色空间
    cv::Mat image;	///< 帧内存实体，浅拷贝共享，需要修改前先 clone
};

/**
 * @brief VisionResultType 单条识别结果的类型
 *
 * 第一版只使用 DetectionBox 和 TextRegion，Classification 和 Measurement
 * 保留位置供后续扩展。
 */
enum class VisionResultType {
    DetectionBox,	///< 目标检测框（YOLO / OpenCV 轮廓等）
    TextRegion,	///< OCR 识别文本区域
    Classification,	///< 整帧分类结果，第一版不实现
    Measurement	///< 尺寸 / 面积 / 计数等测量结果，第一版不实现
};

/**
 * @brief DetectionBox 单个目标检测框
 */
struct DetectionBox {
    QRectF box;	///< 检测框在原图坐标系下的位置和尺寸
    QString label;	///< 目标类别标签，可能为空
    float confidence = 0.0F;	///< 置信度，范围 0.0 - 1.0
    int classId = -1;	///< 类别 ID，未知或不适用时为 -1
};

/**
 * @brief TextRegion 单个 OCR 文本区域
 */
struct TextRegion {
    QRectF box;	///< 文本区域在原图坐标系下的位置和尺寸
    QString text;	///< 识别出的文本内容
    float confidence = 0.0F;	///< 识别置信度，范围 0.0 - 1.0
};

/**
 * @brief VisionResult 单条识别管线在一帧上的输出
 *
 * 单激活模式下 VisionResultSet 通常只有一条 VisionResult。
 */
struct VisionResult {
    QString pipelineId;	///< 结果来自哪条 pipeline，用于溯源和过滤
    VisionResultType type = VisionResultType::DetectionBox;	///< 结果主类型
    QVector<DetectionBox> boxes;	///< 目标检测框列表
    QVector<TextRegion> texts;	///< OCR 文本区域列表
    double elapsedMs = 0.0;	///< 本次识别耗时，单位毫秒
    QString errorMessage;	///< 识别失败原因，空表示成功
};

/**
 * @brief VisionResultSet 一帧对应的完整识别结果集合
 *
 * OverlayRenderer 根据 VisionResultSet 在 cv::Mat 上叠加检测框、
 * 标签和文本；上游 sink 或未来的持久化模块也可以订阅结构化数据。
 */
struct VisionResultSet {
    int64_t pts = 0;	///< 对应帧的显示时间戳
    QVector<VisionResult> results;	///< 各条 pipeline 的输出集合
};

#endif // TINYVIDEOPROCESSOR_VISIONTYPES_H
