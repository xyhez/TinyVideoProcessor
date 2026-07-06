# TODO: Phase 8 视觉任务配置化

## 当前进度

- 已完成：
  - Phase 7 多视觉处理器架构验收通过。
  - OpenCV 轮廓检测管线可通过 UI 运行。
  - YOLO ONNX 检测管线可通过 UI 运行。
  - Phase 8 架构设计文档已完成。
  - 配置文件路径确认使用 `config/vision_pipelines.json`。
  - Phase 8.1 配置格式与默认配置已完成。
- 未完成：
  - Phase 8.2 Factory 按配置注册。
  - Phase 8.3 YOLO 参数配置化。
  - Phase 8.4 OpenCV 参数配置化。
  - Phase 8.5 初始化链路接入。
  - Phase 8.6 验收报告。
- 已知风险：
  - 自训练 YOLO 的 labels 文件格式需要保持简单：一行一个类别名。
  - 配置错误不能影响程序启动，只能影响对应 pipeline 激活。
- 本轮范围：
  - 新增 Phase 8.1 配置结构、JSON 加载器和默认配置文件。
  - 不接入 `VisionProcessorFactory` / `MultiStreamManager` 运行链路。
  - 不编译。

## 阶段目标

把 Phase 7 中写死在代码里的视觉任务参数迁移到配置层，为自训练 YOLO、OCR、分割和复合检测管线做准备。

第一版目标链路：

```text
config/vision_pipelines.json
    -> VisionConfigLoader
    -> VisionPipelineConfigSet
    -> VisionProcessorFactory
    -> VisionPipelineManager
    -> UI pipeline list
```

## 参考文档

- `doc/Phase8_视觉任务配置化设计.md`
- `doc/Phase7_多视觉处理器架构设计.md`
- `doc/Phase7_多视觉处理器架构验收报告.md`
- `doc/Phase7TODO.md`

## Phase 8.1 配置格式与默认配置

**目标**：新增视觉管线配置结构和加载器，支持读取 JSON 配置，并在配置缺失或解析失败时返回默认配置。

**预计修改文件**：

- `src/infrastructure/vision/visionpipelineconfig.h`
- `src/infrastructure/vision/visionpipelineconfig.cpp`
- `src/infrastructure/vision/visionconfigloader.h`
- `src/infrastructure/vision/visionconfigloader.cpp`
- `config/vision_pipelines.json`
- `doc/Phase8TODO.md`

**任务清单**：

- [x] 定义 `VisionPipelineConfig`。
- [x] 定义 `VisionPipelineConfigSet`。
- [x] 实现默认配置生成。
- [x] 实现 `VisionConfigLoader::Load(path)`。
- [x] 支持 JSON 解析失败时回退默认配置。
- [x] 支持缺字段时使用默认值或跳过非法 pipeline。
- [x] 支持相对路径解析策略说明。
- [x] 新增默认配置文件 `config/vision_pipelines.json`。

**验收标准**：

- [x] 无配置文件时能得到 OpenCV + YOLO 默认配置。
- [x] 合法 JSON 能解析为 `VisionPipelineConfigSet`。
- [x] 非法 JSON 不崩溃，并回退默认配置。
- [x] 缺少 `id` / `type` 的 pipeline 被跳过。

**测试流程**：

- [x] 静态检查：`git diff --check -- src\infrastructure\vision\visionpipelineconfig.h src\infrastructure\vision\visionpipelineconfig.cpp src\infrastructure\vision\visionconfigloader.h src\infrastructure\vision\visionconfigloader.cpp config\vision_pipelines.json doc\Phase8TODO.md`
- [x] 定向搜索：`rg -n "VisionPipelineConfig|VisionPipelineConfigSet|VisionConfigLoader|vision_pipelines.json|Default" src config doc\Phase8TODO.md`

**Commit message**：

```text
feat: 增加视觉管线配置加载基础结构

- 新增 VisionPipelineConfig / VisionPipelineConfigSet
- 新增 VisionConfigLoader 读取 JSON 配置
- 支持配置缺失或解析失败时回退默认配置
```

## Phase 8.2 Factory 按配置注册

**目标**：让 `VisionProcessorFactory` 可以根据配置注册 pipeline，同时保留 Phase 7 的内置回退路径。

**预计修改文件**：

- `src/infrastructure/vision/visionprocessorfactory.h`
- `src/infrastructure/vision/visionprocessorfactory.cpp`
- `doc/Phase8TODO.md`

**任务清单**：

