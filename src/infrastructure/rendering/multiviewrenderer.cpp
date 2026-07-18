#include "multiviewrenderer.h"

#include "tiny_log/Logging.h"

#include <algorithm>
#include <QMutexLocker>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOpenGLPixelTransferOptions>
#include <QOpenGLTexture>
#include <QMetaObject>
#include <QStringList>
#include <QThread>
#include <QtGlobal>
#include <utility>

extern "C" {
#include <libavutil/pixdesc.h>
}

namespace {
constexpr int kGrid2x2Columns = 2;	///< 2x2 布局的列数
constexpr int kGrid2x2Rows = 2;	///< 2x2 布局的行数
constexpr int kGrid3x3Columns = 3;	///< 3x3 布局的列数
constexpr int kGrid3x3Rows = 3;	///< 3x3 布局的行数
constexpr int kGrid4x4Columns = 4;	///< 4x4 布局的列数
constexpr int kGrid4x4Rows = 4;	///< 4x4 布局的行数
constexpr int kMaxLogicalSlots = kGrid4x4Columns * kGrid4x4Rows;	///< 当前支持的最大逻辑画面槽位数
constexpr int kSlotBorderWidth = 2;	///< 普通画面槽位边框宽度，单位像素
constexpr int kActiveSlotBorderWidth = 4;	///< active stream 画面槽位边框宽度，单位像素
constexpr qint64 kPerformanceLogIntervalMs = 5000;	///< 多画面渲染性能日志输出间隔，单位毫秒

constexpr float kBackgroundRed = 0.027451f;	///< 主题背景色 #070A0D 红色通道值
constexpr float kBackgroundGreen = 0.039216f;	///< 主题背景色 #070A0D 绿色通道值
constexpr float kBackgroundBlue = 0.050980f;	///< 主题背景色 #070A0D 蓝色通道值
constexpr float kSlotRed = 0.050980f;	///< 已绑定槽位底色 #0D1217 红色通道值
constexpr float kSlotGreen = 0.070588f;	///< 已绑定槽位底色 #0D1217 绿色通道值
constexpr float kSlotBlue = 0.090196f;	///< 已绑定槽位底色 #0D1217 蓝色通道值
constexpr float kEmptySlotRed = 0.027451f;	///< 空槽位底色 #070A0D 红色通道值
constexpr float kEmptySlotGreen = 0.039216f;	///< 空槽位底色 #070A0D 绿色通道值
constexpr float kEmptySlotBlue = 0.050980f;	///< 空槽位底色 #070A0D 蓝色通道值
constexpr float kBorderRed = 0.125490f;	///< 普通槽位弱边框 #202A33 红色通道值
constexpr float kBorderGreen = 0.164706f;	///< 普通槽位弱边框 #202A33 绿色通道值
constexpr float kBorderBlue = 0.200000f;	///< 普通槽位弱边框 #202A33 蓝色通道值
constexpr float kActiveBorderRed = 0.960784f;	///< active stream 强调边框 #F59E0B 红色通道值
constexpr float kActiveBorderGreen = 0.619608f;	///< active stream 强调边框 #F59E0B 绿色通道值
constexpr float kActiveBorderBlue = 0.043137f;	///< active stream 强调边框 #F59E0B 蓝色通道值

struct YuvPlaneUpload {
    const uchar* data = nullptr;
    int width = 0;
    int height = 0;
    int rowLength = 0;
    QOpenGLTexture::TextureFormat textureFormat = QOpenGLTexture::R8_UNorm;
    QOpenGLTexture::PixelFormat pixelFormat = QOpenGLTexture::Red;
};

struct Nv12ColorConversion {
    float yOffset = 0.0f;
    float yScale = 1.0f;
    float uvOffset = 128.0f / 255.0f;
    float uvScale = 1.0f;
    float redV = 1.5748f;
    float greenU = 0.187324f;
    float greenV = 0.468124f;
    float blueU = 1.8556f;
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

