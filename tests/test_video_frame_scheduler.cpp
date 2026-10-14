#include <QtTest>

#include <limits>

#include "infrastructure/sync/videoframescheduler.h"

namespace {
class CountingVideoFrameSink final : public IVideoFrameSink {
public:
    void PresentFrame(AVFramePtr frame) override
    {
        if (frame) {
            ++presentedFrameCount;
        }
    }

    void ClearFrame() override
    {
    }

    int presentedFrameCount = 0;
};
}

class VideoFrameSchedulerTest : public QObject {
    Q_OBJECT

private slots:
    void initializesKnownFrameRates();
    void fallsBackForInvalidFrameRates();
    void subtractsProcessingTimeFromFrameBudget();
    void compensatesTimerOvershootWithAbsoluteDeadline();
    void reanchorsAfterSevereDelay();
    void resetsCadenceWhenPlaybackParametersChange();
    void restartCancelsPendingTick();
    void emitsPlaybackDrainedOnceForEmptyEndOfStream();
};

void VideoFrameSchedulerTest::initializesKnownFrameRates()
{
    VideoFrameScheduler scheduler;

    scheduler.Initialize({}, {}, 30.0, 1.0 / 1000.0);
    QCOMPARE(scheduler.frameIntervalMs_, 33);

    scheduler.Initialize({}, {}, 120.0, 1.0 / 1000.0);
    QCOMPARE(scheduler.frameIntervalMs_, 8);

    scheduler.Initialize({}, {}, 240.0, 1.0 / 1000.0);
    QCOMPARE(scheduler.frameIntervalMs_, 4);
}

void VideoFrameSchedulerTest::fallsBackForInvalidFrameRates()
{
    VideoFrameScheduler scheduler;

    scheduler.Initialize({}, {}, 120.0, 1.0 / 1000.0);
    QCOMPARE(scheduler.frameIntervalMs_, 8);

    scheduler.Initialize({}, {}, 0.0, 1.0 / 1000.0);
    QCOMPARE(scheduler.frameIntervalMs_, 33);

    scheduler.Initialize({}, {}, std::numeric_limits<double>::quiet_NaN(), 1.0 / 1000.0);
    QCOMPARE(scheduler.frameIntervalMs_, 33);

    scheduler.Initialize({}, {}, std::numeric_limits<double>::infinity(), 1.0 / 1000.0);
    QCOMPARE(scheduler.frameIntervalMs_, 33);
}

void VideoFrameSchedulerTest::subtractsProcessingTimeFromFrameBudget()
{
    VideoFrameScheduler scheduler;
    scheduler.ResetCadenceTiming();

    const int delayMs = scheduler.CalculateCadenceDelayMs(
        33,
        0,
        8 * 1000 * 1000);

    QCOMPARE(delayMs, 25);
}

void VideoFrameSchedulerTest::compensatesTimerOvershootWithAbsoluteDeadline()
{
    VideoFrameScheduler scheduler;
    scheduler.ResetCadenceTiming();

    QCOMPARE(scheduler.CalculateCadenceDelayMs(
                 33,
                 0,
                 8 * 1000 * 1000),
             25);

    QCOMPARE(scheduler.CalculateCadenceDelayMs(
                 33,
                 35 * 1000 * 1000,
                 43 * 1000 * 1000),
             23);
}

void VideoFrameSchedulerTest::reanchorsAfterSevereDelay()
{
    VideoFrameScheduler scheduler;
    scheduler.ResetCadenceTiming();

    QCOMPARE(scheduler.CalculateCadenceDelayMs(
                 8,
                 0,
                 10 * 1000 * 1000),
             0);

    QCOMPARE(scheduler.CalculateCadenceDelayMs(
                 8,
                 10 * 1000 * 1000,
                 30 * 1000 * 1000),
             0);

    QCOMPARE(scheduler.CalculateCadenceDelayMs(
                 8,
                 30 * 1000 * 1000,
                 32 * 1000 * 1000),
             6);
}

