#ifndef TINYVIDEOPROCESSOR_VISIONPIPELINECONFIG_H
#define TINYVIDEOPROCESSOR_VISIONPIPELINECONFIG_H

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

/**
 * @brief VisionPipelineConfig 单条视觉管线的配置描述
 *
 * 该结构只描述 pipeline 创建参数，不保存运行状态。
 */
struct VisionPipelineConfig {
    QString id;          ///< 管线唯一 id，例如 yolo-onnx
    QString type;        ///< 工厂创建类型，例如 yolo-onnx / opencv-contour
    QString displayName; ///< UI 展示名
    bool enabled = true; ///< 是否注册该 pipeline
    QJsonObject params;  ///< 具体 pipeline 参数
};

/**
 * @brief VisionPipelineConfigSet 视觉管线配置集合
 */
struct VisionPipelineConfigSet {
    QVector<VisionPipelineConfig> pipelines; ///< 按配置顺序排列的 pipeline
    QString sourcePath;                      ///< 实际读取或尝试读取的配置路径
    QStringList warnings;                    ///< 非阻断性解析告警
    bool usingDefault = false;               ///< 是否使用内置默认配置

    bool HasEnabledPipelines() const;
};

/**
 * @brief DefaultVisionPipelineConfigSet 生成内置默认视觉管线配置
 */
VisionPipelineConfigSet DefaultVisionPipelineConfigSet();

#endif // TINYVIDEOPROCESSOR_VISIONPIPELINECONFIG_H
