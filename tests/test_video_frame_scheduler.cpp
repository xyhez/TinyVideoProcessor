#include <QtTest>

#include <limits>

#include "infrastructure/sync/videoframescheduler.h"

class VideoFrameSchedulerTest : public QObject {
    Q_OBJECT

private slots:
    void initializesKnownFrameRates();
    void fallsBackForInvalidFrameRates();
    void subtractsProcessingTimeFromFrameBudget();
    void compensatesTimerOvershootWithAbsoluteDeadline();
    void reanchorsAfterSevereDelay();
    void resetsCadenceWhenPlaybackParametersChange();
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

QTEST_APPLESS_MAIN(VideoFrameSchedulerTest)

#include "test_video_frame_scheduler.moc"
