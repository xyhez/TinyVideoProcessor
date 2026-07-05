# Phase 7 多视觉处理器架构设计

## 项目愿景

在现有多路视频播放、基础图像处理和 OpenCV 目标检测能力之上，进一步演进为可扩展的视频分析平台。

Phase 7 的目标不是直接绑定某一个检测库，而是建立一套稳定的**视觉处理管线**抽象，让 OpenCV 传统视觉、OCR、YOLO/ONNX 模型、深度学习分割等能力可以按统一方式接入、启停和切换。

最终呈现：在同一条视频显示链路上，用户可以按当前场景需求选择一种视觉分析算法运行，识别结果统一叠加到画面上，结构化结果暴露给主程序用于后续统计、报警、存储等。

## 设计原则

- 复用现有链路：继续沿用现有 sink 插入方式，不侵入 decoder、scheduler、renderer 或 UI 本体。
- 以"识别算法"为单位切分管线：一种识别算法 = 一条完整的独立 `IVisionPipeline`（自己的预处理 + 分析 + 后处理），互相不共享中间状态。
- 单激活模式：同一时刻只有一条 pipeline 在运行，用户根据场景切换。避免不同算法的预处理需求互相冲突、避免多条并行推理拖垮性能。
- 明确 OpenCV 的复合角色：OpenCV 既可以作为独立的识别管线，也可以在其他识别管线内部作为通用预处理/后处理组件（**类库复用**，不是流水线阶段复用）。
- 显式描述帧格式：不能让处理器私下假设输入一定是 RGB 或 BGR，必须通过元数据声明颜色空间、尺寸、stride 和时间戳。
- 结果和显示解耦：识别结果先进入统一 `VisionResultSet`，再由 `VisionOverlayRenderer` 统一绘制；具体识别算法不直接画到图像上。
- 接入统一：主程序依赖 `IVisionPipeline` 顶层接口，具体算法作为其子类在源码内实现，通过 `VisionProcessorFactory` 在启动时注册到 `VisionPipelineManager`。
- 第一版保持同步链路：只处理 active stream，同步执行；异步推理、降帧推理作为后续演进。

## 当前基础

当前项目已经具备以下基础：

```text
VideoFrameScheduler
    -> ImageProcessingFrameSink
        -> VideoFrameMatConverter
        -> ImageProcessorPipeline
        -> downstream IVideoFrameSink
            -> SingleStreamFrameSinkAdapter
            -> MultiViewRenderer
```

已有职责边界：

- `VideoFrameScheduler` 负责取帧、同步和提交可显示帧。
- `ImageProcessingFrameSink` 负责在进入渲染前执行图像处理。
- `VideoFrameMatConverter` 负责 `AVFrame <-> cv::Mat` 转换。
- `ImageProcessorPipeline` 负责顺序执行 `IImageProcessor`。
- `MultiViewRenderer` 只负责显示，不理解算法细节。

当前不足：

- `IImageProcessor::Process(cv::Mat& image)` 只适合"改图"类处理，不适合 OCR/YOLO 这类需要输出结构化结果的处理器。
- 当前中间格式实际是 `cv::Mat + BGR24`，但没有显式帧格式描述。
- 检测结果如果只画到图像上，后续无法做统计、报警、导出、存储和跨帧跟踪。
## 总体架构

Phase 7 采用**多条独立识别管线 + 单激活模式**：注册多种识别算法作为独立的 `IVisionPipeline`，运行时同一时刻只有一条被激活，由 `VisionPipelineManager` 统一管理注册和切换。

### 顶层视角

```text
              VisionFrame (原始输入,只读)
                     │
                     ↓
          VisionPipelineManager
           ├─ [dormant]  YoloVisionPipeline
           ├─ [dormant]  OcrVisionPipeline
           ├─ [dormant]  SegmentationVisionPipeline
           └─ [active]   OpenCvContourVisionPipeline   ← 当前激活的一条
                     │
                     ↓
            IVisionPipeline::Run(input, resultSet)
                     │
                     ↓
              VisionResultSet
                     │
                     ↓
              VisionOverlayRenderer
                     │
                     ↓
                  AVFrame → 显示
```

### 单条识别管线内部

每条 `IVisionPipeline` 是一种识别算法的**完整闭包**，包含自己的预处理、分析、后处理三个环节：

