#ifndef VIDEORENDER_H
#define VIDEORENDER_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QTimer>
#include <QMutex>
#include <memory>

#include "infrastructure/syncmanager.h"
#include "core/queue.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/rational.h>
}

/**
 * @brief 视频渲染器 - 极简事件驱动版
 * @details 核心逻辑：Timer按视频帧率触发 → 取帧 → 同步判断 → 渲染/等待/丢帧
 */
class VideoRender : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    explicit VideoRender(QWidget* parent = nullptr);
    ~VideoRender();

    // 初始化：传入帧队列、同步管理器、视频帧率（关键！）
    void initvideoRender(std::shared_ptr<SyncQueue<AVFrame*>> frameQueue,
                         std::shared_ptr<syncmanager> syncManager,
                         double frameRate,
                         double videoTimeBase);

    void startRendering();
    void stopRendering();
    void pause() { stopRendering(); }

private slots:
    void renderFrame();

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private:
    void uploadTexture(AVFrame* frame);
    void drawFrame();
    void calcRect(float& x, float& y, float& w, float& h);
    void freeTextures();

    std::shared_ptr<SyncQueue<AVFrame*>> m_frameQueue;
    std::shared_ptr<syncmanager> m_syncManager;

    QOpenGLShaderProgram m_shader;
    QOpenGLTexture *m_texY = nullptr, *m_texU = nullptr, *m_texV = nullptr;
    bool m_glInited = false;

    AVFrame* m_displayFrame = nullptr;
    QMutex m_frameMutex;

    QTimer* m_timer = nullptr;
    int m_frameInterval = 33;
    double m_videoTimeBase = 0.0;
    bool m_hasFrame = false;

    int m_videoW = 0, m_videoH = 0;
    AVRational m_sar = {1, 1};

    int m_rendered = 0;
    int m_dropped = 0;
};

#endif // VIDEORENDER_H
