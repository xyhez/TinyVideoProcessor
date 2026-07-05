#ifndef TINYVIDEOPROCESSOR_VISIONPROCESSINGFRAMESINK_H
#define TINYVIDEOPROCESSOR_VISIONPROCESSINGFRAMESINK_H

#include "infrastructure/rendering/ivideoframesink.h"
#include "infrastructure/vision/videoframematconverter.h"
#include "infrastructure/vision/visionoverlayrenderer.h"

class VisionPipelineManager;

/**
 * @brief VisionProcessingFrameSink 视觉分析帧 sink
 *
 * 位于 sink 链的 ImageProcessingFrameSink 之后：
 *   Scheduler → ImageProcessingFrameSink → VisionProcessingFrameSink → downstream
 *
 * 每路 MediaPipeline 各持有一个，但只有 active stream 那个 isActive_=true
 * 才会真正跑分析和绘制。其他路直接透传原帧。
 *
 * isActive_ 与 overlayEnabled_ 正交：
 * - isActive_=false: 完全透传，不 Run 不 Draw
 * - isActive_=true, overlayEnabled_=true: Run + Draw
 * - isActive_=true, overlayEnabled_=false: Run 但不 Draw（结果集合可供未来订阅者消费）
 *
 * VisionPipelineManager 由上层持有，sink 借用其指针，不管理生命周期。
 */
class VisionProcessingFrameSink : public IVideoFrameSink {
public:
    /**
     * @brief VisionProcessingFrameSink 构造函数
     * @param manager 视觉管线管理器，nullptr 则等同关闭视觉分析
     * @param downstream 下游 sink，可稍后通过 SetDownstreamSink 设置
     */
    VisionProcessingFrameSink(VisionPipelineManager* manager = nullptr,
                              IVideoFrameSink* downstream = nullptr);

    void PresentFrame(AVFramePtr frame) override;
    void ClearFrame() override;

    /**
     * @brief SetActive 切换本 sink 是否执行视觉分析
     * @param active true 表示本 sink 属于 active stream
     */
    void SetActive(bool active);
    bool IsActive() const;

    /**
     * @brief SetOverlayEnabled 切换是否把识别结果绘制到画面
     */
    void SetOverlayEnabled(bool enabled);
    bool IsOverlayEnabled() const;

    /**
     * @brief SetDownstreamSink 更新下游 sink
     */
    void SetDownstreamSink(IVideoFrameSink* sink);

private:
    VisionPipelineManager* manager_ = nullptr;	///< 视觉管线管理器，借用不拥有
    IVideoFrameSink* downstreamSink_ = nullptr;	///< 下游 sink，借用不拥有
    VideoFrameMatConverter converter_;	///< AVFrame ↔ cv::Mat 转换器
    VisionOverlayRenderer overlayRenderer_;	///< 识别结果绘制器
    bool isActive_ = false;	///< 是否属于 active stream
    bool overlayEnabled_ = true;	///< 是否把结果绘制到画面
};

#endif // TINYVIDEOPROCESSOR_VISIONPROCESSINGFRAMESINK_H
