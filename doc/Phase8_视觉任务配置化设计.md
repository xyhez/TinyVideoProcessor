# Phase 8 视觉任务配置化设计

## 项目愿景

Phase 7 已经完成多视觉处理器架构，OpenCV 轮廓检测和 YOLO ONNX 检测可以通过统一 `IVisionPipeline` 接入、切换和绘制。

Phase 8 的目标是把 Phase 7 中硬编码的视觉任务参数收拢到配置层，让后续接入自训练 YOLO、OCR、分割模型和复合检测任务时，不再通过修改 C++ 常量完成调参。

最终呈现：用户或开发者可以通过一个项目级配置文件声明有哪些视觉管线、每条管线使用什么模型、输入尺寸、阈值和类别文件。程序启动时加载配置，配置缺失或非法时回退到内置默认值，保证当前 OpenCV / YOLO 验收链路不被破坏。

## 设计原则

- 保持 Phase 7 架构边界：`MainWindow` 不直接解析配置，`IVisionPipeline` 不访问 UI，`VisionPipelineManager` 仍只管理 pipeline 生命周期。
- 配置只描述任务，不承载运行状态：active pipeline、播放状态、当前 stream 不写入 Phase 8 配置。
- 内置默认值优先保证可运行：配置文件不存在时，仍注册 OpenCV 和 YOLO 默认管线。
- 第一版使用 JSON：复用 Qt `QJsonDocument` / `QJsonObject`，不引入 YAML、TOML 或第三方依赖。
- 路径支持项目相对路径：模型、labels 文件可以写成 `models/yolo.onnx`，加载时基于项目根目录或运行目录解析。
- 单条 pipeline 独立配置：每个 pipeline 有自己的 id、displayName、type、enabled 和 params。
- 保持单激活模式：配置可以声明多条 pipeline，但运行时仍只允许 `VisionPipelineManager` 激活一条。
- 失败可诊断：配置解析失败、模型路径缺失、参数越界时要记录明确日志，并尽可能回退默认值。

## 当前问题

Phase 7 中以下内容仍硬编码在代码里：

- `YoloVisionPipeline` 默认模型路径：`models/yolo.onnx`
- YOLO 输入尺寸：`640x640`
- YOLO confidence 阈值：`0.35`
- YOLO NMS 阈值：`0.45`
- YOLO 类别名：内置 COCO 80 类
- OpenCV 轮廓检测阈值：`128`
- `VisionProcessorFactory` 固定注册 OpenCV + YOLO

这些硬编码在验证架构时可接受，但会阻碍后续场景：

- 自训练 YOLO 的类别不是 COCO 80 类。
- 不同模型可能需要 `320x320`、`640x640`、`1280x1280` 等输入尺寸。
- 工业检测场景经常需要按视频源、光照和目标尺寸调整阈值。
- OCR / 分割 / 复合检测管线需要更多模型和参数。

## 总体架构

Phase 8 新增一个轻量配置层，位于 `infrastructure/vision` 内部，由 factory 使用，不上穿到 UI。

```text
vision_pipelines.json
        │
        ↓
VisionConfigLoader
        │  解析 / 校验 / 默认值合并
        ↓
VisionPipelineConfigSet
        │
        ↓
VisionProcessorFactory::RegisterConfiguredPipelines(...)
        │
        ├─ OpenCvContourVisionPipeline(OpenCvContourConfig)
        └─ YoloVisionPipeline(YoloConfig)
                ├─ OpenCvLetterbox(width, height)
                ├─ YoloOnnxAnalyzer(modelPath, confidence, labels)
                └─ YoloNms(confidence, nms)
```

### 职责划分

#### VisionPipelineConfig

结构化描述一条 pipeline：

```cpp
struct VisionPipelineConfig {
    QString id;
    QString type;
    QString displayName;
    bool enabled = true;
    QJsonObject params;
};
```

第一版不追求强类型配置树，顶层使用通用 `params`，在具体 pipeline 构造时读取自己关心的字段。等 OCR / 分割参数稳定后，再拆强类型结构。

#### VisionConfigLoader

职责：

- 定位配置文件。
- 读取 JSON。
- 校验基本字段。
- 合并内置默认配置。
- 输出 `VisionPipelineConfigSet`。
- 记录解析错误，不直接弹 UI。

不负责：

- 不创建 pipeline 对象。
- 不持有 `VisionPipelineManager`。
- 不处理 active pipeline。
- 不做模型文件下载。

#### VisionProcessorFactory

Phase 7 的工厂只固定注册内置 pipeline。Phase 8 增加按配置注册：

```cpp
class VisionProcessorFactory {
public:
    static void RegisterBuiltinPipelines(VisionPipelineManager& manager);
    static void RegisterConfiguredPipelines(
        VisionPipelineManager& manager,
        const VisionPipelineConfigSet& configSet);
};
```

第一版实现策略：

- 配置加载成功：按配置注册 enabled pipeline。
- 配置加载失败或没有任何 enabled pipeline：回退 `RegisterBuiltinPipelines`。
- 未识别 type：跳过并记录日志。

#### Pipeline 构造方式

现有 pipeline 需要增加配置构造入口，但不改变 `IVisionPipeline` 接口：

```cpp
struct YoloPipelineOptions {
    QString modelPath = "models/yolo.onnx";
    QString labelsPath;
    int inputWidth = 640;
    int inputHeight = 640;
    float confidenceThreshold = 0.35F;
    float nmsThreshold = 0.45F;
};

class YoloVisionPipeline : public IVisionPipeline {
public:
    explicit YoloVisionPipeline(YoloPipelineOptions options = {});
};
```

OpenCV 轮廓检测同理：

```cpp
struct OpenCvContourPipelineOptions {
    double threshold = 128.0;
    double maxValue = 255.0;
};
```

