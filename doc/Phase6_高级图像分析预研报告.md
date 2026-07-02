# Phase 6 高级图像分析预研报告

## 目标

Phase 6 的目标是在 Phase 5 图像处理基础链路上，增加第一版可演示、可扩展的高级图像分析能力。

本阶段不追求复杂模型推理，而是先实现基于 OpenCV 的传统视觉分析闭环：

```text
active stream
    -> ImageProcessingFrameSink
    -> ImageProcessorPipeline
    -> ObjectDetectionProcessor
    -> 画框 / 计数 / 预览
    -> MultiViewRenderer
```

第一版推荐做：**连通域 / 轮廓目标检测 + 画框 + 计数**。

## 为什么先做连通域 / 轮廓检测

### 与当前架构匹配

当前 Phase 5 已经建立：

- `IImageProcessor`
- `ImageProcessorPipeline`
- `VideoFrameMatConverter`
- `ImageProcessingFrameSink`
- active stream 图像处理开关和模式选择

连通域 / 轮廓检测可以直接作为一个新的 `IImageProcessor` 实现，不需要改播放、解码、调度和 OpenGL 渲染主链路。

### 简历表达更完整

灰度化、二值化、边缘检测更多是基础图像处理。目标检测、画框和计数能体现更完整的工程闭环：

- 视频源接入
- 实时帧处理
- 目标区域提取
- 结果可视化
- 参数可调
- 多路架构下 active stream 分析策略

### 风险可控

传统 OpenCV 检测不需要模型文件、CUDA、ONNX Runtime 或额外推理框架。对当前项目来说，风险主要集中在算法参数和性能，而不是依赖接入。

## 第一版范围

Phase 6 第一版只处理 active stream。

包含：

- 新增目标检测处理器。
- 基于二值化或边缘/轮廓提取目标区域。
- 支持最小面积过滤。
- 支持最大面积过滤。
- 在画面上绘制目标框。
- 在画面上绘制目标数量。
- UI 支持选择目标检测模式。
- UI 支持调整基础检测参数。
- 验证文件源、摄像头源和 RTSP 源。

不包含：

- 多路同时检测。
- 每路独立检测参数。
- 深度学习模型推理。
- GPU 图像处理。
- 目标跟踪。
- 标定和真实尺寸测量。
- 检测结果存储。
- 截图归档。
- SQLite、报表、统计分析。

## 架构边界

### UI 层

`MainWindow` 只负责：

- 展示检测开关和参数控件。
- 把用户操作转发给 `MediaPlayController`。
- 不直接访问 `cv::Mat`。
- 不直接访问 `AVFrame`。
- 不直接访问目标检测处理器内部状态。

### 应用层

`MediaPlayController` 负责：

- 保存用户级检测配置。
- 根据 UI 选择配置 active stream 的处理器链。
- 保持 active stream 策略。
- 切换 active stream 时重新应用检测配置。

### 领域层

`MultiStreamManager` 和 `MediaPipeline` 不应承担检测算法。

它们只负责：

- 找到 active stream 对应的图像处理管线。
- 控制 active stream 是否启用处理。
- 保持多路播放、音频、seek、生命周期语义不变。

### 基础设施层

目标检测处理器属于基础设施层图像处理能力。

推荐新增：

```text
src/infrastructure/objectdetectionprocessor.h
src/infrastructure/objectdetectionprocessor.cpp
```

可选新增：

```text
src/infrastructure/detectiontypes.h
```

其中 `ObjectDetectionProcessor` 仍实现 `IImageProcessor`，输入输出仍是 `cv::Mat&`。

## 检测方案选择

### 方案 A：在处理器内直接绘制检测结果

处理流程：

```text
BGR 图像
    -> 灰度
    -> 高斯滤波
    -> 二值化 / Canny
    -> findContours / connectedComponents
    -> 面积过滤
    -> boundingRect
    -> rectangle + count text
    -> 输出 BGR 图像
```

优点：

- 改动小。
- 可以复用当前 `IImageProcessor` 接口。
- 不需要新增结果总线。
- UI 和 Renderer 不需要理解检测结果。
- 第一版可快速验收。

缺点：

