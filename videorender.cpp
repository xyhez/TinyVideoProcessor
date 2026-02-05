#include "videorender.h"
#include <QDebug>
#include <QOpenGLPixelTransferOptions>

static const int SYNC_THRESHOLD_MS = 40;

VideoRender::VideoRender(QWidget* parent)
    : QOpenGLWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(320, 240);
}

VideoRender::~VideoRender()
{
    stopRendering();
    makeCurrent();
    freeTextures();
    m_frameMutex.lock();
    av_frame_free(&m_displayFrame);
    m_frameMutex.unlock();
    doneCurrent();
}

void VideoRender::initvideoRender(std::shared_ptr<std::deque<AVFrame*>> frameQueue,
                                  std::shared_ptr<syncmanager> syncManager)
{
    m_frameQueue = frameQueue;
    m_syncManager = syncManager;

    if (g_videoinfo.FrameRate > 0 && g_videoinfo.FrameRate < 120) {
        m_frameInterval = qRound(1000.0 / g_videoinfo.FrameRate);
    }
}

void VideoRender::startRendering()
{
    if (!m_timer) {
        m_timer = new QTimer(this);
        connect(m_timer, &QTimer::timeout, this, &VideoRender::renderFrame);
    }
    m_timer->start(m_frameInterval);
    m_rendered = 0;
    m_dropped = 0;
}

void VideoRender::stopRendering()
{
    if (m_timer) {
        m_timer->stop();
    }
    cv_V_frame_NotFull.notify_all();
}

void VideoRender::renderFrame()
{
    if (!m_frameQueue || !m_syncManager) return;

    AVFrame* nextFrame = nullptr;
    {
        std::lock_guard<std::mutex> lock(mtx_V_frame);
        if (!m_frameQueue->empty()) {
            nextFrame = m_frameQueue->front();
        }
    }

    if (!nextFrame) {
        return;
    }

    double videoPts = nextFrame->pts * g_videoinfo.V_timeBase * 1000.0;
    double audioPts = m_syncManager->getClock();
    double diff = videoPts - audioPts;
    qDebug()<<"时钟:"<<audioPts<<"  当前视频帧的时间:"<<videoPts<<" 差距:"<<diff;

    if (diff > SYNC_THRESHOLD_MS) {
        int waitMs = qMin(qRound(diff), 30);
        m_timer->setInterval(waitMs);
        return;
    }

    if (diff < -SYNC_THRESHOLD_MS) {
        AVFrame* drop = nullptr;
        {
            std::lock_guard<std::mutex> lock(mtx_V_frame);
            if (!m_frameQueue->empty()) {
                drop = m_frameQueue->front();
                m_frameQueue->pop_front();
            }
        }
        if (drop) {
            av_frame_free(&drop);
            m_dropped++;
            cv_V_frame_NotFull.notify_one();
        }
        m_timer->setInterval(0);
        return;
    }

    AVFrame* newFrame = nullptr;
    {
        std::lock_guard<std::mutex> lock(mtx_V_frame);
        if (!m_frameQueue->empty()) {
            newFrame = m_frameQueue->front();
            m_frameQueue->pop_front();
        }
    }

    if (!newFrame) return;

    m_frameMutex.lock();
    av_frame_free(&m_displayFrame);
    m_displayFrame = newFrame;
    m_videoW = newFrame->width;
    m_videoH = newFrame->height;
    m_sar = newFrame->sample_aspect_ratio;
    m_hasFrame = true;
    m_frameMutex.unlock();

    cv_V_frame_NotFull.notify_one();
    m_rendered++;

    m_timer->setInterval(m_frameInterval);
    update();
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

    uploadTexture(frame);
    drawFrame();
}

void VideoRender::uploadTexture(AVFrame* f)
{
    if (f->format != AV_PIX_FMT_YUV420P) return;

    if (m_texY && (m_texY->width() != f->width || m_texY->height() != f->height)) {
        freeTextures();
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

void VideoRender::drawFrame()
{
    if (!m_glInited) return;

    m_shader.bind();
    m_texY->bind(0);
    m_texU->bind(1);
    m_texV->bind(2);

    float x, y, w, h;
    calcRect(x, y, w, h);

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

void VideoRender::calcRect(float& x, float& y, float& w, float& h)
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

void VideoRender::freeTextures()
{
    delete m_texY; m_texY = nullptr;
    delete m_texU; m_texU = nullptr;
    delete m_texV; m_texV = nullptr;
}
