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
 * @brief 视频渲染器 - 自调度事件驱动版
 * @details 核心逻辑：自调度 RenderFrame → 取帧 → 同步判断 diff = frame.PTS - masterClock
 *          → diff > 阈值：ScheduleNextFrame(delay) 精确等待
 *          → diff < -阈值：丢帧，立即 ScheduleNextFrame(0) 追赶
 *          → 正常：渲染，ScheduleNextFrame(帧间隔)
 *          空闲时以帧间隔回退轮询，队列有帧则按需唤醒。
 *          参考 ffplay video_refresh() 的自调度模式。
 */
class VideoRender : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    explicit VideoRender(QWidget* parent = nullptr);
    ~VideoRender();

    // 初始化：传入帧队列、同步管理器、视频帧率（关键！）
    void InitVideoRender(std::shared_ptr<SyncQueue<AVFrame*>> frameQueue,
                         std::shared_ptr<syncmanager> syncManager,
                         double frameRate,
                         double videoTimeBase);

    void StartRendering();
    void StopRendering();
    void Pause() { StopRendering(); }

private slots:
    void RenderFrame();

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private:
    void UploadTexture(AVFrame* frame);
    void DrawFrame();
    void CalcRect(float& x, float& y, float& w, float& h);
    void FreeTextures();
    void ScheduleNextFrame(int delayMs);

    std::shared_ptr<SyncQueue<AVFrame*>> m_frameQueue;
    std::shared_ptr<syncmanager> m_syncManager;

    QOpenGLShaderProgram m_shader;
    QOpenGLTexture *m_texY = nullptr, *m_texU = nullptr, *m_texV = nullptr;
    bool m_glInited = false;

    AVFrame* m_displayFrame = nullptr;
    QMutex m_frameMutex;

    int m_frameIntervalMs = 33;
    double m_videoTimeBase = 0.0;
    bool m_hasFrame = false;
    bool m_running = false;

    int m_videoW = 0, m_videoH = 0;
    AVRational m_sar = {1, 1};

    int m_rendered = 0;
    int m_dropped = 0;
};

#endif // VIDEORENDER_H
