#include "visionprocessingframesink.h"

#include <utility>

#include "infrastructure/vision/visionanalysisservice.h"
#include "infrastructure/vision/visionpipelinemanager.h"
#include "infrastructure/vision/visionresultstore.h"

VisionProcessingFrameSink::VisionProcessingFrameSink(
    VisionPipelineManager* manager,
    VisionAnalysisService* analysisService,
    std::shared_ptr<VisionResultStore> resultStore,
    IVideoFrameSink* downstream)
    : manager_(manager)
    , analysisService_(analysisService)
    , resultStore_(std::move(resultStore))
    , downstreamSink_(downstream)
{
}

void VisionProcessingFrameSink::PresentFrame(AVFramePtr frame)
{
    if (!downstreamSink_) {
        return;
    }

    if (!isActive_.load() || !analysisService_ || !manager_
        || !manager_->HasActivePipeline() || !frame) {
        downstreamSink_->PresentFrame(std::move(frame));
        return;
    }

    // 只投递引用，不拷贝像素；分析服务忙时会自动丢弃旧分析帧。
    analysisService_->SubmitFrame(streamId_.load(), frame.get());

    // 显示帧立即透传，视觉推理/overlay 不再占用显示路径。
    downstreamSink_->PresentFrame(std::move(frame));
}

void VisionProcessingFrameSink::ClearFrame()
{
    if (downstreamSink_) {
        downstreamSink_->ClearFrame();
    }
}

void VisionProcessingFrameSink::SetStreamId(int streamId)
{
    streamId_.store(streamId);
}

void VisionProcessingFrameSink::SetActive(bool active)
{
    isActive_.store(active);
    if (!active && resultStore_ && streamId_.load() >= 0) {
        resultStore_->Clear(streamId_.load());
    }
}

bool VisionProcessingFrameSink::IsActive() const
{
    return isActive_.load();
}

void VisionProcessingFrameSink::SetOverlayEnabled(bool enabled)
{
    overlayEnabled_.store(enabled);
    if (resultStore_) {
        resultStore_->SetOverlayEnabled(enabled);
    }
}

bool VisionProcessingFrameSink::IsOverlayEnabled() const
{
    return overlayEnabled_.load();
}

void VisionProcessingFrameSink::SetDownstreamSink(IVideoFrameSink* sink)
{
    downstreamSink_ = sink;
}
