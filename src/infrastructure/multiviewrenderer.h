#ifndef MULTIVIEWRENDERER_H
#define MULTIVIEWRENDERER_H

#include <QMap>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>

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
    Layout layout_ = Layout::Single;
    QMap<int, StreamId> viewSlots_;
    StreamId activeStreamId_ = InvalidStreamId;
};

#endif // MULTIVIEWRENDERER_H