## 配置文件格式

建议文件路径：

```text
config/vision_pipelines.json
```

第一版示例：

```json
{
  "version": 1,
  "pipelines": [
    {
      "id": "opencv-contour",
      "type": "opencv-contour",
      "displayName": "OpenCV Contour Detection",
      "enabled": true,
      "params": {
        "threshold": 128,
        "maxValue": 255
      }
    },
    {
      "id": "yolo-onnx",
      "type": "yolo-onnx",
      "displayName": "YOLO ONNX Detection",
      "enabled": true,
      "params": {
        "modelPath": "models/yolo.onnx",
        "labelsPath": "models/coco.names",
        "inputWidth": 640,
        "inputHeight": 640,
        "confidenceThreshold": 0.35,
        "nmsThreshold": 0.45,
        "backend": "opencv-dnn",
        "target": "cpu"
      }
    }
  ]
}
```

### 字段约束

- `version`：配置格式版本，第一版固定为 `1`。
- `pipelines`：数组，按顺序注册到 `VisionPipelineManager`。
- `id`：唯一 pipeline id，对应 UI 和结果里的 `pipelineId`。
- `type`：factory 用于选择具体 C++ pipeline 类型。
- `displayName`：UI 下拉框展示名。
- `enabled`：为 `false` 时不注册该 pipeline。
- `params`：具体 pipeline 参数，由对应 pipeline 解析。

### 默认配置

即使没有 JSON 文件，程序也应使用等价默认配置：

```text
opencv-contour:
  threshold = 128
  maxValue = 255

yolo-onnx:
  modelPath = models/yolo.onnx
  inputWidth = 640
  inputHeight = 640
  confidenceThreshold = 0.35
  nmsThreshold = 0.45
  labelsPath = empty -> 使用内置 COCO labels
```

## 路径解析策略

第一版路径解析顺序：

1. 绝对路径：直接使用。
2. 相对路径：优先基于项目根目录 `TINY_LOG_PROJECT_ROOT`。
3. 如果没有项目根宏：基于应用运行目录。

模型文件不存在不应阻止程序启动。只有用户切换到该 pipeline 时，`Startup()` 加载失败并由现有错误链路提示。

## 错误处理策略

### 配置文件不存在

记录 debug 日志，使用内置默认配置。

### JSON 解析失败

记录 warn 日志，使用内置默认配置。

### pipeline 字段缺失

- 缺 `id` 或 `type`：跳过该条。
- 缺 `displayName`：使用 `id`。
- 缺 `enabled`：默认 `true`。
- 缺 `params`：使用对应 pipeline 默认参数。

### 参数越界

使用 clamp 或默认值：

- `inputWidth` / `inputHeight`：建议限制 `32 - 4096`。
- `confidenceThreshold` / `nmsThreshold`：限制 `0.0 - 1.0`。
- OpenCV threshold：限制 `0 - 255`。

## 与 UI 的关系

Phase 8 第一版不做复杂配置界面。UI 仍然只显示可用 pipeline 列表：

```text
Off
OpenCV Contour Detection
YOLO ONNX Detection
```

区别是列表来自配置注册结果，而不是完全由固定工厂决定。

后续如果要做 UI 配置，应该先在 application 层暴露修改接口，再决定是否写回 JSON，不能让 `MainWindow` 直接改 `VisionPipelineConfig` 内部结构。

## 与自训练模型的关系

自训练 YOLO 模型至少需要配置：

- `modelPath`
- `labelsPath`
- `inputWidth`
- `inputHeight`
- `confidenceThreshold`
- `nmsThreshold`

`YoloOnnxAnalyzer` 应支持从 `labelsPath` 读取类别名。如果文件不存在或行数不足，使用 `class:<id>` 回退，不应崩溃。

## 不做范围

- 不做配置热重载。
- 不做 UI 参数编辑器。
- 不做模型下载器。
- 不做多 active pipeline。
- 不做按 stream 配置不同 pipeline。
- 不做检测结果持久化。
- 不做 OCR / 分割具体算法实现。
- 不做 ONNX Runtime / TensorRT 后端接入。

## Phase 拆分

### Phase 8.1 配置格式与默认配置

新增 `VisionPipelineConfig` / `VisionConfigLoader`，读取 `config/vision_pipelines.json`，配置缺失时输出默认配置。

### Phase 8.2 Factory 按配置注册

扩展 `VisionProcessorFactory`，按 `VisionPipelineConfigSet` 注册 enabled pipeline，保留内置回退。

### Phase 8.3 YOLO 参数配置化

让 `YoloVisionPipeline` 接收 `YoloPipelineOptions`，配置模型路径、输入尺寸、confidence、NMS 和 labels 文件。

### Phase 8.4 OpenCV 参数配置化

让 `OpenCvContourVisionPipeline` 接收 `OpenCvContourPipelineOptions`，配置 threshold / maxValue。

### Phase 8.5 接入 MultiStreamManager 初始化链路

在视觉管理初始化时加载配置，并将配置传给 `VisionProcessorFactory`。

### Phase 8.6 验收与文档收尾

验证无配置、合法配置、非法配置、模型缺失、自训练 labels 等场景，形成 Phase 8 验收报告。

## 风险与沟通点

- 配置文件路径需要最终确认：第一版建议 `config/vision_pipelines.json`。
- 是否提交默认配置文件需要确认：提交示例配置更利于验收，但用户本地模型路径可能不同。
- 当前 CMake 会把 `doc/*.md` 纳入 source glob，新增文档不影响运行，但会出现在 IDE 工程里。
- 如果后续支持按 stream 配置 pipeline，需要重新设计 active stream 与配置绑定关系，不纳入 Phase 8 第一版。
