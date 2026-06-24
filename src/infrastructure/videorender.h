#ifndef VIDEORENDER_H
#define VIDEORENDER_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QTimer>
#include <QMutex>
#include <memory>
#include <atomic>

#include "infrastructure/ivideoframesink.h"
#include "infrastructure/syncmanager.h"
#include "core/queue.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
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
     * @brief InitVideoRender 初始化视频渲染器
     * @param frameQueue 视频帧队列
     * @param syncManager 同步时钟管理器
     * @param frameRate 视频帧率
     * @param videoTimeBase 视频流时间基
     */
    void InitVideoRender(std::shared_ptr<SyncQueue<AVFrame*>> frameQueue,
                         std::shared_ptr<syncmanager> syncManager,
                         double frameRate,
                         double videoTimeBase);

    /**
     * @brief StartRendering 启动视频渲染调度
     */
    void StartRendering();

    /**
     * @brief StopRendering 停止视频渲染调度
     */
    void StopRendering();

    /**
     * @brief Pause 暂停视频渲染
     */
    void Pause() { StopRendering(); }

    /**
     * @brief SetSpeed 设置渲染调度感知的播放速度
     * @param speed 支持的播放速度档位
     */
    void SetSpeed(double speed);

    /**
     * @brief SetExternalFrameSink 设置外部视频帧输出目标
     * @details 设置后 VideoRender 仍负责取帧和同步判断，但可显示帧会转交给
     *          externalFrameSink。传入 nullptr 时恢复默认自身渲染路径。
     * @param externalFrameSink 外部帧消费者，不转移所有权
     */
    void SetExternalFrameSink(IVideoFrameSink* externalFrameSink);

    /**
     * @brief PresentFrame 接管可显示帧并请求重绘
     * @details IVideoFrameSink 接口实现。调用后 frame 所有权转移给 VideoRender
     *          的当前显示帧。
     * @param frame 可显示帧
     */
    void PresentFrame(AVFrame* frame) override;

    /**
     * @brief ClearFrame 清理当前显示帧
     * @details IVideoFrameSink 接口实现。只释放当前显示帧并刷新画面，
     *          不清空队列、不刷新 decoder、不重置同步时钟。
     */
    void ClearFrame() override;

private slots:
    /**
     * @brief RenderFrame 自调度渲染循环入口
     * @details 当前同时承担帧调度职责：查看队首帧、按 syncmanager 判断早晚、
     *          丢弃过晚帧、接管可显示帧所有权，并触发下一次调度。
     *          后续拆分帧输出边界时，应优先从该函数切出调度逻辑。
     */
    void RenderFrame();

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
     * @brief FrameTimingDecision 表示当前队首帧的调度判断结果
     */
    enum class FrameTimingDecision {
        Display,
        Wait,
        Drop
    };

    /**
     * @brief CanRenderFrame 判断当前是否具备渲染调度条件
     * @return true 可以继续调度
     * @return false 当前缺少运行状态、帧队列或同步管理器
     */
    bool CanRenderFrame() const;

    /**
     * @brief CalculateScaledFrameInterval 计算播放速度修正后的帧间隔
     * @param speed 当前播放速度快照
     * @return 修正后的帧间隔，单位毫秒
     */
    int CalculateScaledFrameInterval(double speed) const;

    /**
     * @brief PeekNextFrame 查看队首帧但不弹出队列
     * @param frame 输出队首帧指针
     * @return true 成功取得队首帧
     * @return false 队列为空或队首帧无效
     */
    bool PeekNextFrame(AVFrame*& frame);

    /**
     * @brief EvaluateFrameTiming 根据同步时钟判断队首帧应显示、等待或丢弃
     * @param frame 队首帧
     * @param syncType 当前同步类型
     * @param waitMs 输出等待时间，只有返回 Wait 时有效
     * @return 当前帧调度结果
     */
    FrameTimingDecision EvaluateFrameTiming(const AVFrame* frame, SYNCTYPE syncType, int& waitMs) const;

    /**
     * @brief DropLateFrame 弹出并释放过晚帧
     * @return true 成功弹出并释放过晚帧
     * @return false 队列中没有可丢弃帧
     */
    bool DropLateFrame();

    /**
     * @brief PopDisplayableFrame 弹出可显示帧并转交给调用方
     * @param frame 输出可显示帧指针
     * @return true 成功弹出可显示帧
     * @return false 没有可显示帧
     */
    bool PopDisplayableFrame(AVFrame*& frame);

    /**
     * @brief RefreshVideoClockIfNeeded 在 VIDEO sync 模式下刷新视频主时钟
     * @param frame 当前已显示帧
     * @param syncType 当前同步类型
     */
    void RefreshVideoClockIfNeeded(const AVFrame* frame, SYNCTYPE syncType);

    /**
     * @brief PresentDisplayableFrame 将可显示帧提交给当前输出 sink
     * @details 调用后 frame 所有权转移给目标 sink，调用方不能再访问该指针。
     * @param frame 已完成调度判断的可显示帧
     */
    void PresentDisplayableFrame(AVFrame* frame);

    /**
     * @brief ClearOutputFrame 清理当前输出 sink 的显示帧
     * @param requestUpdate 默认自身渲染路径下是否请求重绘
     */
    void ClearOutputFrame(bool requestUpdate);

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

    /**
     * @brief ScheduleNextFrame 安排下一次渲染调度
     * @details 帧调度职责，当前使用 Qt 事件循环自调度下一次 RenderFrame。
     * @param delayMs 延迟时间，单位毫秒
     */
    void ScheduleNextFrame(int delayMs);

    std::shared_ptr<SyncQueue<AVFrame*>> m_frameQueue;
    std::shared_ptr<syncmanager> m_syncManager;

    QOpenGLShaderProgram m_shader;
    QOpenGLTexture *m_texY = nullptr, *m_texU = nullptr, *m_texV = nullptr;
    bool m_glInited = false;

    // 当前显示帧所有权由 VideoRender 接管：从队列弹出后赋值给该指针，
    // 替换显示帧或析构时由 VideoRender 调用 av_frame_free() 释放。
    AVFrame* m_displayFrame = nullptr;
    IVideoFrameSink* m_externalFrameSink = nullptr;
    QMutex m_frameMutex;

    int m_frameIntervalMs = 33;
    double m_videoTimeBase = 0.0;
    std::atomic<double> m_speed {1.0};
    bool m_hasFrame = false;
    bool m_running = false;

    int m_videoW = 0, m_videoH = 0;
    AVRational m_sar = {1, 1};

    int m_rendered = 0;
    int m_dropped = 0;

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