void VideoFrameSchedulerTest::resetsCadenceWhenPlaybackParametersChange()
{
    VideoFrameScheduler scheduler;

    scheduler.hasCadenceDeadline_ = true;
    scheduler.nextFrameDeadlineNs_ = 123;
    scheduler.SetSpeed(1.5);
    QVERIFY(!scheduler.hasCadenceDeadline_);
    QCOMPARE(scheduler.nextFrameDeadlineNs_, qint64{0});

    scheduler.hasCadenceDeadline_ = true;
    scheduler.nextFrameDeadlineNs_ = 456;
    scheduler.ClearOutputFrame();
    QVERIFY(!scheduler.hasCadenceDeadline_);
    QCOMPARE(scheduler.nextFrameDeadlineNs_, qint64{0});

    scheduler.hasCadenceDeadline_ = true;
    scheduler.nextFrameDeadlineNs_ = 789;
    scheduler.Stop();
    QVERIFY(!scheduler.hasCadenceDeadline_);
    QCOMPARE(scheduler.nextFrameDeadlineNs_, qint64{0});
}

void VideoFrameSchedulerTest::restartCancelsPendingTick()
{
    auto frameQueue = std::make_shared<SyncQueue<AVFramePtr>>(2);
    auto syncManager = std::make_shared<SyncManager>();
    syncManager->SetSyncType(SYNCTYPE::VIDEO);

    AVFramePtr firstFrame = MakeAVFrame();
    AVFramePtr secondFrame = MakeAVFrame();
    QVERIFY(firstFrame);
    QVERIFY(secondFrame);
    firstFrame->pts = 0;
    secondFrame->pts = 1;
    QVERIFY(frameQueue->Push(std::move(firstFrame)));
    QVERIFY(frameQueue->Push(std::move(secondFrame)));

    CountingVideoFrameSink frameSink;
    VideoFrameScheduler scheduler;
    scheduler.Initialize(frameQueue, syncManager, 1.0, 1.0 / 1000.0);
    scheduler.SetFrameSink(&frameSink);

    scheduler.Start();
    scheduler.Stop();
    scheduler.Start();

    QTRY_VERIFY_WITH_TIMEOUT(frameSink.presentedFrameCount > 0, 200);
    QTest::qWait(50);
    QCOMPARE(frameSink.presentedFrameCount, 1);
    QCOMPARE(frameQueue->Size(), size_t{1});

    scheduler.Stop();
}

void VideoFrameSchedulerTest::emitsPlaybackDrainedOnceForEmptyEndOfStream()
{
    auto frameQueue = std::make_shared<SyncQueue<AVFramePtr>>(1);
    auto syncManager = std::make_shared<SyncManager>();
    CountingVideoFrameSink frameSink;
    VideoFrameScheduler scheduler;
    scheduler.Initialize(frameQueue, syncManager, 30.0, 1.0 / 1000.0);
    scheduler.SetFrameSink(&frameSink);

    QSignalSpy drainedSpy(&scheduler, &VideoFrameScheduler::VideoPlaybackDrained);

    scheduler.Start();
    scheduler.Stop();
    QCoreApplication::processEvents();
    QCOMPARE(drainedSpy.count(), 0);

    frameQueue->SignalEndOfStream();
    scheduler.Start();
    QTRY_COMPARE_WITH_TIMEOUT(drainedSpy.count(), 1, 200);
    QVERIFY(!scheduler.running_.load());
    QVERIFY(!scheduler.tickTimer_.isActive());

    QTest::qWait(50);
    QCOMPARE(drainedSpy.count(), 1);
    scheduler.Stop();
    QCOMPARE(drainedSpy.count(), 1);
}

QTEST_GUILESS_MAIN(VideoFrameSchedulerTest)

#include "test_video_frame_scheduler.moc"
