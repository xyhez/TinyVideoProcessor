//
// Created by adminstrator on 2026/6/5.
//

#include "MediaPlayController.h"
#include "domain/MultiStreamManager.h"

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
    , multiStreamManager_(std::make_unique<MultiStreamManager>())
{
    ConnectManagerSignals();
}

MediaPlayController::~MediaPlayController() {
    Shutdown();
}

bool MediaPlayController::Initialize(std::shared_ptr<IMediaSource> mediaSource, QWidget *rendererContainer) {
    if (!mediaSource) {
        qCritical() << "Media source is null";
        return false;
    }

    const MultiStreamManager::StreamId streamId =
        multiStreamManager_->AddStream(mediaSource, rendererContainer);
    if (streamId == MultiStreamManager::InvalidStreamId) {
        qCritical() << "Failed to initialize media stream";
        SetState(PlaybackState::Stopped);
        return false;
    }

    if (!multiStreamManager_->SetActiveStream(streamId)) {
        qCritical() << "Failed to set active stream";
        multiStreamManager_->RemoveStream(streamId);
        SetState(PlaybackState::Stopped);
        return false;
    }

    ApplyUserSettingsToActiveStream();
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
    multiStreamManager_->ShutdownAll();
    SetState(PlaybackState::Stopped);

    const MultiStreamManager::StreamId streamId =
        multiStreamManager_->AddStream(mediaSource, rendererContainer);
    if (streamId == MultiStreamManager::InvalidStreamId) {
        qWarning() << "SwitchSource failed: initialize new media source failed";
        SetState(PlaybackState::Stopped);
        emit DurationChanged(0.0);
        emit PositionChanged(0.0);
        return false;
    }

    if (!multiStreamManager_->SetActiveStream(streamId)) {
        qWarning() << "SwitchSource failed: set active stream failed";
        multiStreamManager_->RemoveStream(streamId);
        SetState(PlaybackState::Stopped);
        emit DurationChanged(0.0);
        emit PositionChanged(0.0);
        return false;
    }

    ApplyUserSettingsToActiveStream();

    if (autoStart) {
        Start();
    } else {
        emit DurationChanged(Duration());
        emit PositionChanged(Position());
    }

    return true;
}

void MediaPlayController::ConnectManagerSignals()
{
    if (!multiStreamManager_) {
        return;
    }

    connect(multiStreamManager_.get(), &MultiStreamManager::StreamDurationChanged,
            this, [this](MultiStreamManager::StreamId id, double seconds) {
                if (id == multiStreamManager_->ActiveStream()) {
                    emit DurationChanged(seconds);
                }
            });

    connect(multiStreamManager_.get(), &MultiStreamManager::StreamPositionChanged,
            this, [this](MultiStreamManager::StreamId id, double seconds) {
                if (id == multiStreamManager_->ActiveStream()) {
                    emit PositionChanged(seconds);
                }
            });

    connect(multiStreamManager_.get(), &MultiStreamManager::StreamPlaybackFinished,
            this, [this](MultiStreamManager::StreamId id) {
                if (id == multiStreamManager_->ActiveStream()) {
                    SetState(PlaybackState::Stopped);
                    emit PlaybackFinished();
                }
            });

    connect(multiStreamManager_.get(), &MultiStreamManager::StreamVolumeChanged,
            this, [this](MultiStreamManager::StreamId id, double volume) {
                if (id != multiStreamManager_->ActiveAudioStream()) {
                    return;
                }

                if (std::fabs(volume_ - volume) >= kVolumeEpsilon) {
                    volume_ = volume;
                    emit VolumeChanged(volume_);
                }
            });

    connect(multiStreamManager_.get(), &MultiStreamManager::StreamMuteChanged,
            this, [this](MultiStreamManager::StreamId id, bool muted) {
                if (id != multiStreamManager_->ActiveAudioStream()) {
                    return;
                }

                if (muted_ != muted) {
                    muted_ = muted;
                    emit MuteChanged(muted_);
                }
            });
}

