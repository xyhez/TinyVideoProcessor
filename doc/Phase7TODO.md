# TODO: Phase 7 多视觉处理器架构

## 当前进度

- 已完成：
  - Phase 5 图像处理基础链路已完成。
  - Phase 6 高级图像分析预研已完成，第一版目标检测处理器已作为 `IImageProcessor` 存在。
  - `src/` 目录按职责重构完成，`infrastructure/` 拆分为 `decoding/audio/sync/rendering/vision/devices/` 六个子目录。
  - 三个类名（`demuxer` / `decoder` / `syncmanager`）统一为 PascalCase。
  - Phase 7.1 架构设计文档已完成，明确"多条独立识别管线 + 单激活 + 懒加载"三条原则。
- 未完成：
  - Phase 7.2 VisionFrame / VisionResultSet 类型定义。
  - Phase 7.3 IVisionPipeline + 三小接口 + VisionPipelineManager。
  - Phase 7.4 VisionOverlayRenderer。
  - Phase 7.5 内置 OpenCvContourVisionPipeline + VisionProcessorFactory。
  - Phase 7.6 VisionProcessingFrameSink 接入现有链路 + UI 最小接入。
  - Phase 7.7 YoloVisionPipeline 端到端接入验证。
  - Phase 7 验收报告。
- 已知风险：
  - YOLO / OCR 单帧推理耗时可能明显高于视频帧间隔，第一版仅同步处理 active stream。
  - `IVisionPipeline::Startup()` 可能耗时几秒（加载模型），第一版同步执行可能阻塞 UI，需要 loading 遮罩。
  - 单激活模式下复合识别场景需要打包为一条 pipeline，不允许并列激活多条。
  - OpenCV 默认 BGR，部分模型需要 RGB，色彩转换必须在 pipeline 的 preprocessor 里显式声明。
  - Pipeline 切换时旧 pipeline `Shutdown()` 与正在运行的 `Run()` 存在并发窗口，需要同步保护。
- 本轮范围：
  - 新增 Phase 7 架构设计文档 `doc/Phase7_多视觉处理器架构设计.md`。
  - 新增 Phase 7 TODO 文档 `doc/Phase7TODO.md`。
  - 明确"识别算法 = 一条 `IVisionPipeline`"、"单激活"、"懒加载"、"不做外部动态库插件"四条基本原则。

## 阶段目标

在 Phase 5 图像处理基础 + Phase 6 传统检测基础上，建立可扩展的**视觉分析管线**抽象。让 OpenCV 传统视觉、OCR、YOLO/ONNX、深度学习分割等能力可以按统一方式接入、启停和切换。

第一版目标链路：

```text
MainWindow
    -> MediaPlayController
        -> MultiStreamManager
            ├─ VisionPipelineManager (成员,单激活)
            │      ├─ [dormant]  YoloVisionPipeline
            │      ├─ [dormant]  OcrVisionPipeline
            │      └─ [active]   OpenCvContourVisionPipeline
            └─ MediaPipeline (每路一个)
                    -> VideoFrameScheduler
                        -> ImageProcessingFrameSink (Phase 5,保留)
                            -> VisionProcessingFrameSink (Phase 7,新增)
                                    -> VisionPipelineManager.RunActive()
                                    -> VisionOverlayRenderer
                                -> SingleStreamFrameSinkAdapter
                                    -> MultiViewRenderer
```

## 架构边界

- 不让 `MainWindow` 直接持有 `IVisionPipeline` 或访问 `VisionFrame` / `VisionResultSet`。
- 不让 `VideoFrameScheduler` 感知视觉分析。
- 不让 `MultiViewRenderer` 执行推理或绘制识别结果。
- 不让 `VisionOverlayRenderer` 反向调用 pipeline。
- 不让 `IVisionPipeline` 访问 `AVFrame`、`sink`、`renderer`、`decoder`、UI。
- 不让 `VisionPipelineManager` 做单例，必须由 `MultiStreamManager` 通过 `std::unique_ptr` 持有。
- 不让 `IVisionAnalyzer` / `IVisionPostprocessor` 修改 `VisionFrame`（只有 `IFramePreprocessor` 可以）。
- 不让不同 pipeline 共享同一份 preprocessed frame，每条 pipeline `Run` 内部必须 `clone()` 出自己的 working frame。
- 不做外部 DLL 动态库插件。
- 不做多激活并列 pipeline，第一版严格单激活。
- 第一版只处理 active stream，其他路 sink `isActive=false` 直接透传。

## 参考文档

