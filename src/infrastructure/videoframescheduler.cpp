#include "videoframescheduler.h"

#include "core/AppLog.h"

#include <QTimer>
#include <QtGlobal>
#include <cmath>

namespace {
constexpr double kSpeedEpsilon = 0.001;

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

void VideoFrameScheduler::Initialize(std::shared_ptr<SyncQueue<AVFrame*>> frameQueue,
                                     std::shared_ptr<syncmanager> syncManager,
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
    if (running_) {
        return;
    }

    running_ = true;
    rendered_ = 0;
    dropped_ = 0;
    QTimer::singleShot(0, this, &VideoFrameScheduler::Tick);
}

void VideoFrameScheduler::Stop()
{
    running_ = false;
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
        APP_LOG_WARN_STREAM() << "VideoFrameScheduler rejected unsupported speed:" << speed;
        return;
    }

    speed_.store(speed);
}

void VideoFrameScheduler::ClearOutputFrame()
{
    if (frameSink_) {
        frameSink_->ClearFrame();
    }
}

void VideoFrameScheduler::Tick()
{
    if (!CanScheduleFrame()) {
        return;
    }

    // 调度逻辑将在后续步骤从 VideoRender::RenderFrame 迁移到这里。
}

bool VideoFrameScheduler::CanScheduleFrame() const
{
    return running_ && frameQueue_ && syncManager_ && frameSink_;
}
