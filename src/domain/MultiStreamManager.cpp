#include "MultiStreamManager.h"

#include <algorithm>
#include <utility>

MultiStreamManager::MultiStreamManager(QObject* parent)
    : QObject(parent) {
}

MultiStreamManager::StreamId MultiStreamManager::AddStream(std::shared_ptr<IMediaSource> mediaSource,
                                                           QWidget* rendererContainer) {
    if (!mediaSource) {
        emit StreamErrorOccurred(InvalidStreamId, "Media source is null");
        return InvalidStreamId;
    }

    if (!mediaSource->Open()) {
        mediaSource->Close();
        emit StreamErrorOccurred(InvalidStreamId, "Open media source failed");
        return InvalidStreamId;
    }

    auto pipeline = std::make_unique<MediaPipeline>();
    if (!pipeline->Initialize(mediaSource, rendererContainer)) {
        pipeline->Shutdown();
        mediaSource->Close();
        emit StreamErrorOccurred(InvalidStreamId, "Initialize media pipeline failed");
        return InvalidStreamId;
    }

    const StreamId id = nextStreamId_++;
    StreamSlot slot;
    slot.mediaSource = std::move(mediaSource);
    slot.rendererContainer = rendererContainer;
    slot.pipeline = std::move(pipeline);

    ConnectPipelineSignals(id, slot.pipeline.get());
    streams_.emplace(id, std::move(slot));

    if (StreamHasAudio(id)) {
        if (activeAudioStreamId_ == InvalidStreamId) {
            activeAudioStreamId_ = id;
            ApplyAudioSettingsToStream(activeAudioStreamId_);
            emit ActiveAudioStreamChanged(activeAudioStreamId_);
        } else {
            SetStreamMuted(id, true);
        }
    }

    emit StreamAdded(id);
    if (activeStreamId_ == InvalidStreamId) {
        activeStreamId_ = id;
        emit ActiveStreamChanged(activeStreamId_);
    }

    return id;
}

bool MultiStreamManager::RemoveStream(StreamId id) {
    auto it = streams_.find(id);
    if (it == streams_.end()) {
        return false;
    }

    if (it->second.pipeline) {
        it->second.pipeline->Shutdown();
    }
    streams_.erase(it);

    emit StreamRemoved(id);

    if (activeStreamId_ == id) {
        activeStreamId_ = SelectFallbackStream();
        emit ActiveStreamChanged(activeStreamId_);
    }

    if (activeAudioStreamId_ == id) {
        activeAudioStreamId_ = SelectFallbackAudioStream();
        if (activeAudioStreamId_ != InvalidStreamId) {
            ApplyAudioSettingsToStream(activeAudioStreamId_);
        }
        emit ActiveAudioStreamChanged(activeAudioStreamId_);
    }

    return true;
}

bool MultiStreamManager::HasStream(StreamId id) const {
    return streams_.find(id) != streams_.end();
}

QList<MultiStreamManager::StreamId> MultiStreamManager::StreamIds() const {
    QList<StreamId> streamIds;
    for (const auto& stream : streams_) {
        streamIds.append(stream.first);
    }

    return streamIds;
}

MultiStreamManager::StreamId MultiStreamManager::ActiveStream() const {
    return activeStreamId_;
}

bool MultiStreamManager::SetActiveStream(StreamId id) {
    if (id != InvalidStreamId && !HasStream(id)) {
        return false;
    }

    if (activeStreamId_ == id) {
        return true;
    }

    activeStreamId_ = id;
    emit ActiveStreamChanged(activeStreamId_);
    return true;
}

double MultiStreamManager::ActiveStreamPosition() const {
    const MediaPipeline* pipeline = FindPipeline(activeStreamId_);
    if (!pipeline) {
        return 0.0;
    }

    return pipeline->Position();
}

double MultiStreamManager::ActiveStreamDuration() const {
    const MediaPipeline* pipeline = FindPipeline(activeStreamId_);
    if (!pipeline) {
        return 0.0;
    }

    return pipeline->Duration();
}

MultiStreamManager::StreamId MultiStreamManager::ActiveAudioStream() const {
    return activeAudioStreamId_;
}