- `doc/Phase7_多视觉处理器架构设计.md`
- `doc/Phase5TODO.md`
- `doc/Phase6TODO.md`
- `doc/架构指导.md`

## Phase 7.1 架构设计与任务拆分

**目标**：明确 Phase 7 的架构方向、核心抽象、集成方式和任务拆分。

**预计修改文件**：

- `doc/Phase7_多视觉处理器架构设计.md`
- `doc/Phase7TODO.md`

**任务清单**：

- [x] 明确"识别算法 = 一条 `IVisionPipeline`"的划分粒度。
- [x] 明确单激活模式，同时只允许一条 pipeline 运行。
- [x] 明确懒加载生命周期：注册时轻构造，激活时 `Startup()`，切走时 `Shutdown()`。
- [x] 明确 `VisionPipelineManager` 挂在 `MultiStreamManager` 里，非单例。
- [x] 明确 `VisionProcessingFrameSink` 每路一个，只 active stream 那个 `isActive=true`。
- [x] 明确 OpenCV 的三重角色：独立管线 / 其他管线的 preprocessor / 其他管线的 postprocessor（类库复用）。
- [x] 明确第一版不做外部动态库插件，只做内置工厂注册。
- [x] 明确第一版不做异步推理、降帧分析、多激活、复合场景 UI。
- [x] 拆分后续可执行的 7.2 - 7.7 子任务。

**验收标准**：

- [x] 能说明"多管线独立 + 单激活 + 懒加载"三条原则的取舍。
- [x] 能说明为什么 `VisionPipelineManager` 不做单例。
- [x] 能说明为什么 `IVisionAnalyzer` 不能修改 `VisionFrame`。
- [x] 能说明为什么复合场景（YOLO+OCR）打包成一条 pipeline 而不是并列激活。
- [x] 能说明每条 pipeline `Run` 为什么要 `clone()` 出 working frame。
- [x] 能说明 `VisionProcessingFrameSink` 与 Phase 5 的 `ImageProcessingFrameSink` 的职责差异。

**测试流程**：

- [x] 静态检查：`git diff --check -- doc\Phase7_多视觉处理器架构设计.md doc\Phase7TODO.md`
- [x] 定向搜索：`rg -n "IVisionPipeline|VisionPipelineManager|VisionProcessingFrameSink|VisionOverlayRenderer|VisionFrame|VisionResultSet|单激活|懒加载" doc\Phase7_多视觉处理器架构设计.md doc\Phase7TODO.md`

**Commit message**：

```text
docs: 增加 Phase 7 多视觉处理器架构规划

- 新增 Phase 7 架构设计文档
- 新增 Phase 7 TODO
- 明确"识别算法 = 一条 IVisionPipeline"的划分粒度
- 明确单激活模式和懒加载生命周期
- 明确 VisionPipelineManager 由 MultiStreamManager 持有,非单例
- 明确 OpenCV 以类库复用形式贯穿三种角色
- 明确第一版不做外部动态库插件
```

## Phase 7.2 VisionFrame / VisionResultSet 类型定义

**目标**：新增视觉分析所需的统一数据类型，作为纯 header 定义，不接入 UI 和 sink 链。

**预计修改文件**：

- `src/infrastructure/vision/visiontypes.h`
- `doc/Phase7TODO.md`

**任务清单**：

- [ ] 新增 `VisionPixelFormat` enum（`BGR24` / `RGB24` / `GRAY8`）。
- [ ] 新增 `VisionFrame` struct（`width` / `height` / `stride` / `pts` / `pixelFormat` / `image`）。
- [ ] 新增 `VisionResultType` enum（`DetectionBox` / `TextRegion` / `Classification` / `Measurement`）。
- [ ] 新增 `DetectionBox` struct（`box` / `label` / `confidence` / `classId`）。
- [ ] 新增 `TextRegion` struct（`box` / `text` / `confidence`）。
- [ ] 新增 `VisionResult` struct（`pipelineId` / `type` / `boxes` / `texts` / `elapsedMs` / `errorMessage`）。
- [ ] 新增 `VisionResultSet` struct（`pts` / `results`）。
- [ ] 给所有成员变量添加 `///<` 注释。
- [ ] 只引用 `<QtCore>` 和 `<opencv2/core/mat.hpp>`，不引用 FFmpeg。
- [ ] 不引用 UI 头文件。
- [ ] `CMakeLists.txt` 当前通过 `file(GLOB_RECURSE src/*.cpp src/*.h ...)` 自动纳入新增文件，本步无需修改。

**验收标准**：

