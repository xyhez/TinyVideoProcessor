# Phase 7 多视觉处理器插件化架构设计

## 项目愿景

在现有多路视频播放、基础图像处理和 OpenCV 目标检测能力之上，进一步演进为可扩展的视频分析平台。

Phase 7 的目标不是直接绑定某一个检测库，而是建立一套稳定的视觉处理器抽象，让 OCR、OpenCV 自研动态库、YOLO/ONNX 模型、传统规则检测等能力可以按统一方式接入、启停、组合和替换。

最终希望达到的状态：

```text
视频源
    -> 解码 AVFrame
    -> 帧格式归一化
    -> 预处理链
    -> 多种视觉分析处理器
    -> 结构化检测结果
    -> 画面 Overlay / 日志 / 事件 / 后续存储
```

## 设计原则

- 复用现有链路：继续沿用 `ImageProcessingFrameSink`、`VideoFrameMatConverter`、`ImageProcessorPipeline` 的插入位置，不把算法塞进 decoder、scheduler、renderer 或 UI。
- 区分图像变换和视觉分析：灰度、二值化、边缘检测属于图像变换；OCR、YOLO、目标检测属于视觉分析，后者必须能输出结构化结果。
- 显式描述帧格式：不能让处理器私下假设输入一定是 RGB 或 BGR，必须通过元数据声明颜色空间、尺寸、stride 和时间戳。
- 结果和显示解耦：检测结果先进入统一结果结构，再由 Overlay 统一绘制；不要让所有检测器都直接在图像上画框。
- 插件边界保持稳定：主程序依赖统一接口，具体算法库通过内置工厂或 DLL 插件接入。
- 第一版保持同步链路可控：先只处理 active stream；异步推理、降帧推理、多路并行作为后续演进。

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

- `IImageProcessor::Process(cv::Mat& image)` 只适合“改图”类处理，不适合 OCR/YOLO 这类需要输出结构化结果的处理器。
- 当前中间格式实际是 `cv::Mat + BGR24`，但没有显式帧格式描述。
- 检测结果如果只画到图像上，后续无法做统计、报警、导出、存储和跨帧跟踪。
- 动态库插件还没有统一加载协议。

## 总体架构

推荐新增一条视觉分析链路，和现有图像处理链路分层组合：

```text
AVFrame
    -> VideoFrameMatConverter
    -> VisionFrame
        -> PreprocessPipeline
            -> Grayscale / Resize / Normalize
        -> VisionAnalyzerPipeline
            -> OCRAnalyzer
            -> OpenCvDllAnalyzer
            -> YoloAnalyzer
        -> VisionResultSet
        -> OverlayRenderer
    -> AVFrame
    -> MultiViewRenderer
```

模块职责：

- `VisionFrame`：统一描述算法输入帧。
- `PreprocessPipeline`：可选预处理链，负责缩放、色彩转换、去噪、归一化。
- `IVisionAnalyzer`：视觉分析策略接口，负责输出结构化结果。
- `VisionAnalyzerPipeline`：管理多个分析器，顺序或按配置执行。
- `VisionResultSet`：统一保存检测框、OCR 文本、分类结果、耗时和错误信息。
- `OverlayRenderer`：把结构化结果绘制到 `cv::Mat` 上。
- `VisionPluginManager`：扫描、加载、卸载外部处理器插件。

## 核心数据结构

### VisionFrame

`VisionFrame` 是算法输入的统一视图。第一版可以继续复用 `cv::Mat` 作为实际图像存储，但必须补齐格式元数据。

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

第一版约束：

- 默认输入格式使用 `BGR24`，和当前 OpenCV 链路一致。
- 如果某个处理器需要 `RGB24`，由处理器内部转换，或通过后续 `FrameFormatAdapter` 按需转换。
- `VisionFrame` 不拥有异步生命周期，处理器不得保存其中的 `cv::Mat` 数据指针。

### VisionResult

视觉分析结果需要覆盖 OCR、目标检测和传统视觉检测。

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
    QString analyzerName;
    VisionResultType type = VisionResultType::DetectionBox;
    QVector<DetectionBox> boxes;
    QVector<TextRegion> texts;
    double elapsedMs = 0.0;
    QString errorMessage;
};

