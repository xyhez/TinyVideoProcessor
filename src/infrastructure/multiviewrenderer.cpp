#include "multiviewrenderer.h"

#include "core/AppLog.h"

#include <algorithm>
#include <QMutexLocker>
#include <QMouseEvent>
#include <QOpenGLPixelTransferOptions>
#include <QOpenGLTexture>
#include <QtGlobal>
#include <utility>

extern "C" {
#include <libavutil/pixdesc.h>
}

namespace {
constexpr int kGrid2x2Columns = 2;
constexpr int kGrid2x2Rows = 2;
constexpr int kGrid3x3Columns = 3;
constexpr int kGrid3x3Rows = 3;
constexpr int kGrid4x4Columns = 4;
constexpr int kGrid4x4Rows = 4;
constexpr int kMaxLogicalSlots = kGrid4x4Columns * kGrid4x4Rows;
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

struct YuvPlaneUpload {
    const uchar* data = nullptr;
    int width = 0;
    int height = 0;
    int rowLength = 0;
};

QString PixelFormatName(AVPixelFormat format) {
    const char* name = av_get_pix_fmt_name(format);
    return name ? QString::fromUtf8(name) : QString("unknown(%1)").arg(static_cast<int>(format));
}

QString ColorRangeName(AVColorRange range) {
    const char* name = av_color_range_name(range);
    return name ? QString::fromUtf8(name) : QString("unknown(%1)").arg(static_cast<int>(range));
}

QString ColorSpaceName(AVColorSpace colorSpace) {
    const char* name = av_color_space_name(colorSpace);
    return name ? QString::fromUtf8(name) : QString("unknown(%1)").arg(static_cast<int>(colorSpace));
}

QString ColorPrimariesName(AVColorPrimaries primaries) {
    const char* name = av_color_primaries_name(primaries);
    return name ? QString::fromUtf8(name) : QString("unknown(%1)").arg(static_cast<int>(primaries));
}

QString ColorTransferName(AVColorTransferCharacteristic transfer) {
    const char* name = av_color_transfer_name(transfer);
    return name ? QString::fromUtf8(name) : QString("unknown(%1)").arg(static_cast<int>(transfer));
}

int ChromaPlaneSize(int lumaSize) {
    return (lumaSize + 1) / 2;
}

bool BuildYuv420pUploadPlanes(const AVFrame* frame, YuvPlaneUpload (&planes)[3]) {
    if (!frame || frame->width <= 0 || frame->height <= 0) {
        return false;
    }

    planes[0] = {frame->data[0], frame->width, frame->height, frame->linesize[0]};
    planes[1] = {frame->data[1], ChromaPlaneSize(frame->width), ChromaPlaneSize(frame->height), frame->linesize[1]};
    planes[2] = {frame->data[2], ChromaPlaneSize(frame->width), ChromaPlaneSize(frame->height), frame->linesize[2]};

    for (const YuvPlaneUpload& plane : planes) {
        if (!plane.data || plane.width <= 0 || plane.height <= 0 || plane.rowLength < plane.width) {
            return false;
        }
    }

    return true;
}

bool TextureMatches(const QOpenGLTexture* texture, const YuvPlaneUpload& plane) {
    return texture && texture->width() == plane.width && texture->height() == plane.height;
}

double SampleAspectRatioValue(AVRational sampleAspectRatio) {
    if (sampleAspectRatio.num <= 0 || sampleAspectRatio.den <= 0) {
        return 1.0;
    }

    return static_cast<double>(sampleAspectRatio.num) / sampleAspectRatio.den;
}

double DisplayAspectRatio(int width, int height, AVRational sampleAspectRatio) {
    if (width <= 0 || height <= 0) {
        return 1.0;
    }

    return static_cast<double>(width) * SampleAspectRatioValue(sampleAspectRatio) / height;
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
        ReleaseStreamResources(it->second);
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
        return kGrid2x2Columns * kGrid2x2Rows;
    case Layout::Grid3x3:
        return kGrid3x3Columns * kGrid3x3Rows;
    case Layout::Grid4x4:
        return kGrid4x4Columns * kGrid4x4Rows;
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
        ToFramebufferRect(qtRect)
    };
}