- [ ] 类型不依赖 UI。
- [ ] 类型不依赖 FFmpeg。
- [ ] `VisionFrame` 显式声明像素格式和元数据。
- [ ] `VisionResultSet` 支持检测框和 OCR 文本。
- [ ] 编译能过（用一个空 cpp include 该 header 通过编译）。

**测试流程**：

- [ ] 静态检查：`git diff --check -- src\infrastructure\vision\visiontypes.h doc\Phase7TODO.md`
- [ ] 新文件空白检查：`git diff --check --no-index -- /dev/null src\infrastructure\vision\visiontypes.h`
- [ ] 定向搜索：`rg -n "VisionFrame|VisionResultSet|VisionResult|DetectionBox|TextRegion|VisionPixelFormat|VisionResultType" src doc\Phase7TODO.md`
- [ ] 用户人工编译验证：项目能编过且无未使用类型的警告。

**Commit message**：

```text
feat: 新增视觉分析统一数据类型

- 新增 VisionFrame 描述算法输入(像素格式/尺寸/stride/pts/Mat)
- 新增 VisionResult / VisionResultSet 描述结构化检测结果
- 新增 DetectionBox / TextRegion 支持目标检测和 OCR 结果
- 类型不依赖 UI 和 FFmpeg,只依赖 Qt Core 和 OpenCV Mat
- 为 Phase 7.3 IVisionPipeline 接口定义准备类型基础
```

## Phase 7.3 IVisionPipeline + 三小接口 + VisionPipelineManager

**目标**：新增视觉分析的顶层接口和管理器，只搭空壳，不接入 sink 链和 UI。

**预计修改文件**：

- `src/infrastructure/vision/ivisionpipeline.h`
- `src/infrastructure/vision/iframepreprocessor.h`
- `src/infrastructure/vision/ivisionanalyzer.h`
- `src/infrastructure/vision/ivisionpostprocessor.h`
- `src/infrastructure/vision/visionpipelinemanager.h`
- `src/infrastructure/vision/visionpipelinemanager.cpp`
- `doc/Phase7TODO.md`

**任务清单**：

- [ ] 新增 `IVisionPipeline` 接口（`Id`/`DisplayName`/`Type`/`Startup`/`Shutdown`/`IsStarted`/`Run`）。
- [ ] 新增 `IFramePreprocessor` 接口（`Name`/`Process(VisionFrame&)`）。
- [ ] 新增 `IVisionAnalyzer` 接口（`Name`/`Analyze(const VisionFrame&, VisionResultSet&)`）。
- [ ] 新增 `IVisionPostprocessor` 接口（`Name`/`Process(const VisionFrame&, VisionResultSet&)`）。
- [ ] 新增 `VisionPipelineManager` 类。
- [ ] 支持 `RegisterPipeline(std::unique_ptr<IVisionPipeline>)`。
- [ ] 支持 `AvailablePipelineIds()`。
- [ ] 支持 `FindPipeline(const QString&)`。
- [ ] 支持 `SetActivePipeline(const QString&)`（自动 Shutdown 旧的、Startup 新的）。
- [ ] 支持 `ActivePipelineId()` / `ActivePipeline()`。
- [ ] 支持 `RunActive(const VisionFrame&, VisionResultSet&)`。
- [ ] Manager 析构时遍历所有 pipeline，`IsStarted` 的调 `Shutdown()`。
- [ ] 定义信号 `ActivePipelineChanged(QString)`。
- [ ] 定义信号 `PipelineStartupFailed(QString, QString)`。
- [ ] 空 id 表示关闭视觉分析。
- [ ] 不接入 `MediaPipeline` / `MultiStreamManager`。
- [ ] 不接入 UI。
- [ ] `CMakeLists.txt` 当前通过 `file(GLOB_RECURSE src/*.cpp src/*.h ...)` 自动纳入新增文件，本步无需修改。

**验收标准**：

- [ ] 接口不依赖 UI。
- [ ] 接口不依赖 `AVFrame` / FFmpeg。
- [ ] `IVisionAnalyzer::Analyze` 的 `VisionFrame` 参数是 `const`。
- [ ] `IVisionPostprocessor::Process` 的 `VisionFrame` 参数是 `const`。
- [ ] Manager 可以注册空 pipeline 并切换。
- [ ] `SetActivePipeline("")` 能安全关闭当前 active。
- [ ] `SetActivePipeline(不存在的id)` 返回 false，不影响当前状态。
- [ ] Startup 失败时 Manager 保持无 active 状态并发信号。
- [ ] 不影响现有播放。

**测试流程**：

