#include "visionprocessingframesink.h"

#include <utility>

#include <opencv2/core/mat.hpp>

#include "infrastructure/vision/visionpipelinemanager.h"
#include "infrastructure/vision/visiontypes.h"

VisionProcessingFrameSink::VisionProcessingFrameSink(VisionPipelineManager* manager,
                                                     IVideoFrameSink* downstream)
    : manager_(manager)
    , downstreamSink_(downstream)
{
}

void VisionProcessingFrameSink::PresentFrame(AVFramePtr frame)
{
    if (!downstreamSink_) {
        return;
    }

    // 视觉分析关闭 / 无 active pipeline → 透传原帧
    if (!isActive_ || !manager_ || !manager_->ActivePipeline()) {
        downstreamSink_->PresentFrame(std::move(frame));
        return;
    }

    cv::Mat image;
    if (!converter_.ConvertFrameToMat(frame, image)) {
        downstreamSink_->PresentFrame(std::move(frame));
        return;
    }

    VisionFrame vf;
    vf.image = image;
    vf.width = image.cols;
    vf.height = image.rows;
    vf.stride = static_cast<int>(image.step);
    vf.pts = frame ? frame->pts : 0;
    vf.pixelFormat = VisionPixelFormat::BGR24;

    VisionResultSet results;
    if (!manager_->RunActive(vf, results)) {
        downstreamSink_->PresentFrame(std::move(frame));
        return;
    }

    // Overlay 关闭时 Run 依然跑（结果集合供未来订阅者消费），
    // 但不修改画面，直接透传原帧
    if (!overlayEnabled_) {
        downstreamSink_->PresentFrame(std::move(frame));
        return;
    }

    overlayRenderer_.Draw(image, results);

    AVFramePtr processedFrame = converter_.ConvertMatToFrame(image, frame);
    if (!processedFrame) {
        downstreamSink_->PresentFrame(std::move(frame));
        return;
    }

    downstreamSink_->PresentFrame(std::move(processedFrame));
}

void VisionProcessingFrameSink::ClearFrame()
{
    if (downstreamSink_) {
        downstreamSink_->ClearFrame();
    }
}

void VisionProcessingFrameSink::SetActive(bool active)
{
    isActive_ = active;
}

bool VisionProcessingFrameSink::IsActive() const
{
    return isActive_;
}

void VisionProcessingFrameSink::SetOverlayEnabled(bool enabled)
{
    overlayEnabled_ = enabled;
}

bool VisionProcessingFrameSink::IsOverlayEnabled() const
{
    return overlayEnabled_;
}

void VisionProcessingFrameSink::SetDownstreamSink(IVideoFrameSink* sink)
{
    downstreamSink_ = sink;
}