void MultiViewRenderer::PresentFrame(StreamId streamId, AVFramePtr frame) {
    if (streamId == InvalidStreamId || !frame) {
        return;
    }

    AVFramePtr displayFrame = frameConverter_.ConvertToDisplayFrame(std::move(frame));
    if (!displayFrame) {
        return;
    }

    QMutexLocker locker(&renderStateMutex_);
    StreamRenderState& state = renderStates_[streamId];
    ReleaseCurrentFrame(state);
    AVFrame* rawFrame = displayFrame.get();
    state.currentFrame = std::move(displayFrame);
    state.videoWidth = rawFrame->width;
    state.videoHeight = rawFrame->height;
    state.sampleAspectRatio = rawFrame->sample_aspect_ratio;
    state.pixelFormat = static_cast<AVPixelFormat>(rawFrame->format);
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

    ReleaseCurrentFrame(it->second);
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
    ReleaseStreamResources(it->second);
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
    const QSize framebufferSize = FramebufferSize();
    glViewport(0, 0, framebufferSize.width(), framebufferSize.height());
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

void MultiViewRenderer::mouseDoubleClickEvent(QMouseEvent* event) {
    if (!event || event->button() != Qt::LeftButton) {
        QOpenGLWidget::mouseDoubleClickEvent(event);
        return;
    }

    const QPoint clickPosition = event->pos();
    for (const Viewport& viewport : Viewports()) {
        if (!viewport.IsBound() || !viewport.qtRect.contains(clickPosition)) {
            continue;
        }

        emit StreamDoubleClicked(viewport.streamId);
        event->accept();
        return;
    }

    QOpenGLWidget::mouseDoubleClickEvent(event);
}

void MultiViewRenderer::ReleaseCurrentFrame(StreamRenderState& state) {
    state.currentFrame.reset();
    state.videoWidth = 0;
    state.videoHeight = 0;
    state.sampleAspectRatio = {1, 1};
    state.pixelFormat = AV_PIX_FMT_NONE;
    state.textureDirty = false;
    state.hasFrame = false;
    state.hasLoggedFrameFormat = false;
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
    case Layout::Grid2x2:
        return GridRectForSlot(slotIndex, kGrid2x2Columns, kGrid2x2Rows);
    case Layout::Grid3x3:
        return GridRectForSlot(slotIndex, kGrid3x3Columns, kGrid3x3Rows);
    case Layout::Grid4x4:
        return GridRectForSlot(slotIndex, kGrid4x4Columns, kGrid4x4Rows);
    }

    return rect();
}

QRect MultiViewRenderer::GridRectForSlot(int slotIndex, int columns, int rows) const {
    if (!IsLogicalSlotIndex(slotIndex) || columns <= 0 || rows <= 0) {
        return {};
    }

    const int maxSlots = columns * rows;
    if (slotIndex >= maxSlots) {
        return {};
    }

    const int row = slotIndex / columns;
    const int column = slotIndex % columns;
    const int left = column * width() / columns;
    const int right = (column + 1) * width() / columns;
    const int top = row * height() / rows;
    const int bottom = (row + 1) * height() / rows;
    return QRect(left, top, right - left, bottom - top);
}

QSize MultiViewRenderer::FramebufferSize() const {
    const qreal pixelRatio = devicePixelRatioF();
    return QSize(qRound(width() * pixelRatio), qRound(height() * pixelRatio));
}

QRect MultiViewRenderer::ToFramebufferRect(const QRect& qtRect) const {
    if (qtRect.isEmpty() || height() <= 0) {
        return {};
    }

    const qreal pixelRatio = devicePixelRatioF();
    const QSize framebufferSize = FramebufferSize();
    const int left = qRound(qtRect.x() * pixelRatio);
    const int right = qRound((qtRect.x() + qtRect.width()) * pixelRatio);
    const int top = qRound(qtRect.y() * pixelRatio);
    const int bottom = qRound((qtRect.y() + qtRect.height()) * pixelRatio);
    return QRect(left, framebufferSize.height() - bottom, right - left, bottom - top);
}

void MultiViewRenderer::DrawViewport(const Viewport& viewport) {
    if (viewport.framebufferRect.isEmpty()) {
        return;
    }

    if (viewport.IsBound()) {
        FillOpenGLRect(viewport.framebufferRect, kSlotRed, kSlotGreen, kSlotBlue, 1.0f);
    } else {
        FillOpenGLRect(viewport.framebufferRect, kEmptySlotRed, kEmptySlotGreen, kEmptySlotBlue, 1.0f);
    }

    if (viewport.IsBound()) {
        QMutexLocker locker(&renderStateMutex_);
        auto it = renderStates_.find(viewport.streamId);
        if (it != renderStates_.end()) {
            DrawStreamFrame(viewport, it->second);
        }
    }

    const bool active = viewport.IsBound() && viewport.streamId == activeStreamId_;
    DrawViewportBorder(viewport.framebufferRect, active);
}

