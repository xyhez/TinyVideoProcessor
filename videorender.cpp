#include "videorender.h"

// 包含必要的头文件
#include <QDebug>
#include <QOpenGLPixelTransferOptions>
#include <QElapsedTimer>
#include <QMutexLocker>

// 严格同步阈值：±30ms
static const double SYNC_THRESHOLD = 50.0;

// 最大连续黑屏数
static const int MAX_CONSECUTIVE_BLACKS = 3;

// 修改：适配项目的构造函数
VideoRender::VideoRender(QWidget* parent)
    : QOpenGLWidget(parent)
    , m_lastValid(false)
{
    setAutoFillBackground(false);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_NoSystemBackground);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumSize(100, 100);
    m_videoTimeBase = av_make_q(1, 1000);
}

// 修改：添加初始化函数
void VideoRender::initvideoRender(std::shared_ptr<QVector<AVFrame*>> Vframeq,
                                  std::shared_ptr<syncmanager> c,
                                  AVRational time_base)
{
    m_videoFrameQueue = Vframeq;
    m_syncManager = c;
    m_videoTimeBase = time_base;
}

VideoRender::~VideoRender()
{
    stopRendering();
    if (isValid()) {
        makeCurrent();
        freeTextures();
        QMutexLocker locker(&m_frameMutex);
        av_frame_free(&m_last);
        doneCurrent();
    }

}

void VideoRender::startRendering()
{
    if (!m_renderTimer) {
        m_renderTimer = new QTimer(this);
        m_renderTimer->setTimerType(Qt::PreciseTimer);
        connect(m_renderTimer, &QTimer::timeout, this, &VideoRender::onRenderTimeout);
    }

    m_clockTimer.start();
    m_renderTimer->start(33);
    m_lastPts = 0.0;
    m_framesRendered = 0;
    m_framesDropped = 0;
    m_lastValid = false;
    m_consecutiveBlacks = 0;
    m_wasCleared = false;
}

void VideoRender::stopRendering()
{
    if (m_renderTimer) {
        m_renderTimer->stop();
        m_renderTimer->deleteLater();
        m_renderTimer = nullptr;
    }
}

void VideoRender::onRenderTimeout()
{
    update();
}

/* -------------------------------------------------- */
/* OpenGL初始化 */
/* -------------------------------------------------- */

void VideoRender::initializeGL()
{
    initializeOpenGLFunctions();
    // 初始清屏一次
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    const char* vsrc = R"(
        attribute vec4 vertexIn;
        attribute vec2 textureIn;
        varying   vec2 textureOut;
        void main(void){
            gl_Position = vertexIn;
            textureOut  = textureIn;
        })";

    const char* fsrc = R"(
        varying vec2 textureOut;
        uniform sampler2D tex_y;
        uniform sampler2D tex_u;
        uniform sampler2D tex_v;
        void main(void){
            float y = texture2D(tex_y, textureOut).r;
            float u = texture2D(tex_u, textureOut).r - 0.5;
            float v = texture2D(tex_v, textureOut).r - 0.5;
            float r = y + 1.402 * v;
            float g = y - 0.344 * u - 0.714 * v;
            float b = y + 1.772 * u;
            gl_FragColor = vec4(r, g, b, 1.0);
        })";

    if (!m_prog.addShaderFromSourceCode(QOpenGLShader::Vertex, vsrc) ||
        !m_prog.addShaderFromSourceCode(QOpenGLShader::Fragment, fsrc) ||
        !m_prog.link()) {
        qWarning() << "Shader error:" << m_prog.log();
    }

    m_prog.bind();
    m_prog.setUniformValue("tex_y", 0);
    m_prog.setUniformValue("tex_u", 1);
    m_prog.setUniformValue("tex_v", 2);
    m_prog.release();
}

void VideoRender::resizeGL(int w, int h)
{
    m_surfaceW = w;
    m_surfaceH = h;
    glViewport(0, 0, w, h);
}

/* -------------------------------------------------- */
/* 辅助函数 */
/* -------------------------------------------------- */

bool VideoRender::ensureFirstFrame()
{
    if (m_lastValid) {
        return true;
    }

    // 尝试从队列获取第一帧
    if (m_videoFrameQueue && m_videoFrameQueue->size() > 0) {
        QMutexLocker locker(&m_frameMutex);
        av_frame_free(&m_last);

        // 修改：适配QVector的访问方式
        if (!m_videoFrameQueue->isEmpty()) {
            m_last = m_videoFrameQueue->first();
            m_videoFrameQueue->removeFirst();
        }

        if (m_last && m_last->pts != AV_NOPTS_VALUE) {
            uploadTexture(m_last);
            if (m_texInited) {
                m_lastValid = true;
                m_lastPts = m_last->pts * av_q2d(m_last->time_base) * 1000.0; // 转换为毫秒
                m_videoW = m_last->width;
                m_videoH = m_last->height;
                m_videoSar = m_last->sample_aspect_ratio;
                qDebug() << "VideoRender: First frame loaded, PTS=" << m_lastPts;
                m_consecutiveBlacks = 0;
                return true;
            }
        }
    }

    // 如果没有帧，保持黑屏但限制频率
    m_consecutiveBlacks++;
    if (m_consecutiveBlacks > MAX_CONSECUTIVE_BLACKS) {
        if (m_renderTimer) {
            m_renderTimer->setInterval(33);
        }
    }
    return false;
}

