#ifndef MULTIVIEWRENDERER_H
#define MULTIVIEWRENDERER_H

#include <QMap>
#include <QElapsedTimer>
#include <QMutex>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>
#include <QRect>
#include <QSize>
#include <QVector>

#include <map>

#include "core/ffmpeg_raii.h"
#include "infrastructure/videoframeconverter.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libavutil/rational.h>
}

class QMouseEvent;
class QOpenGLTexture;

/**
 * @brief MultiViewRenderer 统一多画面渲染器空壳
 *
 * 当前阶段只定义统一渲染器的布局、slot 和点击边界，不接真实视频帧。
 * 该类不持有 MediaPipeline，不访问 demuxer/decoder/audio player。
 */
class MultiViewRenderer : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT

public:
    using StreamId = int;
    static constexpr StreamId InvalidStreamId = -1;	///< 无效流 ID

    /**
     * @brief Layout 多画面布局类型
     */
    enum class Layout {
        Single,
        Grid2x2,
        Grid3x3,
        Grid4x4
    };

    /**
     * @brief Viewport 单个逻辑 slot 的显示区域
     */
    struct Viewport {
        int slotIndex = -1;	///< slot 索引
        StreamId streamId = InvalidStreamId;	///< 绑定的流 ID
        QRect qtRect;	///< Qt 逻辑坐标区域
        QRect framebufferRect;	///< OpenGL framebuffer 坐标区域

        /**
         * @brief IsBound 判断当前 viewport 是否绑定有效流
         * @return true 已绑定有效流
         * @return false 空 slot
         */
        bool IsBound() const;
    };

    /**
     * @brief MultiViewRenderer 构造函数
     * @param parent Qt 父控件
     */
    explicit MultiViewRenderer(QWidget* parent = nullptr);

    /**
     * @brief MultiViewRenderer 析构函数，释放当前持有的视频帧状态
     */
    ~MultiViewRenderer() override;

    /**
     * @brief SetLayout 设置当前多画面布局
     * @param layout 目标布局
     */
    void SetLayout(Layout layout);

    /**
     * @brief CurrentLayout 获取当前多画面布局
     * @return 当前布局
     */
    Layout CurrentLayout() const;

    /**
     * @brief SetViewSlots 设置 view slot 与 StreamId 的绑定关系
     * @param viewSlots slot 索引到 StreamId 的映射
     */
    void SetViewSlots(const QMap<int, StreamId>& viewSlots);

    /**
     * @brief ViewSlots 获取当前 view slot 绑定关系
     * @return slot 索引到 StreamId 的映射
     */
    QMap<int, StreamId> ViewSlots() const;

    /**
     * @brief SetActiveStream 设置当前活动显示流
     * @param streamId 目标 StreamId
     */
    void SetActiveStream(StreamId streamId);

    /**
     * @brief ActiveStream 获取当前活动显示流
     * @return 当前活动 StreamId
     */
    StreamId ActiveStream() const;

    /**
     * @brief MaxVisibleSlots 获取当前布局下可见 slot 数量
     * @return 可见 slot 数量
     */
    int MaxVisibleSlots() const;

    /**
     * @brief Viewports 计算当前布局下所有可见 viewport
     * @return viewport 列表，坐标同时包含 Qt 坐标和 OpenGL viewport 坐标
     */
    QVector<Viewport> Viewports() const;

    /**
     * @brief ViewportForSlot 计算指定 slot 的 viewport
     * @param slotIndex slot 索引
     * @return slot 可见时返回有效 viewport，不可见或索引无效时返回空 viewport
     */
    Viewport ViewportForSlot(int slotIndex) const;

    /**
     * @brief PresentFrame 接收指定 stream 的可显示视频帧
     * @details 调用后 frame 所有权转移给 MultiViewRenderer。当前阶段只接管
     *          和保存 current frame，不做 OpenGL 纹理上传。
     * @param streamId 目标 StreamId
     * @param frame 已经过调度判断、允许显示的视频帧
     */
    void PresentFrame(StreamId streamId, AVFramePtr frame);

    /**
     * @brief ClearFrame 清理指定 stream 当前显示帧
     * @details 保留 stream 渲染状态，不移除 slot 绑定。
     * @param streamId 目标 StreamId
     */
    void ClearFrame(StreamId streamId);

    /**
     * @brief DetachStream 移除指定 stream 的渲染状态
     * @details 释放当前帧并移除 stream 渲染状态，不隐式修改 slot 绑定。
     * @param streamId 目标 StreamId
     */
    void DetachStream(StreamId streamId);

signals:
    /**
     * @brief StreamClicked 点击已绑定 view slot 时发出的信号
     * @param streamId 被点击的 StreamId
     */
    void StreamClicked(MultiViewRenderer::StreamId streamId);

    /**
     * @brief StreamDoubleClicked 双击已绑定 view slot 时发出的信号
     * @param streamId 被双击的 StreamId
     */
    void StreamDoubleClicked(MultiViewRenderer::StreamId streamId);

