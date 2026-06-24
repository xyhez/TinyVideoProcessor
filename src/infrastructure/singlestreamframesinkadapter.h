#ifndef SINGLESTREAMFRAMESINKADAPTER_H
#define SINGLESTREAMFRAMESINKADAPTER_H

#include <QPointer>

#include "infrastructure/ivideoframesink.h"
#include "infrastructure/multiviewrenderer.h"

/**
 * @brief SingleStreamFrameSinkAdapter 单路视频帧 sink 适配器空壳
 * @details 后续用于把单路 IVideoFrameSink 调用转发到带 StreamId 的
 *          MultiViewRenderer 接口。当前阶段 MultiViewRenderer 尚未提供
 *          真实接帧接口，因此 PresentFrame 只负责安全释放帧，避免泄漏。
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
     * @details 当前空壳阶段尚不能转发到 MultiViewRenderer，调用后立即释放
     *          frame，保证 IVideoFrameSink 所有权语义不泄漏。
     * @param frame 已经过调度判断、允许显示的视频帧
     */
    void PresentFrame(AVFrame* frame) override;

    /**
     * @brief ClearFrame 清理当前显示帧
     * @details 当前空壳阶段 MultiViewRenderer 尚未持有帧状态，因此该方法
     *          只保留接口边界，不执行渲染状态清理。
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
