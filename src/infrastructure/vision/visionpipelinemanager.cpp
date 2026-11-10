#include "visionpipelinemanager.h"

#include "tiny_log/Logging.h"

#include <QMutexLocker>

VisionPipelineManager::VisionPipelineManager(QObject* parent)
    : QObject(parent)
{
}

VisionPipelineManager::~VisionPipelineManager()
{
    QMutexLocker locker(&mutex_);

    // 遍历所有 pipeline，仍处于 started 状态的显式 Shutdown，
    // 避免 unique_ptr 析构时 pipeline 还持有 GPU / 模型资源。
    for (const QString& id : registrationOrder_) {
        auto it = pipelines_.find(id);
        if (it == pipelines_.end() || !it->second) {
            continue;
        }
        if (it->second->IsStarted()) {
            LOG_DEBUG_STREAM() << "VisionPipelineManager destructor: shutdown pipeline" << id;
            it->second->Shutdown();
        }
    }
}

void VisionPipelineManager::RegisterPipeline(std::unique_ptr<IVisionPipeline> pipeline)
{
    if (!pipeline) {
        LOG_WARN("VisionPipelineManager register: pipeline is null, ignored");
        return;
    }

    const QString id = pipeline->Id();
    if (id.isEmpty()) {
        LOG_WARN("VisionPipelineManager register: pipeline id is empty, ignored");
        return;
    }

    QMutexLocker locker(&mutex_);

    if (pipelines_.count(id) > 0) {
        LOG_WARN_STREAM() << "VisionPipelineManager register: pipeline id already exists"
                          << id << ", ignored";
        return;
    }

    pipelines_.emplace(id, std::move(pipeline));
    registrationOrder_.append(id);
    LOG_DEBUG_STREAM() << "VisionPipelineManager register: pipeline added" << id;
}

QList<QString> VisionPipelineManager::AvailablePipelineIds() const
{
    QMutexLocker locker(&mutex_);

    QList<QString> ids;
    ids.reserve(registrationOrder_.size());
    for (const QString& id : registrationOrder_) {
        ids.append(id);
    }

    return ids;
}

IVisionPipeline* VisionPipelineManager::FindPipeline(const QString& id) const
{
    QMutexLocker locker(&mutex_);
    return FindPipelineLocked(id);
}

IVisionPipeline* VisionPipelineManager::FindPipelineLocked(const QString& id) const
{
    if (id.isEmpty()) {
        return nullptr;
    }

    auto it = pipelines_.find(id);
    if (it == pipelines_.end() || !it->second) {
        return nullptr;
    }
    return it->second.get();
}

bool VisionPipelineManager::SetActivePipeline(const QString& id)
{
    bool startupFailed = false;
    QString errorMessage;
    QString changedId;

    {
        QMutexLocker locker(&mutex_);

        // 幂等：目标与当前一致，直接返回。
        if (id == activeId_) {
            return true;
        }

        // 关闭视觉分析。
        if (id.isEmpty()) {
            IVisionPipeline* oldPipeline = FindPipelineLocked(activeId_);
            if (oldPipeline && oldPipeline->IsStarted()) {
                LOG_DEBUG_STREAM() << "VisionPipelineManager: shutdown active pipeline" << activeId_;
                oldPipeline->Shutdown();
            }
            activeId_.clear();
            hasActivePipeline_.store(false);
            changedId.clear();
        } else {
            // 目标 id 必须已注册。
            IVisionPipeline* targetPipeline = FindPipelineLocked(id);
            if (!targetPipeline) {
                LOG_WARN_STREAM() << "VisionPipelineManager: pipeline not registered" << id
                                  << ", keep current" << activeId_;
                return false;
            }

            // 先 Shutdown 旧 active，避免两条 pipeline 同时占用 GPU / 模型资源。
            IVisionPipeline* oldPipeline = FindPipelineLocked(activeId_);
            if (oldPipeline && oldPipeline->IsStarted()) {
                LOG_DEBUG_STREAM() << "VisionPipelineManager: shutdown old active" << activeId_;
                oldPipeline->Shutdown();
            }

            activeId_.clear();
            hasActivePipeline_.store(false);

            if (!targetPipeline->Startup()) {
                errorMessage = QString("Pipeline '%1' startup failed").arg(id);
                startupFailed = true;
            } else {
                activeId_ = id;
                hasActivePipeline_.store(true);
                changedId = id;
            }
        }
    }

    // 信号在锁外发射，避免 UI 槽回调 Manager 时死锁。
    if (startupFailed) {
        LOG_WARN_STREAM() << "VisionPipelineManager: pipeline startup failed" << id;
        Q_EMIT ActivePipelineChanged(activeId_);
        Q_EMIT PipelineStartupFailed(id, errorMessage);
        return false;
    }

    Q_EMIT ActivePipelineChanged(changedId);
    return true;
}

QString VisionPipelineManager::ActivePipelineId() const
{
    QMutexLocker locker(&mutex_);
    return activeId_;
}

IVisionPipeline* VisionPipelineManager::ActivePipeline() const
{
    QMutexLocker locker(&mutex_);
    return FindPipelineLocked(activeId_);
}

bool VisionPipelineManager::HasActivePipeline() const
{
    // 显示线程高频调用，不能等待推理持有的 mutex。
    return hasActivePipeline_.load();
}

bool VisionPipelineManager::RunActive(const VisionFrame& input, VisionResultSet& out)
{
    QMutexLocker locker(&mutex_);

    IVisionPipeline* active = FindPipelineLocked(activeId_);
    if (!active) {
        return false;
    }
    if (!active->IsStarted()) {
        LOG_WARN_STREAM() << "VisionPipelineManager: active pipeline not started" << activeId_;
        return false;
    }
    return active->Run(input, out);
}
