#ifndef TINYVIDEOPROCESSOR_VISIONPROCESSINGFRAMESINK_H
#define TINYVIDEOPROCESSOR_VISIONPROCESSINGFRAMESINK_H

#include <atomic>
#include <memory>

#include "infrastructure/rendering/ivideoframesink.h"

class VisionAnalysisService;
class VisionPipelineManager;
class VisionResultStore;

/**
 * @brief VisionProcessingFrameSink 视觉分析帧 sink
 *
 * 当前 sink 不再同步执行 OpenCV/YOLO，而是把帧引用投递给
 * VisionAnalysisService。显示帧立即透传给下游，overlay 由渲染器读取
 * VisionResultStore 后绘制。
 */
class VisionProcessingFrameSink : public IVideoFrameSink
{
public:
    /**
     * @brief VisionProcessingFrameSink 构造函数
     * @param manager 视觉管线管理器，借用不拥有
     * @param analysisService 异步分析服务，借用不拥有
     * @param resultStore 结果仓库，服务与渲染器共享
     * @param downstream 下游 sink
     */
    VisionProcessingFrameSink(VisionPipelineManager* manager,
                              VisionAnalysisService* analysisService,
                              std::shared_ptr<VisionResultStore> resultStore,
                              IVideoFrameSink* downstream = nullptr);

    void PresentFrame(AVFramePtr frame) override;
    void ClearFrame() override;

    /**
     * @brief SetStreamId 设置当前 sink 所属流 ID
     * @param streamId 流 ID
     */
    void SetStreamId(int streamId);

    /**
     * @brief SetActive 切换本 sink 是否提交分析
     * @param active true 表示 active stream
     */
    void SetActive(bool active);
    bool IsActive() const;

    /**
     * @brief SetOverlayEnabled 设置视觉叠加开关
     * @param enabled true 允许绘制
     */
    void SetOverlayEnabled(bool enabled);
    bool IsOverlayEnabled() const;

    /**
     * @brief SetDownstreamSink 更新下游 sink
     * @param sink 下游视频帧 sink
     */
    void SetDownstreamSink(IVideoFrameSink* sink);

private:
    VisionPipelineManager* manager_ = nullptr;	///< 视觉管线管理器，借用不拥有
    VisionAnalysisService* analysisService_ = nullptr;	///< 异步分析服务，借用不拥有
    std::shared_ptr<VisionResultStore> resultStore_;	///< 结果仓库
    IVideoFrameSink* downstreamSink_ = nullptr;	///< 下游视频帧 sink
    std::atomic<int> streamId_{-1};	///< 当前流 ID
    std::atomic<bool> isActive_{false};	///< 是否属于 active stream
    std::atomic<bool> overlayEnabled_{true};	///< 是否允许绘制叠加
};

#endif // TINYVIDEOPROCESSOR_VISIONPROCESSINGFRAMESINK_H
