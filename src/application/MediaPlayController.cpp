//
// Created by adminstrator on 2026/6/5.
//

#include "MediaPlayController.h"
#include <QDebug>
#include <qlayout.h>
#include <cmath>

namespace {
constexpr double kSeekStepSeconds = 10.0;
constexpr double kSpeedEpsilon = 0.001;

bool IsSupportedPlaybackSpeed(double speed)
{
    return std::fabs(speed - 0.5) < kSpeedEpsilon
        || std::fabs(speed - 1.0) < kSpeedEpsilon
        || std::fabs(speed - 1.5) < kSpeedEpsilon
        || std::fabs(speed - 2.0) < kSpeedEpsilon;
}
}

MediaPlayController::MediaPlayController(QObject *parent)
    : QObject(parent)
{

}

MediaPlayController::~MediaPlayController() {
    ReleaseResources();
}

bool MediaPlayController::Initialize(std::shared_ptr<IMediaSource> mediaSource, QWidget *rendererContainer) {
    // 1.初始化队列
    APacket_ = std::make_shared<SyncQueue<AVPacket*>>(kPacketQueueMax, packetDeleter);
    VPacket_ = std::make_shared<SyncQueue<AVPacket*>>(kPacketQueueMax, packetDeleter);
    AFrame_ = std::make_shared<SyncQueue<AVFrame*>>(kFrameQueueMax,  frameDeleter);
    VFrame_ = std::make_shared<SyncQueue<AVFrame*>>(kFrameQueueMax,  frameDeleter);

    //2.初始化媒体源
    if (!mediaSource) {
        qCritical() << "Media source is null";
        return false;
    }
    mediaSource_ = mediaSource;
    if (!mediaSource_->Open()) {
        qCritical() << "Failed to open media source";
        return false;
    }

    //3.基层对象构造
    demuxer_ = std::make_unique<demuxer>(mediaSource_,APacket_,VPacket_,AFrame_,VFrame_);
    const Fileinfo& sourceInfo = mediaSource_->GetSourceInfo();
    Adecoder_ = std::make_unique<decoder>();
    Vdecoder_ = std::make_unique<decoder>();
    audioplay_ = std::make_unique<AudioPlayer>();
    videoRender_ = new VideoRender(rendererContainer);
    syncManager_ = std::make_shared<syncmanager>();
    positionTimer_ = std::make_unique<QTimer>();
    positionTimer_->setInterval(200); // 间隔200毫秒

    // 4.子线程初始化
    demuxerTH_ = std::make_unique<QThread>();
    AdecoderTH_ = std::make_unique<QThread>();
    VdecoderTH_ = std::make_unique<QThread>();
    audioplayTH_ = std::make_unique<QThread>();

    // 5.将对象初始化并移动到对应的线程
    demuxer_->moveToThread(demuxerTH_.get());
    Adecoder_->InitDecoder(APacket_,AFrame_,demuxer_->ReAcoderPar(),demuxer_->ReAtimebase());
    Adecoder_->moveToThread(AdecoderTH_.get());
    Vdecoder_->InitDecoder(VPacket_,VFrame_,demuxer_->ReVcoderPar(),demuxer_->ReVtimebase());
    Vdecoder_->moveToThread(VdecoderTH_.get());
    audioplay_->InitAudioPlay(demuxer_->ReAcoderPar(), AFrame_, syncManager_, sourceInfo.A_timeBase);
    audioplay_->moveToThread(audioplayTH_.get());
    videoRender_->InitVideoRender(VFrame_, syncManager_, sourceInfo.FrameRate, sourceInfo.V_timeBase);
    audioplay_->SetSpeed(playbackSpeed_);
    videoRender_->SetSpeed(playbackSpeed_);

    //6.设置videorender
    if (rendererContainer) {
        rendererContainer->setLayout(new QVBoxLayout());
        rendererContainer->layout()->addWidget(videoRender_);
    }

    //7.链接信号槽
    CreateConnects();

    emit DurationChanged(Duration());
    emit PositionChanged(Position());

    return true;
}

