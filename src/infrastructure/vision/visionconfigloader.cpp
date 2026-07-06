#include "visionconfigloader.h"

#include <utility>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSet>

#include "tiny_log/Logging.h"

namespace {
QString ReadString(const QJsonObject& object, const QString& key)
{
    const QJsonValue value = object.value(key);
    return value.isString() ? value.toString().trimmed() : QString();
}

bool ReadBool(const QJsonObject& object, const QString& key, bool defaultValue)
{
    const QJsonValue value = object.value(key);
    return value.isBool() ? value.toBool() : defaultValue;
}

const VisionPipelineConfig* FindDefaultConfig(const QString& type,
                                              const VisionPipelineConfigSet& defaults)
{
    for (const VisionPipelineConfig& config : defaults.pipelines) {
        if (config.type == type || config.id == type) {
            return &config;
        }
    }
    return nullptr;
}
}

VisionConfigLoader::VisionConfigLoader(QString projectRoot)
    : projectRoot_(std::move(projectRoot))
{
}

QString VisionConfigLoader::DefaultConfigPath()
{
    return QStringLiteral("config/vision_pipelines.json");
}

VisionPipelineConfigSet VisionConfigLoader::Load(const QString& path) const
{
    const QString resolvedPath = ResolvePath(path);
    QFile file(resolvedPath);
    if (!file.exists()) {
        return DefaultConfig(
            resolvedPath,
            QString("Vision config not found, fallback to defaults: %1").arg(resolvedPath));
    }

    if (!file.open(QIODevice::ReadOnly)) {
        return DefaultConfig(
            resolvedPath,
            QString("Vision config open failed, fallback to defaults: %1").arg(resolvedPath));
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return DefaultConfig(
            resolvedPath,
            QString("Vision config parse failed, fallback to defaults: %1")
                .arg(parseError.errorString()));
    }

    const QJsonObject root = document.object();
    const QJsonValue pipelinesValue = root.value(QStringLiteral("pipelines"));
    if (!pipelinesValue.isArray()) {
        return DefaultConfig(
            resolvedPath,
            QStringLiteral("Vision config missing pipelines array, fallback to defaults"));
    }

    const VisionPipelineConfigSet defaults = DefaultVisionPipelineConfigSet();
    VisionPipelineConfigSet configSet;
    configSet.sourcePath = resolvedPath;
    configSet.usingDefault = false;

    QSet<QString> ids;
    const QJsonArray pipelines = pipelinesValue.toArray();
    for (const QJsonValue& value : pipelines) {
        if (!value.isObject()) {
            configSet.warnings.append(QStringLiteral("Skip non-object pipeline config"));
            continue;
        }

        const VisionPipelineConfig config = ParsePipeline(value.toObject(), defaults);
        if (config.id.isEmpty() || config.type.isEmpty()) {
            configSet.warnings.append(QStringLiteral("Skip pipeline config with empty id or type"));
            continue;
        }
        if (ids.contains(config.id)) {
            configSet.warnings.append(QString("Skip duplicated pipeline id: %1").arg(config.id));
            continue;
        }

        ids.insert(config.id);
        configSet.pipelines.append(config);
    }

    if (configSet.pipelines.isEmpty()) {
        return DefaultConfig(
            resolvedPath,
            QStringLiteral("Vision config has no valid pipelines, fallback to defaults"));
    }

    for (const QString& warning : configSet.warnings) {
        LOG_WARN_STREAM() << warning;
    }
    return configSet;
}

QString VisionConfigLoader::ResolvePath(const QString& path) const
{
    const QString effectivePath = path.isEmpty() ? DefaultConfigPath() : path;
    QFileInfo fileInfo(effectivePath);
    if (fileInfo.isAbsolute()) {
        return QDir::cleanPath(fileInfo.absoluteFilePath());
    }

    return QDir(ProjectRoot()).absoluteFilePath(effectivePath);
}

QString VisionConfigLoader::ProjectRoot() const
{
    if (!projectRoot_.isEmpty()) {
        return projectRoot_;
    }

#ifdef TINY_LOG_PROJECT_ROOT
    return QStringLiteral(TINY_LOG_PROJECT_ROOT);
#else
    return QCoreApplication::applicationDirPath();
#endif
}

VisionPipelineConfigSet VisionConfigLoader::DefaultConfig(const QString& sourcePath,
                                                          const QString& warning) const
{
    LOG_WARN_STREAM() << warning;
    VisionPipelineConfigSet configSet = DefaultVisionPipelineConfigSet();
    configSet.sourcePath = sourcePath;
    configSet.warnings.append(warning);
    return configSet;
}

VisionPipelineConfig VisionConfigLoader::ParsePipeline(
    const QJsonObject& object,
    const VisionPipelineConfigSet& defaults) const
{
    VisionPipelineConfig config;
    config.id = ReadString(object, QStringLiteral("id"));
    config.type = ReadString(object, QStringLiteral("type"));
    config.displayName = ReadString(object, QStringLiteral("displayName"));
    config.enabled = ReadBool(object, QStringLiteral("enabled"), true);

    const QJsonValue paramsValue = object.value(QStringLiteral("params"));
    const QJsonObject params = paramsValue.isObject() ? paramsValue.toObject() : QJsonObject();
    config.params = MergeDefaultParams(config.type, params, defaults);

    if (config.displayName.isEmpty()) {
        const VisionPipelineConfig* defaultConfig = FindDefaultConfig(config.type, defaults);
        config.displayName = defaultConfig ? defaultConfig->displayName : config.id;
    }

    return config;
}

QJsonObject VisionConfigLoader::MergeDefaultParams(
    const QString& type,
    const QJsonObject& params,
    const VisionPipelineConfigSet& defaults) const
{
    const VisionPipelineConfig* defaultConfig = FindDefaultConfig(type, defaults);
    QJsonObject merged = defaultConfig ? defaultConfig->params : QJsonObject();
    for (auto it = params.begin(); it != params.end(); ++it) {
        merged.insert(it.key(), it.value());
    }
    return merged;
}