```text
IVisionPipeline (以 YoloVisionPipeline 为例)
  ├─ preprocessors:
  │      OpenCvLetterbox(640, 640)
  │      OpenCvBgrToRgb
  ├─ analyzer:
  │      YoloOnnxAnalyzer("model.onnx")
  └─ postprocessors:
         YoloNms(0.5f)
         CoordinateMapper
```

三种角色的构建块（`IFramePreprocessor` / `IVisionAnalyzer` / `IVisionPostprocessor`）作为 pipeline 内部的可重用类库存在，具体的 OpenCV 预处理/后处理类可以被不同的 pipeline 各自实例化使用。

### 集成到现有链路

`VisionProcessingFrameSink` 是新增的 sink，接在现有 `ImageProcessingFrameSink` 之后：

```text
VideoFrameScheduler                     (Phase 5 已有)
    ↓ AVFrame
ImageProcessingFrameSink                (Phase 5 已有,跑 IImageProcessor 链)
    ↓ AVFrame
VisionProcessingFrameSink               ★ Phase 7 新增
    · 持有 VisionPipelineManager*
    · 持有自己的 StreamId
    · 持有 isActive_ flag
    · 只有 isActive_ && 有 active pipeline 时才跑分析,否则透传
    ↓ AVFrame (可能被画上检测框)
SingleStreamFrameSinkAdapter            (Phase 5 已有)
    ↓
MultiViewRenderer                       (Phase 5 已有)
```

### 所有权关系

```text
MediaPlayController
  └─ MultiStreamManager
       ├─ VisionPipelineManager (unique_ptr 成员,非单例)
       │      ├─ [dormant]  YoloVisionPipeline
       │      ├─ [dormant]  OcrVisionPipeline
       │      └─ [active]   OpenCvContourVisionPipeline
       │
       └─ streams: [Stream 1, Stream 2, ...]
             每个 Stream 是一个 MediaPipeline
             每个 MediaPipeline 内部持有自己的 VisionProcessingFrameSink
             所有 sink 借用同一个 VisionPipelineManager 指针
```

关键关系：`VisionPipelineManager` 只有**一个**（管全局激活状态、给 UI 用）；`VisionProcessingFrameSink` **每路 stream 各一个**（跟着帧走），但只有 active stream 那路的 sink 真正跑分析，其他 sink 透传原帧。

## OpenCV 的架构定位

OpenCV 在 Phase 7 中有三种使用方式，但**不是横跨三段流水线**，而是三种角色的类库都可以被任意 `IVisionPipeline` 组合使用。

### 1. 作为独立识别管线

OpenCV 可以自己组成一条完整的 `IVisionPipeline`，例如 `OpenCvContourVisionPipeline`：

```text
OpenCvContourVisionPipeline
  ├─ preprocessors: [threshold]
  ├─ analyzer:      OpenCvContourAnalyzer
  └─ postprocessors: [OpenCvMeasurement]
```

适合固定光照、固定相机位、明确规则的工业检测场景。

### 2. 作为其他识别管线的预处理组件

深度学习模型（YOLO/OCR/分割）几乎都需要 resize、letterbox、色彩转换等前处理。这些通用能力用 OpenCV 实现为 `IFramePreprocessor` 类，被具体 pipeline 复用：

```text
YoloVisionPipeline
  ├─ preprocessors: [OpenCvLetterbox, OpenCvBgrToRgb]  ← 复用 OpenCV 预处理类
  ├─ analyzer:      YoloOnnxAnalyzer
  └─ postprocessors: [YoloNms, CoordinateMapper]
```

### 3. 作为其他识别管线的后处理组件

深度学习模型的输出（检测框、分割 mask）也需要几何后处理，OpenCV 同样以 `IVisionPostprocessor` 类库形式复用：

```text
SegmentationVisionPipeline
  ├─ preprocessors: [OpenCvResize]
  ├─ analyzer:      OnnxSegAnalyzer
  └─ postprocessors: [OpenCvMaskContour, OpenCvMeasurement]  ← 复用 OpenCV 后处理类
```

**关键**：复用方式是**类库复用**（同一个 `OpenCvLetterbox` 类，被不同 pipeline 各自 `new` 一个实例），不是"共享同一份 preprocessed frame"。这样每条 pipeline 的预处理需求独立、互不干扰。

## 核心数据结构

### VisionFrame

`VisionFrame` 是算法输入的统一视图。第一版复用 `cv::Mat` 作为实际图像存储，补齐格式元数据。

