#include "imageprocessingframesink.h"

#include <utility>

#include <opencv2/core/mat.hpp>

void ImageProcessingFrameSink::PresentFrame(AVFramePtr frame)
{
    if (!downstreamSink_) {
        return;
    }

    if (!imageProcessingEnabled_ || imageProcessorPipeline_.ProcessorCount() == 0) {
        downstreamSink_->PresentFrame(std::move(frame));
        return;
    }

    cv::Mat image;
    if (!frameMatConverter_.ConvertFrameToMat(frame, image)) {
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
    imageProcessingEnabled_ = enabled;
}

bool ImageProcessingFrameSink::IsImageProcessingEnabled() const
{
    return imageProcessingEnabled_;
}

ImageProcessorPipeline* ImageProcessingFrameSink::ImageProcessingPipeline()
{
    return &imageProcessorPipeline_;
}

const ImageProcessorPipeline* ImageProcessingFrameSink::ImageProcessingPipeline() const
{
    return &imageProcessorPipeline_;
}