    planes[0] = {frame->data[0], frame->width, frame->height, frame->linesize[0],
                 QOpenGLTexture::R8_UNorm, QOpenGLTexture::Red};
    planes[1] = {frame->data[1], ChromaPlaneSize(frame->width),
                 ChromaPlaneSize(frame->height), frame->linesize[1],
                 QOpenGLTexture::R8_UNorm, QOpenGLTexture::Red};
    planes[2] = {frame->data[2], ChromaPlaneSize(frame->width),
                 ChromaPlaneSize(frame->height), frame->linesize[2],
                 QOpenGLTexture::R8_UNorm, QOpenGLTexture::Red};

    for (const YuvPlaneUpload& plane : planes) {
        if (!plane.data || plane.width <= 0 || plane.height <= 0 || plane.rowLength < plane.width) {
            return false;
        }
    }

    return true;
}

bool BuildNv12UploadPlanes(const AVFrame* frame, YuvPlaneUpload (&planes)[3]);

bool TextureMatches(const QOpenGLTexture* texture, const YuvPlaneUpload& plane) {
    return texture
        && texture->width() == plane.width
        && texture->height() == plane.height
        && texture->format() == plane.textureFormat;
}

QOpenGLTexture* CreateYuvTexture(const YuvPlaneUpload& plane) {
    auto* texture = new QOpenGLTexture(QOpenGLTexture::Target2D);
    texture->setSize(plane.width, plane.height);
    texture->setFormat(plane.textureFormat);
    texture->setMinMagFilters(QOpenGLTexture::Linear, QOpenGLTexture::Linear);
    texture->setWrapMode(QOpenGLTexture::ClampToEdge);
    texture->allocateStorage();
    return texture;
}

Nv12ColorConversion ResolveNv12ColorConversion(const AVFrame* frame) {
    Nv12ColorConversion conversion;
    if (!frame) {
        return conversion;
    }

    if (frame->color_range != AVCOL_RANGE_JPEG) {
        conversion.yOffset = 16.0f / 255.0f;
        conversion.yScale = 255.0f / 219.0f;
        conversion.uvScale = 255.0f / 224.0f;
    }

    switch (frame->colorspace) {
    case AVCOL_SPC_BT709:
        break;
    case AVCOL_SPC_BT2020_NCL:
    case AVCOL_SPC_BT2020_CL:
        conversion.redV = 1.4746f;
        conversion.greenU = 0.164553f;
        conversion.greenV = 0.571353f;
        conversion.blueU = 1.8814f;
        break;
    case AVCOL_SPC_FCC:
    case AVCOL_SPC_BT470BG:
    case AVCOL_SPC_SMPTE170M:
        conversion.redV = 1.402f;
        conversion.greenU = 0.344136f;
        conversion.greenV = 0.714136f;
        conversion.blueU = 1.772f;
        break;
    default:
        if (frame->width < 1280) {
            conversion.redV = 1.402f;
            conversion.greenU = 0.344136f;
            conversion.greenV = 0.714136f;
            conversion.blueU = 1.772f;
        }
        break;
    }

    return conversion;
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
    performanceTimer_.start();
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
    RequestRenderUpdate();
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
    RequestRenderUpdate();
}

QMap<int, MultiViewRenderer::StreamId> MultiViewRenderer::ViewSlots() const {
    return viewSlots_;
}

void MultiViewRenderer::SetActiveStream(StreamId streamId) {
    if (activeStreamId_ == streamId) {
        return;
    }

    activeStreamId_ = streamId;
    RequestRenderUpdate();
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

    {
        QMutexLocker locker(&renderStateMutex_);
        StreamRenderState& state = renderStates_[streamId];
        if (state.pendingFrame) {
            state.coalescedFrames += 1;
            totalCoalescedFrames_ += 1;
        }
        state.pendingFrame = std::move(frame);
        state.presentedFrames += 1;
        totalPresentedFrames_ += 1;
        state.lastPresentedElapsedMs = performanceTimer_.elapsed();
    }

    RequestRenderUpdate();
}

void MultiViewRenderer::ClearFrame(StreamId streamId) {
    {
        QMutexLocker locker(&renderStateMutex_);
        auto it = renderStates_.find(streamId);
        if (it == renderStates_.end()) {
            return;
        }

        ReleaseCurrentFrame(it->second);
    }

    RequestRenderUpdate();
}

void MultiViewRenderer::DetachStream(StreamId streamId) {
    {
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
    }

    RequestRenderUpdate();
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
    {
        QMutexLocker locker(&renderUpdateMutex_);
        renderUpdatePending_ = false;
    }

    paintCount_ += 1;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    const QSize framebufferSize = FramebufferSize();
    glViewport(0, 0, framebufferSize.width(), framebufferSize.height());
    glClearColor(kBackgroundRed, kBackgroundGreen, kBackgroundBlue, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_SCISSOR_TEST);
    const int visibleSlotCount = MaxVisibleSlots();
    for (int slotIndex = 0; slotIndex < visibleSlotCount; ++slotIndex) {
        DrawViewport(ViewportForSlot(slotIndex));
    }
    glDisable(GL_SCISSOR_TEST);

    LogPerformanceStatsIfNeeded();
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
    state.pendingFrame.reset();
    state.currentFrame.reset();
    state.videoWidth = 0;
    state.videoHeight = 0;
    state.sampleAspectRatio = {1, 1};
    state.pixelFormat = AV_PIX_FMT_NONE;
    state.textureDirty = false;
    state.hasFrame = false;
}

bool MultiViewRenderer::PreparePendingFrameForDisplay(StreamRenderState& state) {
    if (!state.pendingFrame) {
        return state.hasFrame && state.currentFrame;
    }

    AVFrame* inputFrame = state.pendingFrame.get();
    const quintptr inputFrameAddress = reinterpret_cast<quintptr>(inputFrame);
    const AVPixelFormat inputPixelFormat = static_cast<AVPixelFormat>(inputFrame->format);
    YuvPlaneUpload nv12Planes[3];
    const bool renderNv12Directly = inputPixelFormat == AV_PIX_FMT_NV12
        && nv12DirectRenderingAvailable_
        && BuildNv12UploadPlanes(inputFrame, nv12Planes);
    AVFramePtr displayFrame = renderNv12Directly
        ? std::move(state.pendingFrame)
        : frameConverter_.ConvertToDisplayFrame(std::move(state.pendingFrame));
    if (!displayFrame) {
        return state.hasFrame && state.currentFrame;
    }

    const bool converted = reinterpret_cast<quintptr>(displayFrame.get()) != inputFrameAddress;
    AVFrame* rawFrame = displayFrame.get();
    state.currentFrame = std::move(displayFrame);
    state.videoWidth = rawFrame->width;
    state.videoHeight = rawFrame->height;
    state.sampleAspectRatio = rawFrame->sample_aspect_ratio;
    state.pixelFormat = static_cast<AVPixelFormat>(rawFrame->format);
    state.textureDirty = true;
    state.hasFrame = true;
    state.preparedFrames += 1;
    totalPreparedFrames_ += 1;
    if (converted) {
        state.formatConvertedFrames += 1;
        totalFormatConvertedFrames_ += 1;
    }
    state.lastInputPixelFormat = inputPixelFormat;
    state.lastFrameConverted = converted;
    return true;
}

namespace {
bool BuildNv12UploadPlanes(const AVFrame* frame, YuvPlaneUpload (&planes)[3]) {
    if (!frame || frame->width <= 0 || frame->height <= 0
        || !frame->data[0] || !frame->data[1]
        || frame->linesize[0] < frame->width || frame->linesize[1] <= 0
        || frame->linesize[1] % 2 != 0) {
        return false;
    }

    const int chromaWidth = ChromaPlaneSize(frame->width);
    const int chromaHeight = ChromaPlaneSize(frame->height);
    const int uvRowLength = frame->linesize[1] / 2;
    if (uvRowLength < chromaWidth) {
        return false;
    }

    planes[0] = {frame->data[0], frame->width, frame->height, frame->linesize[0],
                 QOpenGLTexture::R8_UNorm, QOpenGLTexture::Red};
    planes[1] = {frame->data[1], chromaWidth, chromaHeight, uvRowLength,
                 QOpenGLTexture::RG8_UNorm, QOpenGLTexture::RG};
    planes[2] = {};
    return true;
}

bool BuildUploadPlanes(const AVFrame* frame,
                       YuvPlaneUpload (&planes)[3],
                       int& planeCount) {
    planeCount = 0;
    if (!frame) {
        return false;
    }

    switch (static_cast<AVPixelFormat>(frame->format)) {
    case AV_PIX_FMT_YUV420P:
        planeCount = 3;
        return BuildYuv420pUploadPlanes(frame, planes);
    case AV_PIX_FMT_NV12:
        planeCount = 2;
        return BuildNv12UploadPlanes(frame, planes);
    default:
        return false;
    }
}
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
    state.texturePixelFormat = AV_PIX_FMT_NONE;
    for (int index = 0; index < 3; ++index) {
        state.texturePlaneWidths[index] = 0;
        state.texturePlaneHeights[index] = 0;
    }
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

