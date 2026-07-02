//
// Created by adminstrator on 2026/6/5.
//

#include "MediaPlayController.h"
#include "tiny_log/Logging.h"
#include "domain/MultiStreamManager.h"
#include "domain/NetworkMediaSource.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {
constexpr double kSeekStepSeconds = 10.0;
constexpr double kSpeedEpsilon = 0.001;
constexpr double kVolumeStep = 0.05;
constexpr double kVolumeEpsilon = 0.0001;
constexpr int kPreviewFrameRateLimitFps = 15;

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
    const StreamId streamId = AddStream(mediaSource, rendererContainer);
    if (streamId == InvalidStreamId) {
        LOG_CRITICAL("Failed to initialize media stream");
        SetState(PlaybackState::Stopped);
        return false;
    }

    if (!SetActiveStream(streamId)) {
        LOG_CRITICAL("Failed to set active stream");
        RemoveStream(streamId);
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
    return SwitchSourceWithFrameSinkFactory(mediaSource, rendererContainer, autoStart, {});
}

bool MediaPlayController::SwitchSourceWithFrameSinkFactory(
    std::shared_ptr<IMediaSource> mediaSource,
    QWidget* rendererContainer,
    bool autoStart,
    FrameSinkFactory frameSinkFactory) {
    if (!mediaSource) {
        LOG_WARN("SwitchSource failed: media source is null");
        return false;
    }

    Stop();
    multiStreamManager_->ShutdownAll();
    SetState(PlaybackState::Stopped);

    const StreamId streamId =
        AddStreamWithFrameSinkFactory(mediaSource, rendererContainer, std::move(frameSinkFactory));
    if (streamId == InvalidStreamId) {
        LOG_WARN("SwitchSource failed: initialize new media source failed");
        SetState(PlaybackState::Stopped);
        emit DurationChanged(0.0);
        emit PositionChanged(0.0);
        return false;
    }

    if (!SetActiveStream(streamId)) {
        LOG_WARN("SwitchSource failed: set active stream failed");
        RemoveStream(streamId);
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

MediaPlayController::StreamId MediaPlayController::AddStream(std::shared_ptr<IMediaSource> mediaSource,
                                                             QWidget* rendererContainer)
{
    return AddStreamWithFrameSinkFactory(mediaSource, rendererContainer, {});
}

MediaPlayController::StreamId MediaPlayController::AddStreamWithFrameSinkFactory(
    std::shared_ptr<IMediaSource> mediaSource,
    QWidget* rendererContainer,
    FrameSinkFactory frameSinkFactory)
{
    if (!multiStreamManager_) {
        LOG_WARN("AddStream failed: multi stream manager is not initialized");
        return InvalidStreamId;
    }

    if (!mediaSource) {
        LOG_WARN("AddStream failed: media source is null");
        return InvalidStreamId;
    }

    const MultiStreamManager::StreamId streamId =
        multiStreamManager_->AddStream(mediaSource, rendererContainer, std::move(frameSinkFactory));
    if (streamId == MultiStreamManager::InvalidStreamId) {
        return InvalidStreamId;
    }

    multiStreamManager_->SetStreamPlaybackSpeed(streamId, playbackSpeed_);
    multiStreamManager_->SetStreamPreviewFrameRateReductionEnabled(
        streamId,
        previewFrameRateReductionEnabled_,
        kPreviewFrameRateLimitFps);
    multiStreamManager_->SetActiveStreamImageProcessingEnabled(imageProcessingEnabled_);

    return streamId;
}

MediaPlayController::StreamId MediaPlayController::AddNetworkStream(
    const NetworkOpenOptions& options,
    QWidget* rendererContainer)
{
    return AddNetworkStreamWithFrameSinkFactory(options, rendererContainer, {});
}

MediaPlayController::StreamId MediaPlayController::AddNetworkStreamWithFrameSinkFactory(
    const NetworkOpenOptions& options,
    QWidget* rendererContainer,
    FrameSinkFactory frameSinkFactory)
{
    auto mediaSource = std::make_shared<NetworkMediaSource>(options);
    return AddStreamWithFrameSinkFactory(
        mediaSource,
        rendererContainer,
        std::move(frameSinkFactory));
}

bool MediaPlayController::RemoveStream(StreamId id)
{
    if (!multiStreamManager_) {
        return false;
    }

    const bool removed = multiStreamManager_->RemoveStream(id);
    if (!removed) {
        return false;
    }

    if (multiStreamManager_->StreamIds().isEmpty()) {
        SetState(PlaybackState::Stopped);
        emit DurationChanged(0.0);
        emit PositionChanged(0.0);
    }

    return true;
}

QList<MediaPlayController::StreamId> MediaPlayController::StreamIds() const
{
    return multiStreamManager_ ? multiStreamManager_->StreamIds() : QList<StreamId>();
}

MediaPlayController::StreamId MediaPlayController::ActiveStream() const
{
    return multiStreamManager_ ? multiStreamManager_->ActiveStream() : InvalidStreamId;
}

bool MediaPlayController::SetActiveStream(StreamId id)
{
    if (!multiStreamManager_) {
        return false;
    }

    const bool changed = multiStreamManager_->SetActiveStream(id);
    if (!changed) {
        return false;
    }

    ApplyUserSettingsToActiveStream();
    emit DurationChanged(Duration());
    emit PositionChanged(Position());

    return true;
}

MediaPlayController::StreamId MediaPlayController::ActiveAudioStream() const
{
    return multiStreamManager_ ? multiStreamManager_->ActiveAudioStream() : InvalidStreamId;
}

bool MediaPlayController::SetActiveAudioStream(StreamId id)
{
    if (!multiStreamManager_) {
        return false;
    }

    const bool changed = multiStreamManager_->SetActiveAudioStream(id);
    if (!changed) {
        return false;
    }

    emit VolumeChanged(volume_);
    emit MuteChanged(muted_);

    return true;
}

bool MediaPlayController::StreamHasAudio(StreamId id) const
{
    return multiStreamManager_ ? multiStreamManager_->StreamHasAudio(id) : false;
}

bool MediaPlayController::IsActiveStreamSeekable() const
{
    return multiStreamManager_ ? multiStreamManager_->IsActiveStreamSeekable() : false;
}

bool MediaPlayController::HasActiveStreamDuration() const
{
    return multiStreamManager_ ? multiStreamManager_->HasActiveStreamDuration() : false;
}

bool MediaPlayController::IsActiveStreamRealtime() const
{
    return multiStreamManager_ ? multiStreamManager_->IsActiveStreamRealtime() : false;
}

void MediaPlayController::StartAll()
{
    if (!multiStreamManager_ || multiStreamManager_->StreamIds().isEmpty()) {
        LOG_WARN("StartAll failed: no media streams");
        return;
    }

    multiStreamManager_->StartAll();
    SetState(PlaybackState::Playing);
}

void MediaPlayController::PauseAll()
{
    if (!multiStreamManager_ || multiStreamManager_->StreamIds().isEmpty()) {
        LOG_WARN("PauseAll failed: no media streams");
        return;
    }

    multiStreamManager_->PauseAll();
    SetState(PlaybackState::Paused);
}

void MediaPlayController::StopAll()
{
    if (!multiStreamManager_) {
        SetState(PlaybackState::Stopped);
        return;
    }

    multiStreamManager_->StopAll();
    SetState(PlaybackState::Stopped);
}

void MediaPlayController::ConnectManagerSignals()
{
    if (!multiStreamManager_) {
        return;
    }

    connect(multiStreamManager_.get(), &MultiStreamManager::StreamAdded,
            this, [this](MultiStreamManager::StreamId id) {
                emit StreamAdded(id);
            });

    connect(multiStreamManager_.get(), &MultiStreamManager::StreamRemoved,
            this, [this](MultiStreamManager::StreamId id) {
                emit StreamRemoved(id);
            });

    connect(multiStreamManager_.get(), &MultiStreamManager::ActiveStreamChanged,
            this, [this](MultiStreamManager::StreamId id) {
                ApplyUserSettingsToActiveStream();
                emit ActiveStreamChanged(id);
                emit DurationChanged(Duration());
                emit PositionChanged(Position());
            });

    connect(multiStreamManager_.get(), &MultiStreamManager::ActiveAudioStreamChanged,
            this, [this](MultiStreamManager::StreamId id) {
                emit ActiveAudioStreamChanged(id);
                emit VolumeChanged(volume_);
                emit MuteChanged(muted_);
            });

    connect(multiStreamManager_.get(), &MultiStreamManager::StreamErrorOccurred,
            this, [this](MultiStreamManager::StreamId id, const QString& message) {
                if (id != MultiStreamManager::InvalidStreamId
                    && id == multiStreamManager_->ActiveStream()) {
                    SetState(PlaybackState::Stopped);
                }
                emit StreamErrorOccurred(id, message);
            });

    connect(multiStreamManager_.get(), &MultiStreamManager::StreamDurationChanged,
            this, [this](MultiStreamManager::StreamId id, double seconds) {
                emit StreamDurationChanged(id, seconds);
                if (id == multiStreamManager_->ActiveStream()) {
                    emit DurationChanged(seconds);
                }
            });

    connect(multiStreamManager_.get(), &MultiStreamManager::StreamPositionChanged,
            this, [this](MultiStreamManager::StreamId id, double seconds) {
                emit StreamPositionChanged(id, seconds);
                if (id == multiStreamManager_->ActiveStream()) {
                    emit PositionChanged(seconds);
                }
            });

    connect(multiStreamManager_.get(), &MultiStreamManager::StreamPlaybackFinished,
            this, [this](MultiStreamManager::StreamId id) {
                emit StreamPlaybackFinished(id);
                if (id == multiStreamManager_->ActiveStream()) {
                    SetState(PlaybackState::Stopped);
                    emit PlaybackFinished();
                }
            });

    connect(multiStreamManager_.get(), &MultiStreamManager::StreamVolumeChanged,
            this, [this](MultiStreamManager::StreamId id, double volume) {
                emit StreamVolumeChanged(id, volume);
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
                emit StreamMuteChanged(id, muted);
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

    multiStreamManager_->SetActiveStreamImageProcessingEnabled(imageProcessingEnabled_);
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

void MediaPlayController::SetPreviewFrameRateReductionEnabled(bool enabled) {
    if (previewFrameRateReductionEnabled_ == enabled) {
        return;
    }

    previewFrameRateReductionEnabled_ = enabled;
    if (multiStreamManager_) {
        multiStreamManager_->SetPreviewFrameRateReductionEnabled(
            previewFrameRateReductionEnabled_,
            kPreviewFrameRateLimitFps);
    }

    emit PreviewFrameRateReductionChanged(previewFrameRateReductionEnabled_);
}

bool MediaPlayController::IsPreviewFrameRateReductionEnabled() const {
    return previewFrameRateReductionEnabled_;
}

void MediaPlayController::SetImageProcessingEnabled(bool enabled) {
    if (imageProcessingEnabled_ == enabled) {
        return;
    }

    imageProcessingEnabled_ = enabled;
    if (multiStreamManager_) {
        multiStreamManager_->SetActiveStreamImageProcessingEnabled(imageProcessingEnabled_);
    }

    emit ImageProcessingEnabledChanged(imageProcessingEnabled_);
}

bool MediaPlayController::IsImageProcessingEnabled() const {
    return imageProcessingEnabled_;
}

void MediaPlayController::SetPlaybackSpeed(double speed) {
    if (!std::isfinite(speed) || !IsSupportedPlaybackSpeed(speed)) {
        LOG_WARN("Unsupported playback speed:", speed);
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
        LOG_WARN("Start failed: playback pipeline is not initialized");
        return;
    }

    if (!multiStreamManager_->StartStream(multiStreamManager_->ActiveStream())) {
        LOG_WARN("Start failed: active stream is invalid");
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
        LOG_WARN("Pause failed: playback pipeline is not initialized");
        return;
    }

    if (!multiStreamManager_->PauseStream(multiStreamManager_->ActiveStream())) {
        LOG_WARN("Pause failed: active stream is invalid");
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
        LOG_WARN("Seek failed: playback pipeline is not initialized");
        return;
    }

    // 检查 active stream 是否支持 seek
    if (!multiStreamManager_->IsActiveStreamSeekable()) {
        LOG_WARN("Seek rejected: active stream is not seekable");
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
        LOG_WARN("Seek failed: playback pipeline is not initialized");
        return;
    }

    // 检查 active stream 是否支持 seek
    if (!multiStreamManager_->IsActiveStreamSeekable()) {
        LOG_WARN("Seek rejected: active stream is not seekable");
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