void MediaPlayController::CreateConnects() {
    connect(demuxerTH_.get(),  &QThread::started,
        demuxer_.get(), &demuxer::StartDemuxer);

    connect(AdecoderTH_.get(), &QThread::started,
        Adecoder_.get(), &decoder::StartDecoder);

    connect(VdecoderTH_.get(), &QThread::started,
        Vdecoder_.get(), &decoder::StartDecoder);

    connect(audioplayTH_.get(), &QThread::started,
        audioplay_.get(), &AudioPlayer::StartAudioPlay);

    connect(positionTimer_.get(), &QTimer::timeout, this , [this]() {
        emit PositionChanged(Position());
    });
}

void MediaPlayController::ReleaseResources() {
    // 确保所有工作循环已停止（防 Stop() 未调）
    if (APacket_) { APacket_->RequestStop(); }
    if (VPacket_) { VPacket_->RequestStop(); }
    if (AFrame_)  { AFrame_->RequestStop();  }
    if (VFrame_)  { VFrame_->RequestStop();  }

    if (demuxer_)   demuxer_->Stop();
    if (Adecoder_)  Adecoder_->Stop();
    if (Vdecoder_)  Vdecoder_->Stop();
    if (audioplay_) audioplay_->Stop();
    if (videoRender_) videoRender_->StopRendering();

    auto stopThread = [](std::unique_ptr<QThread>& th) {
        if (th) {
            th->quit();
            th->wait();
            th.reset();
        }
    };
    stopThread(demuxerTH_);
    stopThread(AdecoderTH_);
    stopThread(VdecoderTH_);
    stopThread(audioplayTH_);

    demuxer_.reset();
    Adecoder_.reset();
    Vdecoder_.reset();
    audioplay_.reset();
    // videoRender_ 由 Qt 父子树管理，无需 delete

    APacket_.reset();
    VPacket_.reset();
    AFrame_.reset();
    VFrame_.reset();
    syncManager_.reset();
    mediaSource_.reset();
    positionTimer_.reset();
}

MediaPlayController::PlaybackState MediaPlayController::State() const {
    return state_;
}

double MediaPlayController::Duration() const {
    return mediaSource_ ? static_cast<double>(mediaSource_->GetSourceInfo().duration_):0.0;
}

double MediaPlayController::Position() const {
    return syncManager_ ? syncManager_->GetClock() / 1000.0 : 0.0;
}

double MediaPlayController::PlaybackSpeed() const {
    return playbackSpeed_;
}

void MediaPlayController::SetPlaybackSpeed(double speed) {
    if (!std::isfinite(speed) || !IsSupportedPlaybackSpeed(speed)) {
        qWarning() << "Unsupported playback speed:" << speed;
        return;
    }

    if (std::fabs(playbackSpeed_ - speed) < kSpeedEpsilon) {
        return;
    }

    playbackSpeed_ = speed;
    if (audioplay_) {
        audioplay_->SetSpeed(playbackSpeed_);
    }
    if (videoRender_) {
        videoRender_->SetSpeed(playbackSpeed_);
    }

    emit PlaybackSpeedChanged(playbackSpeed_);
}

void MediaPlayController::SetState(PlaybackState state) {
    if (state_ == state) {
        return;
    }

    state_ = state;
    emit StateChanged(state_);
}

void MediaPlayController::Start() {
    if (!demuxer_ || !Adecoder_ || !Vdecoder_ || !audioplay_ || !videoRender_ || !positionTimer_) {
        qWarning() << "Start failed: playback pipeline is not initialized";
        return;
    }

    SetState(PlaybackState::Playing);

    positionTimer_->start();
    emit PositionChanged(Position());
    emit DurationChanged(Duration());

    demuxerTH_->start();
    AdecoderTH_->start();
    VdecoderTH_->start();
    audioplayTH_->start();

    demuxer_->Start();
    Adecoder_->Start();
    Vdecoder_->Start();
    audioplay_->Start();
    videoRender_->StartRendering();
}

void MediaPlayController::Stop() {
    if (!demuxer_ || !Adecoder_ || !Vdecoder_ || !syncManager_ || !positionTimer_) {
        SetState(PlaybackState::Stopped);
        return;
    }

    if (state_ != PlaybackState::Stopped && !SuspendPipelineAndWait()) {
        qWarning() << "Stop failed: playback pipeline suspend failed";
        return;
    }

    emit PositionChanged(0.0);
    positionTimer_->stop();

    ResetPipelineToStart();
    SetState(PlaybackState::Stopped);
}

