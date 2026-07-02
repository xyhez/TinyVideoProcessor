#ifndef TINYVIDEOPROCESSOR_IMAGEPROCESSINGFRAMESINK_H
#define TINYVIDEOPROCESSOR_IMAGEPROCESSINGFRAMESINK_H

#include "infrastructure/imageprocessorpipeline.h"
#include "infrastructure/ivideoframesink.h"
#include "infrastructure/videoframematconverter.h"

/**
 * @brief ImageProcessingFrameSink 在视频帧调度器和最终显示 sink 之间执行图像处理
 */
class ImageProcessingFrameSink : public IVideoFrameSink {
public:
    /**
     * @brief PresentFrame 接收调度后的帧并按配置处理后转发给下游 sink
     * @param frame 视频帧所有权
     */
    void PresentFrame(AVFramePtr frame) override;

    /**
     * @brief ClearFrame 清空下游显示帧
     */
    void ClearFrame() override;

    /**
     * @brief SetDownstreamSink 设置处理完成后的帧输出目标
     * @param sink 下游视频帧 sink
     */
    void SetDownstreamSink(IVideoFrameSink* sink);

    /**
     * @brief SetImageProcessingEnabled 设置是否启用图像处理链路
     * @param enabled 是否启用
     */
    void SetImageProcessingEnabled(bool enabled);

    /**
     * @brief IsImageProcessingEnabled 判断当前是否启用图像处理链路
     * @return true 已启用
     * @return false 未启用
     */
    bool IsImageProcessingEnabled() const;

    /**
     * @brief ImageProcessingPipeline 获取图像处理器管线
     * @return 图像处理器管线指针
     */
    ImageProcessorPipeline* ImageProcessingPipeline();

    /**
     * @brief ImageProcessingPipeline 获取只读图像处理器管线
     * @return 图像处理器管线指针
     */
    const ImageProcessorPipeline* ImageProcessingPipeline() const;

private:
    IVideoFrameSink* downstreamSink_ = nullptr;	///< 下游视频帧 sink
    ImageProcessorPipeline imageProcessorPipeline_;	///< 图像处理器管线
    VideoFrameMatConverter frameMatConverter_;	///< 视频帧与 OpenCV Mat 转换器
    bool imageProcessingEnabled_ = false;	///< 是否启用图像处理
};

#endif // TINYVIDEOPROCESSOR_IMAGEPROCESSINGFRAMESINK_H