bool VideoRender::syncAndGetNextFrame(AVFrame*& outFrame)
{
    outFrame = nullptr;

    if (!m_videoFrameQueue || !m_syncManager) {
        return false;
    }

    // 获取下一帧预览
    AVFrame* nextFrame = nullptr;
    if (!m_videoFrameQueue->isEmpty()) {
        nextFrame = m_videoFrameQueue->first();  // 只预览，不取出
    }

    if (!nextFrame || nextFrame->pts == AV_NOPTS_VALUE) {
        return false; // 没有新帧
    }

    double videoPts = nextFrame->pts * av_q2d(nextFrame->time_base) * 1000.0; // 毫秒
    double audioClock = m_syncManager->getClock();  // 修改：使用getClock接口
    double diff = videoPts - audioClock;

    // 1. 视频提前 >30ms：等待音频
    if (diff > SYNC_THRESHOLD) {
        // 计算等待时间（但最小为1ms）
        int delayMs = static_cast<int>(diff - SYNC_THRESHOLD);
        delayMs = qBound(1, delayMs, 50);
        if (m_renderTimer) {
            m_renderTimer->setInterval(delayMs);
        }

        // 使用上一帧
        if (m_lastValid) {
            outFrame = m_last;
            return true;
        }
        return false;
    }

    // 2. 视频落后 <-30ms：丢帧追赶
    if (diff < -SYNC_THRESHOLD) {
        // 只丢一帧，避免连续丢帧导致卡顿
        AVFrame* dropFrame = nullptr;
        if (!m_videoFrameQueue->isEmpty()) {
            QMutexLocker locker(&m_frameMutex);
            dropFrame = m_videoFrameQueue->takeFirst();
        }

        if (dropFrame) {
            m_framesDropped++;
            // qDebug()<<"丢弃视频帧";
            av_frame_free(&dropFrame);
        }

        // 立即重试
        if (m_renderTimer) {
            m_renderTimer->setInterval(0);
        }
        return false;
    }

    // 3. 同步良好：取出当前帧
    QMutexLocker locker(&m_frameMutex);
    av_frame_free(&m_last);

    if (!m_videoFrameQueue->isEmpty()) {
        m_last = m_videoFrameQueue->takeFirst();
    } else {
        return false;
    }

    if (!m_last) {
        return false;
    }

    // 更新帧信息
    m_lastPts = m_last->pts * av_q2d(m_last->time_base) * 1000.0;
    m_videoW = m_last->width;
    m_videoH = m_last->height;
    m_videoSar = m_last->sample_aspect_ratio;

    outFrame = m_last;
    m_lastValid = true;
    m_consecutiveBlacks = 0;

    return true;
}

/* -------------------------------------------------- */
/* 主渲染循环 - 修复黑闪问题 */
/* -------------------------------------------------- */

void VideoRender::paintGL()
{
    // 只在首次或没有有效帧时清屏
    if (!m_lastValid || !m_texInited) {
        if (!m_wasCleared) {
            glClear(GL_COLOR_BUFFER_BIT);
            m_wasCleared = true;
        }

        // 尝试获取第一帧
        if (!ensureFirstFrame()) {
            if (m_renderTimer) {
                m_renderTimer->setInterval(10);
            }
            return;
        }
        m_wasCleared = false;
    }

    if (!m_syncManager || !m_renderTimer) {
        return;
    }

    // 同步并获取下一帧
    AVFrame* frameToRender = nullptr;
    if (syncAndGetNextFrame(frameToRender)) {
        // 有帧可渲染
        renderFrame(frameToRender);
        m_framesRendered++;

        // 计算下一帧间隔（基于帧率）
        if (m_lastFrameDuration > 0 && m_renderTimer) {
            m_renderTimer->setInterval(qBound(1, static_cast<int>(m_lastFrameDuration), 50));
        } else if (m_renderTimer) {
            m_renderTimer->setInterval(33); // 默认30fps
        }
    } else if (m_lastValid) {
        // 没有新帧，但有上一帧可显示
        renderFrame(m_last);
        if (m_renderTimer) {
            m_renderTimer->setInterval(10); // 降低刷新率等待新帧
        }
    } else {
        // 完全没有帧，保持当前状态（不清屏，显示上一帧或黑屏）
        if (m_renderTimer) {
            m_renderTimer->setInterval(33);
        }
    }
}

