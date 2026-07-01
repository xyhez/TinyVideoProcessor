#include "imageprocessorpipeline.h"

#include <chrono>
#include <utility>

bool ImageProcessorPipeline::AddProcessor(std::shared_ptr<IImageProcessor> processor)
{
    if (!processor) {
        return false;
    }

    processors_.push_back(std::move(processor));
    return true;
}

void ImageProcessorPipeline::ClearProcessors()
{
    processors_.clear();
    lastProcessElapsedMs_ = 0.0;
}

bool ImageProcessorPipeline::Process(cv::Mat& image)
{
    const auto startTime = std::chrono::steady_clock::now();
    bool success = true;

    for (const auto& processor : processors_) {
        if (!processor || !processor->Enabled()) {
            continue;
        }

        if (!processor->Process(image)) {
            success = false;
            break;
        }
    }

    const auto finishTime = std::chrono::steady_clock::now();
    lastProcessElapsedMs_ =
        std::chrono::duration<double, std::milli>(finishTime - startTime).count();

    return success;
}

int ImageProcessorPipeline::ProcessorCount() const
{
    return static_cast<int>(processors_.size());
}

double ImageProcessorPipeline::LastProcessElapsedMs() const
{
    return lastProcessElapsedMs_;
}