    const char* yuv420pFragmentShader = R"(
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

    const char* nv12FragmentShader = R"(
        varying vec2 textureOut;
        uniform sampler2D tex_y;
        uniform sampler2D tex_uv;
        uniform float y_offset;
        uniform float y_scale;
        uniform float uv_offset;
        uniform float uv_scale;
        uniform float red_v;
        uniform float green_u;
        uniform float green_v;
        uniform float blue_u;
        void main() {
            float y = (texture2D(tex_y, textureOut).r - y_offset) * y_scale;
            vec2 uv = (texture2D(tex_uv, textureOut).rg - vec2(uv_offset)) * uv_scale;
            vec3 rgb = vec3(
                y + red_v * uv.y,
                y - green_u * uv.x - green_v * uv.y,
                y + blue_u * uv.x);
            gl_FragColor = vec4(clamp(rgb, 0.0, 1.0), 1.0);
        })";

    const auto initializeProgram = [vertexShader](QOpenGLShaderProgram& program,
                                                   const char* fragmentShader,
                                                   const char* shaderName) {
        const bool vertexShaderReady =
            program.addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShader);
        const bool fragmentShaderReady =
            program.addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShader);
        const bool shaderLinked = program.link();
        if (!vertexShaderReady || !fragmentShaderReady || !shaderLinked) {
            LOG_WARN_STREAM().noquote()
                << QString("MultiViewRenderer %1 shader init failed:").arg(shaderName)
                << program.log();
            return false;
        }
        return true;
    };

    if (initializeProgram(frameShader_, yuv420pFragmentShader, "YUV420P")) {
        frameShader_.bind();
        frameShader_.setUniformValue("tex_y", 0);
        frameShader_.setUniformValue("tex_u", 1);
        frameShader_.setUniformValue("tex_v", 2);
        frameShader_.release();
    }

    QOpenGLContext* currentContext = QOpenGLContext::currentContext();
    const bool supportsRgTexture = currentContext
        && (currentContext->format().majorVersion() >= 3
            || currentContext->hasExtension("GL_ARB_texture_rg")
            || currentContext->hasExtension("GL_EXT_texture_rg"));
    if (!supportsRgTexture) {
        LOG_WARN("MultiViewRenderer NV12 direct rendering disabled: RG texture unsupported");
    } else if (initializeProgram(nv12FrameShader_, nv12FragmentShader, "NV12")) {
        nv12FrameShader_.bind();
        nv12FrameShader_.setUniformValue("tex_y", 0);
        nv12FrameShader_.setUniformValue("tex_uv", 1);
        nv12FrameShader_.release();
        nv12DirectRenderingAvailable_ = true;
        LOG_INFO("MultiViewRenderer NV12 direct rendering enabled");
    }
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
    if (!glInitialized_ || !PreparePendingFrameForDisplay(state)) {
        return false;
    }

    const bool supported = IsSupportedFrameFormat(state);
    LogFrameFormatIfNeeded(viewport.streamId, state, supported);
    if (!supported || !UploadTexturesIfNeeded(state)) {
        return false;
    }

    const bool isNv12 = state.pixelFormat == AV_PIX_FMT_NV12;
    if (!state.textureY || !state.textureU || (!isNv12 && !state.textureV)) {
        return false;
    }

    QOpenGLShaderProgram& shader = isNv12 ? nv12FrameShader_ : frameShader_;
    shader.bind();
    state.textureY->bind(0);
    state.textureU->bind(1);
    if (!isNv12) {
        state.textureV->bind(2);
    } else {
        const Nv12ColorConversion conversion = ResolveNv12ColorConversion(state.currentFrame.get());
        shader.setUniformValue("y_offset", conversion.yOffset);
        shader.setUniformValue("y_scale", conversion.yScale);
        shader.setUniformValue("uv_offset", conversion.uvOffset);
        shader.setUniformValue("uv_scale", conversion.uvScale);
        shader.setUniformValue("red_v", conversion.redV);
        shader.setUniformValue("green_u", conversion.greenU);
        shader.setUniformValue("green_v", conversion.greenV);
        shader.setUniformValue("blue_u", conversion.blueU);
    }

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

    shader.setAttributeArray("vertexIn", vertices, 2);
    shader.setAttributeArray("textureIn", textures, 2);
    shader.enableAttributeArray("vertexIn");
    shader.enableAttributeArray("textureIn");

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    shader.disableAttributeArray("vertexIn");
    shader.disableAttributeArray("textureIn");
    shader.release();

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
    int planeCount = 0;
    if (!BuildUploadPlanes(frame, planes, planeCount)) {
        return false;
    }

    const bool requiredTexturesAvailable = state.textureY && state.textureU
        && (planeCount == 2 || state.textureV);
    if (!state.textureDirty && requiredTexturesAvailable) {
        return true;
    }

    const int planeWidths[3] = {planes[0].width, planes[1].width, planes[2].width};
    const int planeHeights[3] = {planes[0].height, planes[1].height, planes[2].height};
    const bool hasExistingTexture = state.textureY || state.textureU || state.textureV;
    QOpenGLTexture* textures[3] = {state.textureY, state.textureU, state.textureV};
    bool texturesMatch = TextureSignatureMatches(state, planeWidths, planeHeights);
    for (int index = 0; texturesMatch && index < planeCount; ++index) {
        texturesMatch = TextureMatches(textures[index], planes[index]);
    }
    if (planeCount == 2 && state.textureV) {
        texturesMatch = false;
    }

    if (!texturesMatch) {
        FreeTextures(state);
        if (hasExistingTexture) {
            state.textureRebuilds += 1;
        }
    }

    if (!state.textureY) {
        state.textureY = CreateYuvTexture(planes[0]);
        state.textureU = CreateYuvTexture(planes[1]);
        if (planeCount == 3) {
            state.textureV = CreateYuvTexture(planes[2]);
        }
        UpdateTextureSignature(state, planeWidths, planeHeights);
    }

    QOpenGLPixelTransferOptions options;
    options.setAlignment(1);
    QOpenGLTexture* uploadTextures[3] = {state.textureY, state.textureU, state.textureV};
    for (int index = 0; index < planeCount; ++index) {
        options.setRowLength(planes[index].rowLength);
        uploadTextures[index]->setData(planes[index].pixelFormat,
                                       QOpenGLTexture::UInt8,
                                       planes[index].data,
                                       &options);
    }

    state.textureUploads += 1;
    state.textureDirty = false;
    return true;
}

