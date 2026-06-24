#include "multiviewrenderer.h"

#include "core/AppLog.h"

#include <algorithm>
#include <QMutexLocker>
#include <QMouseEvent>
#include <QOpenGLPixelTransferOptions>
#include <QOpenGLTexture>

extern "C" {
#include <libavutil/pixdesc.h>
}

namespace {
constexpr int kMaxLogicalSlots = 4;
constexpr int kGrid2x2Columns = 2;
constexpr int kGrid2x2Rows = 2;
constexpr int kSlotBorderWidth = 2;
constexpr int kActiveSlotBorderWidth = 4;

constexpr float kBackgroundRed = 0.035f;
constexpr float kBackgroundGreen = 0.04f;
constexpr float kBackgroundBlue = 0.045f;
constexpr float kSlotRed = 0.07f;
constexpr float kSlotGreen = 0.075f;
constexpr float kSlotBlue = 0.08f;
constexpr float kEmptySlotRed = 0.055f;
constexpr float kEmptySlotGreen = 0.06f;
constexpr float kEmptySlotBlue = 0.065f;
constexpr float kBorderRed = 0.23f;
constexpr float kBorderGreen = 0.25f;
constexpr float kBorderBlue = 0.28f;
constexpr float kActiveBorderRed = 0.16f;
constexpr float kActiveBorderGreen = 0.48f;
constexpr float kActiveBorderBlue = 0.92f;

QString PixelFormatName(AVPixelFormat format) {
    const char* name = av_get_pix_fmt_name(format);
    return name ? QString::fromUtf8(name) : QString("unknown(%1)").arg(static_cast<int>(format));
}
}

MultiViewRenderer::MultiViewRenderer(QWidget* parent)
    : QOpenGLWidget(parent) {
    setMinimumSize(320, 240);
}

MultiViewRenderer::~MultiViewRenderer() {
    QMutexLocker locker(&renderStateMutex_);
    if (context()) {
        makeCurrent();
    }

    for (auto it = renderStates_.begin(); it != renderStates_.end(); ++it) {
        ReleaseStreamResources(it.value());
    }
    renderStates_.clear();

    if (context()) {
        doneCurrent();
    }
}

bool MultiViewRenderer::Viewport::IsBound() const {
    return streamId != InvalidStreamId;
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
    QMap<int, StreamId> normalizedViewSlots;
    for (auto it = viewSlots.cbegin(); it != viewSlots.cend(); ++it) {
        if (!IsLogicalSlotIndex(it.key()) || it.value() == InvalidStreamId) {
            continue;
        }
        normalizedViewSlots.insert(it.key(), it.value());
    }

    if (viewSlots_ == normalizedViewSlots) {
        return;
    }

    viewSlots_ = normalizedViewSlots;
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

int MultiViewRenderer::MaxVisibleSlots() const {
    switch (layout_) {
    case Layout::Single:
        return 1;
    case Layout::Grid2x2:
        return kMaxLogicalSlots;
    }

    return 1;
}

QVector<MultiViewRenderer::Viewport> MultiViewRenderer::Viewports() const {
    QVector<Viewport> viewports;
    viewports.reserve(MaxVisibleSlots());

    for (int slotIndex = 0; slotIndex < MaxVisibleSlots(); ++slotIndex) {
        viewports.append(ViewportForSlot(slotIndex));
    }

    return viewports;
}

MultiViewRenderer::Viewport MultiViewRenderer::ViewportForSlot(int slotIndex) const {
    if (!IsLogicalSlotIndex(slotIndex) || slotIndex >= MaxVisibleSlots()) {
        return {};
    }

    const QRect qtRect = QtRectForSlot(slotIndex);
    return Viewport {
        slotIndex,
        viewSlots_.value(slotIndex, InvalidStreamId),
        qtRect,
        ToOpenGLViewportRect(qtRect)
    };
}

void MultiViewRenderer::PresentFrame(StreamId streamId, AVFrame* frame) {
    if (streamId == InvalidStreamId || !frame) {
        av_frame_free(&frame);
        return;
    }

    QMutexLocker locker(&renderStateMutex_);
    StreamRenderState& state = renderStates_[streamId];
    ReleaseCurrentFrame(state);
    state.currentFrame = frame;
    state.videoWidth = frame->width;
    state.videoHeight = frame->height;
    state.sampleAspectRatio = frame->sample_aspect_ratio;
    state.pixelFormat = static_cast<AVPixelFormat>(frame->format);
    state.textureDirty = true;
    state.hasFrame = true;

    update();
}

void MultiViewRenderer::ClearFrame(StreamId streamId) {
    QMutexLocker locker(&renderStateMutex_);
    auto it = renderStates_.find(streamId);
    if (it == renderStates_.end()) {
        return;
    }

    ReleaseCurrentFrame(it.value());
    update();
}

void MultiViewRenderer::DetachStream(StreamId streamId) {
    QMutexLocker locker(&renderStateMutex_);
    auto it = renderStates_.find(streamId);
    if (it == renderStates_.end()) {
        return;
    }

    if (context()) {
        makeCurrent();
    }
    ReleaseStreamResources(it.value());
    if (context()) {
        doneCurrent();
    }
    renderStates_.erase(it);
    update();
}

void MultiViewRenderer::initializeGL() {
    initializeOpenGLFunctions();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(kBackgroundRed, kBackgroundGreen, kBackgroundBlue, 1.0f);
    InitializeFrameShader();
    glInitialized_ = frameShader_.isLinked();
}

void MultiViewRenderer::paintGL() {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, width(), height());
    glClearColor(kBackgroundRed, kBackgroundGreen, kBackgroundBlue, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_SCISSOR_TEST);
    for (const Viewport& viewport : Viewports()) {
        DrawViewport(viewport);
    }
    glDisable(GL_SCISSOR_TEST);
}

