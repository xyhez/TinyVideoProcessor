#include "yolovisionpipeline.h"

#include "tiny_log/Logging.h"

#include "infrastructure/vision/opencvpostprocessors.h"
#include "infrastructure/vision/opencvpreprocessors.h"
#include "infrastructure/vision/yoloonnxanalyzer.h"

#include <algorithm>
#include <utility>

namespace {
constexpr int kMinYoloInputSize = 32;
constexpr int kMaxYoloInputSize = 4096;
const QString kPipelineId = QStringLiteral("yolo-onnx");
const QString kDisplayName = QStringLiteral("YOLO ONNX Detection");
const QString kType = QStringLiteral("detection");

QString DefaultModelPath()
{
#ifdef TINY_LOG_PROJECT_ROOT
    return QStringLiteral(TINY_LOG_PROJECT_ROOT "/models/yolo.onnx");
#else
    return QStringLiteral("models/yolo.onnx");
#endif
}

YoloPipelineOptions NormalizeOptions(YoloPipelineOptions options)
{
    const YoloPipelineOptions defaults = DefaultYoloPipelineOptions();
    if (options.modelPath.isEmpty()) {
        options.modelPath = defaults.modelPath;
    }
    options.inputWidth = std::clamp(options.inputWidth, kMinYoloInputSize, kMaxYoloInputSize);
    options.inputHeight = std::clamp(options.inputHeight, kMinYoloInputSize, kMaxYoloInputSize);
    options.confidenceThreshold = std::clamp(options.confidenceThreshold, 0.0F, 1.0F);
    options.nmsThreshold = std::clamp(options.nmsThreshold, 0.0F, 1.0F);
    return options;
}
}

YoloPipelineOptions DefaultYoloPipelineOptions()
{
    YoloPipelineOptions options;
    options.modelPath = DefaultModelPath();
    options.inputWidth = 640;
    options.inputHeight = 640;
    options.confidenceThreshold = 0.35F;
    options.nmsThreshold = 0.45F;
    return options;
}

YoloVisionPipeline::YoloVisionPipeline(YoloPipelineOptions options)
    : options_(NormalizeOptions(std::move(options)))
{
}

YoloVisionPipeline::~YoloVisionPipeline()
{
    Shutdown();
}

QString YoloVisionPipeline::Id() const
{
    return kPipelineId;
}

QString YoloVisionPipeline::DisplayName() const
{
    return kDisplayName;
}

QString YoloVisionPipeline::Type() const
{
    return kType;
}

bool YoloVisionPipeline::Startup()
{
    if (started_) {
        return true;
    }

    auto letterbox = std::make_unique<OpenCvLetterbox>(options_.inputWidth, options_.inputHeight);
    letterbox_ = letterbox.get();
    preprocessors_.push_back(std::move(letterbox));
    preprocessors_.push_back(std::make_unique<OpenCvBgrToRgb>());

    analyzer_ = std::make_unique<YoloOnnxAnalyzer>(
        kPipelineId,
        options_.modelPath,
        options_.confidenceThreshold,
        options_.labelsPath);
    if (!analyzer_->Load()) {
        LOG_WARN_STREAM() << "YoloVisionPipeline startup failed:" << analyzer_->LastError();
        Shutdown();
        return false;
    }

    postprocessors_.push_back(std::make_unique<YoloNms>(
        kPipelineId,
        options_.confidenceThreshold,
        options_.nmsThreshold));
    postprocessors_.push_back(std::make_unique<CoordinateMapper>(kPipelineId, letterbox_));

    started_ = true;
    LOG_DEBUG_STREAM() << "YoloVisionPipeline started with model" << options_.modelPath;
    return true;
}

void YoloVisionPipeline::Shutdown()
{
    preprocessors_.clear();
    letterbox_ = nullptr;
    analyzer_.reset();
    postprocessors_.clear();
    if (started_) {
        LOG_DEBUG("YoloVisionPipeline shutdown");
    }
    started_ = false;
}

bool YoloVisionPipeline::IsStarted() const
{
    return started_;
}

bool YoloVisionPipeline::Run(const VisionFrame& input, VisionResultSet& out)
{
    if (!started_ || !analyzer_) {
        return false;
    }
    if (input.image.empty()) {
        return false;
    }

    VisionFrame working = input;
    working.image = input.image.clone();

    for (auto& pp : preprocessors_) {
        if (!pp->Process(working)) {
            LOG_WARN_STREAM() << "YoloVisionPipeline preprocessor failed:" << pp->Name();
            return false;
        }
    }

    if (!analyzer_->Analyze(working, out)) {
        LOG_WARN_STREAM() << "YoloVisionPipeline analyzer failed:" << analyzer_->LastError();
        return false;
    }

    for (auto& pp : postprocessors_) {
        if (!pp->Process(working, out)) {
            LOG_WARN_STREAM() << "YoloVisionPipeline postprocessor failed:" << pp->Name();
            return false;
        }
    }

    return true;
}
