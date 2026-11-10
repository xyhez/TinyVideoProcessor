#include "imageprocessingframesink.h"

#include <utility>

#include <opencv2/core/mat.hpp>

#include "core/frameprofiler.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

void ImageProcessingFrameSink::PresentFrame(AVFramePtr frame)
{
    if (!downstreamSink_) {
        return;
    }

    if (!imageProcessingEnabled_.load() || imageProcessorPipeline_.ProcessorCount() == 0) {
        downstreamSink_->PresentFrame(std::move(frame));
        return;
    }

    // 快速路径：灰度/阈值/边缘直接处理 YUV Y 平面，避免 YUV→BGR→YUV 全帧往返。
    if (frame && frame->format == AV_PIX_FMT_YUV420P
        && imageProcessorPipeline_.SupportsYuvProcessing()) {
        FrameProfiler::Scope profile("image.yuv");

        // 如果分析线程持有同一帧引用，这里会触发 copy-on-write，避免数据竞争。
        if (av_frame_make_writable(frame.get()) == 0
            && imageProcessorPipeline_.ProcessYuv(frame.get())) {
            downstreamSink_->PresentFrame(std::move(frame));
            return;
        }
    }

    // 回退路径：必须使用彩色/多处理器组合时，保留原有 BGR 处理链。
    FrameProfiler::Scope profile("image.bgr");

    cv::Mat image;
    if (!frameMatConverter_.ConvertFrameToMat(frame.get(), image)) {
        downstreamSink_->PresentFrame(std::move(frame));
        return;
    }

    if (!imageProcessorPipeline_.Process(image)) {
        downstreamSink_->PresentFrame(std::move(frame));
        return;
    }

    AVFramePtr processedFrame = frameMatConverter_.ConvertMatToFrame(image, frame);
    if (!processedFrame) {
        downstreamSink_->PresentFrame(std::move(frame));
        return;
    }

    downstreamSink_->PresentFrame(std::move(processedFrame));
}

void ImageProcessingFrameSink::ClearFrame()
{
    if (downstreamSink_) {
        downstreamSink_->ClearFrame();
    }
}

void ImageProcessingFrameSink::SetDownstreamSink(IVideoFrameSink* sink)
{
    downstreamSink_ = sink;
}

void ImageProcessingFrameSink::SetImageProcessingEnabled(bool enabled)
{
    imageProcessingEnabled_.store(enabled);
}

bool ImageProcessingFrameSink::IsImageProcessingEnabled() const
{
    return imageProcessingEnabled_.load();
}

ImageProcessorPipeline* ImageProcessingFrameSink::ImageProcessingPipeline()
{
    return &imageProcessorPipeline_;
}

const ImageProcessorPipeline* ImageProcessingFrameSink::ImageProcessingPipeline() const
{
    return &imageProcessorPipeline_;
}