```cpp
enum class VisionPixelFormat {
    BGR24,
    RGB24,
    GRAY8
};

struct VisionFrame {
    int width = 0;
    int height = 0;
    int stride = 0;
    int64_t pts = 0;
    VisionPixelFormat pixelFormat = VisionPixelFormat::BGR24;
    cv::Mat image;
};
```

约束：

- 默认输入格式为 `BGR24`，与当前 OpenCV 链路一致。
- Pipeline 内部需要修改帧数据时，必须先 `clone()` 出一份 working frame，不能污染上游传入的 `const VisionFrame&`。
- 处理器不得保存 `VisionFrame` 中的 `cv::Mat` 数据指针跨帧使用。

### VisionResult / VisionResultSet

统一识别结果结构：

```cpp
enum class VisionResultType {
    DetectionBox,
    TextRegion,
    Classification,
    Measurement
};

struct DetectionBox {
    QRectF box;
    QString label;
    float confidence = 0.0F;
    int classId = -1;
};

struct TextRegion {
    QRectF box;
    QString text;
    float confidence = 0.0F;
};

struct VisionResult {
    QString pipelineId;          // 结果来自哪条 pipeline
    VisionResultType type = VisionResultType::DetectionBox;
    QVector<DetectionBox> boxes;
    QVector<TextRegion> texts;
    double elapsedMs = 0.0;
    QString errorMessage;
};

struct VisionResultSet {
    int64_t pts = 0;
    QVector<VisionResult> results;   // 单激活模式下通常只有 1 项
};
```

第一版只实现 `DetectionBox` 和 `TextRegion`；`Classification`、`Measurement` 后续再补。

## 接口设计

### 顶层接口：IVisionPipeline

主链路的核心抽象——**一种识别算法 = 一个 `IVisionPipeline`**：

```cpp
class IVisionPipeline {
public:
    virtual ~IVisionPipeline() = default;

    // 身份和展示
    virtual QString Id() const = 0;              // 唯一标识,如 "yolo-onnx"
    virtual QString DisplayName() const = 0;     // UI 显示名,如 "YOLO 目标检测"
    virtual QString Type() const = 0;            // 分类,如 "detection" / "ocr" / "segmentation"

    // 懒加载生命周期
    virtual bool Startup() = 0;                  // 加载模型 / 分配 GPU 资源
    virtual void Shutdown() = 0;                 // 释放资源
    virtual bool IsStarted() const = 0;

    // 执行:预处理 → 分析 → 后处理
    virtual bool Run(const VisionFrame& input, VisionResultSet& out) = 0;
};
```

约束：

- `Startup()` 可能耗时（YOLO 模型加载几百 MB），第一版仍同步调用，UI 侧显示 loading；后续演进到独立线程。
- `Run()` 接收 `const VisionFrame&`，内部要修改必须自己 `clone()`。
- `Shutdown()` 后 pipeline 回到 dormant 状态，可以再次 `Startup()`。

### Pipeline 内部构建块

三个小接口作为 pipeline 内部的可重用组件（**不直接暴露给主链路**）：

```cpp
class IFramePreprocessor {
public:
    virtual ~IFramePreprocessor() = default;
    virtual QString Name() const = 0;
    virtual bool Process(VisionFrame& frame) = 0;  // in-place 修改
};

class IVisionAnalyzer {
public:
    virtual ~IVisionAnalyzer() = default;
    virtual QString Name() const = 0;
    virtual bool Analyze(const VisionFrame& frame, VisionResultSet& out) = 0;
};

class IVisionPostprocessor {
public:
    virtual ~IVisionPostprocessor() = default;
    virtual QString Name() const = 0;
    virtual bool Process(const VisionFrame& frame, VisionResultSet& results) = 0;
};
```

约束：

- Preprocessor 可以修改 `VisionFrame`（包括图像内容和元数据尺寸）。
- Analyzer 不能修改 `VisionFrame`（`const`），只写 `VisionResultSet`。
- Postprocessor 不能修改 `VisionFrame`（`const`），只加工 `VisionResultSet`。

### VisionPipelineManager

管理所有已注册管线的注册表 + 激活切换器：