void MultiViewRenderer::mousePressEvent(QMouseEvent* event) {
    if (!event || event->button() != Qt::LeftButton) {
        QOpenGLWidget::mousePressEvent(event);
        return;
    }

    const QPoint clickPosition = event->pos();
    for (const Viewport& viewport : Viewports()) {
        if (!viewport.IsBound() || !viewport.qtRect.contains(clickPosition)) {
            continue;
        }

        emit StreamClicked(viewport.streamId);
        event->accept();
        return;
    }

    QOpenGLWidget::mousePressEvent(event);
}

void MultiViewRenderer::ReleaseCurrentFrame(StreamRenderState& state) {
    av_frame_free(&state.currentFrame);
    state.videoWidth = 0;
    state.videoHeight = 0;
    state.sampleAspectRatio = {1, 1};
    state.pixelFormat = AV_PIX_FMT_NONE;
    state.textureDirty = false;
    state.hasFrame = false;
    state.unsupportedFormatLogged = false;
}

void MultiViewRenderer::ReleaseStreamResources(StreamRenderState& state) {
    ReleaseCurrentFrame(state);
    FreeTextures(state);
}

void MultiViewRenderer::FreeTextures(StreamRenderState& state) {
    delete state.textureY;
    delete state.textureU;
    delete state.textureV;
    state.textureY = nullptr;
    state.textureU = nullptr;
    state.textureV = nullptr;
}

void MultiViewRenderer::InitializeFrameShader() {
    const char* vertexShader = R"(
        attribute vec4 vertexIn;
        attribute vec2 textureIn;
        varying vec2 textureOut;
        void main() {
            gl_Position = vertexIn;
            textureOut = textureIn;
        })";

    const char* fragmentShader = R"(
        varying vec2 textureOut;
        uniform sampler2D tex_y;
        uniform sampler2D tex_u;
        uniform sampler2D tex_v;
        void main() {
            float y = texture2D(tex_y, textureOut).r;
            float u = texture2D(tex_u, textureOut).r - 0.5;
            float v = texture2D(tex_v, textureOut).r - 0.5;
            gl_FragColor = vec4(
                y + 1.402 * v,
                y - 0.344 * u - 0.714 * v,
                y + 1.772 * u,
                1.0);
        })";

    const bool vertexShaderReady =
        frameShader_.addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShader);
    const bool fragmentShaderReady =
        frameShader_.addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShader);
    const bool shaderLinked = frameShader_.link();
    if (!vertexShaderReady || !fragmentShaderReady || !shaderLinked) {
        APP_LOG_WARN_STREAM().noquote()
            << "MultiViewRenderer frame shader init failed:"
            << frameShader_.log();
        return;
    }

    frameShader_.bind();
    frameShader_.setUniformValue("tex_y", 0);
    frameShader_.setUniformValue("tex_u", 1);
    frameShader_.setUniformValue("tex_v", 2);
    frameShader_.release();
}

bool MultiViewRenderer::IsLogicalSlotIndex(int slotIndex) const {
    return slotIndex >= 0 && slotIndex < kMaxLogicalSlots;
}

