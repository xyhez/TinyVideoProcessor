#ifndef VIDEORENDER_H
#define VIDEORENDER_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QMutex>

#include "infrastructure/ivideoframesink.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libavutil/rational.h>
}

/**
 * @brief 视频渲染器
 * @details 默认视频帧输出目标和 OpenGL 绘制窗口。取帧、同步判断和调度由
 *          VideoFrameScheduler 负责，VideoRender 只接管可显示帧并完成本地绘制。
 */
class VideoRender : public QOpenGLWidget, public IVideoFrameSink, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    /**
     * @brief VideoRender 构造函数
     * @param parent Qt 父窗口
     */
    explicit VideoRender(QWidget* parent = nullptr);

    /**
     * @brief VideoRender 析构函数，释放 OpenGL 纹理和显示帧
     */
    ~VideoRender();

    /**
     * @brief PresentFrame 接管可显示帧并请求重绘
     * @details IVideoFrameSink 接口实现。frame 所有权转移给 VideoRender 的当前显示帧。
     * @param frame 可显示帧
     */
    void PresentFrame(AVFrame* frame) override;

    /**
     * @brief ClearFrame 清理当前显示帧
     * @details IVideoFrameSink 接口实现。只释放当前显示帧并刷新画面，
     *          不清空队列、不刷新 decoder、不重置同步时钟。
     */
    void ClearFrame() override;

protected:
    /**
     * @brief initializeGL 初始化 OpenGL 状态和纹理资源
     */
    void initializeGL() override;

    /**
     * @brief resizeGL 处理渲染窗口尺寸变化
     * @param w 窗口宽度
     * @param h 窗口高度
     */
    void resizeGL(int w, int h) override;

    /**
     * @brief paintGL 绘制当前显示帧
     */
    void paintGL() override;

private:
    /**
     * @brief ClearCurrentFrame 释放当前显示帧并重置显示状态
     * @param requestUpdate 是否请求重绘
     */
    void ClearCurrentFrame(bool requestUpdate);

    /**
     * @brief UploadTexture 上传 AVFrame 的 YUV 数据到纹理
     * @details OpenGL 绘制职责，只负责把当前可显示帧上传到纹理，
     *          不负责取帧、同步判断或帧释放。
     * @param frame 待上传的视频帧
     */
    void UploadTexture(AVFrame* frame);

    /**
     * @brief IsSupportedFrameFormat 判断当前渲染器是否支持该帧格式
     * @param frame 待判断的视频帧
     * @return true 支持渲染
     * @return false 不支持渲染
     */
    bool IsSupportedFrameFormat(const AVFrame* frame) const;

    /**
     * @brief LogFrameFormatIfNeeded 在格式签名变化时输出诊断日志
     * @param frame 当前视频帧
     * @param supported 是否为受支持格式
     */
    void LogFrameFormatIfNeeded(const AVFrame* frame, bool supported);

    /**
     * @brief DrawFrame 绘制当前纹理帧
     * @details OpenGL 绘制职责，只负责使用当前纹理和显示矩形完成绘制。
     */
    void DrawFrame();

    /**
     * @brief CalcRect 计算保持宽高比后的渲染矩形
     * @param x 输出矩形左下角 x
     * @param y 输出矩形左下角 y
     * @param w 输出矩形宽度
     * @param h 输出矩形高度
     */
    void CalcRect(float& x, float& y, float& w, float& h);

    /**
     * @brief FreeTextures 释放 OpenGL 纹理资源
     */
    void FreeTextures();

    QOpenGLShaderProgram m_shader;
    QOpenGLTexture *m_texY = nullptr, *m_texU = nullptr, *m_texV = nullptr;
    bool m_glInited = false;

    // 当前显示帧所有权由 VideoRender 接管：从队列弹出后赋值给该指针，
    // 替换显示帧或析构时由 VideoRender 调用 av_frame_free() 释放。
    AVFrame* m_displayFrame = nullptr;
    QMutex m_frameMutex;

    bool m_hasFrame = false;

    int m_videoW = 0, m_videoH = 0;
    AVRational m_sar = {1, 1};

    AVPixelFormat m_lastLoggedFormat = AV_PIX_FMT_NONE;
    int m_lastLoggedWidth = 0;
    int m_lastLoggedHeight = 0;
    int m_lastLoggedLinesize[4] = {0, 0, 0, 0};
    AVColorRange m_lastLoggedColorRange = AVCOL_RANGE_UNSPECIFIED;
    AVColorSpace m_lastLoggedColorSpace = AVCOL_SPC_UNSPECIFIED;
    AVColorPrimaries m_lastLoggedColorPrimaries = AVCOL_PRI_UNSPECIFIED;
    AVColorTransferCharacteristic m_lastLoggedColorTrc = AVCOL_TRC_UNSPECIFIED;
    bool m_lastLoggedSupported = false;
    bool m_hasLoggedFrameFormat = false;
};

#endif // VIDEORENDER_H
