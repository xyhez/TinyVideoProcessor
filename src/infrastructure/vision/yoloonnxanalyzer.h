#ifndef TINYVIDEOPROCESSOR_YOLOONNXANALYZER_H
#define TINYVIDEOPROCESSOR_YOLOONNXANALYZER_H

#include <QString>
#include <QStringList>

#include <opencv2/dnn.hpp>

#include "infrastructure/vision/ivisionanalyzer.h"

/**
 * @brief YoloOnnxAnalyzer 使用 OpenCV DNN 执行 YOLO ONNX 推理
 *
 * 输入要求：RGB24、三通道、尺寸已由所属 pipeline 的 preprocessor 调整到模型输入尺寸。
 * 输出坐标仍处于模型输入坐标系，后续由 CoordinateMapper 映射回原始视频坐标。
 */
class YoloOnnxAnalyzer : public IVisionAnalyzer {
public:
    YoloOnnxAnalyzer(QString pipelineId,
                     QString modelPath,
                     float confidenceThreshold = 0.35F);

    QString Name() const override;
    bool Analyze(const VisionFrame& frame, VisionResultSet& out) override;

    bool Load();
    bool IsLoaded() const;
    QString LastError() const;

private:
    bool ParseOutput(const cv::Mat& output, const VisionFrame& frame, VisionResult& result) const;
    QString ClassLabel(int classId) const;

    QString pipelineId_;	///< 结果归属 pipeline id
    QString modelPath_;	///< ONNX 模型路径
    float confidenceThreshold_ = 0.35F;	///< 检测置信度阈值
    cv::dnn::Net net_;	///< OpenCV DNN 网络
    QStringList classLabels_;	///< COCO 类别名
    QString lastError_;	///< 最近一次加载或推理错误
    bool loaded_ = false;	///< 模型是否已加载
};

#endif // TINYVIDEOPROCESSOR_YOLOONNXANALYZER_H