struct VisionResultSet {
    int64_t pts = 0;
    QVector<VisionResult> results;
};
```

第一版可以只实现 `DetectionBox` 和 `TextRegion`，测量、分类、跟踪 ID 后续追加。

## 处理器接口设计

### 现有 IImageProcessor 的定位

保留现有 `IImageProcessor`：

```cpp
class IImageProcessor {
public:
    virtual ~IImageProcessor() = default;
    virtual QString Name() const = 0;
    virtual bool Enabled() const = 0;
    virtual void SetEnabled(bool enabled) = 0;
    virtual bool Process(cv::Mat& image) = 0;
};
```

它继续服务：

- 灰度化。
- 二值化。
- 边缘检测。
- 轮廓画框这类第一阶段演示处理。

### 新增 IVisionAnalyzer

新增分析型接口：

```cpp
class IVisionAnalyzer {
public:
    virtual ~IVisionAnalyzer() = default;

    virtual QString Name() const = 0;
    virtual bool Enabled() const = 0;
    virtual void SetEnabled(bool enabled) = 0;

    virtual bool Analyze(const VisionFrame& frame, VisionResultSet& resultSet) = 0;
};
```

约束：

- 不访问 UI。
- 不访问 `AVFrame`。
- 不访问 OpenGL。
- 不保存当前帧指针。
- 失败时写入 `errorMessage`，不能中断播放主链路。

### OverlayRenderer

统一绘制检测结果：

```cpp
class VisionOverlayRenderer {
public:
    void Draw(cv::Mat& image, const VisionResultSet& resultSet);
};
```

第一版绘制内容：

- 目标框。
- 标签。
- 置信度。
- OCR 文本。
- 分析耗时或目标数量。

## 设计模式

本阶段推荐组合使用以下设计模式：

- Adapter：`VideoFrameMatConverter` / `FrameFormatAdapter` 把 FFmpeg 帧转换为视觉算法输入。
- Strategy：`IVisionAnalyzer` 统一封装 OCR、YOLO、OpenCV 动态库等算法。
- Pipeline：`VisionAnalyzerPipeline` 组合多个分析器。
- Factory：`VisionAnalyzerFactory` 根据配置创建内置分析器。
- Plugin：`VisionPluginManager` 动态加载外部 DLL。
- Decorator：沿用 `ImageProcessingFrameSink` 在调度器和渲染器之间增强处理能力。

## 插件加载设计

### 第一阶段：内置工厂

先实现内置处理器工厂，不直接上动态库：

```text
VisionAnalyzerFactory
    -> Create("opencv-contour")
    -> Create("ocr")
    -> Create("yolo")
```

优点：

- 调试简单。
- ABI 风险低。
- 适合先把结果通道和 Overlay 打通。

### 第二阶段：C ABI 动态库插件

外部 DLL 不建议直接暴露 C++ 类。推荐使用 C ABI，降低编译器、Qt、STL ABI 不一致风险。

主程序定义稳定结构：

```cpp
struct TvpFrameView {
    int width;
    int height;
    int stride;
    int pixelFormat;
    int64_t pts;
    const unsigned char* data;
};

struct TvpDetectionBox {
    float x;
    float y;
    float width;
    float height;
    float confidence;
    int classId;
};
```

插件导出函数：

```cpp
extern "C" __declspec(dllexport)
int tvp_create_processor(void** handle);

extern "C" __declspec(dllexport)
int tvp_process_frame(
    void* handle,
    const TvpFrameView* frame,
    TvpDetectionBox* boxes,
    int maxBoxCount,
    int* outputBoxCount
);

extern "C" __declspec(dllexport)
void tvp_destroy_processor(void* handle);
```

主程序侧：

```text
VisionPluginManager
    -> QLibrary::load()
    -> resolve create/process/destroy
    -> wrap as IVisionAnalyzer
```

插件目录建议：

```text
plugins/vision/
    opencv_shape_detector/
        processor.dll
        plugin.json
    yolo_onnx/
        processor.dll
        plugin.json
```

`plugin.json` 建议包含：

```json
{
  "id": "yolo-onnx",
  "name": "YOLO ONNX Detector",
  "version": "1.0.0",
  "inputFormat": "BGR24",
  "type": "object-detection",
  "supportsBatch": false
}
```

## 配置模型

第一版可以使用内存配置，后续再持久化。

```cpp
struct VisionAnalyzerConfig {
    QString id;
    QString type;
    bool enabled = false;
    QVariantMap parameters;
};

struct VisionPipelineConfig {
    bool enabled = false;
    QVector<VisionAnalyzerConfig> analyzers;
    bool drawOverlay = true;
};
```

典型配置：

```text
enabled = true
analyzers:
    - id: "opencv-contour"
      type: "builtin"
      enabled: true
      parameters:
          threshold: 128
          minArea: 100
          maxArea: 20000
    - id: "yolo-onnx"
      type: "plugin"
      enabled: false
      parameters:
          modelPath: "models/yolo.onnx"
          confidence: 0.5
