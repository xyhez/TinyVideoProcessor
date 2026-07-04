#include "videoframescheduler.h"

#include "tiny_log/Logging.h"

#include <QTimer>
#include <QtGlobal>
#include <cmath>
#include <utility>

extern "C" {
#include <libavutil/avutil.h>
}

static const int SYNC_THRESHOLD_MS = 40;	///< 视频同步阈值，超过该毫秒差值时触发追赶或等待策略

namespace {
constexpr double kSpeedEpsilon = 0.001;	///< 播放速度浮点比较容差，避免 1.0 等档位因精度误差判断失败
constexpr int kMinPreviewFrameRateLimitFps = 1;	///< 预览降帧目标帧率下限，防止目标间隔过大导致画面长期不刷新
constexpr int kMaxPreviewFrameRateLimitFps = 120;	///< 预览降帧目标帧率上限，防止异常参数让调度间隔过小
constexpr double kPreviewFrameRateEpsilonMs = 0.5;	///< 预览降帧 PTS 间隔判断容差，降低毫秒级舍入误差导致的误丢帧

bool IsSupportedSpeed(double speed)
{
    return std::fabs(speed - 0.5) < kSpeedEpsilon
        || std::fabs(speed - 1.0) < kSpeedEpsilon
        || std::fabs(speed - 1.5) < kSpeedEpsilon
        || std::fabs(speed - 2.0) < kSpeedEpsilon;
}
}

VideoFrameScheduler::VideoFrameScheduler(QObject* parent)
    : QObject(parent)
{
}

VideoFrameScheduler::~VideoFrameScheduler()
{
    Stop();
    SetFrameSink(nullptr);
}

void VideoFrameScheduler::Initialize(std::shared_ptr<SyncQueue<AVFramePtr>> frameQueue,
                                     std::shared_ptr<SyncManager> syncManager,
                                     double frameRate,
                                     double videoTimeBase)
{
    frameQueue_ = frameQueue;
    syncManager_ = syncManager;
    videoTimeBase_ = videoTimeBase;

    if (frameRate > 0.0 && frameRate < 120.0) {
        frameIntervalMs_ = qRound(1000.0 / frameRate);
    }
}

void VideoFrameScheduler::SetFrameSink(IVideoFrameSink* frameSink)
{
    frameSink_ = frameSink;
}

void VideoFrameScheduler::Start()
{
    if (running_.load()) {
        return;
    }

    running_.store(true);
    hasPreviewPresentedPts_ = false;
    rendered_.store(0);
    dropped_.store(0);
    QTimer::singleShot(0, this, &VideoFrameScheduler::Tick);
}

void VideoFrameScheduler::Stop()
{
    running_.store(false);
    if (frameQueue_) {
        frameQueue_->WakeAll();
    }
}

void VideoFrameScheduler::Pause()
{
    Stop();
}

void VideoFrameScheduler::SetSpeed(double speed)
{
    if (!std::isfinite(speed) || !IsSupportedSpeed(speed)) {
        LOG_WARN_STREAM() << "VideoFrameScheduler rejected unsupported speed:" << speed;
        return;
    }

    speed_.store(speed);
}

void VideoFrameScheduler::SetPreviewFrameRateReductionEnabled(bool enabled, int targetFps)
{
    const int clampedTargetFps = qBound(kMinPreviewFrameRateLimitFps,
                                       targetFps,
                                       kMaxPreviewFrameRateLimitFps);
    previewFrameRateReductionEnabled_.store(enabled);
    previewFrameIntervalMs_.store(qMax(1, qRound(1000.0 / clampedTargetFps)));
    hasPreviewPresentedPts_ = false;
}

void VideoFrameScheduler::ClearOutputFrame()
{
    hasPreviewPresentedPts_ = false;
    if (frameSink_) {
        frameSink_->ClearFrame();
    }
}

void VideoFrameScheduler::Tick()
{
    if (!CanScheduleFrame()) {
        return;
    }

    const double speedSnapshot = speed_.load();
    const int scaledInterval = CalculateScaledFrameInterval(speedSnapshot);

    FrameTimingSnapshot nextFrame;
    if (!PeekNextFrameTiming(nextFrame)) {
        ScheduleNextFrame(scaledInterval);
        return;
    }

    const SYNCTYPE syncType = syncManager_->SyncType();
    int waitMs = 0;
    const FrameTimingDecision timingDecision = EvaluateFrameTiming(nextFrame, syncType, waitMs);
    if (timingDecision == FrameTimingDecision::Wait) {
        ScheduleNextFrame(waitMs);
        return;
    }
    if (timingDecision == FrameTimingDecision::Drop) {
        DropLateFrame();
        ScheduleNextFrame(0);
        return;
    }

    if (ShouldDropForPreviewFrameRateLimit(nextFrame, speedSnapshot)) {
        DropPreviewRateLimitedFrame(syncType);
        ScheduleNextFrame(0);
        return;
    }

    AVFramePtr newFrame;
    if (!PopDisplayableFrame(newFrame)) {
        ScheduleNextFrame(scaledInterval);
        return;
    }

    RefreshVideoClockIfNeeded(newFrame.get(), syncType);
    PresentDisplayableFrame(std::move(newFrame));
    ScheduleNextFrame(scaledInterval);
}

