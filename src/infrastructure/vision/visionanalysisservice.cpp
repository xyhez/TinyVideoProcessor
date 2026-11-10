#include "visionanalysisservice.h"

#include <algorithm>
#include <utility>

#include <opencv2/core/mat.hpp>

#include "core/frameprofiler.h"
#include "infrastructure/vision/visionpipelinemanager.h"
#include "infrastructure/vision/visionresultstore.h"
#include "infrastructure/vision/visiontypes.h"

VisionAnalysisService::VisionAnalysisService(VisionPipelineManager* manager,
                                             std::shared_ptr<VisionResultStore> resultStore)
    : manager_(manager)
    , resultStore_(std::move(resultStore))
{
    // 等所有同步成员构造完成后再启动线程。
    worker_ = std::thread(&VisionAnalysisService::WorkerLoop, this);
}

VisionAnalysisService::~VisionAnalysisService()
{
    {
        std::lock_guard<std::mutex> locker(mutex_);
        stopping_ = true;
    }
    condition_.notify_all();

    if (worker_.joinable()) {
        worker_.join();
    }
}

void VisionAnalysisService::SetTargetFps(int fps)
{
    targetFps_.store(std::clamp(fps, 1, 30));
}

int VisionAnalysisService::TargetFps() const
{
    return targetFps_.load();
}

void VisionAnalysisService::SetOverlayEnabled(bool enabled)
{
    if (resultStore_) {
        resultStore_->SetOverlayEnabled(enabled);
    }
}

void VisionAnalysisService::SubmitFrame(int streamId, const AVFrame* frame)
{
    if (!manager_ || !resultStore_ || !frame || streamId < 0) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> locker(mutex_);
        if (stopping_) {
            return;
        }

        const auto interval = std::chrono::milliseconds(1000 / std::max(1, targetFps_.load()));
        if (hasLastSubmitTime_ && now - lastSubmitTime_ < interval) {
            return;
        }
        lastSubmitTime_ = now;
        hasLastSubmitTime_ = true;
    }

    AVFramePtr frameRef = MakeAVFrame();
    if (!frameRef || av_frame_ref(frameRef.get(), frame) < 0) {
        return;
    }

    {
        std::lock_guard<std::mutex> locker(mutex_);
        if (stopping_) {
            return;
        }

        // 只保留最新帧：旧的分析帧直接释放，不排队。
        pendingFrame_ = std::move(frameRef);
        pendingStreamId_ = streamId;
        hasPendingFrame_ = true;
    }

    condition_.notify_one();
}

void VisionAnalysisService::ClearStream(int streamId)
{
    if (resultStore_) {
        resultStore_->Clear(streamId);
    }
}

void VisionAnalysisService::ClearAll()
{
    if (resultStore_) {
        resultStore_->ClearAll();
    }
}

void VisionAnalysisService::WorkerLoop()
{
    while (true) {
        AVFramePtr frame;
        int streamId = -1;

        {
            std::unique_lock<std::mutex> locker(mutex_);
            condition_.wait(locker, [this]() {
                return stopping_ || hasPendingFrame_;
            });

            if (stopping_) {
                break;
            }

            frame = std::move(pendingFrame_);
            streamId = pendingStreamId_;
            hasPendingFrame_ = false;
            pendingStreamId_ = -1;
        }

        if (!frame || !manager_ || !resultStore_) {
            continue;
        }

        const int64_t framePts = frame->pts;
        cv::Mat image;
        if (!converter_.ConvertFrameToMat(frame.get(), image)) {
            continue;
        }

        // 像素已经复制到 cv::Mat，尽快释放 AVFrame 引用，
        // 避免显示线程的 av_frame_make_writable() 长时间触发 copy-on-write。
        frame.reset();

        VisionFrame visionFrame;
        visionFrame.image = image;
        visionFrame.width = image.cols;
        visionFrame.height = image.rows;
        visionFrame.stride = static_cast<int>(image.step);
        visionFrame.pts = framePts;
        visionFrame.pixelFormat = VisionPixelFormat::BGR24;

        VisionResultSet results;
        FrameProfiler::Scope analysisProfile("vision.analyze");
        if (!manager_->RunActive(visionFrame, results)) {
            continue;
        }

        auto sharedResults = std::make_shared<VisionResultSet>(std::move(results));
        resultStore_->SetResults(streamId,
                                 std::move(sharedResults),
                                 framePts,
                                 NowMs());
    }
}

int64_t VisionAnalysisService::NowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