- [ ] 新增 `RegisterConfiguredPipelines`。
- [ ] 按 `enabled` 过滤 pipeline。
- [ ] 按 `type` 创建对应 pipeline。
- [ ] 未识别 `type` 时跳过并记录日志。
- [ ] 没有任何有效 pipeline 时回退内置注册。

**验收标准**：

- [ ] 配置只启用 OpenCV 时，UI 只出现 OpenCV。
- [ ] 配置只启用 YOLO 时，UI 只出现 YOLO。
- [ ] 配置为空或全部非法时，仍有默认 OpenCV + YOLO。

**测试流程**：

- [ ] 静态检查：`git diff --check -- src\infrastructure\vision\visionprocessorfactory.h src\infrastructure\vision\visionprocessorfactory.cpp doc\Phase8TODO.md`
- [ ] 定向搜索：`rg -n "RegisterConfiguredPipelines|RegisterBuiltinPipelines|VisionPipelineConfig|enabled|type" src doc\Phase8TODO.md`

**Commit message**：

```text
feat: 支持按配置注册视觉管线

- 扩展 VisionProcessorFactory 支持配置驱动注册
- 按 enabled 和 type 创建内置 pipeline
- 保留无有效配置时的默认回退
```

## Phase 8.3 YOLO 参数配置化

**目标**：让 YOLO 模型路径、输入尺寸、confidence、NMS 和 labels 文件从配置读取。

**预计修改文件**：

- `src/infrastructure/vision/yolovisionpipeline.h`
- `src/infrastructure/vision/yolovisionpipeline.cpp`
- `src/infrastructure/vision/yoloonnxanalyzer.h`
- `src/infrastructure/vision/yoloonnxanalyzer.cpp`
- `src/infrastructure/vision/visionprocessorfactory.cpp`
- `doc/Phase8TODO.md`

**任务清单**：

- [ ] 新增 `YoloPipelineOptions`。
- [ ] `YoloVisionPipeline` 改为接收 options。
- [ ] 移除 YOLO 输入尺寸和阈值硬编码。
- [ ] `YoloOnnxAnalyzer` 支持从 labels 文件读取类别名。
- [ ] labels 文件缺失时回退 COCO 内置 labels。
- [ ] 参数越界时 clamp 或回退默认值。

**验收标准**：

- [ ] 默认配置下 YOLO 行为与 Phase 7 一致。
- [ ] 修改 `modelPath` 后使用新模型路径。
- [ ] 修改 `confidenceThreshold` 后检测框数量有可观察变化。
- [ ] 自定义 labels 文件能影响 overlay 标签显示。

**测试流程**：

- [ ] 静态检查：`git diff --check -- src\infrastructure\vision\yolovisionpipeline.h src\infrastructure\vision\yolovisionpipeline.cpp src\infrastructure\vision\yoloonnxanalyzer.h src\infrastructure\vision\yoloonnxanalyzer.cpp src\infrastructure\vision\visionprocessorfactory.cpp doc\Phase8TODO.md`
- [ ] 定向搜索：`rg -n "YoloPipelineOptions|labelsPath|confidenceThreshold|nmsThreshold|inputWidth|inputHeight|ReadLabels" src doc\Phase8TODO.md`
- [ ] 人工运行：使用默认 `models/yolo.onnx`，确认 YOLO 仍可绘制检测框。

**Commit message**：

```text
feat: 支持 YOLO 管线参数配置化

- 新增 YoloPipelineOptions
- 支持配置模型路径、输入尺寸、confidence 和 NMS 阈值
- 支持从 labels 文件读取类别名
```

## Phase 8.4 OpenCV 参数配置化

**目标**：让 OpenCV 轮廓检测阈值从配置读取。

**预计修改文件**：

- `src/infrastructure/vision/opencvcontourvisionpipeline.h`
- `src/infrastructure/vision/opencvcontourvisionpipeline.cpp`
- `src/infrastructure/vision/visionprocessorfactory.cpp`
- `doc/Phase8TODO.md`

**任务清单**：

- [ ] 新增 `OpenCvContourPipelineOptions`。
- [ ] `OpenCvContourVisionPipeline` 接收 options。
- [ ] `threshold` / `maxValue` 从配置读取。
- [ ] 参数越界时 clamp 到 `0 - 255`。

**验收标准**：

- [ ] 默认配置下 OpenCV 轮廓检测行为与 Phase 7 一致。
- [ ] 修改 threshold 后轮廓结果有可观察变化。

**测试流程**：

