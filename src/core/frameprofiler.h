#ifndef TINYVIDEOPROCESSOR_FRAMEPROFILER_H
#define TINYVIDEOPROCESSOR_FRAMEPROFILER_H

#include <chrono>
#include <cstddef>
#include <map>
#include <mutex>
#include <string>
#include <vector>

/**
 * @brief FrameProfiler 轻量级分阶段耗时统计
 *
 * 只在关键帧路径上记录耗时，不参与业务逻辑。每 5 秒输出一次 p50/p95/max，
 * 用于判断瓶颈在转换、处理、上传还是绘制。
 */
class FrameProfiler
{
public:
    /**
     * @brief Instance 获取全局实例
     * @return FrameProfiler& 全局实例
     */
    static FrameProfiler& Instance();

    /**
     * @brief Record 记录一次阶段耗时
     * @param stage 阶段名称
     * @param elapsedMs 耗时，单位毫秒
     */
    void Record(const char* stage, double elapsedMs);

    /**
     * @brief Scope RAII 计时器
     */
    class Scope
    {
    public:
        /**
         * @brief Scope 开始计时
         * @param stage 阶段名称
         */
        explicit Scope(const char* stage);

        /**
         * @brief ~Scope 结束计时并记录
         */
        ~Scope();

        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        const char* stage_ = nullptr;	///< 阶段名称
        std::chrono::steady_clock::time_point start_;	///< 起始时间
    };

private:
    /**
     * @brief StageStats 单阶段样本
     */
    struct StageStats {
        std::vector<double> samples;	///< 最近样本
    };

    FrameProfiler() = default;

    /**
     * @brief Percentile 计算分位数
     * @param samples 样本
     * @param percentile 0~1
     * @return double 分位数
     */
    static double Percentile(std::vector<double> samples, double percentile);

    mutable std::mutex mutex_;	///< 保护 stages_
    std::map<std::string, StageStats> stages_;	///< 阶段统计
    int64_t windowStartMs_ = -1;	///< 当前统计窗口起点
    static constexpr size_t kMaxSamplesPerStage = 512;	///< 每阶段最多保留样本数
    static constexpr int64_t kReportIntervalMs = 5000;	///< 报告间隔
};

#endif // TINYVIDEOPROCESSOR_FRAMEPROFILER_H
