#include "videorender.h"
#include <QOpenGLPixelTransferOptions>
#include <QDebug>
#include <cmath>

extern "C" {
#include <libavutil/pixdesc.h>
}

static const int SYNC_THRESHOLD_MS = 40;

namespace {
constexpr double kSpeedEpsilon = 0.001;

bool IsSupportedSpeed(double speed)
{
    return std::fabs(speed - 0.5) < kSpeedEpsilon
        || std::fabs(speed - 1.0) < kSpeedEpsilon
        || std::fabs(speed - 1.5) < kSpeedEpsilon
        || std::fabs(speed - 2.0) < kSpeedEpsilon;
}

QString PixelFormatName(int format)
{
    const char* name = av_get_pix_fmt_name(static_cast<AVPixelFormat>(format));
    return name ? QString::fromUtf8(name) : QString("unknown(%1)").arg(format);
}

QString ColorRangeName(AVColorRange range)
{
    const char* name = av_color_range_name(range);
    return name ? QString::fromUtf8(name) : QString("unknown(%1)").arg(static_cast<int>(range));
}

QString ColorSpaceName(AVColorSpace colorSpace)
{
    const char* name = av_color_space_name(colorSpace);
    return name ? QString::fromUtf8(name) : QString("unknown(%1)").arg(static_cast<int>(colorSpace));
}

QString ColorPrimariesName(AVColorPrimaries primaries)
{
    const char* name = av_color_primaries_name(primaries);
    return name ? QString::fromUtf8(name) : QString("unknown(%1)").arg(static_cast<int>(primaries));
}

QString ColorTransferName(AVColorTransferCharacteristic transfer)
{
    const char* name = av_color_transfer_name(transfer);
    return name ? QString::fromUtf8(name) : QString("unknown(%1)").arg(static_cast<int>(transfer));
}
}

VideoRender::VideoRender(QWidget* parent)
    : QOpenGLWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(320, 240);
}

VideoRender::~VideoRender()
{
    StopRendering();
    makeCurrent();
    FreeTextures();
    m_frameMutex.lock();
    av_frame_free(&m_displayFrame);
    m_frameMutex.unlock();
    doneCurrent();
}

void VideoRender::InitVideoRender(std::shared_ptr<SyncQueue<AVFrame*>> frameQueue,
                                  std::shared_ptr<syncmanager> syncManager,
                                  double frameRate,
                                  double videoTimeBase)
{
    m_frameQueue = frameQueue;
    m_syncManager = syncManager;
    m_videoTimeBase = videoTimeBase;

    if (frameRate > 0 && frameRate < 120) {
        m_frameIntervalMs = qRound(1000.0 / frameRate);
    }
}

void VideoRender::StartRendering()
{
    m_running = true;
    m_rendered = 0;
    m_dropped = 0;
    QTimer::singleShot(0, this, &VideoRender::RenderFrame);
}

void VideoRender::StopRendering()
{
    m_running = false;
    if (m_frameQueue) {
        m_frameQueue->WakeAll();
    }
}

void VideoRender::SetSpeed(double speed)
{
    if (!std::isfinite(speed) || !IsSupportedSpeed(speed)) {
        qWarning() << "VideoRender rejected unsupported speed:" << speed;
        return;
    }

    m_speed.store(speed);
}

