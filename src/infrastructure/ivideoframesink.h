#ifndef IVIDEOFRAMESINK_H
#define IVIDEOFRAMESINK_H

extern "C" {
#include <libavutil/frame.h>
}

#include "core/ffmpeg_raii.h"

/**
 * @brief IVideoFrameSink 可显示视频帧消费者接口
 * @details 该接口只接收已经完成调度判断的可显示帧，不负责取帧、同步判断、
 *          丢帧、解码、音频策略或多路布局管理。
 */
class IVideoFrameSink
{
public:
    virtual ~IVideoFrameSink() = default;

    /**
     * @brief PresentFrame 接收一帧可显示视频帧
     * @details 调用成功后，frame 的所有权转移给 sink。调用方不得继续使用该帧。
     * @param frame 已经过调度判断、允许显示的视频帧
     */
    virtual void PresentFrame(AVFramePtr frame) = 0;

    /**
     * @brief ClearFrame 清理 sink 当前持有的显示帧
     * @details 该方法只清理当前显示帧，不负责清空解码队列、刷新解码器、
     *          重置时钟或执行 seek / stop 流程。
     */
    virtual void ClearFrame() = 0;
};

#endif // IVIDEOFRAMESINK_H
