#include "yolovisionpipeline.h"

#include "tiny_log/Logging.h"

#include "infrastructure/vision/opencvpostprocessors.h"
#include "infrastructure/vision/opencvpreprocessors.h"
#include "infrastructure/vision/yoloonnxanalyzer.h"

#include <utility>

namespace {
constexpr int kYoloInputWidth = 640;	///< YOLO 默认输入宽度
constexpr int kYoloInputHeight = 640;	///< YOLO 默认输入高度
constexpr float kConfidenceThreshold = 0.35F;	///< YOLO 检测置信度阈值
constexpr float kNmsThreshold = 0.45F;	///< YOLO NMS 阈值
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
}

YoloVisionPipeline::YoloVisionPipeline(QString modelPath)
    : modelPath_(modelPath.isEmpty() ? DefaultModelPath() : std::move(modelPath))
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

    auto letterbox = std::make_unique<OpenCvLetterbox>(kYoloInputWidth, kYoloInputHeight);
    letterbox_ = letterbox.get();
    preprocessors_.push_back(std::move(letterbox));
    preprocessors_.push_back(std::make_unique<OpenCvBgrToRgb>());

    analyzer_ = std::make_unique<YoloOnnxAnalyzer>(
        kPipelineId,
        modelPath_,
        kConfidenceThreshold);
    if (!analyzer_->Load()) {
        LOG_WARN_STREAM() << "YoloVisionPipeline startup failed:" << analyzer_->LastError();
        Shutdown();
        return false;
    }

    postprocessors_.push_back(std::make_unique<YoloNms>(
        kPipelineId,
        kConfidenceThreshold,
        kNmsThreshold));
    postprocessors_.push_back(std::make_unique<CoordinateMapper>(kPipelineId, letterbox_));

    started_ = true;
    LOG_DEBUG_STREAM() << "YoloVisionPipeline started with model" << modelPath_;
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