```

## 线程与性能策略

### 第一版：同步 active stream

第一版保持简单：

```text
active stream frame
    -> 同步分析
    -> 画框
    -> 显示
```

约束：

- 只处理 active stream。
- 默认关闭视觉分析。
- 单帧处理失败时转发原帧。
- 单帧耗时过高时记录低频日志，不阻塞关闭流程。

### 第二版：降帧分析

当 OCR/YOLO 推理耗时较高时，引入分析帧率控制：

```text
显示帧率 30fps
分析帧率 5fps / 10fps
检测结果缓存并复用到后续显示帧
```

### 第三版：异步分析

后续可增加异步推理队列：

```text
FrameSubmitter
    -> bounded queue
    -> worker thread
    -> latest VisionResultSet cache
    -> OverlayRenderer
```

异步版必须单独设计：

- 队列上限。
- 关闭中断。
- 旧结果过期策略。
- seek 后结果清空。
- remove stream 后生命周期释放。

第一版不建议直接实现异步，避免生命周期和关闭链路复杂化。

## UI 边界

UI 只负责展示配置和结果摘要，不直接访问帧数据。

推荐控制链路：

```text
MainWindow
    -> MediaPlayController
        -> MultiStreamManager
            -> active MediaPipeline
                -> ImageProcessingFrameSink / VisionProcessingFrameSink
```

第一版 UI 控件：

- 视觉分析总开关。
- 分析器列表。
- 单个分析器启停。
- 基础参数入口。
- Overlay 开关。
- 当前分析耗时。

不建议第一版做：

- 插件热更新。
- 复杂节点编排界面。
- 多路独立参数面板。
- 检测结果表格和历史查询。

## 与现有 Phase 的关系

```text
Phase 5
    图像处理基础链路
    IImageProcessor / ImageProcessorPipeline

Phase 6
    传统 OpenCV 目标检测
    可先继续用 IImageProcessor 画框计数

Phase 7
    多视觉分析器与插件化架构
    IVisionAnalyzer / VisionResultSet / PluginManager / OverlayRenderer
```

Phase 7 不要求推翻 Phase 6。推荐保留 Phase 6 的 `ObjectDetectionProcessor`，后续将其改造或包一层适配器，输出 `VisionResultSet`。

## 推荐实施 Phase

### Phase 7.1 架构设计与任务拆分

目标：

- 输出本设计文档。
- 明确结果通道、插件边界和演进顺序。

验收：

- 能说明 `IImageProcessor` 和 `IVisionAnalyzer` 的职责差异。
- 能说明为什么先做结果通道再接 YOLO/OCR。
- 能说明插件 DLL 为什么采用 C ABI。

预计耗时：0.5 天。

### Phase 7.2 VisionFrame 与 VisionResult 类型定义

目标：

- 新增视觉帧和结果类型。
- 不接入 UI。
- 不接入插件。

预计修改文件：

```text
src/infrastructure/visiontypes.h
```

验收：

- 类型不依赖 UI。
- 类型不依赖 FFmpeg。
- 可表达检测框和 OCR 文本。

预计耗时：0.5 天。

### Phase 7.3 IVisionAnalyzer 与 Pipeline 空壳

目标：

- 新增 `IVisionAnalyzer`。
- 新增 `VisionAnalyzerPipeline`。
- 支持添加、清空、启停和耗时统计。

预计修改文件：

```text
src/infrastructure/ivisionanalyzer.h
src/infrastructure/visionanalyzerpipeline.h
src/infrastructure/visionanalyzerpipeline.cpp
```

验收：

- 不影响现有播放。
- 不影响现有 `IImageProcessor`。
- Pipeline 可执行空分析器。

预计耗时：1 天。

### Phase 7.4 OverlayRenderer

目标：

- 新增统一 Overlay 绘制器。
- 支持根据 `VisionResultSet` 在 `cv::Mat` 上绘制框、标签、OCR 文本。

预计修改文件：

```text
src/infrastructure/visionoverlayrenderer.h
src/infrastructure/visionoverlayrenderer.cpp
```

验收：

- 绘制逻辑不进入 OCR/YOLO 处理器。
- 可关闭 Overlay。
- 输入空结果时不修改图像或只保持原图。

预计耗时：1 天。

### Phase 7.5 内置 OpenCV Analyzer 适配

目标：

- 将传统轮廓检测能力作为一个 `IVisionAnalyzer` 接入。
- 输出 `DetectionBox`，由 `OverlayRenderer` 统一画框。

预计修改文件：

```text
src/infrastructure/opencvcontouranalyzer.h
src/infrastructure/opencvcontouranalyzer.cpp
```

验收：

- active stream 可以看到目标框。
- 检测结果不再只能保存在图像像素里。
- 后续可用于统计和存储。

预计耗时：1-2 天。

### Phase 7.6 VisionProcessingFrameSink 接入

目标：

- 在现有 `ImageProcessingFrameSink` 基础上接入视觉分析链。
- 可以选择保留一个 sink，也可以新增 `VisionProcessingFrameSink`，但职责必须清晰。

推荐链路：

```text
ImageProcessingFrameSink
    -> IImageProcessor 预处理
    -> IVisionAnalyzer 分析
    -> OverlayRenderer 绘制
    -> downstream sink
