#include "MultiStreamManager.h"

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
        activeAudioStreamId_ = InvalidStreamId;
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

MultiStreamManager::StreamId MultiStreamManager::ActiveAudioStream() const {
    return activeAudioStreamId_;
}

bool MultiStreamManager::SetActiveAudioStream(StreamId id) {
    if (id != InvalidStreamId && !HasStream(id)) {
        return false;
    }

    if (activeAudioStreamId_ == id) {
        return true;
    }

    activeAudioStreamId_ = id;
    emit ActiveAudioStreamChanged(activeAudioStreamId_);
    return true;
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

MultiStreamManager::StreamId MultiStreamManager::SelectFallbackStream() const {
    if (streams_.empty()) {
        return InvalidStreamId;
    }

    return streams_.begin()->first;
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
