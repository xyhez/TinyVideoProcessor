#ifndef MULTIVIEWRENDERER_H
#define MULTIVIEWRENDERER_H

#include <QMap>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QRect>
#include <QVector>

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
    static constexpr StreamId InvalidStreamId = -1;

    /**
     * @brief Layout 多画面布局类型
     */
    enum class Layout {
        Single,
        Grid2x2
    };

    /**
     * @brief Viewport 单个逻辑 slot 的显示区域
     */
    struct Viewport {
        int slotIndex = -1;
        StreamId streamId = InvalidStreamId;
        QRect qtRect;
        QRect glViewportRect;

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

signals:
    /**
     * @brief StreamClicked 点击已绑定 view slot 时发出的信号
     * @param streamId 被点击的 StreamId
     */
    void StreamClicked(MultiViewRenderer::StreamId streamId);

protected:
    /**
     * @brief initializeGL 初始化 OpenGL 状态
     */
    void initializeGL() override;

    /**
     * @brief paintGL 绘制空壳背景
     */
    void paintGL() override;

private:
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
     * @brief ToOpenGLViewportRect 将 Qt 坐标区域转换为 OpenGL viewport 坐标
     * @param qtRect Qt 坐标系区域
     * @return OpenGL viewport 坐标区域
     */
    QRect ToOpenGLViewportRect(const QRect& qtRect) const;

    /**
     * @brief DrawViewport 绘制单个空 viewport 区域
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

    Layout layout_ = Layout::Single;
    QMap<int, StreamId> viewSlots_;
    StreamId activeStreamId_ = InvalidStreamId;
};

#endif // MULTIVIEWRENDERER_H