void VideoRender::RenderFrame()
{
    if (!m_running || !m_frameQueue || !m_syncManager) return;

    const double speedSnapshot = m_speed.load();
    const int scaledInterval = qMax(1, qRound(m_frameIntervalMs / speedSnapshot));

    // 1. 查看队首帧
    AVFrame* nextFrame = nullptr;
    if (!m_frameQueue->Front(nextFrame) || !nextFrame) {
        ScheduleNextFrame(scaledInterval);
        return;
    }

    const SYNCTYPE syncType = m_syncManager->SyncType();
    if (syncType == SYNCTYPE::AUDIO) {
        double videoPts = nextFrame->pts * m_videoTimeBase * 1000.0;
        double audioPts = m_syncManager->GetClock() - 20;
        double diff = videoPts - audioPts;

        // 2. 帧来得太早 → 精确等待后重判
        if (diff > SYNC_THRESHOLD_MS) {
            int waitMs = qMin(qRound(diff), 30);
            ScheduleNextFrame(waitMs);
            return;
        }

        // 3. 帧来得太晚 → 丢帧追赶，立即判下一帧
        if (diff < -SYNC_THRESHOLD_MS) {
            AVFrame* drop = nullptr;
            if (m_frameQueue->TryPop(drop) && drop) {
                av_frame_free(&drop);
                m_dropped++;
            }
            ScheduleNextFrame(0);
            return;
        }
    }

    // 4. 在同步窗口内 → 渲染
    AVFrame* newFrame = nullptr;
    if (!m_frameQueue->TryPop(newFrame) || !newFrame) {
        ScheduleNextFrame(scaledInterval);
        return;
    }

    m_frameMutex.lock();
    av_frame_free(&m_displayFrame);
    m_displayFrame = newFrame;
    m_videoW = newFrame->width;
    m_videoH = newFrame->height;
    m_sar = newFrame->sample_aspect_ratio;
    m_hasFrame = true;
    m_frameMutex.unlock();

    m_rendered++;
    update();

    if (syncType == SYNCTYPE::VIDEO &&
        newFrame->pts != AV_NOPTS_VALUE &&
        m_videoTimeBase > 0.0) {
        const double videoPts = newFrame->pts * m_videoTimeBase * 1000.0;
        m_syncManager->RefreshClock(videoPts);
    }

    // 5. 调度下一次：按帧间隔
    ScheduleNextFrame(scaledInterval);
}

void VideoRender::ScheduleNextFrame(int delayMs)
{
    QTimer::singleShot(delayMs, this, &VideoRender::RenderFrame);
}

void VideoRender::initializeGL()
{
    initializeOpenGLFunctions();
    glClearColor(0, 0, 0, 1);

    const char* vs = R"(
        attribute vec4 vertexIn;
        attribute vec2 textureIn;
        varying vec2 textureOut;
        void main() {
            gl_Position = vertexIn;
            textureOut = textureIn;
        })";

    const char* fs = R"(
        varying vec2 textureOut;
        uniform sampler2D tex_y, tex_u, tex_v;
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

    m_shader.addShaderFromSourceCode(QOpenGLShader::Vertex, vs);
    m_shader.addShaderFromSourceCode(QOpenGLShader::Fragment, fs);
    m_shader.link();
    m_shader.bind();
    m_shader.setUniformValue("tex_y", 0);
    m_shader.setUniformValue("tex_u", 1);
    m_shader.setUniformValue("tex_v", 2);
    m_shader.release();

    m_glInited = true;
}

void VideoRender::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

void VideoRender::paintGL()
{
    if (!m_hasFrame) {
        glClear(GL_COLOR_BUFFER_BIT);
        return;
    }

    m_frameMutex.lock();
    AVFrame* frame = m_displayFrame;
    bool hasFrame = m_hasFrame;
    m_frameMutex.unlock();

    if (!hasFrame || !frame) {
        glClear(GL_COLOR_BUFFER_BIT);
        return;
    }

    const bool supported = IsSupportedFrameFormat(frame);
    LogFrameFormatIfNeeded(frame, supported);
    if (!supported) {
        glClear(GL_COLOR_BUFFER_BIT);
        return;
    }

    UploadTexture(frame);
    DrawFrame();
}

void VideoRender::UploadTexture(AVFrame* f)
{
    if (m_texY && (m_texY->width() != f->width || m_texY->height() != f->height)) {
        FreeTextures();
    }

    if (!m_texY) {
        auto createTex = [f](int w, int h) -> QOpenGLTexture* {
            auto* t = new QOpenGLTexture(QOpenGLTexture::Target2D);
            t->setSize(w, h);
            t->setFormat(QOpenGLTexture::R8_UNorm);
            t->setMinMagFilters(QOpenGLTexture::Linear, QOpenGLTexture::Linear);
            t->setWrapMode(QOpenGLTexture::ClampToEdge);
            t->allocateStorage();
            return t;
        };
        m_texY = createTex(f->width, f->height);
        m_texU = createTex(f->width/2, f->height/2);
        m_texV = createTex(f->width/2, f->height/2);
    }

    QOpenGLPixelTransferOptions opt;

    opt.setRowLength(f->linesize[0]);
    m_texY->setData(QOpenGLTexture::Red, QOpenGLTexture::UInt8, f->data[0], &opt);

    opt.setRowLength(f->linesize[1]);
    m_texU->setData(QOpenGLTexture::Red, QOpenGLTexture::UInt8, f->data[1], &opt);

    opt.setRowLength(f->linesize[2]);
    m_texV->setData(QOpenGLTexture::Red, QOpenGLTexture::UInt8, f->data[2], &opt);
}

