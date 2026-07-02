# Phase 5 图像处理基础验收报告

## 验收结论

Phase 5 图像处理基础能力验收通过。

当前版本已经完成从播放管线到图像处理管线再到统一多画面渲染器的最小闭环：

```text
MediaPipeline
    -> VideoFrameScheduler
    -> ImageProcessingFrameSink
    -> ImageProcessorPipeline
    -> IVideoFrameSink
    -> MultiViewRenderer
```

用户可以通过主窗口控制 active stream 的图像处理开关、处理模式和二值化阈值。图像处理默认关闭，开启后只影响当前 active stream，不影响其它非 active stream。

## 验收范围

本次验收覆盖 Phase 5.1 到 Phase 5.8 的基础能力：

- OpenCV 依赖接入预检查。
- 图像处理抽象接口。
- `AVFrame` 与 `cv::Mat` 转换边界。
- `ImageProcessingFrameSink` 链路接入。
- 灰度化、二值化、边缘检测三个基础处理器。
- 主窗口最小 UI 控制入口。
- active stream 图像处理策略。
- 文件源、实时源、多路切换和生命周期风险检查。

本次不覆盖：

- 多路同时图像处理。
- 每路独立处理器链。
- 处理器拖拽排序。
- 复杂参数面板。
- GPU 图像处理。
- 目标检测、尺寸测量、标定、数据存储和报表导出。

## 架构边界验收

### UI 层

`MainWindow` 只负责控件初始化、信号槽绑定和用户输入转发。

已确认：

- UI 不直接访问 `AVFrame`。
- UI 不直接访问 `cv::Mat`。
- UI 不直接创建或持有 `IImageProcessor`。
- UI 不访问 `ImageProcessingFrameSink` 内部状态。
- UI 不访问 `SyncManager`、队列或 OpenGL 纹理。

### 应用层

`MediaPlayController` 负责保存用户级图像处理参数，并将配置下发给 active stream。

已确认：

- 图像处理开关保存在 Controller。
- 图像处理模式保存在 Controller。
- 二值化阈值保存在 Controller。
- Controller 负责根据模式创建处理器。
- 切换 active stream 后，Controller 会重新把当前图像处理配置应用到新的 active stream。

### 领域层

`MultiStreamManager` 负责多路流状态和 active stream 管理。

已确认：

- `MultiStreamManager` 只暴露 active stream 的图像处理管线入口。
- 未暴露任意 stream 的自由处理器访问口。
- `SetActiveStreamImageProcessingEnabled()` 只启用 active stream。
- 非 active stream 会被显式关闭图像处理。

### 基础设施层

`ImageProcessingFrameSink` 作为 `IVideoFrameSink` 装饰器插入调度器和最终显示 sink 之间。

已确认：

- 默认关闭处理时直接转发原帧。
- 开启处理时调用 `ImageProcessorPipeline`。
- 处理失败时不向下游输出异常帧。
- 下游 sink 可在释放阶段置空，避免访问已释放显示对象。

## 功能验收

### 图像处理开关

验收结果：通过。

表现：

- 默认关闭图像处理。
- 勾选图像处理后，active stream 进入处理链路。
- 取消勾选后，画面恢复原图链路。
- 无 active stream 时，控件安全禁用。

### 灰度化

验收结果：通过。

表现：

- 切换到灰度模式后，画面显示为灰度效果。
- 输出仍保持显示链路可接受的 BGR/YUV 转换边界。
- 不影响播放控制、音频输出和多路列表状态。

### 二值化

验收结果：通过。

表现：

- 切换到二值化模式后，画面显示为黑白阈值效果。
- 阈值控件仅在二值化模式且启用处理时可用。
- 调整阈值后，Controller 重新配置 active stream 处理器。

### 边缘检测

验收结果：通过。

表现：

- 切换到边缘检测模式后，画面显示 Canny 边缘效果。
- 第一版使用固定 Canny 参数。
- 处理器不保存帧指针，不依赖 UI 状态。

## 播放控制验收

### 播放和暂停

验收结果：通过。

图像处理开启和关闭状态下，播放、暂停行为正常，没有发现状态错乱。

### 停止

验收结果：通过。

停止后不会继续输出旧处理帧。再次播放时按当前 active stream 配置进入对应链路。

### Seek

验收结果：通过。

Seek 后没有发现旧处理帧残留问题。图像处理链路跟随调度器输出的新帧工作。

### 播放结束

验收结果：通过。

播放结束后，图像处理链路不阻塞窗口关闭，不影响已有播放结束状态处理。

## 多路和 active stream 验收

验收结果：通过。

已确认：

- 多路播放时只处理 active stream。
- 切换 active stream 后，新的 active stream 使用当前图像处理配置。
- 原 active stream 不继续图像处理。
- active audio stream 与图像处理 active stream 的职责保持分离。
- remove stream 后不访问已释放帧。

## 实时源验收

验收结果：通过。

已确认：

- 图像处理 UI 对文件源和实时源使用同一套 Controller 接口。
- RTSP / 摄像头等实时源不走 seek 依赖。
- 处理链路不改变实时源的基本播放控制边界。

## 生命周期验收

验收结果：通过。

已确认：

- 播放中关闭窗口正常。
- 暂停后关闭窗口正常。
- Stop All 后关闭窗口正常。
- remove stream 后关闭窗口正常。
- 图像处理链路没有新增阻塞释放路径。

## 性能记录

本次验收以功能正确性和生命周期稳定性为主，未记录精确 CPU / GPU / 内存数值。

当前明确结论：

- 图像处理会增加 CPU 和内存带宽开销。
- 第一版只允许 active stream 处理，是合理的性能边界。
- 多路同时处理、9/16 路处理和 GPU 图像处理不属于 Phase 5 范围。
- 后续进入高级图像分析前，需要补一轮量化性能验收。

建议后续量化指标：

- 单路 1080P 灰度化 CPU / GPU / 内存占用。
- 单路 1080P 二值化 CPU / GPU / 内存占用。
- 单路 1080P 边缘检测 CPU / GPU / 内存占用。
- 4 路播放时 active stream 处理的资源变化。
- RTSP 源开启处理后的延迟变化。
- 处理耗时均值、最大值和抖动。

## 风险和后续建议

### 已知风险

- `AVFrame -> cv::Mat -> AVFrame` 会产生额外拷贝和格式转换成本。
- 边缘检测在高分辨率视频上开销高于灰度化和二值化。
- 当前 UI 是最小入口，不支持每路独立配置。
- 当前处理器链不支持复杂参数面板。

### 后续建议

Phase 5 基础链路完成后，可以进入 Phase 6 高级图像分析。

进入 Phase 6 前建议先确认：

- 目标检测场景：工业零件、运动目标、颜色目标还是通用物体。
- 目标输出：只预览效果，还是需要统计、截图、记录和导出。
- 性能边界：优先单路实时处理，还是要求多路同时处理。
- 样本来源：文件、摄像头、RTSP 哪一种作为主验证来源。

## 最终结论

Phase 5 达成预期目标：

- 图像处理链路已接入播放管线。
- 基础处理器已可通过 UI 控制。
- UI、Controller、Domain、Infrastructure 边界清晰。
- active stream 策略符合当前多路架构。
- 生命周期验收通过。

可以进入下一阶段高级图像分析规划。
