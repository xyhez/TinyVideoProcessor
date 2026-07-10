#include <QtTest>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "infrastructure/vision/visionconfigloader.h"

namespace {
bool WriteFile(const QString& path, const QByteArray& content)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return file.write(content) == content.size();
}
}

class VisionConfigLoaderTest : public QObject {
    Q_OBJECT

private slots:
    void missingFileFallsBackToDefaults();
    void malformedJsonFallsBackToDefaults();
    void validConfigMergesDefaultParameters();
    void duplicatedIdsAreSkippedWithWarning();
    void relativePathUsesConfiguredProjectRoot();
};

void VisionConfigLoaderTest::missingFileFallsBackToDefaults()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    VisionConfigLoader loader(tempDir.path());

    const VisionPipelineConfigSet result = loader.Load(QStringLiteral("missing.json"));

    QVERIFY(result.usingDefault);
    QCOMPARE(result.pipelines.size(), 2);
    QVERIFY(!result.warnings.isEmpty());
    QVERIFY(result.sourcePath.endsWith(QStringLiteral("missing.json")));
}

void VisionConfigLoaderTest::malformedJsonFallsBackToDefaults()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString configPath = tempDir.filePath(QStringLiteral("invalid.json"));
    QVERIFY(WriteFile(configPath, QByteArrayLiteral("{")));

    VisionConfigLoader loader(tempDir.path());
    const VisionPipelineConfigSet result = loader.Load(configPath);

    QVERIFY(result.usingDefault);
    QCOMPARE(result.pipelines.size(), 2);
    QVERIFY(!result.warnings.isEmpty());
}

void VisionConfigLoaderTest::validConfigMergesDefaultParameters()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString configPath = tempDir.filePath(QStringLiteral("vision.json"));
    const QByteArray json = R"json({
        "pipelines": [
            {
                "id": "opencv-custom",
                "type": "opencv-contour",
                "enabled": true,
                "params": { "threshold": 42 }
            }
        ]
    })json";
    QVERIFY(WriteFile(configPath, json));

    VisionConfigLoader loader(tempDir.path());
    const VisionPipelineConfigSet result = loader.Load(configPath);

    QVERIFY(!result.usingDefault);
    QCOMPARE(result.pipelines.size(), 1);
    const VisionPipelineConfig& pipeline = result.pipelines.first();
    QCOMPARE(pipeline.id, QStringLiteral("opencv-custom"));
    QCOMPARE(pipeline.displayName, QStringLiteral("OpenCV Contour Detection"));
    QCOMPARE(pipeline.params.value(QStringLiteral("threshold")).toInt(), 42);
    QCOMPARE(pipeline.params.value(QStringLiteral("maxValue")).toInt(), 255);
}

void VisionConfigLoaderTest::duplicatedIdsAreSkippedWithWarning()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString configPath = tempDir.filePath(QStringLiteral("duplicate.json"));
    const QByteArray json = R"json({
        "pipelines": [
            { "id": "same", "type": "opencv-contour" },
            { "id": "same", "type": "yolo-onnx" }
        ]
    })json";
    QVERIFY(WriteFile(configPath, json));

    VisionConfigLoader loader(tempDir.path());
    const VisionPipelineConfigSet result = loader.Load(configPath);

    QVERIFY(!result.usingDefault);
    QCOMPARE(result.pipelines.size(), 1);
    QCOMPARE(result.warnings.size(), 1);
    QVERIFY(result.warnings.first().contains(QStringLiteral("duplicated")));
}

void VisionConfigLoaderTest::relativePathUsesConfiguredProjectRoot()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    VisionConfigLoader loader(tempDir.path());

    QCOMPARE(loader.ResolvePath(QStringLiteral("config/vision.json")),
             QDir(tempDir.path()).absoluteFilePath(QStringLiteral("config/vision.json")));
}

QTEST_APPLESS_MAIN(VisionConfigLoaderTest)

#include "test_vision_config_loader.moc"
