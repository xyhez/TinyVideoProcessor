#include "MultiStreamManager.h"

MultiStreamManager::MultiStreamManager(QObject* parent)
    : QObject(parent) {
}

MultiStreamManager::StreamId MultiStreamManager::AddStream() {
    const StreamId id = nextStreamId_++;
    streamIds_.append(id);

    emit StreamAdded(id);
    if (activeStreamId_ == InvalidStreamId) {
        activeStreamId_ = id;
        emit ActiveStreamChanged(activeStreamId_);
    }

    return id;
}

bool MultiStreamManager::RemoveStream(StreamId id) {
    const int index = streamIds_.indexOf(id);
    if (index < 0) {
        return false;
    }

    streamIds_.removeAt(index);
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
    return streamIds_.contains(id);
}

QList<MultiStreamManager::StreamId> MultiStreamManager::StreamIds() const {
    return streamIds_;
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
    if (streamIds_.isEmpty()
        && activeStreamId_ == InvalidStreamId
        && activeAudioStreamId_ == InvalidStreamId) {
        return;
    }

    const QList<StreamId> removedStreamIds = streamIds_;
    streamIds_.clear();

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
    if (streamIds_.isEmpty()) {
        return InvalidStreamId;
    }

    return streamIds_.first();
}
