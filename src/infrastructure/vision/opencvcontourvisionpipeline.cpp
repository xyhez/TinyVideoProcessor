#include "opencvcontourvisionpipeline.h"

#include "tiny_log/Logging.h"

#include "infrastructure/vision/opencvcontouranalyzer.h"
#include "infrastructure/vision/opencvpostprocessors.h"
#include "infrastructure/vision/opencvpreprocessors.h"

namespace {
constexpr double kDefaultThreshold = 128.0;	///< 默认二值化阈值
constexpr double kDefaultMaxValue = 255.0;	///< 二值化输出的最大值
constexpr double kDefaultMinArea = 100.0;	///< 默认最小轮廓面积过滤
constexpr double kDefaultMaxArea = -1.0;	///< 默认最大轮廓面积，<=0 表示不限制
const QString kPipelineId = QStringLiteral("opencv-contour");
const QString kDisplayName = QStringLiteral("OpenCV Contour Detection");
const QString kType = QStringLiteral("detection");
}

OpenCvContourVisionPipeline::OpenCvContourVisionPipeline() = default;

OpenCvContourVisionPipeline::~OpenCvContourVisionPipeline()
{
    Shutdown();
}

QString OpenCvContourVisionPipeline::Id() const
{
    return kPipelineId;
}

QString OpenCvContourVisionPipeline::DisplayName() const
{
    return kDisplayName;
}

QString OpenCvContourVisionPipeline::Type() const
{
    return kType;
}

bool OpenCvContourVisionPipeline::Startup()
{
    if (started_) {
        return true;
    }

    preprocessors_.push_back(std::make_unique<OpenCvGrayscale>());
    preprocessors_.push_back(std::make_unique<OpenCvThreshold>(kDefaultThreshold, kDefaultMaxValue));
    analyzer_ = std::make_unique<OpenCvContourAnalyzer>(kPipelineId, kDefaultMinArea, kDefaultMaxArea);
    postprocessors_.push_back(std::make_unique<OpenCvMeasurement>(kPipelineId));

    started_ = true;
    LOG_DEBUG("OpenCvContourVisionPipeline started");
    return true;
}

void OpenCvContourVisionPipeline::Shutdown()
{
    preprocessors_.clear();
    analyzer_.reset();
    postprocessors_.clear();
    if (started_) {
        LOG_DEBUG("OpenCvContourVisionPipeline shutdown");
    }
    started_ = false;
}

bool OpenCvContourVisionPipeline::IsStarted() const
{
    return started_;
}

bool OpenCvContourVisionPipeline::Run(const VisionFrame& input, VisionResultSet& out)
{
    if (!started_) {
        return false;
    }
    if (input.image.empty()) {
        return false;
    }

    // 每次 Run 一份 working frame,避免污染上游传入的 const VisionFrame
    VisionFrame working = input;
    working.image = input.image.clone();

    for (auto& pp : preprocessors_) {
        if (!pp->Process(working)) {
            LOG_WARN_STREAM() << "OpenCvContourVisionPipeline preprocessor failed:" << pp->Name();
            return false;
        }
    }

    if (!analyzer_ || !analyzer_->Analyze(working, out)) {
        LOG_WARN("OpenCvContourVisionPipeline analyzer failed");
        return false;
    }

    for (auto& pp : postprocessors_) {
        if (!pp->Process(working, out)) {
            LOG_WARN_STREAM() << "OpenCvContourVisionPipeline postprocessor failed:" << pp->Name();
            return false;
        }
    }

    return true;
}