bool VideoRender::IsSupportedFrameFormat(const AVFrame* frame) const
{
    return frame && frame->format == AV_PIX_FMT_YUV420P;
}

void VideoRender::LogFrameFormatIfNeeded(const AVFrame* frame, bool supported)
{
    if (!frame) {
        return;
    }

    const bool changed = !m_hasLoggedFrameFormat
        || m_lastLoggedFormat != static_cast<AVPixelFormat>(frame->format)
        || m_lastLoggedWidth != frame->width
        || m_lastLoggedHeight != frame->height
        || m_lastLoggedLinesize[0] != frame->linesize[0]
        || m_lastLoggedLinesize[1] != frame->linesize[1]
        || m_lastLoggedLinesize[2] != frame->linesize[2]
        || m_lastLoggedLinesize[3] != frame->linesize[3]
        || m_lastLoggedColorRange != frame->color_range
        || m_lastLoggedColorSpace != frame->colorspace
        || m_lastLoggedColorPrimaries != frame->color_primaries
        || m_lastLoggedColorTrc != frame->color_trc
        || m_lastLoggedSupported != supported;
    if (!changed) {
        return;
    }

    m_hasLoggedFrameFormat = true;
    m_lastLoggedFormat = static_cast<AVPixelFormat>(frame->format);
    m_lastLoggedWidth = frame->width;
    m_lastLoggedHeight = frame->height;
    for (int index = 0; index < 4; ++index) {
        m_lastLoggedLinesize[index] = frame->linesize[index];
    }
    m_lastLoggedColorRange = frame->color_range;
    m_lastLoggedColorSpace = frame->colorspace;
    m_lastLoggedColorPrimaries = frame->color_primaries;
    m_lastLoggedColorTrc = frame->color_trc;
    m_lastLoggedSupported = supported;

    const QString prefix = supported
        ? "VideoRender accepted frame format:"
        : "VideoRender unsupported frame format:";
    qInfo().noquote()
        << prefix
        << QString("pix_fmt=%1").arg(PixelFormatName(frame->format))
        << QString("size=%1x%2").arg(frame->width).arg(frame->height)
        << QString("linesize=[%1,%2,%3,%4]")
              .arg(frame->linesize[0])
              .arg(frame->linesize[1])
              .arg(frame->linesize[2])
              .arg(frame->linesize[3])
        << QString("color_range=%1").arg(ColorRangeName(frame->color_range))
        << QString("colorspace=%1").arg(ColorSpaceName(frame->colorspace))
        << QString("color_primaries=%1").arg(ColorPrimariesName(frame->color_primaries))
        << QString("color_trc=%1").arg(ColorTransferName(frame->color_trc));
}

void VideoRender::DrawFrame()
{
    if (!m_glInited) return;

    m_shader.bind();
    m_texY->bind(0);
    m_texU->bind(1);
    m_texV->bind(2);

    float x, y, w, h;
    CalcRect(x, y, w, h);

    GLfloat verts[] = {x,y, x+w,y, x,y+h, x+w,y+h};
    GLfloat texs[] = {0,1, 1,1, 0,0, 1,0};

    m_shader.setAttributeArray("vertexIn", verts, 2);
    m_shader.setAttributeArray("textureIn", texs, 2);
    m_shader.enableAttributeArray("vertexIn");
    m_shader.enableAttributeArray("textureIn");

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    m_shader.disableAttributeArray("vertexIn");
    m_shader.disableAttributeArray("textureIn");
    m_shader.release();
}

void VideoRender::CalcRect(float& x, float& y, float& w, float& h)
{
    int vw = width(), vh = height();
    if (m_videoW <= 0 || m_videoH <= 0 || vw <= 0 || vh <= 0) {
        x = y = -1; w = h = 2;
        return;
    }

    double sar = (m_sar.num && m_sar.den) ? (double)m_sar.num / m_sar.den : 1.0;
    double dar = (double)m_videoW * sar / m_videoH;
    double war = (double)vw / vh;

    if (dar > war) {
        w = 2.0f;
        h = 2.0f * war / dar;
    } else {
        h = 2.0f;
        w = 2.0f * dar / war;
    }
    x = -w / 2;
    y = -h / 2;
}

void VideoRender::FreeTextures()
{
    delete m_texY; m_texY = nullptr;
    delete m_texU; m_texU = nullptr;
    delete m_texV; m_texV = nullptr;
}