QRect MultiViewRenderer::QtRectForSlot(int slotIndex) const {
    if (width() <= 0 || height() <= 0) {
        return {};
    }

    switch (layout_) {
    case Layout::Single:
        return slotIndex == 0 ? rect() : QRect();
    case Layout::Grid2x2: {
        if (!IsLogicalSlotIndex(slotIndex)) {
            return {};
        }

        const int row = slotIndex / kGrid2x2Columns;
        const int column = slotIndex % kGrid2x2Columns;
        const int left = column * width() / kGrid2x2Columns;
        const int right = (column + 1) * width() / kGrid2x2Columns;
        const int top = row * height() / kGrid2x2Rows;
        const int bottom = (row + 1) * height() / kGrid2x2Rows;
        return QRect(left, top, right - left, bottom - top);
    }
    }

    return rect();
}

QRect MultiViewRenderer::ToOpenGLViewportRect(const QRect& qtRect) const {
    if (qtRect.isEmpty() || height() <= 0) {
        return {};
    }

    return QRect(qtRect.x(),
                 height() - qtRect.y() - qtRect.height(),
                 qtRect.width(),
                 qtRect.height());
}

void MultiViewRenderer::DrawViewport(const Viewport& viewport) {
    if (viewport.glViewportRect.isEmpty()) {
        return;
    }

    if (viewport.IsBound()) {
        FillOpenGLRect(viewport.glViewportRect, kSlotRed, kSlotGreen, kSlotBlue, 1.0f);
    } else {
        FillOpenGLRect(viewport.glViewportRect, kEmptySlotRed, kEmptySlotGreen, kEmptySlotBlue, 1.0f);
    }

    if (viewport.slotIndex == 0 && viewport.IsBound()) {
        QMutexLocker locker(&renderStateMutex_);
        auto it = renderStates_.find(viewport.streamId);
        if (it != renderStates_.end()) {
            DrawStreamFrame(viewport, it.value());
        }
    }

    const bool active = viewport.IsBound() && viewport.streamId == activeStreamId_;
    DrawViewportBorder(viewport.glViewportRect, active);
}

bool MultiViewRenderer::DrawStreamFrame(const Viewport& viewport, StreamRenderState& state) {
    if (!glInitialized_ || !state.hasFrame || !state.currentFrame) {
        return false;
    }

    if (!UploadTexturesIfNeeded(state)) {
        LogUnsupportedFrameIfNeeded(viewport.streamId, state);
        return false;
    }

    if (!state.textureY || !state.textureU || !state.textureV) {
        return false;
    }

    frameShader_.bind();
    state.textureY->bind(0);
    state.textureU->bind(1);
    state.textureV->bind(2);

    float x = -1.0f;
    float y = -1.0f;
    float w = 2.0f;
    float h = 2.0f;
    CalcFrameRect(viewport, state, x, y, w, h);

    const QRect& rect = viewport.glViewportRect;
    glViewport(rect.x(), rect.y(), rect.width(), rect.height());
    glScissor(rect.x(), rect.y(), rect.width(), rect.height());

    GLfloat vertices[] = {x, y, x + w, y, x, y + h, x + w, y + h};
    GLfloat textures[] = {0, 1, 1, 1, 0, 0, 1, 0};

    frameShader_.setAttributeArray("vertexIn", vertices, 2);
    frameShader_.setAttributeArray("textureIn", textures, 2);
    frameShader_.enableAttributeArray("vertexIn");
    frameShader_.enableAttributeArray("textureIn");

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    frameShader_.disableAttributeArray("vertexIn");
    frameShader_.disableAttributeArray("textureIn");
    frameShader_.release();

    glViewport(0, 0, width(), height());
    return true;
}

bool MultiViewRenderer::UploadTexturesIfNeeded(StreamRenderState& state) {
    AVFrame* frame = state.currentFrame;
    if (!frame || !IsSupportedFrameFormat(state)) {
        return false;
    }

    state.unsupportedFormatLogged = false;
    if (!state.textureDirty && state.textureY && state.textureU && state.textureV) {
        return true;
    }

    if (state.textureY &&
        (state.textureY->width() != frame->width || state.textureY->height() != frame->height)) {
        FreeTextures(state);
    }

    if (!state.textureY) {
        auto createTexture = [](int width, int height) -> QOpenGLTexture* {
            auto* texture = new QOpenGLTexture(QOpenGLTexture::Target2D);
            texture->setSize(width, height);
            texture->setFormat(QOpenGLTexture::R8_UNorm);
            texture->setMinMagFilters(QOpenGLTexture::Linear, QOpenGLTexture::Linear);
            texture->setWrapMode(QOpenGLTexture::ClampToEdge);
            texture->allocateStorage();
            return texture;
        };

        state.textureY = createTexture(frame->width, frame->height);
        state.textureU = createTexture(frame->width / 2, frame->height / 2);
        state.textureV = createTexture(frame->width / 2, frame->height / 2);
    }

    QOpenGLPixelTransferOptions options;

    options.setRowLength(frame->linesize[0]);
    state.textureY->setData(QOpenGLTexture::Red, QOpenGLTexture::UInt8, frame->data[0], &options);

    options.setRowLength(frame->linesize[1]);
    state.textureU->setData(QOpenGLTexture::Red, QOpenGLTexture::UInt8, frame->data[1], &options);

    options.setRowLength(frame->linesize[2]);
    state.textureV->setData(QOpenGLTexture::Red, QOpenGLTexture::UInt8, frame->data[2], &options);

    state.textureDirty = false;
    return true;
}