void MediaPlayController::Pause() {
    if (!demuxer_ || !Adecoder_ || !Vdecoder_ || !audioplay_ || !videoRender_ || !positionTimer_) {
        qWarning() << "Pause failed: playback pipeline is not initialized";
        return;
    }

    SetState(PlaybackState::Paused);
    emit PositionChanged(Position());
    demuxer_->Pause();
    Adecoder_->Pause();
    Vdecoder_->Pause();
    audioplay_->Pause();
    videoRender_->Pause();
    positionTimer_->stop();
}

void MediaPlayController::VolumeUp() {
    audioplay_->VolumeUp();
}

void MediaPlayController::VolumeDown() {
    audioplay_->VolumeDown();
}

void MediaPlayController::SeekTo(double seconds) {
    if (!demuxer_ || !Adecoder_ || !Vdecoder_ || !syncManager_) {
        qWarning() << "Seek failed: playback pipeline is not initialized";
        return;
    }

    double targetSeconds = seconds;
    if (targetSeconds < 0.0) {
        targetSeconds = 0.0;
    }

    if (mediaSource_) {
        const int durationSeconds = mediaSource_->GetSourceInfo().duration_;
        if (durationSeconds > 0 && targetSeconds > durationSeconds) {
            targetSeconds = durationSeconds;
        }
    }

    const PlaybackState previousState = State();

    if (previousState != PlaybackState::Stopped && !SuspendPipelineAndWait()) {
        qWarning() << "Seek failed: playback pipeline suspend failed";
        RestorePlaybackState(previousState);
        return;
    }

    ClearQueues();

    const int seekResult = demuxer_->SeekTo(targetSeconds);
    if (seekResult != 0) {
        qWarning() << "Seek failed: demuxer rejected target" << targetSeconds;
        RestorePlaybackState(previousState);
        return;
    }

    FlushDecoders();
    Vdecoder_->SetDiscardBefore(targetSeconds);
    ClearQueues();
    syncManager_->SeekTo(targetSeconds * 1000.0);

    emit PositionChanged(Position());

    RestorePlaybackState(previousState);
}

void MediaPlayController::SeekRelative(double seconds) {
    if (!demuxer_) {
        qWarning() << "Seek failed: demuxer is not initialized";
        return;
    }

    SeekTo(demuxer_->Position() + seconds);
}

void MediaPlayController::Rewind() {
    SeekRelative(-kSeekStepSeconds);

}

void MediaPlayController::Forward() {
    SeekRelative(kSeekStepSeconds);
}

bool MediaPlayController::SuspendPipelineAndWait() {
    SetState(PlaybackState::Paused);

    if (videoRender_) {
        videoRender_->Pause();
    }
    if (audioplay_) {
        audioplay_->Pause();
    }

    bool suspended = true;
    if (demuxer_ && demuxerTH_ && demuxerTH_->isRunning()) {
        suspended = demuxer_->SuspendAndWait() == 0 && suspended;
    }
    if (Adecoder_ && AdecoderTH_ && AdecoderTH_->isRunning()) {
        suspended = Adecoder_->SuspendAndWait() == 0 && suspended;
    }
    if (Vdecoder_ && VdecoderTH_ && VdecoderTH_->isRunning()) {
        suspended = Vdecoder_->SuspendAndWait() == 0 && suspended;
    }

    return suspended;
}

void MediaPlayController::RestorePlaybackState(PlaybackState previousState) {
    if (previousState == PlaybackState::Playing) {
        Start();
        return;
    }

    SetState(previousState);
}

void MediaPlayController::ResetPipelineToStart() {
    if (videoRender_) {
        videoRender_->StopRendering();
    }
    if (audioplay_) {
        audioplay_->Pause();
    }

    ClearQueues();
    demuxer_->SeekTo(0.0);
    FlushDecoders();
    ClearQueues();
    syncManager_->SeekTo(0.0);
}

void MediaPlayController::ClearQueues() {
    if (APacket_) { APacket_->ClearAndWake(); }
    if (VPacket_) { VPacket_->ClearAndWake(); }
    if (AFrame_)  { AFrame_->ClearAndWake();  }
    if (VFrame_)  { VFrame_->ClearAndWake();  }
}

void MediaPlayController::FlushDecoders() {
    if (Adecoder_) {
        Adecoder_->ClearDiscardBefore();
        Adecoder_->Flush();
    }
    if (Vdecoder_) {
        Vdecoder_->ClearDiscardBefore();
        Vdecoder_->Flush();
    }
}

