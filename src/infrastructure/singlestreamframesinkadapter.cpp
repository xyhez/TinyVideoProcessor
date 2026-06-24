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
    if (!frame) {
        return;
    }

    MultiViewRenderer* renderer = renderer_.data();
    if (!renderer || streamId_ == MultiViewRenderer::InvalidStreamId) {
        av_frame_free(&frame);
        return;
    }

    renderer->PresentFrame(streamId_, frame);
}

void SingleStreamFrameSinkAdapter::ClearFrame()
{
    MultiViewRenderer* renderer = renderer_.data();
    if (!renderer || streamId_ == MultiViewRenderer::InvalidStreamId) {
        return;
    }

    renderer->ClearFrame(streamId_);
}

MultiViewRenderer* SingleStreamFrameSinkAdapter::Renderer() const
{
    return renderer_.data();
}

MultiViewRenderer::StreamId SingleStreamFrameSinkAdapter::StreamIdValue() const
{
    return streamId_;
}