protected:
    /**
     * @brief initializeGL 初始化 OpenGL 状态
     */
    void initializeGL() override;

    /**
     * @brief paintGL 绘制空壳背景
     */
    void paintGL() override;

    /**
     * @brief mousePressEvent 处理 viewport 点击命中
     * @param event 鼠标按下事件
     */
    void mousePressEvent(QMouseEvent* event) override;

    /**
     * @brief mouseDoubleClickEvent 处理 viewport 双击命中
     * @param event 鼠标双击事件
     */
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    /**
     * @brief StreamRenderState 单路渲染状态
     */
    struct StreamRenderState {
        AVFramePtr currentFrame;	///< 当前显示帧
        QOpenGLTexture* textureY = nullptr;	///< Y 分量纹理
        QOpenGLTexture* textureU = nullptr;	///< U 分量纹理
        QOpenGLTexture* textureV = nullptr;	///< V 分量纹理
        AVPixelFormat texturePixelFormat = AV_PIX_FMT_NONE;	///< 当前纹理对应的像素格式
        int texturePlaneWidths[3] = {0, 0, 0};	///< 当前纹理各平面宽度
        int texturePlaneHeights[3] = {0, 0, 0};	///< 当前纹理各平面高度
        int videoWidth = 0;	///< 视频宽度
        int videoHeight = 0;	///< 视频高度
        AVRational sampleAspectRatio = {1, 1};	///< 像素宽高比
        AVPixelFormat pixelFormat = AV_PIX_FMT_NONE;	///< 像素格式
        bool textureDirty = false;	///< 纹理是否需要更新
        bool hasFrame = false;	///< 是否持有显示帧
        AVPixelFormat lastLoggedFormat = AV_PIX_FMT_NONE;	///< 上次记录的像素格式
        int lastLoggedWidth = 0;	///< 上次记录的视频宽度
        int lastLoggedHeight = 0;	///< 上次记录的视频高度
        int lastLoggedLinesize[4] = {0, 0, 0, 0};	///< 上次记录的行步长
        AVColorRange lastLoggedColorRange = AVCOL_RANGE_UNSPECIFIED;	///< 上次记录的色彩范围
        AVColorSpace lastLoggedColorSpace = AVCOL_SPC_UNSPECIFIED;	///< 上次记录的色彩空间
        AVColorPrimaries lastLoggedColorPrimaries = AVCOL_PRI_UNSPECIFIED;	///< 上次记录的色彩基色
        AVColorTransferCharacteristic lastLoggedColorTrc = AVCOL_TRC_UNSPECIFIED;	///< 上次记录的传输特性
        AVRational lastLoggedSampleAspectRatio = {0, 1};	///< 上次记录的像素宽高比
        bool lastLoggedSupported = false;	///< 上次记录的格式支持状态
        bool hasLoggedFrameFormat = false;	///< 是否已记录过帧格式
        qint64 presentedFrames = 0;	///< 接收显示帧次数
        qint64 textureUploads = 0;	///< 纹理上传次数
        qint64 textureRebuilds = 0;	///< 纹理重建次数
        qint64 lastPresentedElapsedMs = -1;	///< 最近一次接收帧的计时器毫秒
        AVPixelFormat lastInputPixelFormat = AV_PIX_FMT_NONE;	///< 最近一次输入帧像素格式
        bool lastFrameConverted = false;	///< 最近一次接收帧是否发生格式转换
    };

    /**
     * @brief ReleaseCurrentFrame 释放单路状态当前帧并重置帧元数据
     * @param state 单路渲染状态
     */
    void ReleaseCurrentFrame(StreamRenderState& state);

    /**
     * @brief ReleaseStreamResources 释放单路帧和 OpenGL 纹理资源
     * @param state 单路渲染状态
     */
    void ReleaseStreamResources(StreamRenderState& state);

    /**
     * @brief FreeTextures 释放单路 OpenGL YUV 纹理
     * @param state 单路渲染状态
     */
    void FreeTextures(StreamRenderState& state);

    /**
     * @brief InitializeFrameShader 初始化 YUV420P 绘制 shader
     */
    void InitializeFrameShader();

    /**
     * @brief DrawStreamFrame 绘制指定 viewport 中绑定 stream 的当前帧
     * @param viewport 目标 viewport
     * @param state 单路渲染状态
     * @return true 已绘制视频帧
     * @return false 没有可绘制帧
     */
    bool DrawStreamFrame(const Viewport& viewport, StreamRenderState& state);

    /**
     * @brief UploadTexturesIfNeeded 必要时上传 YUV420P 数据到 OpenGL 纹理
     * @param state 单路渲染状态
     * @return true 纹理可用于绘制
     * @return false 当前帧格式不支持或纹理不可用
     */
    bool UploadTexturesIfNeeded(StreamRenderState& state);

    /**
     * @brief TextureSignatureMatches 判断当前纹理签名是否匹配待上传帧
     * @param state 单路渲染状态
     * @param planeWidths YUV 平面宽度
     * @param planeHeights YUV 平面高度
     * @return true 当前纹理可复用
     * @return false 需要重建纹理
     */
    bool TextureSignatureMatches(const StreamRenderState& state,
                                 const int (&planeWidths)[3],
                                 const int (&planeHeights)[3]) const;

    /**
     * @brief UpdateTextureSignature 记录当前纹理对应的格式和平面尺寸
     * @param state 单路渲染状态
     * @param planeWidths YUV 平面宽度
     * @param planeHeights YUV 平面高度
     */
    void UpdateTextureSignature(StreamRenderState& state,
                                const int (&planeWidths)[3],
                                const int (&planeHeights)[3]);

    /**
     * @brief IsSupportedFrameFormat 判断 MultiViewRenderer 当前是否支持该帧格式
     * @param state 单路渲染状态
     * @return true 支持
     * @return false 不支持
     */
    bool IsSupportedFrameFormat(const StreamRenderState& state) const;

    /**
     * @brief CalcFrameRect 计算保持宽高比后的帧绘制矩形
     * @param viewport 目标 viewport
     * @param state 单路渲染状态
     * @param x 输出 OpenGL NDC 左下角 x
     * @param y 输出 OpenGL NDC 左下角 y
     * @param w 输出 OpenGL NDC 宽度
     * @param h 输出 OpenGL NDC 高度
     */
    void CalcFrameRect(const Viewport& viewport,
                       const StreamRenderState& state,
                       float& x,
                       float& y,
                       float& w,
                       float& h) const;

    /**
     * @brief LogFrameFormatIfNeeded 在格式签名变化时输出诊断日志
     * @param streamId 目标 StreamId
     * @param state 单路渲染状态
     * @param supported 是否为受支持格式
     */
    void LogFrameFormatIfNeeded(StreamId streamId, StreamRenderState& state, bool supported);

    /**
     * @brief LogPerformanceStatsIfNeeded 按固定时间窗口输出渲染性能统计
     */
    void LogPerformanceStatsIfNeeded();

    /**
     * @brief IsLogicalSlotIndex 判断是否为合法逻辑 slot
     * @param slotIndex slot 索引
     * @return true 合法
     */
    bool IsLogicalSlotIndex(int slotIndex) const;

    /**
     * @brief QtRectForSlot 计算 slot 在 Qt 坐标系下的显示区域
     * @param slotIndex slot 索引
     * @return Qt 坐标系显示区域
     */
    QRect QtRectForSlot(int slotIndex) const;

    /**
     * @brief GridRectForSlot 计算网格布局中指定 slot 的 Qt 坐标区域
     * @param slotIndex slot 索引
     * @param columns 网格列数
     * @param rows 网格行数
     * @return Qt 坐标系显示区域
     */
    QRect GridRectForSlot(int slotIndex, int columns, int rows) const;

    /**
     * @brief FramebufferSize 获取 OpenGL framebuffer 物理像素尺寸
     * @return framebuffer 尺寸
     */
    QSize FramebufferSize() const;

    /**
     * @brief ToFramebufferRect 将 Qt 逻辑坐标区域转换为 OpenGL framebuffer 坐标
     * @param qtRect Qt 逻辑坐标区域
     * @return OpenGL framebuffer 坐标区域
     */
    QRect ToFramebufferRect(const QRect& qtRect) const;

    /**
     * @brief DrawViewport 绘制单个 viewport 的背景、视频帧和边框
     * @param viewport 待绘制 viewport
     */
    void DrawViewport(const Viewport& viewport);

    /**
     * @brief FillOpenGLRect 使用当前 OpenGL 上下文填充矩形区域
     * @param rect OpenGL viewport 坐标系矩形
     * @param red 红色通道
     * @param green 绿色通道
     * @param blue 蓝色通道
     * @param alpha 透明度通道
     */
    void FillOpenGLRect(const QRect& rect, float red, float green, float blue, float alpha);

    /**
     * @brief DrawViewportBorder 绘制 viewport 边界
     * @param rect OpenGL viewport 坐标系矩形
     * @param active 是否绘制 active stream 高亮边界
     */
    void DrawViewportBorder(const QRect& rect, bool active);

    Layout layout_ = Layout::Single;	///< 当前布局
    QMap<int, StreamId> viewSlots_;	///< slot 到流 ID 的绑定表
    std::map<StreamId, StreamRenderState> renderStates_;	///< 每路渲染状态
    StreamId activeStreamId_ = InvalidStreamId;	///< 当前活动显示流
    QOpenGLShaderProgram frameShader_;	///< 视频帧绘制 shader
    VideoFrameConverter frameConverter_;	///< 可显示帧格式转换器
    QMutex renderStateMutex_;	///< 渲染状态互斥锁
    QElapsedTimer performanceTimer_;	///< 性能统计计时器
    qint64 performanceWindowStartMs_ = 0;	///< 当前性能统计窗口起始毫秒
    qint64 paintCount_ = 0;	///< paintGL 调用总次数
    qint64 lastLoggedPaintCount_ = 0;	///< 上次日志记录时的 paint 次数
    qint64 lastLoggedTextureUploads_ = 0;	///< 上次日志记录时的纹理上传次数
    qint64 lastLoggedTextureRebuilds_ = 0;	///< 上次日志记录时的纹理重建次数
    bool glInitialized_ = false;	///< OpenGL 是否已初始化
};

#endif // MULTIVIEWRENDERER_H
