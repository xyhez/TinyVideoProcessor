#include "singlestreamframesinkadapter.h"

#include <utility>

SingleStreamFrameSinkAdapter::SingleStreamFrameSinkAdapter(
    MultiViewRenderer* renderer,
    MultiViewRenderer::StreamId streamId)
    : renderer_(renderer)
    , streamId_(streamId)
{
}

void SingleStreamFrameSinkAdapter::PresentFrame(AVFramePtr frame)
{
    if (!frame) {
        return;
    }

    MultiViewRenderer* renderer = renderer_.data();
    if (!renderer || streamId_ == MultiViewRenderer::InvalidStreamId) {
        return;
    }

    renderer->PresentFrame(streamId_, std::move(frame));
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