```

验收：

- 默认关闭，不影响现有播放。
- 只处理 active stream。
- 处理失败时转发原帧。

预计耗时：1-2 天。

### Phase 7.7 插件管理器

目标：

- 新增 `VisionPluginManager`。
- 支持扫描插件目录、读取 `plugin.json`、加载 DLL、解析 C ABI 函数。

预计修改文件：

```text
src/infrastructure/visionpluginmanager.h
src/infrastructure/visionpluginmanager.cpp
```

验收：

- 插件加载失败不影响主程序启动。
- 插件处理失败不影响播放。
- 卸载时能调用 destroy 函数。

预计耗时：2-3 天。

### Phase 7.8 YOLO / OCR 接入验证

目标：

- 选择一个最小可控的外部能力接入。
- 推荐先接 OpenCV DNN / ONNX YOLO，OCR 放到后续。

验收：

- 能从模型输出检测框。
- 检测结果进入 `VisionResultSet`。
- Overlay 绘制检测框和标签。

预计耗时：3-5 天，取决于模型格式、运行环境和依赖链。

## 最终呈现效果

完成 Phase 7 后，用户可以：

- 对 active stream 开启视觉分析。
- 选择一个或多个分析器。
- 使用传统 OpenCV 检测器、OCR 或 YOLO 检测器。
- 在画面上看到统一风格的检测框、标签、OCR 文本。
- 在内部获得结构化检测结果，为后续统计、报警、截图和存储打基础。

## 风险与处理

### 1. C++ ABI 风险

风险：

- 外部 DLL 如果直接暴露 C++ 类，容易受 MSVC 版本、Qt 版本、STL ABI 影响。

处理：

- 动态插件采用 C ABI。
- 主程序用 wrapper 转成 `IVisionAnalyzer`。

### 2. 模型推理性能风险

风险：

- YOLO/OCR 单帧耗时可能明显高于视频帧间隔，造成卡顿。

处理：

- 第一版只处理 active stream。
- 后续增加降帧分析和异步队列。
- 高分辨率视频可先缩放后检测，再映射回原图坐标。

### 3. 颜色空间错误风险

风险：

- OpenCV 默认 BGR，部分 YOLO/OCR 模型需要 RGB。

处理：

- `VisionFrame` 显式声明 `pixelFormat`。
- 每个 Analyzer 声明或内部处理所需格式。
- 不在调用链中隐式假设颜色空间。

### 4. 结果生命周期风险

风险：

- 异步分析时，旧帧结果可能叠加到新帧，seek/remove 后可能访问已释放资源。

处理：

- 第一版同步处理。
- 异步版必须按 stream id、pts 和 generation id 管理结果。
- seek/remove/close 时清空缓存并停止 worker。

### 5. 过早插件化风险

风险：

- 在结果通道和 Overlay 没稳定前直接接 DLL，会导致调试困难。

处理：

- 先做内置 `IVisionAnalyzer`。
- 再做插件 wrapper。
- 最后接 YOLO/OCR。

## 当前推荐结论

下一步不建议直接接 YOLO 或 OCR。

推荐先完成：

```text
VisionFrame
    -> IVisionAnalyzer
    -> VisionResultSet
    -> OverlayRenderer
```

原因：

- 这是 OCR、YOLO、OpenCV DLL 的共同底座。
- 可以避免检测结果只能画死在图像上。
- 后续统计、报警、截图、存储都依赖结构化结果。
- 插件化加载可以在接口稳定后自然接入，不会反复改主链路。