- 检测结果只体现在画面上，不便于后续统计。
- 如果以后要数据记录，需要再抽出 `DetectionResult` 的结果通道。

结论：推荐 Phase 6 第一版采用。

### 方案 B：处理器输出 DetectionResult，再由 Renderer 统一 overlay

处理流程：

```text
ObjectDetectionProcessor
    -> QVector<DetectionResult>
    -> Controller / Manager
    -> MultiViewRenderer overlay
```

优点：

- 架构更适合后续统计、存储和多路 overlay。
- 检测数据和画面显示解耦。

缺点：

- 当前 `IImageProcessor` 只返回处理后的图像，不具备结果输出通道。
- 需要扩展 Controller、Manager、Renderer 的跨层数据流。
- Phase 6 第一版改动面偏大。

结论：保留为后续 Phase 6.7 或 Phase 7 的演进方向。

## 参数设计

第一版建议参数：

- `threshold`：二值化阈值，范围 0-255。
- `minArea`：最小目标面积，过滤噪声。
- `maxArea`：最大目标面积，过滤过大区域。
- `drawBoundingBox`：是否绘制目标框，第一版默认开启。
- `drawCount`：是否绘制计数，第一版默认开启。

不建议第一版加入：

- 圆形/矩形/多边形分类。
- 颜色范围选择。
- ROI 区域配置。
- 目标跟踪 ID。
- 标定比例。

这些功能应在检测基础链路稳定后追加。

## UI 设计建议

复用 Phase 5 的图像处理控制区：

- 图像处理开关继续作为总开关。
- 模式下拉框新增“目标检测”。
- 阈值控件在二值化和目标检测模式可用。
- 新增最小面积 / 最大面积控件。

第一版 UI 不做复杂参数面板，避免 `MainWindow` 继续膨胀。后续如果参数变多，应考虑独立 `ImageProcessingSettingsPanel` 或检测参数对话框。

## 性能边界

目标检测比灰度化和二值化更重，尤其是高分辨率视频。

第一版性能策略：

- 只处理 active stream。
- 不处理非 active stream。
- 不承诺 9/16 路同时检测。
- 参数更新只重新配置 active stream 的处理器。
- 高分辨率视频如果性能不足，后续再考虑降采样检测。

后续可优化方向：

- 检测前缩放图像。
- 降低分析帧率。
- 只在 ROI 内检测。
- 将检测结果缓存到若干帧。
- GPU 或 OpenCL 加速。

## 推荐 Phase 拆分

### Phase 6.1 预研和任务拆分

输出本报告和 `Phase6TODO.md`。

### Phase 6.2 检测参数和类型定义

新增检测参数结构，为目标检测处理器准备稳定配置入口。

### Phase 6.3 目标检测处理器空壳

新增 `ObjectDetectionProcessor`，先实现接口和参数保存，不接入 UI。

### Phase 6.4 连通域 / 轮廓检测实现

实现灰度、阈值、轮廓提取、面积过滤、画框和计数。

### Phase 6.5 Controller 和 UI 最小接入

模式下拉框新增目标检测，支持阈值和面积参数调整。

### Phase 6.6 验收报告

验证文件源、摄像头源、RTSP 源、多路 active stream、关闭释放和基本性能。

## 验收标准

Phase 6 第一版完成时应满足：

- 可以开启目标检测模式。
- active stream 上能看到目标框。
- active stream 上能看到目标计数。
- 最小面积参数能过滤小噪声。
- 最大面积参数能过滤异常大区域。
- 切换 active stream 后检测只作用于新的 active stream。
- 关闭检测后画面恢复普通图像处理链路。
- remove stream 和关闭窗口不崩溃、不阻塞。

## 风险

- 普通视频素材目标不一定清晰，检测效果依赖画面内容和阈值。
- 固定阈值对光照变化敏感。
- 轮廓检测容易把字幕、噪声、纹理识别成目标。
- 4K 或高帧率视频下检测开销明显。
- 后续如果需要统计结果，必须新增独立结果通道。

## 结论

Phase 6 可以开始。

推荐先实现传统 OpenCV 目标检测处理器，并以 active stream 画框和计数为第一版验收目标。该方案符合当前分层架构，改动范围可控，适合作为简历项目中“实时图像分析”的第一阶段成果。
