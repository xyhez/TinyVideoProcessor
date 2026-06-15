#include "PlaylistManager.h"

#include <QFileInfo>

PlaylistManager::PlaylistManager(QObject* parent)
    : QObject(parent) {
}

int PlaylistManager::Count() const {
    return items_.size();
}

int PlaylistManager::CurrentIndex() const {
    return currentIndex_;
}

QList<PlaylistItem> PlaylistManager::Items() const {
    return items_;
}

std::optional<PlaylistItem> PlaylistManager::CurrentItem() const {
    if (currentIndex_ < 0 || currentIndex_ >= items_.size()) {
        return std::nullopt;
    }

    return items_.at(currentIndex_);
}

void PlaylistManager::AddFile(const QString& filePath) {
    const int previousIndex = currentIndex_;
    if (!AddFileInternal(filePath)) {
        return;
    }

    emit PlaylistChanged();
    if (previousIndex != currentIndex_) {
        emit CurrentIndexChanged(currentIndex_);
    }
}

void PlaylistManager::AddFiles(const QStringList& filePaths) {
    const int previousIndex = currentIndex_;
    bool changed = false;

    for (const QString& filePath : filePaths) {
        changed = AddFileInternal(filePath) || changed;
    }

    if (!changed) {
        return;
    }

    emit PlaylistChanged();
    if (previousIndex != currentIndex_) {
        emit CurrentIndexChanged(currentIndex_);
    }
}

void PlaylistManager::RemoveAt(int index) {
    if (index < 0 || index >= items_.size()) {
        return;
    }

    const int previousIndex = currentIndex_;
    items_.removeAt(index);

    if (items_.isEmpty()) {
        currentIndex_ = -1;
    } else if (index < previousIndex) {
        currentIndex_ = previousIndex - 1;
    } else if (index == previousIndex) {
        currentIndex_ = index >= items_.size() ? items_.size() - 1 : index;
    }

    emit PlaylistChanged();
    if (previousIndex != currentIndex_) {
        emit CurrentIndexChanged(currentIndex_);
    }
}

void PlaylistManager::Clear() {
    if (items_.isEmpty() && currentIndex_ == -1) {
        return;
    }

    const int previousIndex = currentIndex_;
    items_.clear();
    currentIndex_ = -1;

    emit PlaylistChanged();
    if (previousIndex != currentIndex_) {
        emit CurrentIndexChanged(currentIndex_);
    }
}

bool PlaylistManager::SetCurrentIndex(int index) {
    if (index < 0 || index >= items_.size()) {
        return false;
    }

    if (currentIndex_ == index) {
        return true;
    }

    currentIndex_ = index;
    emit CurrentIndexChanged(currentIndex_);
    return true;
}

void PlaylistManager::SetPlayMode(PlayMode mode) {
    if (playMode_ == mode) {
        return;
    }

    playMode_ = mode;
    emit PlayModeChanged(playMode_);
}

PlaylistManager::PlayMode PlaylistManager::GetPlayMode() const {
    return playMode_;
}

bool PlaylistManager::AddFileInternal(const QString& filePath) {
    const QString normalizedPath = NormalizePath(filePath);
    if (normalizedPath.isEmpty()) {
        return false;
    }

    for (const PlaylistItem& item : items_) {
        if (item.filePath == normalizedPath) {
            return false;
        }
    }

    QFileInfo fileInfo(normalizedPath);
    PlaylistItem item;
    item.filePath = normalizedPath;
    item.displayName = fileInfo.fileName().isEmpty() ? normalizedPath : fileInfo.fileName();
    item.durationSeconds = 0.0;

    items_.append(item);
    if (currentIndex_ == -1) {
        currentIndex_ = 0;
    }

    return true;
}

QString PlaylistManager::NormalizePath(const QString& filePath) const {
    const QString trimmedPath = filePath.trimmed();
    if (trimmedPath.isEmpty()) {
        return {};
    }

    QFileInfo fileInfo(trimmedPath);
    const QString canonicalPath = fileInfo.canonicalFilePath();
    if (!canonicalPath.isEmpty()) {
        return canonicalPath;
    }

    return fileInfo.absoluteFilePath();
}
