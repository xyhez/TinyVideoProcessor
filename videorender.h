#ifndef VIDEORENDER_H
#define VIDEORENDER_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QTimer>
#include <QMutex>
#include <QElapsedTimer>
#include <QSizePolicy>
#include <memory>

// 修改：包含项目中的头文件
#include "syncmanager.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/rational.h>
}

/**
 * @brief 视频渲染器（严格同步版）- 修复黑闪问题
 * @details 同步策略：视频提前(>30ms)则显示上一帧，落后(<-30ms)则丢帧追赶
 *          修复：避免不必要的清屏，确保总有一帧可显示
 */
class VideoRender : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    // 修改：适配项目的构造函数
    VideoRender(QWidget* parent = nullptr);
    ~VideoRender();

    // 修改：使用QVector<AVFrame*>作为帧队列
    void initvideoRender(std::shared_ptr<QVector<AVFrame*>> Vframeq,
                         std::shared_ptr<syncmanager> c,
                         AVRational time_base);

    void setVideoTimeBase(AVRational tb) { m_videoTimeBase = tb; }
    void startRendering();
    void stopRendering();
    void pause() { stopRendering(); }  // 适配项目的pause接口

    // 获取渲染统计
    int getFramesRendered() const { return m_framesRendered; }
    int getFramesDropped() const { return m_framesDropped; }

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private slots:
    void onRenderTimeout();

private:
    void uploadTexture(AVFrame* frame);
    void renderFrame(AVFrame* frame);
    void calcRect(float& x, float& y, float& w, float& h);
    void freeTextures();
    bool ensureFirstFrame();  // 确保第一帧可用
    bool syncAndGetNextFrame(AVFrame*& outFrame);  // 同步并获取下一帧

    // 修改：适配项目的帧队列类型
    std::shared_ptr<QVector<AVFrame*>> m_videoFrameQueue;
    std::shared_ptr<syncmanager> m_syncManager;
    AVRational m_videoTimeBase;
    int m_framesDropped = 0;
    int m_framesRendered = 0;

    // OpenGL资源
    QOpenGLShaderProgram m_prog;
    QOpenGLTexture* m_texY = nullptr;
    QOpenGLTexture* m_texU = nullptr;
    QOpenGLTexture* m_texV = nullptr;
    bool m_texInited = false;

    // 帧数据（m_last始终保存最新有效帧）
    AVFrame* m_last = nullptr;          ///< 上一帧（始终有效）
    bool m_lastValid = false;           ///< m_last是否可渲染
    QMutex m_frameMutex;
    int m_videoW = 0;
    int m_videoH = 0;
    AVRational m_videoSar = {1, 1};

    // 渲染控制
    QTimer* m_renderTimer = nullptr;
    int m_surfaceW = 0;
    int m_surfaceH = 0;

    // 同步计时器
    QElapsedTimer m_clockTimer;
    double m_lastPts = 0.0;
    double m_lastFrameDuration = 33.0;  // 默认33ms（30fps）

    // 调试统计
    int m_consecutiveBlacks = 0;
    bool m_wasCleared = false;
};

#endif // VIDEORENDER_H