```cpp
class VisionPipelineManager : public QObject {
    Q_OBJECT
public:
    void RegisterPipeline(std::unique_ptr<IVisionPipeline>);

    QList<QString> AvailablePipelineIds() const;
    IVisionPipeline* FindPipeline(const QString& id) const;

    // 切换激活:自动 Shutdown 旧的, Startup 新的
    bool SetActivePipeline(const QString& id);   // 空 id = 关闭视觉分析
    QString ActivePipelineId() const;
    IVisionPipeline* ActivePipeline() const;

    // 执行入口:由 VisionProcessingFrameSink 每帧调用
    bool RunActive(const VisionFrame& input, VisionResultSet& out);

signals:
    void ActivePipelineChanged(QString id);
    void PipelineStartupFailed(QString id, QString error);

private:
    QHash<QString, std::unique_ptr<IVisionPipeline>> pipelines_;
    QString activeId_;
};
```

行为约束：

- `SetActivePipeline` 先 `Shutdown()` 旧 active（如果有），再 `Startup()` 新 target；失败时保持无 active 状态并发 `PipelineStartupFailed`。
- `RunActive` 无 active pipeline 时直接返回 false（sink 会透传原帧）。
- Manager 析构时遍历所有 pipeline，若 `IsStarted()` 则 `Shutdown()`。

### VisionOverlayRenderer

统一绘制识别结果，与具体识别算法解耦：

```cpp
class VisionOverlayRenderer {
public:
    void Draw(cv::Mat& image, const VisionResultSet& resultSet);
};
```

第一版绘制内容：目标框、标签、置信度、OCR 文本、分析耗时。

### VisionProcessingFrameSink

每路视频流各持有一个，用于插入到 sink 链：

```cpp
class VisionProcessingFrameSink : public IVideoFrameSink {
public:
    VisionProcessingFrameSink(VisionPipelineManager* manager,
                              IVideoFrameSink* downstream,
                              MultiStreamManager::StreamId streamId);

    void SetActive(bool active) { isActive_ = active; }
    void SetOverlayEnabled(bool enabled) { overlayEnabled_ = enabled; }

    void PresentFrame(AVFramePtr frame) override;
    void ClearFrame() override;

private:
    VisionPipelineManager* manager_;    // 借用,不拥有
    IVideoFrameSink* downstream_;
    VisionOverlayRenderer overlay_;
    VideoFrameMatConverter converter_;
    MultiStreamManager::StreamId streamId_;
    bool isActive_ = false;
    bool overlayEnabled_ = true;
};
```

行为：

- `isActive_ == false` 或 `manager->ActivePipeline() == nullptr` → 透传原帧。
- 否则 → 转 `VisionFrame` → 调 `manager->RunActive` → Overlay → 转回 `AVFrame` → 转发下游。
- 处理失败（如 pipeline 内部 Run 返回 false）→ 记录低频日志，转发原帧，不阻断播放。

## 单条 Pipeline 的内部组织

每条 `IVisionPipeline` 内部自己用 `QVector` 组织构建块，不需要专门的 Pipeline 容器类（每条管线的构建块数量不多，专门抽一层是过度设计）。

以 `YoloVisionPipeline` 为例：

```cpp
class YoloVisionPipeline : public IVisionPipeline {
public:
    QString Id() const override { return "yolo-onnx"; }
    QString DisplayName() const override { return "YOLO 目标检测"; }
    QString Type() const override { return "detection"; }

    bool Startup() override {
        if (started_) return true;
        preprocessors_.push_back(std::make_unique<OpenCvLetterbox>(640, 640));
        preprocessors_.push_back(std::make_unique<OpenCvBgrToRgb>());
        analyzer_ = std::make_unique<YoloOnnxAnalyzer>("model.onnx");
        if (!analyzer_->Load()) { Shutdown(); return false; }
        postprocessors_.push_back(std::make_unique<YoloNms>(0.5f));
        postprocessors_.push_back(std::make_unique<CoordinateMapper>());
        started_ = true;
        return true;
    }

    void Shutdown() override {
        preprocessors_.clear();
        analyzer_.reset();       // 释放模型
        postprocessors_.clear();
        started_ = false;
    }

    bool IsStarted() const override { return started_; }

    bool Run(const VisionFrame& input, VisionResultSet& out) override {
        if (!started_) return false;

        VisionFrame working = input;
        working.image = input.image.clone();   // 每次 Run 一份 working frame

        for (auto& pp : preprocessors_) {
            if (!pp->Process(working)) return false;
        }
        if (!analyzer_->Analyze(working, out)) return false;
        for (auto& pp : postprocessors_) {
            if (!pp->Process(working, out)) return false;
        }
        return true;
    }

private:
    QVector<std::unique_ptr<IFramePreprocessor>> preprocessors_;
    std::unique_ptr<IVisionAnalyzer> analyzer_;
    QVector<std::unique_ptr<IVisionPostprocessor>> postprocessors_;
    bool started_ = false;
};
```