bool MultiViewRenderer::DrawStreamFrame(const Viewport& viewport, StreamRenderState& state) {
    if (!glInitialized_ || !state.hasFrame || !state.currentFrame) {
        return false;
    }

    const bool supported = IsSupportedFrameFormat(state);
    LogFrameFormatIfNeeded(viewport.streamId, state, supported);
    if (!supported || !UploadTexturesIfNeeded(state)) {
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

    const QRect& rect = viewport.framebufferRect;
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

    const QSize framebufferSize = FramebufferSize();
    glViewport(0, 0, framebufferSize.width(), framebufferSize.height());
    return true;
}

bool MultiViewRenderer::UploadTexturesIfNeeded(StreamRenderState& state) {
    AVFrame* frame = state.currentFrame.get();
    if (!frame || !IsSupportedFrameFormat(state)) {
        return false;
    }

    YuvPlaneUpload planes[3];
    if (!BuildYuv420pUploadPlanes(frame, planes)) {
        return false;
    }

    if (!state.textureDirty && state.textureY && state.textureU && state.textureV) {
        return true;
    }

    if (!TextureMatches(state.textureY, planes[0])
        || !TextureMatches(state.textureU, planes[1])
        || !TextureMatches(state.textureV, planes[2])) {
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

        state.textureY = createTexture(planes[0].width, planes[0].height);
        state.textureU = createTexture(planes[1].width, planes[1].height);
        state.textureV = createTexture(planes[2].width, planes[2].height);
    }

    QOpenGLPixelTransferOptions options;
    options.setAlignment(1);

    options.setRowLength(planes[0].rowLength);
    state.textureY->setData(QOpenGLTexture::Red, QOpenGLTexture::UInt8, planes[0].data, &options);

    options.setRowLength(planes[1].rowLength);
    state.textureU->setData(QOpenGLTexture::Red, QOpenGLTexture::UInt8, planes[1].data, &options);

    options.setRowLength(planes[2].rowLength);
    state.textureV->setData(QOpenGLTexture::Red, QOpenGLTexture::UInt8, planes[2].data, &options);

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
    const int viewportWidth = viewport.framebufferRect.width();
    const int viewportHeight = viewport.framebufferRect.height();
    if (state.videoWidth <= 0 || state.videoHeight <= 0 ||
        viewportWidth <= 0 || viewportHeight <= 0) {
        x = -1.0f;
        y = -1.0f;
        w = 2.0f;
        h = 2.0f;
        return;
    }

    const double displayAspectRatio =
        DisplayAspectRatio(state.videoWidth, state.videoHeight, state.sampleAspectRatio);
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

void MultiViewRenderer::LogFrameFormatIfNeeded(StreamId streamId,
                                               StreamRenderState& state,
                                               bool supported) {
    AVFrame* frame = state.currentFrame.get();
    if (!frame) {
        return;
    }

    const bool changed = !state.hasLoggedFrameFormat
        || state.lastLoggedFormat != static_cast<AVPixelFormat>(frame->format)
        || state.lastLoggedWidth != frame->width
        || state.lastLoggedHeight != frame->height
        || state.lastLoggedLinesize[0] != frame->linesize[0]
        || state.lastLoggedLinesize[1] != frame->linesize[1]
        || state.lastLoggedLinesize[2] != frame->linesize[2]
        || state.lastLoggedLinesize[3] != frame->linesize[3]
        || state.lastLoggedColorRange != frame->color_range
        || state.lastLoggedColorSpace != frame->colorspace
        || state.lastLoggedColorPrimaries != frame->color_primaries
        || state.lastLoggedColorTrc != frame->color_trc
        || state.lastLoggedSampleAspectRatio.num != frame->sample_aspect_ratio.num
        || state.lastLoggedSampleAspectRatio.den != frame->sample_aspect_ratio.den
        || state.lastLoggedSupported != supported;
    if (!changed) {
        return;
    }

    state.hasLoggedFrameFormat = true;
    state.lastLoggedFormat = static_cast<AVPixelFormat>(frame->format);
    state.lastLoggedWidth = frame->width;
    state.lastLoggedHeight = frame->height;
    for (int index = 0; index < 4; ++index) {
        state.lastLoggedLinesize[index] = frame->linesize[index];
    }
    state.lastLoggedColorRange = frame->color_range;
    state.lastLoggedColorSpace = frame->colorspace;
    state.lastLoggedColorPrimaries = frame->color_primaries;
    state.lastLoggedColorTrc = frame->color_trc;
    state.lastLoggedSampleAspectRatio = frame->sample_aspect_ratio;
    state.lastLoggedSupported = supported;

    const QString prefix = supported
        ? "MultiViewRenderer accepted frame format:"
        : "MultiViewRenderer unsupported frame format:";
    APP_LOG_INFO_STREAM().noquote()
        << prefix
        << QString("streamId=%1").arg(streamId)
        << QString("pix_fmt=%1").arg(PixelFormatName(static_cast<AVPixelFormat>(frame->format)))
        << QString("size=%1x%2").arg(frame->width).arg(frame->height)
        << QString("linesize=[%1,%2,%3,%4]")
              .arg(frame->linesize[0])
              .arg(frame->linesize[1])
              .arg(frame->linesize[2])
              .arg(frame->linesize[3])
        << QString("sar=%1/%2").arg(frame->sample_aspect_ratio.num).arg(frame->sample_aspect_ratio.den)
        << QString("color_range=%1").arg(ColorRangeName(frame->color_range))
        << QString("colorspace=%1").arg(ColorSpaceName(frame->colorspace))
        << QString("color_primaries=%1").arg(ColorPrimariesName(frame->color_primaries))
        << QString("color_trc=%1").arg(ColorTransferName(frame->color_trc));
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