- [ ] 静态检查：`git diff --check -- src\infrastructure\vision\ivisionpipeline.h src\infrastructure\vision\iframepreprocessor.h src\infrastructure\vision\ivisionanalyzer.h src\infrastructure\vision\ivisionpostprocessor.h src\infrastructure\vision\visionpipelinemanager.h src\infrastructure\vision\visionpipelinemanager.cpp doc\Phase7TODO.md`
- [ ] 定向搜索：`rg -n "IVisionPipeline|IFramePreprocessor|IVisionAnalyzer|IVisionPostprocessor|VisionPipelineManager|Startup|Shutdown|IsStarted|RunActive|SetActivePipeline" src doc\Phase7TODO.md`
- [ ] 用户人工编译验证：项目能编过。

**Commit message**：

```text
feat: 新增视觉分析顶层接口与管线管理器

- 新增 IVisionPipeline 作为一种识别算法的完整闭包接口
- 新增 IFramePreprocessor / IVisionAnalyzer / IVisionPostprocessor 三个构建块接口
- 新增 VisionPipelineManager,支持注册和单激活切换
- 支持 Startup/Shutdown 懒加载生命周期
- 支持 ActivePipelineChanged 和 PipelineStartupFailed 信号
- 暂不接入 MediaPipeline 和 UI
```

## Phase 7.4 VisionOverlayRenderer

**目标**：新增统一识别结果绘制器，把 `VisionResultSet` 画到 `cv::Mat` 上，与具体识别算法解耦。

**预计修改文件**：

- `src/infrastructure/vision/visionoverlayrenderer.h`
- `src/infrastructure/vision/visionoverlayrenderer.cpp`
- `doc/Phase7TODO.md`

**任务清单**：

- [ ] 新增 `VisionOverlayRenderer` 类。
- [ ] 支持 `Draw(cv::Mat&, const VisionResultSet&)`。
- [ ] 绘制 `DetectionBox`（矩形框 + 标签 + 置信度）。
- [ ] 绘制 `TextRegion`（矩形框 + OCR 文本）。
- [ ] 绘制分析耗时（在画面角落）。
- [ ] 空 `VisionResultSet` 时不修改图像。
- [ ] 不访问 pipeline / Manager / UI。
- [ ] 不执行识别推理。
- [ ] 只使用 OpenCV 绘制 API，不依赖 Qt Widgets。
- [ ] `CMakeLists.txt` 当前通过 `file(GLOB_RECURSE src/*.cpp src/*.h ...)` 自动纳入新增文件，本步无需修改。

**验收标准**：

- [ ] 绘制逻辑不在具体 pipeline 里。
- [ ] 输入空结果时图像不被修改。
- [ ] 可以在同一张 `cv::Mat` 上叠加多个 `VisionResult`。
- [ ] 中文标签能正常显示或明确降级为英文。

**测试流程**：

- [ ] 静态检查：`git diff --check -- src\infrastructure\vision\visionoverlayrenderer.h src\infrastructure\vision\visionoverlayrenderer.cpp doc\Phase7TODO.md`
- [ ] 定向搜索：`rg -n "VisionOverlayRenderer|Draw|DetectionBox|TextRegion|cv::rectangle|cv::putText" src doc\Phase7TODO.md`
- [ ] 用户人工编译验证：项目能编过。
- [ ] 单元测试建议：给定一个手工构造的 `VisionResultSet`，`Draw` 后 `cv::Mat` 像素被修改。

**Commit message**：

```text
feat: 新增视觉结果统一绘制器

- 新增 VisionOverlayRenderer 支持在 cv::Mat 上绘制识别结果
- 支持 DetectionBox 目标框和标签绘制
- 支持 TextRegion OCR 结果绘制
- 支持在画面角落显示分析耗时
- 绘制逻辑与具体识别算法解耦,不进入 pipeline 内部
```

## Phase 7.5 内置 OpenCvContourVisionPipeline + VisionProcessorFactory

**目标**：实现第一条实际能用的识别管线（`OpenCvContourVisionPipeline`），并提供内置工厂在启动时注册所有内置 pipeline 到 Manager。

**预计修改文件**：

- `src/infrastructure/vision/opencvpreprocessors.h`
- `src/infrastructure/vision/opencvpreprocessors.cpp`
- `src/infrastructure/vision/opencvpostprocessors.h`
- `src/infrastructure/vision/opencvpostprocessors.cpp`
- `src/infrastructure/vision/opencvcontouranalyzer.h`
- `src/infrastructure/vision/opencvcontouranalyzer.cpp`
- `src/infrastructure/vision/opencvcontourvisionpipeline.h`
- `src/infrastructure/vision/opencvcontourvisionpipeline.cpp`
- `src/infrastructure/vision/visionprocessorfactory.h`
- `src/infrastructure/vision/visionprocessorfactory.cpp`
- `doc/Phase7TODO.md`