## 集成到现有链路

### VisionProcessingFrameSink 挂点

在 `MediaPipeline::CreatePipelineResources` 里创建 sink 时，链路装配为：

```text
VideoFrameScheduler
    ↓
ImageProcessingFrameSink (Phase 5,已有)
    ↓
VisionProcessingFrameSink (Phase 7,新增)
    · 构造时传入 VisionPipelineManager* (从 MultiStreamManager 拿)
    · 传入自己的 StreamId
    ↓
SingleStreamFrameSinkAdapter
    ↓
MultiViewRenderer
```

### 三个关键事件流

**添加一路视频流**：

```text
MediaPlayController::AddStream(mediaSource, container)
   ↓
MultiStreamManager::AddStream(...)
   ↓
新建 MediaPipeline
   ↓
MediaPipeline 内部:
   · 创建 ImageProcessingFrameSink (已有)
   · 创建 VisionProcessingFrameSink,传入:
      - VisionPipelineManager* (从上层拿)
      - StreamId (这路的 ID)
   · 装配 sink 链: sched → imgProc → visionProc → adapter → renderer
   ↓
返回 StreamId → MultiStreamManager 判断是否设为 active stream
   ↓
若成为 active stream → 该 sink SetActive(true)
```

**切换 active stream**：

```text
UI → SetActiveStream(id)
   ↓
MultiStreamManager::SetActiveStream(id):
   · 旧 active stream 的 VisionProcessingFrameSink::SetActive(false)
   · 新 active stream 的 VisionProcessingFrameSink::SetActive(true)
   · 发信号 ActiveStreamChanged
```

**注意**：切换 active stream 不重新加载模型（pipeline 是共享的、模型已经 Startup 好了），只是拨 sink 的 flag，非常轻量。

**切换识别管线**：

```text
UI 下拉框选 YOLO → MediaPlayController::SetVisionPipeline("yolo-onnx")
   ↓
VisionPipelineManager::SetActivePipeline("yolo-onnx"):
   · 旧 active pipeline (如果有) → Shutdown()  释放资源
   · 新目标 pipeline → Startup()  加载模型(可能耗时,UI 显示 loading)
   · 更新 activeId_
   · 发信号 ActivePipelineChanged
   ↓
下一帧 active stream 的 VisionProcessingFrameSink::PresentFrame:
   · manager_->ActivePipeline() 返回 YoloVisionPipeline
   · 调 manager_->RunActive(vf, results)
   · Overlay 用 YOLO 输出的 DetectionBox 画框
```

### 一帧完整流转（active stream + active pipeline）

```text
1. Demuxer/Decoder 出帧
      ↓ AVFrame
2. VideoFrameScheduler (同步判断)
      ↓ AVFrame
3. ImageProcessingFrameSink (如果启用,跑 IImageProcessor 链)
      ↓ AVFrame
4. VisionProcessingFrameSink                           ← 新增这一步
   · isActive_ && manager 有 active pipeline
   · AVFrame → VisionFrame (BGR24 + 元数据)
   · manager->RunActive(vf, results):
        · 转发给 YoloVisionPipeline::Run()
        · 内部: working = clone → letterbox → BGR2RGB → YOLO → NMS → 坐标映射
   · overlayRenderer.Draw(vf.image, results)  画检测框
   · VisionFrame → AVFrame
      ↓ AVFrame (已画好检测框)
5. SingleStreamFrameSinkAdapter → MultiViewRenderer
      ↓
6. 显示到窗口
```

## 设计模式

- **Strategy**：`IVisionPipeline` 是"识别算法"的策略接口，`IFramePreprocessor` / `IVisionAnalyzer` / `IVisionPostprocessor` 是 pipeline 内部的子策略。
- **Registry / Manager**：`VisionPipelineManager` 是注册表 + 状态机（激活切换）。
- **Adapter**：`VideoFrameMatConverter` 把 FFmpeg 帧转为视觉输入。
- **Decorator**：`VisionProcessingFrameSink` 在调度器和渲染器之间增强处理能力。
- **Factory**：`VisionProcessorFactory` 在启动时创建并注册所有内置识别管线到 `VisionPipelineManager`。