bool MultiViewRenderer::TextureSignatureMatches(const StreamRenderState& state,
                                                const int (&planeWidths)[3],
                                                const int (&planeHeights)[3]) const {
    if (state.texturePixelFormat != state.pixelFormat) {
        return false;
    }

    for (int index = 0; index < 3; ++index) {
        if (state.texturePlaneWidths[index] != planeWidths[index]
            || state.texturePlaneHeights[index] != planeHeights[index]) {
            return false;
        }
    }

    return true;
}

void MultiViewRenderer::UpdateTextureSignature(StreamRenderState& state,
                                               const int (&planeWidths)[3],
                                               const int (&planeHeights)[3]) {
    state.texturePixelFormat = state.pixelFormat;
    for (int index = 0; index < 3; ++index) {
        state.texturePlaneWidths[index] = planeWidths[index];
        state.texturePlaneHeights[index] = planeHeights[index];
    }
}

bool MultiViewRenderer::IsSupportedFrameFormat(const StreamRenderState& state) const {
    if (!state.currentFrame) {
        return false;
    }

    return state.pixelFormat == AV_PIX_FMT_YUV420P
        || (state.pixelFormat == AV_PIX_FMT_NV12 && nv12DirectRenderingAvailable_);
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
    LOG_INFO_STREAM().noquote()
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

void MultiViewRenderer::LogPerformanceStatsIfNeeded() {
    const qint64 nowMs = performanceTimer_.elapsed();
    const qint64 windowMs = nowMs - performanceWindowStartMs_;
    if (windowMs < kPerformanceLogIntervalMs) {
        return;
    }

    const QVector<Viewport> viewports = Viewports();
    int visibleStreams = 0;
    int attachedStreams = 0;
    int activeFrameStreams = 0;
    int maxFrameWidth = 0;
    int maxFrameHeight = 0;
    qint64 totalTextureUploads = 0;
    qint64 totalTextureRebuilds = 0;
    qint64 totalPresentedFrames = 0;
    qint64 totalCoalescedFrames = 0;
    qint64 totalPreparedFrames = 0;
    qint64 totalFormatConvertedFrames = 0;
    QStringList streamSummaries;

    {
        QMutexLocker locker(&renderStateMutex_);
        attachedStreams = static_cast<int>(renderStates_.size());
        totalPresentedFrames = totalPresentedFrames_;
        totalCoalescedFrames = totalCoalescedFrames_;
        totalPreparedFrames = totalPreparedFrames_;
        totalFormatConvertedFrames = totalFormatConvertedFrames_;

        for (const Viewport& viewport : viewports) {
            if (!viewport.IsBound()) {
                continue;
            }

            auto it = renderStates_.find(viewport.streamId);
            if (it != renderStates_.end() && it->second.hasFrame) {
                visibleStreams += 1;
            }
        }

        for (auto it = renderStates_.cbegin(); it != renderStates_.cend(); ++it) {
            const StreamId streamId = it->first;
            const StreamRenderState& state = it->second;
            totalTextureUploads += state.textureUploads;
            totalTextureRebuilds += state.textureRebuilds;

            if (!state.hasFrame && !state.pendingFrame) {
                continue;
            }

            activeFrameStreams += 1;
            if (state.videoWidth * state.videoHeight > maxFrameWidth * maxFrameHeight) {
                maxFrameWidth = state.videoWidth;
                maxFrameHeight = state.videoHeight;
            }

            const qint64 lastFrameAgeMs = state.lastPresentedElapsedMs >= 0
                ? nowMs - state.lastPresentedElapsedMs
                : -1;
            streamSummaries.append(QString("#%1 %2x%3 input=%4 display=%5 received=%6 coalesced=%7 prepared=%8 formatConverted=%9 uploads=%10 ageMs=%11 lastConverted=%12")
                                       .arg(streamId)
                                       .arg(state.videoWidth)
                                       .arg(state.videoHeight)
                                       .arg(PixelFormatName(state.lastInputPixelFormat))
                                       .arg(PixelFormatName(state.pixelFormat))
                                       .arg(state.presentedFrames)
                                       .arg(state.coalescedFrames)
                                       .arg(state.preparedFrames)
                                       .arg(state.formatConvertedFrames)
                                       .arg(state.textureUploads)
                                       .arg(lastFrameAgeMs)
                                       .arg(state.lastFrameConverted ? "yes" : "no"));
        }
    }

    const qint64 paintDelta = paintCount_ - lastLoggedPaintCount_;
    const qint64 presentedDelta = totalPresentedFrames - lastLoggedPresentedFrames_;
    const qint64 coalescedDelta = totalCoalescedFrames - lastLoggedCoalescedFrames_;
    const qint64 preparedDelta = totalPreparedFrames - lastLoggedPreparedFrames_;
    const qint64 formatConvertedDelta =
        totalFormatConvertedFrames - lastLoggedFormatConvertedFrames_;
    const qint64 uploadDelta = totalTextureUploads - lastLoggedTextureUploads_;
    const qint64 rebuildDelta = totalTextureRebuilds - lastLoggedTextureRebuilds_;
    const double windowSeconds = static_cast<double>(windowMs) / 1000.0;
    const double paintFps = windowSeconds > 0.0 ? paintDelta / windowSeconds : 0.0;
    const bool idleRenderer = attachedStreams == 0
        && activeFrameStreams == 0
        && totalTextureUploads == 0
        && totalTextureRebuilds == 0;
    if (idleRenderer) {
        performanceWindowStartMs_ = nowMs;
        lastLoggedPaintCount_ = paintCount_;
        lastLoggedPresentedFrames_ = totalPresentedFrames;
        lastLoggedCoalescedFrames_ = totalCoalescedFrames;
        lastLoggedPreparedFrames_ = totalPreparedFrames;
        lastLoggedFormatConvertedFrames_ = totalFormatConvertedFrames;
        lastLoggedTextureUploads_ = totalTextureUploads;
        lastLoggedTextureRebuilds_ = totalTextureRebuilds;
        return;
    }

    LOG_INFO_STREAM().noquote()
        << "MultiViewRenderer perf:"
        << QString("paintFps=%1").arg(paintFps, 0, 'f', 1)
        << QString("paintCount=%1").arg(paintDelta)
        << QString("visibleStreams=%1").arg(visibleStreams)
        << QString("activeFrameStreams=%1").arg(activeFrameStreams)
        << QString("attachedStreams=%1").arg(attachedStreams)
        << QString("received=%1").arg(presentedDelta)
        << QString("coalesced=%1").arg(coalescedDelta)
        << QString("prepared=%1").arg(preparedDelta)
        << QString("formatConverted=%1").arg(formatConvertedDelta)
        << QString("uploads=%1").arg(uploadDelta)
        << QString("textureRebuilds=%1").arg(rebuildDelta)
        << QString("maxFrame=%1x%2").arg(maxFrameWidth).arg(maxFrameHeight);

    if (!streamSummaries.isEmpty()) {
        LOG_INFO_STREAM().noquote()
            << "MultiViewRenderer stream perf:"
            << streamSummaries.join(" | ");
    }

    performanceWindowStartMs_ = nowMs;
    lastLoggedPaintCount_ = paintCount_;
    lastLoggedPresentedFrames_ = totalPresentedFrames;
    lastLoggedCoalescedFrames_ = totalCoalescedFrames;
    lastLoggedPreparedFrames_ = totalPreparedFrames;
    lastLoggedFormatConvertedFrames_ = totalFormatConvertedFrames;
    lastLoggedTextureUploads_ = totalTextureUploads;
    lastLoggedTextureRebuilds_ = totalTextureRebuilds;
}

void MultiViewRenderer::RequestRenderUpdate() {
    bool shouldRequestUpdate = false;
    {
        QMutexLocker locker(&renderUpdateMutex_);
        if (!renderUpdatePending_) {
            renderUpdatePending_ = true;
            shouldRequestUpdate = true;
        }
    }

    if (!shouldRequestUpdate) {
        return;
    }

    if (QThread::currentThread() == thread()) {
        update();
        return;
    }

    QMetaObject::invokeMethod(this, [this]() {
        update();
    }, Qt::QueuedConnection);
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