**任务清单**：

- [ ] 新增 `OpenCvThreshold`（`IFramePreprocessor`，参数化阈值）。
- [ ] 新增 `OpenCvGrayscale`（`IFramePreprocessor`，BGR → GRAY）。
- [ ] 新增 `OpenCvMeasurement`（`IVisionPostprocessor`，为 `DetectionBox` 补面积/周长信息）。
- [ ] 新增 `OpenCvContourAnalyzer`（`IVisionAnalyzer`，`cv::findContours` + `boundingRect` 输出 `DetectionBox`）。
- [ ] 新增 `OpenCvContourVisionPipeline` 类，实现 `IVisionPipeline`。
- [ ] `Startup()` 中 new preprocessors / analyzer / postprocessors。
- [ ] `Shutdown()` 中 reset 所有构建块。
- [ ] `Run()` 中 clone working frame，按顺序调 preprocessors、analyzer、postprocessors。
- [ ] `Id()` 返回 `"opencv-contour"`。
- [ ] `DisplayName()` 返回 `"OpenCV 轮廓检测"`。
- [ ] `Type()` 返回 `"detection"`。
- [ ] 新增 `VisionProcessorFactory`。
- [ ] 提供 `RegisterBuiltinPipelines(VisionPipelineManager&)` 静态方法，注册所有内置 pipeline。
- [ ] 处理失败时返回 false，不抛异常。
- [ ] 不访问 UI、`AVFrame`、sink。
- [ ] `CMakeLists.txt` 当前通过 `file(GLOB_RECURSE src/*.cpp src/*.h ...)` 自动纳入新增文件，本步无需修改。

**验收标准**：

- [ ] 手工构造 BGR 图像调用 `Run()` 能得到非空 `VisionResultSet`。
- [ ] `Shutdown()` 后 `IsStarted()` 返回 false。
- [ ] 未 `Startup()` 时 `Run()` 返回 false。
- [ ] preprocessors / analyzer / postprocessors 组合��责单一，可以被其他 pipeline 复用（如 `OpenCvGrayscale` 未来给 OCR 用）。
- [ ] Factory 注册的 pipeline 都是 dormant 状态，未 `Startup()`。

**测试流程**：

- [ ] 静态检查：`git diff --check -- src\infrastructure\vision\opencvpreprocessors.h src\infrastructure\vision\opencvpreprocessors.cpp src\infrastructure\vision\opencvpostprocessors.h src\infrastructure\vision\opencvpostprocessors.cpp src\infrastructure\vision\opencvcontouranalyzer.h src\infrastructure\vision\opencvcontouranalyzer.cpp src\infrastructure\vision\opencvcontourvisionpipeline.h src\infrastructure\vision\opencvcontourvisionpipeline.cpp src\infrastructure\vision\visionprocessorfactory.h src\infrastructure\vision\visionprocessorfactory.cpp doc\Phase7TODO.md`
- [ ] 定向搜索：`rg -n "OpenCvContourVisionPipeline|OpenCvContourAnalyzer|OpenCvThreshold|OpenCvGrayscale|OpenCvMeasurement|VisionProcessorFactory|RegisterBuiltinPipelines|findContours|boundingRect" src doc\Phase7TODO.md`
- [ ] 用户人工编译验证：项目能编过。

**Commit message**：

```text
feat: 新增 OpenCV 轮廓检测识别管线与内置工厂

- 新增 OpenCvContourVisionPipeline 完整实现 IVisionPipeline
- 内部组合 OpenCvThreshold + OpenCvContourAnalyzer + OpenCvMeasurement
- 拆分为独立的 preprocessor / analyzer / postprocessor 类,便于其他管线复用
- 新增 VisionProcessorFactory 提供 RegisterBuiltinPipelines 统一注册入口
- 尚未接入 MediaPipeline sink 链和 UI
```

## Phase 7.6 VisionProcessingFrameSink 接入现有链路

**目标**：把视觉分析层挂到现有 sink 链，让 UI 能选择识别模式并看到检测结果画到画面上。

**预计修改文件**：

