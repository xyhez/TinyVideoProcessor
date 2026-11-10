#include "videorender.h"
#include "tiny_log/Logging.h"
#include <QOpenGLPixelTransferOptions>
#include <utility>

extern "C" {
#include <libavutil/pixdesc.h>
}

namespace {
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
    makeCurrent();
    FreeTextures();
    ClearCurrentFrame(false);
    doneCurrent();
}

void VideoRender::PresentFrame(AVFramePtr frame)
{
    if (!frame) {
        return;
    }

    m_frameMutex.lock();
    AVFrame* rawFrame = frame.get();
    m_displayFrame = std::move(frame);
    m_videoW = rawFrame->width;
    m_videoH = rawFrame->height;
    m_sar = rawFrame->sample_aspect_ratio;
    m_colorRange = rawFrame->color_range;
    m_colorSpace = rawFrame->colorspace;
    m_hasFrame = true;
    m_frameMutex.unlock();

    update();
}

void VideoRender::ClearFrame()
{
    ClearCurrentFrame(true);
}

void VideoRender::ClearCurrentFrame(bool requestUpdate)
{
    m_frameMutex.lock();
    m_displayFrame.reset();
    m_videoW = 0;
    m_videoH = 0;
    m_sar = {1, 1};
    m_hasFrame = false;
    m_frameMutex.unlock();

    if (requestUpdate) {
        update();
    }
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
        uniform int bt709;
        uniform float rangeOffset;
        uniform float rangeScale;
        void main() {
            float y = (texture2D(tex_y, textureOut).r - rangeOffset) * rangeScale;
            float u = texture2D(tex_u, textureOut).r - 0.5;
            float v = texture2D(tex_v, textureOut).r - 0.5;

            float r;
            float g;
            float b;
            if (bt709 == 1) {
                r = y + 1.793 * v;
                g = y - 0.213 * u - 0.533 * v;
                b = y + 2.112 * u;
            } else {
                r = y + 1.596 * v;
                g = y - 0.391 * u - 0.813 * v;
                b = y + 2.018 * u;
            }

            gl_FragColor = vec4(clamp(vec3(r, g, b), 0.0, 1.0), 1.0);
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
    // 调度线程与 GUI 线程并发：绘制期间持有显示帧锁，避免 m_displayFrame
    // 被替换后 UploadTexture 使用悬空指针。单路回退路径允许短暂阻塞提交。
    m_frameMutex.lock();

    if (!m_hasFrame || !m_displayFrame) {
        m_frameMutex.unlock();
        glClear(GL_COLOR_BUFFER_BIT);
        return;
    }

    AVFrame* frame = m_displayFrame.get();
    const bool supported = IsSupportedFrameFormat(frame);
    LogFrameFormatIfNeeded(frame, supported);
    if (!supported) {
        m_frameMutex.unlock();
        glClear(GL_COLOR_BUFFER_BIT);
        return;
    }

    UploadTexture(frame);
    DrawFrame();
    m_frameMutex.unlock();
}

void VideoRender::UploadTexture(AVFrame* f)
{
    // OpenGL 上传职责：只消费当前显示帧的数据，不接管或释放 AVFrame。
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
    LOG_INFO_STREAM().noquote()
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
    // OpenGL 绘制职责：只绘制已上传纹理，不参与同步调度和 AVFrame 生命周期管理。
    if (!m_glInited) return;

    m_shader.bind();
    m_texY->bind(0);
    m_texU->bind(1);
    m_texV->bind(2);

    const bool fullRange = m_colorRange == AVCOL_RANGE_JPEG;
    const bool bt709 = m_colorSpace == AVCOL_SPC_BT709
        || m_colorSpace == AVCOL_SPC_BT2020_NCL
        || m_colorSpace == AVCOL_SPC_BT2020_CL;
    m_shader.setUniformValue("rangeOffset", fullRange ? 0.0f : 16.0f / 255.0f);
    m_shader.setUniformValue("rangeScale", fullRange ? 1.0f : 255.0f / 219.0f);
    m_shader.setUniformValue("bt709", bt709 ? 1 : 0);

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