/* -------------------------------------------------- */
/* 纹理上传与渲染 */
/* -------------------------------------------------- */

void VideoRender::uploadTexture(AVFrame* frame)
{
    if (!frame || frame->format != AV_PIX_FMT_YUV420P) {
        // 格式不支持，但保留已有纹理
        return;
    }

    try {
        // 尺寸变化时重新创建纹理
        if (m_texY && (m_texY->width() != frame->width || m_texY->height() != frame->height)) {
            freeTextures();
        }

        if (!m_texY) {
            m_texY = new QOpenGLTexture(QOpenGLTexture::Target2D);
            m_texU = new QOpenGLTexture(QOpenGLTexture::Target2D);
            m_texV = new QOpenGLTexture(QOpenGLTexture::Target2D);

            for (auto t : {m_texY, m_texU, m_texV}) {
                if (t) {
                    t->setMinMagFilters(QOpenGLTexture::Linear, QOpenGLTexture::Linear);
                    t->setWrapMode(QOpenGLTexture::ClampToEdge);
                }
            }

            if (m_texY) m_texY->setSize(frame->width, frame->height);
            if (m_texU) m_texU->setSize(frame->width / 2, frame->height / 2);
            if (m_texV) m_texV->setSize(frame->width / 2, frame->height / 2);

            for (auto t : {m_texY, m_texU, m_texV}) {
                if (t) {
                    t->setFormat(QOpenGLTexture::R8_UNorm);
                    t->allocateStorage();
                }
            }
        }

        // 上传数据
        QOpenGLPixelTransferOptions opt;
        opt.setRowLength(frame->linesize[0]);
        if (m_texY) {
            m_texY->setData(QOpenGLTexture::Red, QOpenGLTexture::UInt8, frame->data[0], &opt);
        }

        opt.setRowLength(frame->linesize[1]);
        if (m_texU) {
            m_texU->setData(QOpenGLTexture::Red, QOpenGLTexture::UInt8, frame->data[1], &opt);
        }

        opt.setRowLength(frame->linesize[2]);
        if (m_texV) {
            m_texV->setData(QOpenGLTexture::Red, QOpenGLTexture::UInt8, frame->data[2], &opt);
        }

        m_texInited = true;
    } catch (...) {
        qWarning() << "uploadTexture: Failed to upload texture data";
        // 不重置m_texInited，保留上一帧纹理
    }
}

void VideoRender::renderFrame(AVFrame* frame)
{
    if (!frame || !m_texInited) {
        // 如果没有有效纹理，尝试使用上一帧
        if (m_lastValid && m_last) {
            frame = m_last;
        } else {
            return; // 无帧可渲染
        }
    }

    // 确保纹理最新
    uploadTexture(frame);

    if (!m_prog.bind()) {
        qWarning() << "renderFrame: Failed to bind shader program";
        return;
    }

    if (m_texY) m_texY->bind(0);
    if (m_texU) m_texU->bind(1);
    if (m_texV) m_texV->bind(2);

    float x, y, w, h;
    calcRect(x, y, w, h);

    GLfloat verts[] = {x, y, x + w, y, x, y + h, x + w, y + h};
    static const GLfloat tex[] = {0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f};

    m_prog.enableAttributeArray("vertexIn");
    m_prog.setAttributeArray("vertexIn", verts, 2);
    m_prog.enableAttributeArray("textureIn");
    m_prog.setAttributeArray("textureIn", tex, 2);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    m_prog.disableAttributeArray("vertexIn");
    m_prog.disableAttributeArray("textureIn");
    m_prog.release();

    if (m_texY) m_texY->release();
    if (m_texU) m_texU->release();
    if (m_texV) m_texV->release();
}

/* -------------------------------------------------- */
/* 工具函数 */
/* -------------------------------------------------- */

void VideoRender::calcRect(float& x, float& y, float& w, float& h)
{
    const int widgetWidth = m_surfaceW;
    const int widgetHeight = m_surfaceH;

    if (m_videoW <= 0 || m_videoH <= 0 || widgetWidth <= 0 || widgetHeight <= 0) {
        x = y = w = h = 0;
        return;
    }

    double sar = (m_videoSar.num && m_videoSar.den) ? (double)m_videoSar.num / m_videoSar.den : 1.0;
    double videoDar = (double)m_videoW * sar / m_videoH;
    double widgetAr = (double)widgetWidth / widgetHeight;

    if (videoDar > widgetAr) {
        w = 2.0f;
        h = 2.0f * widgetAr / videoDar;
    } else {
        h = 2.0f;
        w = 2.0f * videoDar / widgetAr;
    }

    x = -w / 2.0f;
    y = -h / 2.0f;
}

void VideoRender::freeTextures()
{
    delete m_texY; m_texY = nullptr;
    delete m_texU; m_texU = nullptr;
    delete m_texV; m_texV = nullptr;
    m_texInited = false;
}
