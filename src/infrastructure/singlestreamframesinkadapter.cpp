#include "singlestreamframesinkadapter.h"

extern "C" {
#include <libavutil/frame.h>
}

SingleStreamFrameSinkAdapter::SingleStreamFrameSinkAdapter(
    MultiViewRenderer* renderer,
    MultiViewRenderer::StreamId streamId)
    : renderer_(renderer)
    , streamId_(streamId)
{
}

void SingleStreamFrameSinkAdapter::PresentFrame(AVFrame* frame)
{
    // MultiViewRenderer 的真实接帧接口将在后续任务落地。
    // 当前阶段必须消费 IVideoFrameSink 转移过来的所有权，防止 AVFrame 泄漏。
    av_frame_free(&frame);
}

void SingleStreamFrameSinkAdapter::ClearFrame()
{
    // 空壳阶段 MultiViewRenderer 还没有 current frame 状态。
    // 后续接入 ClearFrame(streamId) 后再转发到 renderer_。
}

MultiViewRenderer* SingleStreamFrameSinkAdapter::Renderer() const
{
    return renderer_.data();
}

MultiViewRenderer::StreamId SingleStreamFrameSinkAdapter::StreamIdValue() const
{
    return streamId_;
}