- [ ] 静态检查：`git diff --check -- src\infrastructure\vision\opencvcontourvisionpipeline.h src\infrastructure\vision\opencvcontourvisionpipeline.cpp src\infrastructure\vision\visionprocessorfactory.cpp doc\Phase8TODO.md`
- [ ] 定向搜索：`rg -n "OpenCvContourPipelineOptions|threshold|maxValue|OpenCvThreshold" src doc\Phase8TODO.md`
- [ ] 人工运行：切换到 OpenCV 轮廓检测，确认 overlay 正常。

**Commit message**：

```text
feat: 支持 OpenCV 轮廓检测参数配置化

- 新增 OpenCvContourPipelineOptions
- 支持配置 threshold 和 maxValue
- 保持默认行为与 Phase 7 一致
```

## Phase 8.5 初始化链路接入

**目标**：在视觉管理初始化时加载配置，并将配置传给 `VisionProcessorFactory`。

**预计修改文件**：

- `src/domain/MultiStreamManager.h`
- `src/domain/MultiStreamManager.cpp`
- `src/infrastructure/vision/visionconfigloader.h`
- `src/infrastructure/vision/visionconfigloader.cpp`
- `doc/Phase8TODO.md`

**任务清单**：

- [ ] 确认 `VisionPipelineManager` 初始化位置。
- [ ] 加载 `config/vision_pipelines.json`。
- [ ] 调用 `RegisterConfiguredPipelines`。
- [ ] 配置失败时回退默认注册。
- [ ] 日志记录配置路径和加载结果。

**验收标准**：

- [ ] 程序启动后 UI pipeline 列表来自配置。
- [ ] 删除配置文件后仍能看到默认 pipeline。
- [ ] 配置非法时程序可启动并回退默认 pipeline。

**测试流程**：

- [ ] 静态检查：`git diff --check -- src\domain\MultiStreamManager.h src\domain\MultiStreamManager.cpp src\infrastructure\vision\visionconfigloader.h src\infrastructure\vision\visionconfigloader.cpp doc\Phase8TODO.md`
- [ ] 定向搜索：`rg -n "VisionConfigLoader|vision_pipelines.json|RegisterConfiguredPipelines|RegisterBuiltinPipelines" src doc\Phase8TODO.md`
- [ ] 人工运行：有配置、无配置、非法配置三种场景启动程序。

**Commit message**：

```text
feat: 接入视觉管线配置初始化链路

- 启动时加载 vision_pipelines.json
- 使用配置注册视觉管线
- 配置缺失或非法时回退默认管线
```

## Phase 8.6 验收与文档收尾

**目标**：完成 Phase 8 验收记录，明确已验证场景和后续限制。

**预计修改文件**：

- `doc/Phase8TODO.md`
- `doc/Phase8_视觉任务配置化验收报告.md`

**任务清单**：

- [ ] 验证无配置文件场景。
- [ ] 验证合法配置场景。
- [ ] 验证非法 JSON 场景。
- [ ] 验证 YOLO 自定义 labels 场景。
- [ ] 验证 OpenCV threshold 配置场景。
- [ ] 编写 Phase 8 验收报告。

**验收标准**：

- [ ] 配置化不破坏 Phase 7 已验收链路。
- [ ] 默认配置下 OpenCV / YOLO 都可运行。
- [ ] 配置错误时程序不崩溃。
- [ ] 验收报告记录残留限制和 Phase 9 建议。

**测试流程**：

- [ ] 静态检查：`git diff --check -- doc\Phase8TODO.md doc\Phase8_视觉任务配置化验收报告.md`
- [ ] 定向搜索：`rg -n "Phase 8|配置|YOLO|OpenCV|验收" doc\Phase8TODO.md doc\Phase8_视觉任务配置化验收报告.md`

**Commit message**：

```text
docs: 完成 Phase 8 视觉任务配置化验收

- 新增 Phase 8 验收报告
- 更新 Phase8 TODO 状态
- 记录配置化验证场景和后续限制
```

## 不做范围

- 不做配置热重载。
- 不做 UI 参数编辑界面。
- 不做模型下载器。
- 不做多 pipeline 并列激活。
- 不做按 stream 绑定不同 pipeline。
- 不做 OCR / 分割算法实现。
- 不做 ONNX Runtime / TensorRT 接入。

## 风险与沟通点

- 默认配置文件已确认提交：`config/vision_pipelines.json`。
- 配置文件路径已确认固定为 `config/vision_pipelines.json`。
- 自训练 YOLO labels 文件格式第一版建议一行一个类别名。
- Phase 8 仍是同步推理，不解决 YOLO 卡顿问题；性能优化应放到后续 Phase。
