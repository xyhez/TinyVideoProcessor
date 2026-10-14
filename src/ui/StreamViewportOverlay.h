#ifndef UI_STREAMVIEWPORTOVERLAY_H
#define UI_STREAMVIEWPORTOVERLAY_H

#include <QMap>
#include <QWidget>

#include "ui/StreamPresentationState.h"

class MultiViewRenderer;
class QEvent;
class QPaintEvent;

/**
 * @brief StreamViewportOverlay 在多视口画面上绘制只读视频源信息
 *
 * 覆盖层只消费 UI 展示快照和渲染器的逻辑 viewport，不访问 Controller，
 * 不参与 OpenGL 绘制，也不接收鼠标或键盘输入。
 */
class StreamViewportOverlay final : public QWidget
{
public:
    explicit StreamViewportOverlay(MultiViewRenderer* renderer);

    /**
     * @brief SetPresentationStates 整表替换当前视频源展示快照
     * @param states StreamId 到展示状态的映射
     */
    void SetPresentationStates(
        const QMap<int, StreamPresentationState>& states);

protected:
    /**
     * @brief eventFilter 跟随渲染器尺寸和显示状态同步覆盖区域
     */
    bool eventFilter(QObject* watched, QEvent* event) override;

    /**
     * @brief paintEvent 绘制可见 viewport 的名称和可靠状态
     */
    void paintEvent(QPaintEvent* event) override;

private:
    /**
     * @brief SyncToRenderer 同步几何尺寸并保持覆盖层位于渲染器内容上方
     */
    void SyncToRenderer();

    MultiViewRenderer* renderer_ = nullptr;	///< 提供逻辑 viewport 的父渲染器
    QMap<int, StreamPresentationState> presentationStates_;  ///< 只读展示快照
};

#endif // UI_STREAMVIEWPORTOVERLAY_H
