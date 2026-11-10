#include "imageprocessorpipeline.h"

#include <chrono>
#include <utility>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

bool ImageProcessorPipeline::AddProcessor(std::shared_ptr<IImageProcessor> processor)
{
    if (!processor) {
        return false;
    }

    std::lock_guard<std::mutex> locker(mutex_);
    processors_.push_back(std::move(processor));
    return true;
}

void ImageProcessorPipeline::ClearProcessors()
{
    std::lock_guard<std::mutex> locker(mutex_);
    processors_.clear();
    lastProcessElapsedMs_ = 0.0;
}

bool ImageProcessorPipeline::Process(cv::Mat& image)
{
    std::lock_guard<std::mutex> locker(mutex_);

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

bool ImageProcessorPipeline::SupportsYuvProcessing() const
{
    std::lock_guard<std::mutex> locker(mutex_);

    bool hasEnabledProcessor = false;
    for (const auto& processor : processors_) {
        if (!processor || !processor->Enabled()) {
            continue;
        }

        hasEnabledProcessor = true;
        if (!processor->SupportsYuvProcessing()) {
            return false;
        }
    }

    return hasEnabledProcessor;
}

bool ImageProcessorPipeline::ProcessYuv(AVFrame* frame)
{
    if (!frame || frame->format != AV_PIX_FMT_YUV420P) {
        return false;
    }

    std::lock_guard<std::mutex> locker(mutex_);

    const auto startTime = std::chrono::steady_clock::now();
    bool success = true;

    // 先确认所有启用处理器都支持 YUV，避免部分修改后才失败。
    for (const auto& processor : processors_) {
        if (!processor || !processor->Enabled()) {
            continue;
        }
        if (!processor->SupportsYuvProcessing()) {
            success = false;
            break;
        }
    }

    if (success) {
        for (const auto& processor : processors_) {
            if (!processor || !processor->Enabled()) {
                continue;
            }

            if (!processor->Process(frame)) {
                success = false;
                break;
            }
        }
    }

    const auto finishTime = std::chrono::steady_clock::now();
    lastProcessElapsedMs_ =
        std::chrono::duration<double, std::milli>(finishTime - startTime).count();

    return success;
}

int ImageProcessorPipeline::ProcessorCount() const
{
    std::lock_guard<std::mutex> locker(mutex_);
    return static_cast<int>(processors_.size());
}

double ImageProcessorPipeline::LastProcessElapsedMs() const
{
    std::lock_guard<std::mutex> locker(mutex_);
    return lastProcessElapsedMs_;
}