bool MultiStreamManager::SetActiveAudioStream(StreamId id) {
    if (id != InvalidStreamId && !StreamHasAudio(id)) {
        return false;
    }

    if (activeAudioStreamId_ == id) {
        return true;
    }

    const StreamId previousAudioStreamId = activeAudioStreamId_;
    activeAudioStreamId_ = id;

    if (previousAudioStreamId != InvalidStreamId && previousAudioStreamId != activeAudioStreamId_) {
        SetStreamMuted(previousAudioStreamId, true);
    }

    if (activeAudioStreamId_ != InvalidStreamId) {
        ApplyAudioSettingsToStream(activeAudioStreamId_);
    }

    emit ActiveAudioStreamChanged(activeAudioStreamId_);
    return true;
}

void MultiStreamManager::SetAudioVolume(double volume) {
    audioVolume_ = std::clamp(volume, 0.0, 1.0);
    if (activeAudioStreamId_ != InvalidStreamId) {
        SetStreamVolume(activeAudioStreamId_, audioVolume_);
    }
}

double MultiStreamManager::AudioVolume() const {
    return audioVolume_;
}

void MultiStreamManager::SetAudioMuted(bool muted) {
    audioMuted_ = muted;
    if (activeAudioStreamId_ != InvalidStreamId) {
        SetStreamMuted(activeAudioStreamId_, audioMuted_);
    }
}

bool MultiStreamManager::IsAudioMuted() const {
    return audioMuted_;
}

bool MultiStreamManager::IsActiveStreamSeekable() const {
    if (activeStreamId_ == InvalidStreamId) {
        return false;
    }

    auto it = streams_.find(activeStreamId_);
    if (it == streams_.end() || !it->second.mediaSource) {
        return false;
    }

    return it->second.mediaSource->IsSeekable();
}

bool MultiStreamManager::IsActiveStreamRealtime() const {
    if (activeStreamId_ == InvalidStreamId) {
        return false;
    }

    auto it = streams_.find(activeStreamId_);
    if (it == streams_.end() || !it->second.mediaSource) {
        return false;
    }

    return it->second.mediaSource->IsRealtime();
}

bool MultiStreamManager::HasActiveStreamDuration() const {
    if (activeStreamId_ == InvalidStreamId) {
        return false;
    }

    auto it = streams_.find(activeStreamId_);
    if (it == streams_.end() || !it->second.mediaSource) {
        return false;
    }

    return it->second.mediaSource->HasDuration();
}

void MultiStreamManager::ShutdownAll() {
    if (streams_.empty()
        && activeStreamId_ == InvalidStreamId
        && activeAudioStreamId_ == InvalidStreamId) {
        return;
    }

    QList<StreamId> removedStreamIds;
    for (auto& stream : streams_) {
        removedStreamIds.append(stream.first);
        if (stream.second.pipeline) {
            stream.second.pipeline->Shutdown();
        }
    }
    streams_.clear();

    for (StreamId id : removedStreamIds) {
        emit StreamRemoved(id);
    }

    if (activeStreamId_ != InvalidStreamId) {
        activeStreamId_ = InvalidStreamId;
        emit ActiveStreamChanged(activeStreamId_);
    }

    if (activeAudioStreamId_ != InvalidStreamId) {
        activeAudioStreamId_ = InvalidStreamId;
        emit ActiveAudioStreamChanged(activeAudioStreamId_);
    }
}

void MultiStreamManager::StartAll() {
    for (auto& stream : streams_) {
        if (stream.second.pipeline) {
            stream.second.pipeline->Start();
        }
    }
}

void MultiStreamManager::PauseAll() {
    for (auto& stream : streams_) {
        if (stream.second.pipeline) {
            stream.second.pipeline->Pause();
        }
    }
}

void MultiStreamManager::StopAll() {
    for (auto& stream : streams_) {
        if (stream.second.pipeline) {
            stream.second.pipeline->Stop();
        }
    }
}

bool MultiStreamManager::StartStream(StreamId id) {
    MediaPipeline* pipeline = FindPipeline(id);
    if (!pipeline) {
        return false;
    }

    pipeline->Start();
    return true;
}