bool VideoFrameScheduler::CanScheduleFrame() const
{
    return running_.load() && frameQueue_ && syncManager_ && frameSink_;
}

int VideoFrameScheduler::CalculateScaledFrameInterval(double speed) const
{
    return qMax(1, qRound(frameIntervalMs_ / speed));
}

bool VideoFrameScheduler::PeekNextFrameTiming(FrameTimingSnapshot& snapshot)
{
    snapshot = {};
    if (!frameQueue_) {
        return false;
    }

    bool hasFrame = false;
    const bool visited = frameQueue_->VisitFront([&snapshot, &hasFrame](const AVFramePtr& frame) {
        if (!frame) {
            return;
        }

        snapshot.pts = frame->pts;
        hasFrame = true;
    });

    return visited && hasFrame;
}

VideoFrameScheduler::FrameTimingDecision VideoFrameScheduler::EvaluateFrameTiming(
    const FrameTimingSnapshot& snapshot,
    SYNCTYPE syncType,
    int& waitMs) const
{
    waitMs = 0;
    if (syncType != SYNCTYPE::AUDIO || snapshot.pts == AV_NOPTS_VALUE) {
        return FrameTimingDecision::Display;
    }

    const double videoPts = snapshot.pts * videoTimeBase_ * 1000.0;
    const double audioPts = syncManager_->GetClock() - 20;
    const double diff = videoPts - audioPts;

    if (diff > SYNC_THRESHOLD_MS) {
        waitMs = qMin(qRound(diff), 30);
        return FrameTimingDecision::Wait;
    }

    if (diff < -SYNC_THRESHOLD_MS) {
        return FrameTimingDecision::Drop;
    }

    return FrameTimingDecision::Display;
}

bool VideoFrameScheduler::DropLateFrame()
{
    AVFramePtr drop;
    if (!frameQueue_ || !frameQueue_->TryPop(drop) || !drop) {
        return false;
    }

    dropped_.fetch_add(1);
    return true;
}

bool VideoFrameScheduler::ShouldDropForPreviewFrameRateLimit(
    const FrameTimingSnapshot& snapshot,
    double speed) const
{
    if (!previewFrameRateReductionEnabled_.load()) {
        return false;
    }

    if (!hasPreviewPresentedPts_ || snapshot.pts == AV_NOPTS_VALUE || videoTimeBase_ <= 0.0) {
        return false;
    }

    const double videoPtsMs = snapshot.pts * videoTimeBase_ * 1000.0;
    const double speedFactor = speed > 0.0 ? speed : 1.0;
    const double targetIntervalMs = previewFrameIntervalMs_.load() * speedFactor;
    return videoPtsMs - lastPreviewPresentedPtsMs_ < targetIntervalMs - kPreviewFrameRateEpsilonMs;
}

bool VideoFrameScheduler::DropPreviewRateLimitedFrame(SYNCTYPE syncType)
{
    AVFramePtr drop;
    if (!PopDisplayableFrame(drop)) {
        return false;
    }

    RefreshVideoClockIfNeeded(drop.get(), syncType);
    dropped_.fetch_add(1);
    return true;
}

bool VideoFrameScheduler::PopDisplayableFrame(AVFramePtr& frame)
{
    frame.reset();
    return frameQueue_ && frameQueue_->TryPop(frame) && frame;
}

void VideoFrameScheduler::RefreshVideoClockIfNeeded(const AVFrame* frame, SYNCTYPE syncType)
{
    if (syncType == SYNCTYPE::VIDEO &&
        frame &&
        frame->pts != AV_NOPTS_VALUE &&
        videoTimeBase_ > 0.0) {
        const double videoPts = frame->pts * videoTimeBase_ * 1000.0;
        syncManager_->RefreshClock(videoPts);
    }
}

void VideoFrameScheduler::RememberPreviewPresentedFrame(const AVFrame* frame)
{
    if (!frame || frame->pts == AV_NOPTS_VALUE || videoTimeBase_ <= 0.0) {
        return;
    }

    lastPreviewPresentedPtsMs_ = frame->pts * videoTimeBase_ * 1000.0;
    hasPreviewPresentedPts_ = true;
}

void VideoFrameScheduler::PresentDisplayableFrame(AVFramePtr frame)
{
    if (!frame) {
        return;
    }

    if (!frameSink_) {
        return;
    }

    RememberPreviewPresentedFrame(frame.get());
    frameSink_->PresentFrame(std::move(frame));
    rendered_.fetch_add(1);
}

void VideoFrameScheduler::ScheduleNextFrame(int delayMs)
{
    QTimer::singleShot(delayMs, this, &VideoFrameScheduler::Tick);
}