- `src/infrastructure/vision/visionprocessingframesink.h`
- `src/infrastructure/vision/visionprocessingframesink.cpp`
- `src/domain/MediaPipeline.h`
- `src/domain/MediaPipeline.cpp`
- `src/domain/MultiStreamManager.h`
- `src/domain/MultiStreamManager.cpp`
- `src/application/MediaPlayController.h`
- `src/application/MediaPlayController.cpp`
- `src/ui/mainwindow.h`
- `src/ui/mainwindow.cpp`
- `src/ui/mainwindow.ui`
- `doc/Phase7TODO.md`

**任务清单**：

- [ ] 新增 `VisionProcessingFrameSink`，实现 `IVideoFrameSink`。
- [ ] 构造时传入 `VisionPipelineManager*` / `downstream sink` / `StreamId`。
- [ ] 支持 `SetActive(bool)` 拨 `isActive_` flag。
- [ ] 支持 `SetOverlayEnabled(bool)`。
- [ ] `PresentFrame`：`isActive=false` 或无 active pipeline 时直接透传原帧。
- [ ] `PresentFrame`：`isActive=true` 且有 active pipeline 时，`AVFrame → VisionFrame → manager.RunActive → overlay.Draw → VisionFrame → AVFrame` → 转发下游。
- [ ] 处理失败时（Run 返回 false 或转换失败）转发原帧，记录低频日志。
- [ ] `MediaPipeline` 内部创建 `VisionProcessingFrameSink`，插入到 `ImageProcessingFrameSink` 之后。
- [ ] `MediaPipeline` 构造时从上层拿 `VisionPipelineManager*`。
- [ ] `MultiStreamManager` 持有 `std::unique_ptr<VisionPipelineManager>` 成员。
- [ ] `MultiStreamManager` 构造时调 `VisionProcessorFactory::RegisterBuiltinPipelines` 注册内置 pipeline。
- [ ] `MultiStreamManager` 切换 active stream 时更新各 sink 的 `SetActive`。
- [ ] `MediaPlayController` 暴露访问 `VisionPipelineManager` 的接口给 UI。
- [ ] UI 新增"识别模式"下拉框，根据 `AvailablePipelineIds` 动态填充。
- [ ] UI 下拉框第一项为"关闭"，选中则 `SetActivePipeline("")`。
- [ ] UI 监听 `ActivePipelineChanged` / `PipelineStartupFailed` 信号，切换时显示 loading 提示，失败时提示错误。
- [ ] UI 保留 Overlay 开关。
- [ ] `CMakeLists.txt` 当前通过 `file(GLOB_RECURSE src/*.cpp src/*.h ...)` 自动纳入新增文件，本步无需修改。

**验收标准**：

- [ ] 默认视觉分析关闭时播放行为完全不变。
- [ ] 选中 OpenCV 轮廓检测后 active stream 画面上出现检测框。
- [ ] 切换 active stream 后视觉分析跟随，旧 active 恢复原图。
- [ ] 切换识别模式时旧 pipeline `Shutdown()`，新 pipeline `Startup()`。
- [ ] 单帧处理失败时转发原帧，不阻断播放。
- [ ] seek / stop / remove stream / close 不崩溃。
- [ ] UI 不直接持有 `IVisionPipeline` 或 `VisionFrame`。

**测试流程**：

- [ ] 静态检查：`git diff --check -- src\infrastructure\vision\visionprocessingframesink.h src\infrastructure\vision\visionprocessingframesink.cpp src\domain\MediaPipeline.h src\domain\MediaPipeline.cpp src\domain\MultiStreamManager.h src\domain\MultiStreamManager.cpp src\application\MediaPlayController.h src\application\MediaPlayController.cpp src\ui\mainwindow.h src\ui\mainwindow.cpp src\ui\mainwindow.ui doc\Phase7TODO.md`
- [ ] 定向搜索：`rg -n "VisionProcessingFrameSink|VisionPipelineManager|SetActivePipeline|SetActive|comboBox_visionPipeline|识别模式" src doc\Phase7TODO.md`
- [ ] 人工运行建议：普通 MP4 播放，切换到 OpenCV 轮廓检测，观察检测框。
- [ ] 人工运行建议：多路播放，切换 active stream，确认识别只作用于当前 active。
- [ ] 人工运行建议：切换识别模式 → 关闭 → 再切换 → 关闭，确认无崩溃、无内存泄漏。
- [ ] 人工运行建议：seek、stop、remove stream、关闭窗口全流程验证。

**Commit message**：