bool MultiStreamManager::PauseStream(StreamId id) {
    MediaPipeline* pipeline = FindPipeline(id);
    if (!pipeline) {
        return false;
    }

    pipeline->Pause();
    return true;
}

bool MultiStreamManager::StopStream(StreamId id) {
    MediaPipeline* pipeline = FindPipeline(id);
    if (!pipeline) {
        return false;
    }

    pipeline->Stop();
    return true;
}

bool MultiStreamManager::SeekStreamTo(StreamId id, double seconds) {
    MediaPipeline* pipeline = FindPipeline(id);
    if (!pipeline) {
        return false;
    }

    return pipeline->SeekTo(seconds);
}

bool MultiStreamManager::SeekStreamRelative(StreamId id, double seconds) {
    MediaPipeline* pipeline = FindPipeline(id);
    if (!pipeline) {
        return false;
    }

    return pipeline->SeekRelative(seconds);
}

bool MultiStreamManager::SetStreamPlaybackSpeed(StreamId id, double speed) {
    MediaPipeline* pipeline = FindPipeline(id);
    if (!pipeline) {
        return false;
    }

    pipeline->SetPlaybackSpeed(speed);
    return true;
}

bool MultiStreamManager::SetStreamVolume(StreamId id, double volume) {
    MediaPipeline* pipeline = FindPipeline(id);
    if (!pipeline) {
        return false;
    }

    pipeline->SetVolume(volume);
    return true;
}

bool MultiStreamManager::SetStreamMuted(StreamId id, bool muted) {
    MediaPipeline* pipeline = FindPipeline(id);
    if (!pipeline) {
        return false;
    }

    if (!muted && StreamHasAudio(id) && id != activeAudioStreamId_) {
        return false;
    }

    pipeline->SetMuted(muted);
    return true;
}

MultiStreamManager::StreamId MultiStreamManager::SelectFallbackStream() const {
    if (streams_.empty()) {
        return InvalidStreamId;
    }

    return streams_.begin()->first;
}

MultiStreamManager::StreamId MultiStreamManager::SelectFallbackAudioStream() const {
    for (const auto& stream : streams_) {
        if (StreamHasAudio(stream.first)) {
            return stream.first;
        }
    }

    return InvalidStreamId;
}

bool MultiStreamManager::StreamHasAudio(StreamId id) const {
    auto it = streams_.find(id);
    if (it == streams_.end() || !it->second.mediaSource) {
        return false;
    }

    return it->second.mediaSource->GetSourceInfo().hasAudio_;
}

void MultiStreamManager::ApplyAudioSettingsToStream(StreamId id) {
    SetStreamVolume(id, audioVolume_);
    SetStreamMuted(id, audioMuted_);
}

MediaPipeline* MultiStreamManager::FindPipeline(StreamId id) {
    auto it = streams_.find(id);
    if (it == streams_.end()) {
        return nullptr;
    }

    return it->second.pipeline.get();
}

const MediaPipeline* MultiStreamManager::FindPipeline(StreamId id) const {
    auto it = streams_.find(id);
    if (it == streams_.end()) {
        return nullptr;
    }

    return it->second.pipeline.get();
}

void MultiStreamManager::ConnectPipelineSignals(StreamId id, MediaPipeline* pipeline) {
    if (!pipeline) {
        return;
    }

    connect(pipeline, &MediaPipeline::PositionChanged, this,
            [this, id](double seconds) {
                emit StreamPositionChanged(id, seconds);
            });

    connect(pipeline, &MediaPipeline::DurationChanged, this,
            [this, id](double seconds) {
                emit StreamDurationChanged(id, seconds);
            });

    connect(pipeline, &MediaPipeline::PlaybackFinished, this,
            [this, id]() {
                emit StreamPlaybackFinished(id);
            });

    connect(pipeline, &MediaPipeline::VolumeChanged, this,
            [this, id](double volume) {
                emit StreamVolumeChanged(id, volume);
            });

    connect(pipeline, &MediaPipeline::MuteChanged, this,
            [this, id](bool muted) {
                emit StreamMuteChanged(id, muted);
            });

    connect(pipeline, &MediaPipeline::ErrorOccurred, this,
            [this, id](const QString& message) {
                emit StreamErrorOccurred(id, message);
            });
}
