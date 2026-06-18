#include "multiviewrenderer.h"

MultiViewRenderer::MultiViewRenderer(QWidget* parent)
    : QOpenGLWidget(parent) {
    setMinimumSize(320, 240);
}

void MultiViewRenderer::SetLayout(Layout layout) {
    if (layout_ == layout) {
        return;
    }

    layout_ = layout;
    update();
}

MultiViewRenderer::Layout MultiViewRenderer::CurrentLayout() const {
    return layout_;
}

void MultiViewRenderer::SetViewSlots(const QMap<int, StreamId>& viewSlots) {
    if (viewSlots_ == viewSlots) {
        return;
    }

    viewSlots_ = viewSlots;
    update();
}

QMap<int, MultiViewRenderer::StreamId> MultiViewRenderer::ViewSlots() const {
    return viewSlots_;
}

void MultiViewRenderer::SetActiveStream(StreamId streamId) {
    if (activeStreamId_ == streamId) {
        return;
    }

    activeStreamId_ = streamId;
    update();
}

MultiViewRenderer::StreamId MultiViewRenderer::ActiveStream() const {
    return activeStreamId_;
}

void MultiViewRenderer::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(0.04f, 0.05f, 0.06f, 1.0f);
}

void MultiViewRenderer::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT);
}