bool MultiViewRenderer::IsSupportedFrameFormat(const StreamRenderState& state) const {
    return state.currentFrame && state.pixelFormat == AV_PIX_FMT_YUV420P;
}

void MultiViewRenderer::CalcFrameRect(const Viewport& viewport,
                                      const StreamRenderState& state,
                                      float& x,
                                      float& y,
                                      float& w,
                                      float& h) const {
    const int viewportWidth = viewport.glViewportRect.width();
    const int viewportHeight = viewport.glViewportRect.height();
    if (state.videoWidth <= 0 || state.videoHeight <= 0 ||
        viewportWidth <= 0 || viewportHeight <= 0) {
        x = -1.0f;
        y = -1.0f;
        w = 2.0f;
        h = 2.0f;
        return;
    }

    const double sar = (state.sampleAspectRatio.num && state.sampleAspectRatio.den)
        ? static_cast<double>(state.sampleAspectRatio.num) / state.sampleAspectRatio.den
        : 1.0;
    const double displayAspectRatio = static_cast<double>(state.videoWidth) * sar / state.videoHeight;
    const double viewportAspectRatio = static_cast<double>(viewportWidth) / viewportHeight;

    if (displayAspectRatio > viewportAspectRatio) {
        w = 2.0f;
        h = static_cast<float>(2.0 * viewportAspectRatio / displayAspectRatio);
    } else {
        h = 2.0f;
        w = static_cast<float>(2.0 * displayAspectRatio / viewportAspectRatio);
    }

    x = -w / 2.0f;
    y = -h / 2.0f;
}

void MultiViewRenderer::LogUnsupportedFrameIfNeeded(StreamId streamId, StreamRenderState& state) {
    if (state.unsupportedFormatLogged || IsSupportedFrameFormat(state)) {
        return;
    }

    state.unsupportedFormatLogged = true;
    APP_LOG_WARN_STREAM().noquote()
        << "MultiViewRenderer unsupported frame format:"
        << QString("streamId=%1").arg(streamId)
        << QString("pix_fmt=%1").arg(PixelFormatName(state.pixelFormat))
        << QString("size=%1x%2").arg(state.videoWidth).arg(state.videoHeight);
}

void MultiViewRenderer::FillOpenGLRect(const QRect& rect,
                                       float red,
                                       float green,
                                       float blue,
                                       float alpha) {
    if (rect.isEmpty()) {
        return;
    }

    glScissor(rect.x(), rect.y(), rect.width(), rect.height());
    glClearColor(red, green, blue, alpha);
    glClear(GL_COLOR_BUFFER_BIT);
}

void MultiViewRenderer::DrawViewportBorder(const QRect& rect, bool active) {
    if (rect.isEmpty()) {
        return;
    }

    const int targetBorderWidth = active ? kActiveSlotBorderWidth : kSlotBorderWidth;
    const int borderWidth = std::max(1, std::min({targetBorderWidth, rect.width(), rect.height()}));
    const float borderRed = active ? kActiveBorderRed : kBorderRed;
    const float borderGreen = active ? kActiveBorderGreen : kBorderGreen;
    const float borderBlue = active ? kActiveBorderBlue : kBorderBlue;
    const int x = rect.x();
    const int y = rect.y();
    const int width = rect.width();
    const int height = rect.height();

    FillOpenGLRect(QRect(x, y, borderWidth, height), borderRed, borderGreen, borderBlue, 1.0f);
    FillOpenGLRect(QRect(x + width - borderWidth, y, borderWidth, height),
                   borderRed,
                   borderGreen,
                   borderBlue,
                   1.0f);
    FillOpenGLRect(QRect(x, y, width, borderWidth), borderRed, borderGreen, borderBlue, 1.0f);
    FillOpenGLRect(QRect(x, y + height - borderWidth, width, borderWidth),
                   borderRed,
                   borderGreen,
                   borderBlue,
                   1.0f);
}