```text
feat: 接入视觉分析帧 sink 到播放链路

- 新增 VisionProcessingFrameSink 作为 IVideoFrameSink 装饰器
- MediaPipeline 在 ImageProcessingFrameSink 之后插入 VisionProcessingFrameSink
- MultiStreamManager 持有 VisionPipelineManager 并注册内置管线
- 切换 active stream 时联动各 sink 的 isActive 状态
- UI 增加识别模式下拉框和 Overlay 开关
- 保持 UI 不直接访问 IVisionPipeline 和 VisionFrame
```

## Phase 7.7 YoloVisionPipeline 端到端接入验证

**目标**：实现一条完整的 `YoloVisionPipeline`，验证从 UI 切换 → Startup 加载模型 → 逐帧检测 → 结果绘制的完整链路。

**预计修改文件**：

- `src/infrastructure/vision/opencvpreprocessors.h`（新增 `OpenCvLetterbox` / `OpenCvBgrToRgb`）
- `src/infrastructure/vision/opencvpreprocessors.cpp`
- `src/infrastructure/vision/opencvpostprocessors.h`（新增 `YoloNms` / `CoordinateMapper`）
- `src/infrastructure/vision/opencvpostprocessors.cpp`
- `src/infrastructure/vision/yoloonnxanalyzer.h`
- `src/infrastructure/vision/yoloonnxanalyzer.cpp`
- `src/infrastructure/vision/yolovisionpipeline.h`
- `src/infrastructure/vision/yolovisionpipeline.cpp`
- `src/infrastructure/vision/visionprocessorfactory.cpp`（新增注册）
- `doc/Phase7TODO.md`
- `doc/Phase7_多视觉处理器架构验收报告.md`

**任务清单**：

- [ ] 确定使用 OpenCV DNN 或 ONNX Runtime（第一版建议 OpenCV DNN，减少依赖）。
- [ ] 准备 YOLO 模型文件（放在 `models/` 目录，`.gitignore` 排除大文件）。
- [ ] 新增 `OpenCvLetterbox` preprocessor（保持宽高比缩放到 640x640）。
- [ ] 新增 `OpenCvBgrToRgb` preprocessor。
- [ ] 新增 `YoloOnnxAnalyzer`（读模型 / 推理 / 输出原始检测框）。
- [ ] 新增 `YoloNms` postprocessor（NMS 过滤重叠框）。
- [ ] 新增 `CoordinateMapper` postprocessor（把模型输入坐标映射回原图尺寸）。
- [ ] 新增 `YoloVisionPipeline`，实现 `IVisionPipeline`。
- [ ] `Id() = "yolo-onnx"`，`DisplayName() = "YOLO 目标检测"`，`Type() = "detection"`。
- [ ] `Startup()` 加载模型，失败返回 false。
- [ ] `Shutdown()` 释放模型和构建块。
- [ ] `Run()` 完整调 preprocessors → analyzer → postprocessors。
- [ ] 在 `VisionProcessorFactory::RegisterBuiltinPipelines` 里加一行注册。
- [ ] 模型路径可配置（第一版硬编码到 `models/yolov5s.onnx`，后续可改）。
- [ ] 推理失败时 `Run()` 返回 false，不崩溃。
- [ ] 输出 `VisionResult::pipelineId = "yolo-onnx"`。
- [ ] 每帧记录 `elapsedMs`。
- [ ] 形成 Phase 7 验收报告。

**验收标准**：

- [ ] UI 下拉框能看到 "YOLO 目标检测" 选项。
- [ ] 选中后 Startup 触发模型加载（可能耗时 1-3 秒，UI 显示 loading）。
- [ ] Startup 成功后 active stream 画面上出现 YOLO 检测框和类别标签。
- [ ] 模型加载失败时 UI 提示错误，回到无 active 状态。
- [ ] 检测框坐标能正确映射回原始视频分辨率（不是 640x640）。
- [ ] 切换回 OpenCV 轮廓检测时 YOLO 正确 `Shutdown` 释放显存/内存。
- [ ] 切换到"关闭"时视觉分析停止，画面恢复原图。
- [ ] 单帧推理失败不阻断播放。
- [ ] 检测结果通过 `VisionResultSet` 暴露（未来可用于统计、报警、存储）。

**测试流程**：