void MediaPlayController::ApplyUserSettingsToActiveStream()
{
    if (!multiStreamManager_) {
        return;
    }

    const MultiStreamManager::StreamId activeStream = multiStreamManager_->ActiveStream();
    if (activeStream != MultiStreamManager::InvalidStreamId) {
        multiStreamManager_->SetStreamPlaybackSpeed(activeStream, playbackSpeed_);
    }

    multiStreamManager_->SetAudioVolume(volume_);
    multiStreamManager_->SetAudioMuted(muted_);
}

MediaPlayController::PlaybackState MediaPlayController::State() const {
    return state_;
}

double MediaPlayController::Duration() const {
    return multiStreamManager_ ? multiStreamManager_->ActiveStreamDuration() : 0.0;
}

double MediaPlayController::Position() const {
    return multiStreamManager_ ? multiStreamManager_->ActiveStreamPosition() : 0.0;
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
    if (multiStreamManager_) {
        const MultiStreamManager::StreamId activeStream = multiStreamManager_->ActiveStream();
        if (activeStream != MultiStreamManager::InvalidStreamId) {
            multiStreamManager_->SetStreamPlaybackSpeed(activeStream, playbackSpeed_);
        }
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
    if (!multiStreamManager_
        || multiStreamManager_->ActiveStream() == MultiStreamManager::InvalidStreamId) {
        qWarning() << "Start failed: playback pipeline is not initialized";
        return;
    }

    if (!multiStreamManager_->StartStream(multiStreamManager_->ActiveStream())) {
        qWarning() << "Start failed: active stream is invalid";
        return;
    }

    SetState(PlaybackState::Playing);
}

void MediaPlayController::Stop() {
    if (!multiStreamManager_) {
        SetState(PlaybackState::Stopped);
        return;
    }

    const MultiStreamManager::StreamId activeStream = multiStreamManager_->ActiveStream();
    if (state_ != PlaybackState::Stopped
        && activeStream != MultiStreamManager::InvalidStreamId) {
        multiStreamManager_->StopStream(activeStream);
    }

    SetState(PlaybackState::Stopped);
}

void MediaPlayController::Shutdown() {
    if (multiStreamManager_) {
        multiStreamManager_->ShutdownAll();
    }
    SetState(PlaybackState::Stopped);
}

void MediaPlayController::Pause() {
    if (!multiStreamManager_
        || multiStreamManager_->ActiveStream() == MultiStreamManager::InvalidStreamId) {
        qWarning() << "Pause failed: playback pipeline is not initialized";
        return;
    }

    if (!multiStreamManager_->PauseStream(multiStreamManager_->ActiveStream())) {
        qWarning() << "Pause failed: active stream is invalid";
        return;
    }

    SetState(PlaybackState::Paused);
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
    if (multiStreamManager_) {
        multiStreamManager_->SetAudioVolume(volume_);
    }
    emit VolumeChanged(volume_);
}

double MediaPlayController::Volume() const {
    return volume_;
}

void MediaPlayController::SetMuted(bool muted) {
    if (muted_ == muted) {
        return;
    }

    muted_ = muted;
    if (multiStreamManager_) {
        multiStreamManager_->SetAudioMuted(muted_);
    }
    emit MuteChanged(muted_);
}

bool MediaPlayController::IsMuted() const {
    return muted_;
}

void MediaPlayController::SeekTo(double seconds) {
    if (!multiStreamManager_
        || multiStreamManager_->ActiveStream() == MultiStreamManager::InvalidStreamId) {
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

    if (!multiStreamManager_->SeekStreamTo(multiStreamManager_->ActiveStream(), targetSeconds)) {
        RestorePlaybackState(previousState);
        return;
    }

    RestorePlaybackState(previousState);
}

void MediaPlayController::SeekRelative(double seconds) {
    if (!multiStreamManager_
        || multiStreamManager_->ActiveStream() == MultiStreamManager::InvalidStreamId) {
        qWarning() << "Seek failed: playback pipeline is not initialized";
        return;
    }

    const PlaybackState previousState = State();

    if (!multiStreamManager_->SeekStreamRelative(multiStreamManager_->ActiveStream(), seconds)) {
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

