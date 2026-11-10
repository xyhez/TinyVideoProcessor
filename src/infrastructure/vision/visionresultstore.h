#ifndef TINYVIDEOPROCESSOR_VISIONRESULTSTORE_H
#define TINYVIDEOPROCESSOR_VISIONRESULTSTORE_H

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>

#include "infrastructure/vision/visiontypes.h"

/**
 * @brief VisionResultStore 线程安全的视觉结果快照仓库
 *
 * AnalysisWorker 在后台线程写入结果，MultiViewRenderer 在 GUI 线程读取。
 * Store 只保存“最新结果”，不参与推理，也不持有视频帧。
 */
class VisionResultStore
{
public:
    /**
     * @brief Snapshot 某一路流的最新视觉结果快照
     */
    struct Snapshot {
        std::shared_ptr<const VisionResultSet> results;	///< 最新结果集合，空表示无结果
        int64_t pts = 0;	///< 结果对应视频帧 PTS
        int64_t producedAtMs = 0;	///< 结果产生时间，steady clock 毫秒
        bool overlayEnabled = true;	///< 当前是否允许绘制结果
    };

    /**
     * @brief SetResults 更新指定流的最新结果
     * @param streamId 目标流 ID
     * @param results 结果集合，允许为空表示清除
     * @param pts 结果对应视频帧 PTS
     * @param producedAtMs 结果产生时间，steady clock 毫秒
     */
    void SetResults(int streamId,
                    std::shared_ptr<const VisionResultSet> results,
                    int64_t pts,
                    int64_t producedAtMs);

    /**
     * @brief Get 获取指定流的最新结果快照
     * @param streamId 目标流 ID
     * @return 无结果时返回空 results 的快照
     */
    Snapshot Get(int streamId) const;

    /**
     * @brief Clear 清除指定流结果
     * @param streamId 目标流 ID
     */
    void Clear(int streamId);

    /**
     * @brief ClearAll 清除所有流结果
     */
    void ClearAll();

    /**
     * @brief SetOverlayEnabled 设置是否允许绘制视觉叠加
     * @param enabled true 允许绘制
     */
    void SetOverlayEnabled(bool enabled);

    /**
     * @brief IsOverlayEnabled 获取叠加绘制开关
     * @return true 允许绘制
     */
    bool IsOverlayEnabled() const;

    /**
     * @brief SetUpdateCallback 设置结果变化通知
     * @details 回调可能来自分析线程；实现方必须自行切换到 GUI 线程。
     * @param callback 结果变化或叠加开关变化时调用
     */
    void SetUpdateCallback(std::function<void()> callback);

private:
    mutable std::mutex mutex_;	///< 保护 results_/overlayEnabled_/updateCallback_
    std::map<int, Snapshot> results_;	///< 每路流最新结果
    bool overlayEnabled_ = true;	///< 是否允许绘制叠加
    std::function<void()> updateCallback_;	///< 结果变化通知

    /**
     * @brief NotifyUpdated 在锁外触发更新回调
     */
    void NotifyUpdated();
};

#endif // TINYVIDEOPROCESSOR_VISIONRESULTSTORE_H