- [ ] 静态检查：`git diff --check -- src\infrastructure\vision\opencvpreprocessors.h src\infrastructure\vision\opencvpreprocessors.cpp src\infrastructure\vision\opencvpostprocessors.h src\infrastructure\vision\opencvpostprocessors.cpp src\infrastructure\vision\yoloonnxanalyzer.h src\infrastructure\vision\yoloonnxanalyzer.cpp src\infrastructure\vision\yolovisionpipeline.h src\infrastructure\vision\yolovisionpipeline.cpp src\infrastructure\vision\visionprocessorfactory.cpp doc\Phase7TODO.md doc\Phase7_多视觉处理器架构验收报告.md`
- [ ] 定向搜索：`rg -n "YoloVisionPipeline|YoloOnnxAnalyzer|YoloNms|OpenCvLetterbox|OpenCvBgrToRgb|CoordinateMapper|cv::dnn|readNet|forward" src doc\Phase7TODO.md`
- [ ] 人工运行建议：普通 MP4 播放，切换到 YOLO 目标检测，观察检测框和标签。
- [ ] 人工运行建议：反复切换 OpenCV / YOLO / 关闭，确认无崩溃、无泄漏。
- [ ] 人工运行建议：切换 active stream 后 YOLO 检测跟随。
- [ ] 人工运行建议：模型文件损坏或缺失时 UI 提示错误。
- [ ] 人工运行建议：seek / stop / remove stream / close 全流程。

**Commit message**：

```text
feat: 接入 YOLO 识别管线并完成 Phase 7 端到端验证

- 新增 YoloVisionPipeline 完整实现 IVisionPipeline
- 内部组合 OpenCvLetterbox + OpenCvBgrToRgb + YoloOnnxAnalyzer
  + YoloNms + CoordinateMapper
- 支持 Startup 时加载模型、Shutdown 时释放
- 检测框坐标从模型输入尺寸映射回原始视频分辨率
- 在 VisionProcessorFactory 中注册为内置管线
- 完成 Phase 7 端到端验收(UI 切换 -> 加载 -> 检测 -> 绘制)
```

## 不做范围

- 不做多激活并列 pipeline（第一版严格单激活）。
- 不做多路同时视觉分析（第一版只 active stream，其他路 sink 透传）。
- 不做外部 DLL 动态库插件（不搞 C ABI / plugin.json / 插件目录）。
- 不做异步推理队列（第一版同步）。
- 不做降帧分析（第一版每帧都走 pipeline）。
- 不做插件热更新。
- 不做单条 pipeline 内部参数的复杂配置界面（默认参数为主）。
- 不做检测结果表格和历史查询。
- 不做检测结果持久化（不写 SQLite）。
- 不做统计报表和数据导出。
- 不做检测事件通道（例如"检测到目标就发信号"）。
- 不做目标跟踪 ID。
- 不做 ROI 检测区域。
- 不做像素标定和真实尺寸测量。
- 不做 GPU 图像处理专用优化。
- 不做多语言 UI（识别模式名先中文）。

## 后续演进

- 降帧分析：显示帧率 30fps，推理帧率 5-10fps，复用最近一次结果。
- 异步推理：pipeline `Run` 放到独立 worker thread，主链路只读最新结果。
- 多路独立视觉分析：每路 `VisionProcessingFrameSink` 独立控制 pipeline。
- 结果事件通道：`VisionPipelineManager` 发出 `ResultReceived` 信号供 UI 或存储订阅。
- 检测结果持久化：SQLite 记录检测事件，支持时间段查询。
- 统计报表和导出。
- OCR 识别管线（`OcrVisionPipeline`）。
- 深度学习分割管线（`SegmentationVisionPipeline`）。
- 复合场景 pipeline（如 `TrafficAnalysisVisionPipeline` 内部串行调用 YOLO + OCR）。
- ROI 区域筛选。
- 目标跟踪 ID（跨帧跟踪）。
- 像素标定和真实尺寸测量。
- Pipeline 内部参数的高级配置界面。

## 风险与沟通点

- YOLO 模型选型（v5 / v8 / 其他）需要用户拍板，第一版建议 YOLOv5s 或 YOLOv8n（小模型加载快）。
- YOLO 模型文件较大（几十 MB 到几百 MB），不能进 git，需要用户单独下载放到 `models/` 目录。
- 如果用 OpenCV DNN，注意当前项目 `Lib/opencv` 版本是否支持 ONNX / YOLO 推理。
- 如果用 ONNX Runtime，需要额外接入依赖，工程量变大。
- 如果 Startup 时间过长（> 3 秒），需要第一版接受同步阻塞 UI，后续演进到独立 worker thread。
- 如果单帧推理耗时 > 视频帧间隔，需要接受当前帧率下降或提前进入降帧分析。
- 如果未来要做工业视觉检测，应优先确认光照条件、目标形态、验收指标，避免通用 YOLO 效果不达标。
- 如果未来需要复合场景（YOLO+OCR），第一版建议直接打包为一条新 pipeline，不引入并列激活机制。