## 内置管线注册

Phase 7 的算法接入统一走**内置工厂**，不涉及外部动态库加载。所有识别管线在源码中作为 `IVisionPipeline` 子类实现，由 `VisionProcessorFactory` 在启动时创建并注册到 `VisionPipelineManager`：

```text
VisionProcessorFactory::RegisterBuiltinPipelines(VisionPipelineManager& manager)
    ├─ manager.RegisterPipeline(std::make_unique<OpenCvContourVisionPipeline>())
    ├─ manager.RegisterPipeline(std::make_unique<YoloVisionPipeline>())
    ├─ manager.RegisterPipeline(std::make_unique<OcrVisionPipeline>())
    └─ manager.RegisterPipeline(std::make_unique<SegmentationVisionPipeline>())
```

新增一种识别算法 = 新增一个 `IVisionPipeline` 子类 + 在 Factory 里加一行注册代码。整个过程在源码内闭环，编译期即可发现接口不匹配。

优点：

- 调试简单，可以直接 step-into pipeline 内部
- 类型安全，编译期即可发现接口不匹配
- 无需处理外部动态库、版本兼容或运行时加载错误

## 配置模型

第一版使用内存配置，后续再持久化：

```cpp
struct VisionConfig {
    QString activePipelineId;                        // 空 = 关闭视觉分析
    QHash<QString, QVariantMap> pipelineParameters;  // 每条 pipeline 的参数
    bool drawOverlay = true;
};
```

每条 pipeline 的具体参数（如 YOLO 的置信度阈值、OpenCV 的阈值等）由该 pipeline 自己解释 `pipelineParameters[id]`。

## 线程与性能策略

### 第一版：同步 active stream

```text
active stream frame
    → 同步预处理
    → 同步分析
    → 同步后处理
    → Overlay
    → 显示
```

约束：

- 只处理 active stream，其他路 sink 直接透传。
- 默认无 active pipeline（视觉分析关闭）。
- 单帧处理失败时转发原帧，不阻断播放。
- 单帧耗时过高时记录低频日志。

### 第二版：降帧分析

推理耗时高时，把推理频率降低到 5-10 fps，显示帧率保持原速，复用最近一次识别结果画到当前帧。

### 第三版：异步分析

推理放到独立 worker thread，主链路只读最新结果。异步版必须处理：

- 队列上限。
- 关闭中断。
- 结果过期策略（旧帧的结果不能画在新帧上）。
- seek / 切换 stream / 切换 pipeline 时清空缓存。

第一版不做异步，避免生命周期复杂化。

## UI 边界

UI 只展示配置和结果摘要，不直接访问帧数据。

推荐控制链路：

```text
MainWindow
    → MediaPlayController
        → MultiStreamManager
            → VisionPipelineManager  (UI 通过 controller 访问)
```

第一版 UI 控件：

- "识别模式" 下拉框：关闭 / OpenCV 传统 / YOLO / OCR / 分割（根据已注册的 pipeline 动态填充）
- Overlay 开关。
- 当前 active pipeline 的耗时和目标数量显示。
- Pipeline Startup 中的 loading 提示。

不建议第一版做：

- 单条 pipeline 内部参数的复杂配置界面（后续再加）。
- 多路独立视觉分析（架构上留了口，第一版不做 UI）。
- 检测结果表格和历史查询。

## 与现有 Phase 的关系

```text
Phase 5
    图像处理基础链路
    IImageProcessor / ImageProcessorPipeline
    ImageProcessingFrameSink

Phase 6
    传统 OpenCV 目标检测 (画框计数)
    继续用 IImageProcessor 模式

Phase 7
    识别管线抽象 + 单激活 + 懒加载
    IVisionPipeline / VisionPipelineManager
    VisionProcessingFrameSink / VisionOverlayRenderer
    OpenCV 三角色以类库形式复用
```

Phase 7 不推翻 Phase 5/6。Phase 5 的 `IImageProcessor` 链路继续作为通用图像变换存在（灰度化、二值化、边缘检测等）；Phase 6 的 `ObjectDetectionProcessor` 后续包装成一条 `IVisionPipeline` 迁移过来，作为 `OpenCvContourVisionPipeline` 的一种。

## 推荐实施 Phase

### Phase 7.1 架构设计与任务拆分

