#ifndef SINGLESTREAMFRAMESINKADAPTER_H
#define SINGLESTREAMFRAMESINKADAPTER_H

#include <QPointer>

#include "infrastructure/ivideoframesink.h"
#include "infrastructure/multiviewrenderer.h"

/**
 * @brief SingleStreamFrameSinkAdapter 单路视频帧 sink 适配器
 * @details 把单路 IVideoFrameSink 调用转发到带 StreamId 的 MultiViewRenderer。
 *          StreamId 只在适配器外层绑定，不进入 MediaPipeline。
 */
class SingleStreamFrameSinkAdapter : public IVideoFrameSink
{
public:
    /**
     * @brief SingleStreamFrameSinkAdapter 构造函数
     * @param renderer 目标统一渲染器
     * @param streamId 当前单路 sink 对应的 StreamId
     */
    SingleStreamFrameSinkAdapter(MultiViewRenderer* renderer,
                                 MultiViewRenderer::StreamId streamId);

    /**
     * @brief PresentFrame 接收一帧可显示视频帧
     * @details renderer 有效时把 frame 所有权转交给 MultiViewRenderer；
     *          renderer 已销毁时释放 frame，保证所有权不泄漏。
     * @param frame 已经完成调度判断、允许显示的视频帧
     */
    void PresentFrame(AVFrame* frame) override;

    /**
     * @brief ClearFrame 清理当前 stream 的显示帧
     */
    void ClearFrame() override;

    /**
     * @brief Renderer 获取当前绑定的统一渲染器
     * @return 渲染器指针，可能为空
     */
    MultiViewRenderer* Renderer() const;

    /**
     * @brief StreamIdValue 获取当前适配器绑定的 StreamId
     * @return 绑定的 StreamId
     */
    MultiViewRenderer::StreamId StreamIdValue() const;

private:
    QPointer<MultiViewRenderer> renderer_;
    MultiViewRenderer::StreamId streamId_ = MultiViewRenderer::InvalidStreamId;
};

#endif // SINGLESTREAMFRAMESINKADAPTER_H
