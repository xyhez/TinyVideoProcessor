#include <QtTest>

#include <QSignalSpy>

#include <memory>

#include "infrastructure/vision/visionpipelinemanager.h"

namespace {
struct PipelineStats {
    int startupCount = 0;
    int shutdownCount = 0;
};

class FakePipeline final : public IVisionPipeline {
public:
    FakePipeline(QString id, std::shared_ptr<PipelineStats> stats, bool startupSucceeds = true)
        : id_(std::move(id))
        , stats_(std::move(stats))
        , startupSucceeds_(startupSucceeds)
    {
    }

    QString Id() const override { return id_; }
    QString DisplayName() const override { return id_; }
    QString Type() const override { return QStringLiteral("fake"); }

    bool Startup() override
    {
        ++stats_->startupCount;
        started_ = startupSucceeds_;
        return startupSucceeds_;
    }

    void Shutdown() override
    {
        ++stats_->shutdownCount;
        started_ = false;
    }

    bool IsStarted() const override { return started_; }

    bool Run(const VisionFrame&, VisionResultSet&) override
    {
        return started_;
    }

private:
    QString id_;
    std::shared_ptr<PipelineStats> stats_;
    bool startupSucceeds_ = true;
    bool started_ = false;
};
}

class VisionPipelineManagerTest : public QObject {
    Q_OBJECT

private slots:
    void registrationPreservesOrderAndRejectsDuplicates();
    void switchingPipelineUpdatesLifecycle();
    void startupFailureLeavesManagerInactive();
    void unknownPipelineKeepsCurrentActive();
};

void VisionPipelineManagerTest::registrationPreservesOrderAndRejectsDuplicates()
{
    VisionPipelineManager manager;
    auto firstStats = std::make_shared<PipelineStats>();
    auto secondStats = std::make_shared<PipelineStats>();

    manager.RegisterPipeline(nullptr);
    manager.RegisterPipeline(std::make_unique<FakePipeline>(QString(), firstStats));
    manager.RegisterPipeline(
        std::make_unique<FakePipeline>(QStringLiteral("first"), firstStats));
    manager.RegisterPipeline(
        std::make_unique<FakePipeline>(QStringLiteral("second"), secondStats));
    manager.RegisterPipeline(
        std::make_unique<FakePipeline>(QStringLiteral("first"), firstStats));

    QCOMPARE(manager.AvailablePipelineIds(),
             QList<QString>({QStringLiteral("first"), QStringLiteral("second")}));
    QVERIFY(manager.FindPipeline(QStringLiteral("first")) != nullptr);
    QVERIFY(manager.FindPipeline(QStringLiteral("missing")) == nullptr);
}

void VisionPipelineManagerTest::switchingPipelineUpdatesLifecycle()
{
    VisionPipelineManager manager;
    auto firstStats = std::make_shared<PipelineStats>();
    auto secondStats = std::make_shared<PipelineStats>();
    manager.RegisterPipeline(
        std::make_unique<FakePipeline>(QStringLiteral("first"), firstStats));
    manager.RegisterPipeline(
        std::make_unique<FakePipeline>(QStringLiteral("second"), secondStats));

    QVERIFY(manager.SetActivePipeline(QStringLiteral("first")));
    QCOMPARE(manager.ActivePipelineId(), QStringLiteral("first"));
    QCOMPARE(firstStats->startupCount, 1);

    QVERIFY(manager.SetActivePipeline(QStringLiteral("second")));
    QCOMPARE(manager.ActivePipelineId(), QStringLiteral("second"));
    QCOMPARE(firstStats->shutdownCount, 1);
    QCOMPARE(secondStats->startupCount, 1);

    QVERIFY(manager.SetActivePipeline(QStringLiteral("second")));
    QCOMPARE(secondStats->startupCount, 1);

    QVERIFY(manager.SetActivePipeline(QString()));
    QVERIFY(manager.ActivePipelineId().isEmpty());
    QCOMPARE(secondStats->shutdownCount, 1);
}

void VisionPipelineManagerTest::startupFailureLeavesManagerInactive()
{
    VisionPipelineManager manager;
    auto goodStats = std::make_shared<PipelineStats>();
    auto badStats = std::make_shared<PipelineStats>();
    manager.RegisterPipeline(
        std::make_unique<FakePipeline>(QStringLiteral("good"), goodStats));
    manager.RegisterPipeline(
        std::make_unique<FakePipeline>(QStringLiteral("bad"), badStats, false));
    QSignalSpy activeSpy(&manager, &VisionPipelineManager::ActivePipelineChanged);
    QSignalSpy failureSpy(&manager, &VisionPipelineManager::PipelineStartupFailed);

    QVERIFY(manager.SetActivePipeline(QStringLiteral("good")));
    QVERIFY(!manager.SetActivePipeline(QStringLiteral("bad")));

    QVERIFY(manager.ActivePipelineId().isEmpty());
    QCOMPARE(goodStats->shutdownCount, 1);
    QCOMPARE(badStats->startupCount, 1);
    QCOMPARE(activeSpy.count(), 2);
    QCOMPARE(failureSpy.count(), 1);
}

void VisionPipelineManagerTest::unknownPipelineKeepsCurrentActive()
{
    VisionPipelineManager manager;
    auto stats = std::make_shared<PipelineStats>();
    manager.RegisterPipeline(
        std::make_unique<FakePipeline>(QStringLiteral("known"), stats));
    QVERIFY(manager.SetActivePipeline(QStringLiteral("known")));

    QVERIFY(!manager.SetActivePipeline(QStringLiteral("missing")));
    QCOMPARE(manager.ActivePipelineId(), QStringLiteral("known"));
    QCOMPARE(stats->shutdownCount, 0);
}

QTEST_APPLESS_MAIN(VisionPipelineManagerTest)

#include "test_vision_pipeline_manager.moc"
