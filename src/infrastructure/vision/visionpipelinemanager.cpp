#include "visionpipelinemanager.h"

#include "tiny_log/Logging.h"

VisionPipelineManager::VisionPipelineManager(QObject* parent)
    : QObject(parent)
{
}

VisionPipelineManager::~VisionPipelineManager()
{
    // 遍历所有 pipeline,仍处于 started 状态的显式 Shutdown,
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
    QList<QString> ids;
    ids.reserve(registrationOrder_.size());
    for (const QString& id : registrationOrder_) {
        ids.append(id);
    }
    return ids;
}

IVisionPipeline* VisionPipelineManager::FindPipeline(const QString& id) const
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
    // 幂等:目标与当前一致,直接返回
    if (id == activeId_) {
        return true;
    }

    // 关闭视觉分析
    if (id.isEmpty()) {
        IVisionPipeline* oldPipeline = FindPipeline(activeId_);
        if (oldPipeline && oldPipeline->IsStarted()) {
            LOG_DEBUG_STREAM() << "VisionPipelineManager: shutdown active pipeline" << activeId_;
            oldPipeline->Shutdown();
        }
        activeId_.clear();
        emit ActivePipelineChanged(activeId_);
        return true;
    }

    // 目标 id 必须已注册
    IVisionPipeline* targetPipeline = FindPipeline(id);
    if (!targetPipeline) {
        LOG_WARN_STREAM() << "VisionPipelineManager: pipeline not registered" << id
                          << ", keep current active" << activeId_;
        return false;
    }

    // 先 Shutdown 旧 active,避免两条 pipeline 同时占用 GPU / 模型资源
    IVisionPipeline* oldPipeline = FindPipeline(activeId_);
    if (oldPipeline && oldPipeline->IsStarted()) {
        LOG_DEBUG_STREAM() << "VisionPipelineManager: shutdown old active" << activeId_;
        oldPipeline->Shutdown();
    }

    // Startup 新 target
    const QString previousId = activeId_;
    activeId_.clear();

    if (!targetPipeline->Startup()) {
        LOG_WARN_STREAM() << "VisionPipelineManager: pipeline startup failed" << id;
        const QString errorMessage = QString("Pipeline '%1' startup failed").arg(id);
        emit PipelineStartupFailed(id, errorMessage);
        if (!previousId.isEmpty()) {
            // 之前有 active,现在被切走,通知 UI 状态变化
            emit ActivePipelineChanged(activeId_);
        }
        return false;
    }

    activeId_ = id;
    LOG_DEBUG_STREAM() << "VisionPipelineManager: active pipeline changed"
                      << previousId << "->" << activeId_;
    emit ActivePipelineChanged(activeId_);
    return true;
}

QString VisionPipelineManager::ActivePipelineId() const
{
    return activeId_;
}

IVisionPipeline* VisionPipelineManager::ActivePipeline() const
{
    return FindPipeline(activeId_);
}

bool VisionPipelineManager::RunActive(const VisionFrame& input, VisionResultSet& out)
{
    IVisionPipeline* active = ActivePipeline();
    if (!active) {
        return false;
    }
    if (!active->IsStarted()) {
        LOG_WARN_STREAM() << "VisionPipelineManager: active pipeline not started" << activeId_;
        return false;
    }
    return active->Run(input, out);
}