- 输出本文档。
- 明确 `IVisionPipeline` / `VisionPipelineManager` / `VisionProcessingFrameSink` 的职责。

验收：能说明"多管线独立 + 单激活 + 懒加载"三条原则的具体含义和取舍。

耗时：已完成。

### Phase 7.2 VisionFrame / VisionResult 类型定义

新增视觉帧和结果类型，不接入 UI 和 sink 链，只作为独立的数据结构定义。

预计修改文件：

```text
src/infrastructure/vision/visiontypes.h
```

验收：

- 类型不依赖 UI 和 FFmpeg。
- 能表达检测框和 OCR 文本。
- `VisionFrame` 显式声明像素格式和元数据。

耗时：0.5 天。

### Phase 7.3 IVisionPipeline + 三小接口 + VisionPipelineManager

新增：

- `IVisionPipeline`、`IFramePreprocessor`、`IVisionAnalyzer`、`IVisionPostprocessor` 接口。
- `VisionPipelineManager` 实现（注册 + 激活切换 + Startup/Shutdown 调度）。

预计修改文件：

```text
src/infrastructure/vision/ivisionpipeline.h
src/infrastructure/vision/iframepreprocessor.h
src/infrastructure/vision/ivisionanalyzer.h
src/infrastructure/vision/ivisionpostprocessor.h
src/infrastructure/vision/visionpipelinemanager.h
src/infrastructure/vision/visionpipelinemanager.cpp
```

验收：

- Manager 可以注册和切换 pipeline。
- 切换时正确调用 Shutdown/Startup。
- Startup 失败时 Manager 保持无 active 状态并发信号。
- 不影响现有播放。

耗时：1 天。

### Phase 7.4 VisionOverlayRenderer

新增统一绘制器，支持在 `cv::Mat` 上绘制 `DetectionBox` / `TextRegion`。

预计修改文件：

```text
src/infrastructure/vision/visionoverlayrenderer.h
src/infrastructure/vision/visionoverlayrenderer.cpp
```

验收：

- 绘制逻辑不在具体 pipeline 里。
- 可关闭 Overlay。
- 空结果时不修改图像。

耗时：1 天。

### Phase 7.5 内置 OpenCvContourVisionPipeline

第一条实际能用的识别管线：

- 内部组织：阈值 preprocessor + 轮廓 analyzer + 测量 postprocessor
- 完全用 OpenCV 类库实现
- 通过 `VisionProcessorFactory` 创建并注册到 Manager

预计修改文件：

```text
src/infrastructure/vision/opencvcontourvisionpipeline.h
src/infrastructure/vision/opencvcontourvisionpipeline.cpp
src/infrastructure/vision/opencvpreprocessors.h
src/infrastructure/vision/opencvpreprocessors.cpp
src/infrastructure/vision/opencvpostprocessors.h
src/infrastructure/vision/opencvpostprocessors.cpp
src/infrastructure/vision/visionprocessorfactory.h
src/infrastructure/vision/visionprocessorfactory.cpp
```

验收：

- active stream 能看到轮廓检测框。
- 检测结果通过 `VisionResultSet` 暴露。
- Startup/Shutdown 生命周期正确。
- 预处理和后处理类（OpenCV resize/threshold/measure 等）设计成可以被其他 pipeline 复用。

耗时：1-2 天。

### Phase 7.6 VisionProcessingFrameSink 接入现有链路

- 新增 `VisionProcessingFrameSink`。
- 在 `MediaPipeline` 里创建 sink，插入到 `ImageProcessingFrameSink` 之后。
- 在 `MultiStreamManager` 里持有 `VisionPipelineManager` 成员。
- 切换 active stream 时，更新 sink 的 `isActive_` flag。
- UI 增加"识别模式"下拉框。

预计修改文件：

```text
src/infrastructure/vision/visionprocessingframesink.h
src/infrastructure/vision/visionprocessingframesink.cpp
src/domain/MediaPipeline.h                          (创建 sink,注入 manager 指针)
src/domain/MediaPipeline.cpp
src/domain/MultiStreamManager.h                     (持有 Manager,管理 sink SetActive)
src/domain/MultiStreamManager.cpp
src/application/MediaPlayController.h               (暴露 Manager 给 UI)
src/application/MediaPlayController.cpp
src/ui/mainwindow.h                                  (识别模式下拉框)
src/ui/mainwindow.cpp
src/ui/mainwindow.ui
```

