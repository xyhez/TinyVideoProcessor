//
// Created by adminstrator on 2026/6/5.
//

#include "MediaPlayController.h"
#include "domain/MediaPipeline.h"

#include <algorithm>
#include <QDebug>
#include <cmath>

namespace {
constexpr double kSeekStepSeconds = 10.0;
constexpr double kSpeedEpsilon = 0.001;
constexpr double kVolumeStep = 0.05;
constexpr double kVolumeEpsilon = 0.0001;

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
    , pipeline_(std::make_unique<MediaPipeline>())
{
    ConnectPipelineSignals();
}

MediaPlayController::~MediaPlayController() {
    Shutdown();
}

bool MediaPlayController::Initialize(std::shared_ptr<IMediaSource> mediaSource, QWidget *rendererContainer) {
    if (!mediaSource) {
        qCritical() << "Media source is null";
        return false;
    }

    if (!mediaSource->Open()) {
        qCritical() << "Failed to open media source";
        return false;
    }

    if (!pipeline_->Initialize(mediaSource, rendererContainer)) {
        pipeline_->Shutdown();
        SetState(PlaybackState::Stopped);
        return false;
    }

    ApplyUserSettingsToPipeline();
    emit DurationChanged(Duration());
    emit PositionChanged(Position());

    return true;
}

bool MediaPlayController::SwitchSource(std::shared_ptr<IMediaSource> mediaSource,
                                       QWidget* rendererContainer,
                                       bool autoStart) {
    if (!mediaSource) {
        qWarning() << "SwitchSource failed: media source is null";
        return false;
    }

    Stop();
    pipeline_->Shutdown();
    SetState(PlaybackState::Stopped);

    if (!mediaSource->Open()) {
        qWarning() << "SwitchSource failed: open new media source failed";
        SetState(PlaybackState::Stopped);
        emit DurationChanged(0.0);
        emit PositionChanged(0.0);
        return false;
    }

    if (!pipeline_->Initialize(mediaSource, rendererContainer)) {
        qWarning() << "SwitchSource failed: initialize new media source failed";
        pipeline_->Shutdown();
        SetState(PlaybackState::Stopped);
        emit DurationChanged(0.0);
        emit PositionChanged(0.0);
        return false;
    }

    ApplyUserSettingsToPipeline();

    if (autoStart) {
        Start();
    } else {
        emit DurationChanged(Duration());
        emit PositionChanged(Position());
    }

    return true;
}

void MediaPlayController::ConnectPipelineSignals()
{
    if (!pipeline_) {
        return;
    }

    connect(pipeline_.get(), &MediaPipeline::DurationChanged,
            this, &MediaPlayController::DurationChanged);
    connect(pipeline_.get(), &MediaPipeline::PositionChanged,
            this, &MediaPlayController::PositionChanged);

    connect(pipeline_.get(), &MediaPipeline::PlaybackFinished, this, [this]() {
        SetState(PlaybackState::Stopped);
        emit PlaybackFinished();
    });

    connect(pipeline_.get(), &MediaPipeline::VolumeChanged, this, [this](double volume) {
        volume_ = volume;
        emit VolumeChanged(volume_);
    });

    connect(pipeline_.get(), &MediaPipeline::MuteChanged, this, [this](bool muted) {
        muted_ = muted;
        emit MuteChanged(muted_);
    });
}

void MediaPlayController::ApplyUserSettingsToPipeline()
{
    if (!pipeline_) {
        return;
    }

    pipeline_->SetPlaybackSpeed(playbackSpeed_);
    pipeline_->SetVolume(volume_);
    pipeline_->SetMuted(muted_);
}

MediaPlayController::PlaybackState MediaPlayController::State() const {
    return state_;
}

double MediaPlayController::Duration() const {
    return pipeline_ ? pipeline_->Duration() : 0.0;
}

double MediaPlayController::Position() const {
    return pipeline_ ? pipeline_->Position() : 0.0;
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
    if (pipeline_) {
        pipeline_->SetPlaybackSpeed(playbackSpeed_);
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
    if (!pipeline_ || !pipeline_->IsInitialized()) {
        qWarning() << "Start failed: playback pipeline is not initialized";
        return;
    }

    SetState(PlaybackState::Playing);
    pipeline_->Start();
}

void MediaPlayController::Stop() {
    if (!pipeline_) {
        SetState(PlaybackState::Stopped);
        return;
    }

    if (state_ != PlaybackState::Stopped) {
        pipeline_->Stop();
    }

    SetState(PlaybackState::Stopped);
}

void MediaPlayController::Shutdown() {
    if (pipeline_) {
        pipeline_->Shutdown();
    }
    SetState(PlaybackState::Stopped);
}

void MediaPlayController::Pause() {
    if (!pipeline_ || !pipeline_->IsInitialized()) {
        qWarning() << "Pause failed: playback pipeline is not initialized";
        return;
    }

    SetState(PlaybackState::Paused);
    pipeline_->Pause();
}

void MediaPlayController::VolumeUp() {
    SetVolume(volume_ + kVolumeStep);
}

void MediaPlayController::VolumeDown() {
    SetVolume(volume_ - kVolumeStep);
}

void MediaPlayController::SetVolume(double volume) {
    const double clampedVolume = std::clamp(volume, 0.0, 1.0);
    if (std::fabs(volume_ - clampedVolume) < kVolumeEpsilon) {
        return;
    }

    volume_ = clampedVolume;
    if (pipeline_) {
        pipeline_->SetVolume(volume_);
    } else {
        emit VolumeChanged(volume_);
    }
}

double MediaPlayController::Volume() const {
    return volume_;
}

void MediaPlayController::SetMuted(bool muted) {
    if (muted_ == muted) {
        return;
    }

    muted_ = muted;
    if (pipeline_) {
        pipeline_->SetMuted(muted_);
    } else {
        emit MuteChanged(muted_);
    }
}

bool MediaPlayController::IsMuted() const {
    return muted_;
}

void MediaPlayController::SeekTo(double seconds) {
    if (!pipeline_ || !pipeline_->IsInitialized()) {
        qWarning() << "Seek failed: playback pipeline is not initialized";
        return;
    }

    double targetSeconds = seconds;
    if (targetSeconds < 0.0) {
        targetSeconds = 0.0;
    }

    const double durationSeconds = Duration();
    if (durationSeconds > 0.0 && targetSeconds > durationSeconds) {
        targetSeconds = durationSeconds;
    }

    const PlaybackState previousState = State();

    if (!pipeline_->SeekTo(targetSeconds)) {
        RestorePlaybackState(previousState);
        return;
    }

    RestorePlaybackState(previousState);
}

void MediaPlayController::SeekRelative(double seconds) {
    if (!pipeline_ || !pipeline_->IsInitialized()) {
        qWarning() << "Seek failed: playback pipeline is not initialized";
        return;
    }

    const PlaybackState previousState = State();

    if (!pipeline_->SeekRelative(seconds)) {
        RestorePlaybackState(previousState);
        return;
    }

    RestorePlaybackState(previousState);
}

void MediaPlayController::Rewind() {
    SeekRelative(-kSeekStepSeconds);

}

void MediaPlayController::Forward() {
    SeekRelative(kSeekStepSeconds);
}

void MediaPlayController::RestorePlaybackState(PlaybackState previousState) {
    if (previousState == PlaybackState::Playing) {
        Start();
        return;
    }

    SetState(previousState);
}

