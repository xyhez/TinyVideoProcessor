#include "visionresultstore.h"

#include <utility>

void VisionResultStore::SetResults(int streamId,
                                   std::shared_ptr<const VisionResultSet> results,
                                   int64_t pts,
                                   int64_t producedAtMs)
{
    {
        std::lock_guard<std::mutex> locker(mutex_);
        if (!results) {
            results_.erase(streamId);
        } else {
            Snapshot snapshot;
            snapshot.results = std::move(results);
            snapshot.pts = pts;
            snapshot.producedAtMs = producedAtMs;
            results_[streamId] = std::move(snapshot);
        }
    }

    NotifyUpdated();
}

VisionResultStore::Snapshot VisionResultStore::Get(int streamId) const
{
    std::lock_guard<std::mutex> locker(mutex_);

    Snapshot snapshot;
    snapshot.overlayEnabled = overlayEnabled_;

    const auto it = results_.find(streamId);
    if (it != results_.end()) {
        snapshot.results = it->second.results;
        snapshot.pts = it->second.pts;
        snapshot.producedAtMs = it->second.producedAtMs;
    }

    return snapshot;
}

void VisionResultStore::Clear(int streamId)
{
    bool changed = false;
    {
        std::lock_guard<std::mutex> locker(mutex_);
        changed = results_.erase(streamId) > 0;
    }

    if (changed) {
        NotifyUpdated();
    }
}

void VisionResultStore::ClearAll()
{
    bool changed = false;
    {
        std::lock_guard<std::mutex> locker(mutex_);
        changed = !results_.empty();
        results_.clear();
    }

    if (changed) {
        NotifyUpdated();
    }
}

void VisionResultStore::SetOverlayEnabled(bool enabled)
{
    bool changed = false;
    {
        std::lock_guard<std::mutex> locker(mutex_);
        if (overlayEnabled_ != enabled) {
            overlayEnabled_ = enabled;
            changed = true;
        }
    }

    if (changed) {
        NotifyUpdated();
    }
}

bool VisionResultStore::IsOverlayEnabled() const
{
    std::lock_guard<std::mutex> locker(mutex_);
    return overlayEnabled_;
}

void VisionResultStore::SetUpdateCallback(std::function<void()> callback)
{
    std::lock_guard<std::mutex> locker(mutex_);
    updateCallback_ = std::move(callback);
}

void VisionResultStore::NotifyUpdated()
{
    std::function<void()> callback;
    {
        std::lock_guard<std::mutex> locker(mutex_);
        callback = updateCallback_;
    }

    if (callback) {
        callback();
    }
}