验收：

- 默认视觉分析关闭。
- 切换 active stream 时视觉分析跟随。
- Pipeline 切换时切走的旧 pipeline 正确 Shutdown。
- 视觉分析失败时透传原帧，不影响播放。

耗时：1-2 天。

### Phase 7.7 YoloVisionPipeline 接入验证

- 选择 OpenCV DNN 或 ONNX Runtime 实现一条完整的 `YoloVisionPipeline`。
- 通过内置工厂注册。
- 验证从 UI 切换 → Startup 加载模型 → 逐帧检测 → 结果绘制的完整链路。

验收：

- 能从模型输出检测框。
- 检测结果进入 `VisionResultSet`。
- Overlay 绘制检测框和标签。
- 模型输入坐标能映射回原始视频尺寸。
- 切换回 OpenCV 时 YOLO 正确 Shutdown 释放资源。

耗时：3-5 天，取决于模型格式、运行环境和依赖链。

## 最终呈现效果

完成 Phase 7 后，用户可以：

- 对 active stream 通过下拉框选择当前识别模式（关闭/OpenCV 轮廓/YOLO/OCR/...）。
- 切换识别模式时，UI 有 loading 提示（pipeline 在加载模型）。
- 切换 active stream 时，视觉分析自动跟随 active stream。
- 在画面上看到统一风格的检测框、标签、OCR 文本。
- 在内部获得结构化 `VisionResultSet`，为后续统计、报警、截图、存储打基础。

## 风险与处理

### 1. 模型推理性能风险

风险：YOLO/OCR 单帧耗时可能明显高于视频帧间隔，造成卡顿。

处理：

- 第一版只处理 active stream。
- 后续增加降帧分析和异步推理。
- 高分辨率视频可先缩放后检测，再由 `CoordinateMapper` 映射回原图坐标。

### 2. 颜色空间错误风险

风险：OpenCV 默认 BGR，部分模型需要 RGB。

处理：

- `VisionFrame` 显式声明 `pixelFormat`。
- 每条 pipeline 在自己的 preprocessors 里处理色彩转换。
- 不在调用链中隐式假设颜色空间。

### 3. Pipeline Startup 阻塞 UI

风险：YOLO 模型加载可能耗时 1-5 秒，同步调用会卡 UI。

处理：

- 第一版：Startup 同步调用，UI 显示 loading 遮罩。
- 后续演进：Startup 放到独立 worker thread，加载完成后通过 `ActivePipelineChanged` 信号回主线程；加载失败通过 `PipelineStartupFailed` 通知 UI。

### 4. Pipeline 切换时的资源清理

风险：旧 pipeline `Shutdown` 前若还有帧正在跑 `Run`，可能崩溃。

处理：

- 切换时先在 sink 层禁用当前 Run 调用（Manager 内部拉一个 pending flag）。
- 等当前帧跑完后再 Shutdown 旧 pipeline。
- 或者用互斥锁保护 pipeline 的 Run/Shutdown。

### 5. 单激活模式下的复合场景

风险：如果需要"检测行人 + 识别车牌"这种复合场景，单激活模式下不能同时开两条 pipeline。

处理：

- 把复合场景打包成**一条新的 pipeline**（如 `TrafficAnalysisVisionPipeline`），内部串行调用 YOLO analyzer 和 OCR analyzer，共享外层通用 preprocessor。
- 对外仍然是一条 pipeline，用户切换到"交通分析"模式即可。
- 保持"一个场景 = 一条 pipeline"的简单模型。

## 当前推荐结论

下一步不建议直接接 YOLO、OCR 或分割。

推荐先完成：

```text
VisionFrame + VisionResultSet
    → IVisionPipeline + 三小接口
    → VisionPipelineManager (单激活 + 懒加载)
    → VisionOverlayRenderer
    → OpenCvContourVisionPipeline (第一条实际管线)
    → VisionProcessingFrameSink 接入现有链路
```

原因：

- 这是所有具体识别算法的共同底座。
- 单激活模式使调试和错误定位极其简单：出问题时你知道就是当前这条 pipeline。
- OpenCV 类库可以在预处理、独立识别、后处理三个位置复用。
- 结构化结果和 Overlay 分离，为后续统计、报警、存储打基础。
- 新识别算法只需实现 `IVisionPipeline` 并在 Factory 里注册一行，接口稳定后新增算法不影响主链路。
