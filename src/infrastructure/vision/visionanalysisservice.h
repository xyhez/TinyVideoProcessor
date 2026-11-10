#ifndef TINYVIDEOPROCESSOR_VISIONANALYSISSERVICE_H
#define TINYVIDEOPROCESSOR_VISIONANALYSISSERVICE_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#include "core/ffmpeg_raii.h"
#include "infrastructure/vision/videoframematconverter.h"

extern "C" {
#include <libavutil/frame.h>
}

class VisionPipelineManager;
class VisionResultStore;

/**
 * @brief VisionAnalysisService 异步视觉分析服务
 *
 * 显示线程只负责把帧引用投递进来；真正的 AVFrame -> cv::Mat、
 * VisionPipelineManager::RunActive 和结果发布都在本服务的工作线程执行。
 * 服务只保留最新待分析帧，忙时丢弃旧帧，避免分析延迟反向拖累显示。
 */
class VisionAnalysisService
{
public:
    /**
     * @brief VisionAnalysisService 构造函数
     * @param manager 视觉管线管理器，生命周期必须覆盖服务
     * @param resultStore 结果仓库，服务与渲染器共享
     */
    VisionAnalysisService(VisionPipelineManager* manager,
                          std::shared_ptr<VisionResultStore> resultStore);

    /**
     * @brief ~VisionAnalysisService 停止工作线程并等待退出
     */
    ~VisionAnalysisService();

    VisionAnalysisService(const VisionAnalysisService&) = delete;
    VisionAnalysisService& operator=(const VisionAnalysisService&) = delete;

    /**
     * @brief SetTargetFps 设置分析采样目标帧率
     * @param fps 目标帧率，限制在 1~30
     */
    void SetTargetFps(int fps);

    /**
     * @brief TargetFps 获取分析采样目标帧率
     * @return int 目标帧率
     */
    int TargetFps() const;

    /**
     * @brief SetOverlayEnabled 设置是否允许绘制视觉叠加
     * @param enabled true 允许绘制
     */
    void SetOverlayEnabled(bool enabled);

    /**
     * @brief SubmitFrame 提交一帧用于异步分析
     * @details 只创建 AVFrame 引用，不拷贝像素；服务忙时新帧会替换旧帧。
     * @param streamId 来源流 ID
     * @param frame 视频帧；调用方保留所有权
     */
    void SubmitFrame(int streamId, const AVFrame* frame);

    /**
     * @brief ClearStream 清除指定流的最新结果
     * @param streamId 目标流 ID
     */
    void ClearStream(int streamId);

    /**
     * @brief ClearAll 清除所有结果
     */
    void ClearAll();

private:
    /**
     * @brief WorkerLoop 分析线程主循环
     */
    void WorkerLoop();

    /**
     * @brief NowMs 获取 steady clock 毫秒
     * @return int64_t 当前毫秒
     */
    static int64_t NowMs();

    VisionPipelineManager* manager_ = nullptr;	///< 视觉管线管理器，借用不拥有
    std::shared_ptr<VisionResultStore> resultStore_;	///< 结果仓库
    VideoFrameMatConverter converter_;	///< 仅工作线程使用的转换器

    std::thread worker_;	///< 分析线程
    mutable std::mutex mutex_;	///< 保护待处理帧和停止标志
    std::condition_variable condition_;	///< 唤醒分析线程
    AVFramePtr pendingFrame_;	///< 最新待分析帧
    int pendingStreamId_ = -1;	///< 待分析帧来源流
    bool hasPendingFrame_ = false;	///< 是否有待处理帧
    bool stopping_ = false;	///< 是否请求停止

    std::atomic<int> targetFps_{10};	///< 分析目标帧率
    std::chrono::steady_clock::time_point lastSubmitTime_;	///< 上次提交时间
    bool hasLastSubmitTime_ = false;	///< 是否已有上次提交时间
};

#endif // TINYVIDEOPROCESSOR_VISIONANALYSISSERVICE_H
