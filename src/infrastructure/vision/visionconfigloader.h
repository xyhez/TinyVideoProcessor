#ifndef TINYVIDEOPROCESSOR_VISIONCONFIGLOADER_H
#define TINYVIDEOPROCESSOR_VISIONCONFIGLOADER_H

#include <QString>

#include "infrastructure/vision/visionpipelineconfig.h"

/**
 * @brief VisionConfigLoader 读取视觉管线 JSON 配置
 *
 * 只负责配置读取、校验和默认值回退，不创建具体 pipeline。
 */
class VisionConfigLoader {
public:
    explicit VisionConfigLoader(QString projectRoot = {});

    static QString DefaultConfigPath();

    VisionPipelineConfigSet Load(const QString& path = DefaultConfigPath()) const;
    QString ResolvePath(const QString& path) const;

private:
    QString ProjectRoot() const;
    VisionPipelineConfigSet DefaultConfig(const QString& sourcePath, const QString& warning) const;
    VisionPipelineConfig ParsePipeline(const QJsonObject& object,
                                       const VisionPipelineConfigSet& defaults) const;
    QJsonObject MergeDefaultParams(const QString& type,
                                   const QJsonObject& params,
                                   const VisionPipelineConfigSet& defaults) const;

    QString projectRoot_; ///< 项目根目录，空时使用 TINY_LOG_PROJECT_ROOT 或应用目录
};

#endif // TINYVIDEOPROCESSOR_VISIONCONFIGLOADER_H
