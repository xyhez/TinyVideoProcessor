#include "frameprofiler.h"

#include <algorithm>
#include <QDebug>
#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace {
int64_t SteadyNowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
}

FrameProfiler& FrameProfiler::Instance()
{
    static FrameProfiler profiler;
    return profiler;
}

FrameProfiler::Scope::Scope(const char* stage)
    : stage_(stage)
    , start_(std::chrono::steady_clock::now())
{
}

FrameProfiler::Scope::~Scope()
{
    if (!stage_) {
        return;
    }

    const auto elapsed = std::chrono::duration<double, std::milli>(
                             std::chrono::steady_clock::now() - start_)
                             .count();
    FrameProfiler::Instance().Record(stage_, elapsed);
}

void FrameProfiler::Record(const char* stage, double elapsedMs)
{
    if (!stage) {
        return;
    }

    const int64_t nowMs = SteadyNowMs();
    QStringList report;

    {
        std::lock_guard<std::mutex> locker(mutex_);

        if (windowStartMs_ < 0) {
            windowStartMs_ = nowMs;
        }

        StageStats& stats = stages_[stage];
        stats.samples.push_back(elapsedMs);
        if (stats.samples.size() > kMaxSamplesPerStage) {
            stats.samples.erase(stats.samples.begin(),
                                stats.samples.begin()
                                    + static_cast<std::ptrdiff_t>(stats.samples.size() - kMaxSamplesPerStage));
        }

        if (nowMs - windowStartMs_ < kReportIntervalMs) {
            return;
        }

        for (auto& entry : stages_) {
            StageStats& stageStats = entry.second;
            if (stageStats.samples.empty()) {
                continue;
            }

            const double p50 = Percentile(stageStats.samples, 0.50);
            const double p95 = Percentile(stageStats.samples, 0.95);
            const double maxMs = *std::max_element(stageStats.samples.begin(),
                                                   stageStats.samples.end());

            report.append(QString("%1 p50=%2 p95=%3 max=%4 n=%5")
                              .arg(QString::fromStdString(entry.first))
                              .arg(p50, 0, 'f', 2)
                              .arg(p95, 0, 'f', 2)
                              .arg(maxMs, 0, 'f', 2)
                              .arg(static_cast<qulonglong>(stageStats.samples.size())));
            stageStats.samples.clear();
        }

        windowStartMs_ = nowMs;
    }

    if (!report.isEmpty()) {
        qInfo().noquote() << "FrameProfiler:" << report.join(" | ");
    }
}

double FrameProfiler::Percentile(std::vector<double> samples, double percentile)
{
    if (samples.empty()) {
        return 0.0;
    }

    std::sort(samples.begin(), samples.end());
    const double position = std::clamp(percentile, 0.0, 1.0) * (samples.size() - 1);
    const size_t lower = static_cast<size_t>(position);
    const size_t upper = std::min(lower + 1, samples.size() - 1);
    const double fraction = position - static_cast<double>(lower);
    return samples[lower] * (1.0 - fraction) + samples[upper] * fraction;
}
