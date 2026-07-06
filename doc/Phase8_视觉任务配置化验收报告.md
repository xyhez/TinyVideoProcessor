# Phase 8 视觉任务配置化验收报告

## 验收结论

Phase 8 验收通过。

本阶段完成视觉管线配置化：程序启动时读取 `config/vision_pipelines.json`，按配置注册视觉管线，并将 OpenCV / YOLO 的关键参数从硬编码迁移到配置结构。

## 已完成能力

- 新增 `VisionPipelineConfig` / `VisionPipelineConfigSet`。
- 新增 `VisionConfigLoader`，支持读取 `config/vision_pipelines.json`。
- 配置缺失、打开失败、JSON 解析失败时回退默认 OpenCV + YOLO 配置。
- `VisionProcessorFactory` 支持按配置注册 enabled pipeline。
- YOLO 管线支持配置：
  - `modelPath`
  - `labelsPath`
  - `inputWidth`
  - `inputHeight`
  - `confidenceThreshold`
  - `nmsThreshold`
- OpenCV 轮廓管线支持配置：
  - `threshold`
  - `maxValue`
- `MultiStreamManager` 初始化时加载配置并调用 `RegisterConfiguredPipelines`。

## 配置文件

默认配置文件：

```text
config/vision_pipelines.json
```

默认包含两条 pipeline：

```text
opencv-contour
yolo-onnx
```

模型文件仍不进入 git。`models/yolo.onnx` 是默认约定路径，文件由本地环境提供。

## 验收记录

用户已确认 Phase 8 验收通过，包含以下运行场景：

- 正常配置文件存在时，程序启动后视觉管线列表来自配置。
- 删除或缺失 `config/vision_pipelines.json` 时，程序可启动并回退默认 OpenCV + YOLO。
- 配置文件为非法 JSON 时，程序可启动并回退默认 OpenCV + YOLO。
- YOLO 默认配置下仍可运行。
- OpenCV 轮廓检测默认配置下仍可运行。
- 修改 YOLO / OpenCV 相关配置后，配置链路可生效。

## 代码复核结论

- 配置加载职责保持在 `infrastructure/vision`，UI 不直接解析配置。
- `VisionPipelineManager` 仍只负责注册表、单激活和生命周期切换。
- `IVisionPipeline` 接口未变化，配置通过具体 pipeline options 注入。
- 配置错误不会阻止程序启动，失败路径会回退默认配置。
- `labelsPath` 为空或不可读时，YOLO 回退内置 COCO 类别名。
- OpenCV 阈值参数限制在 `0-255`。
- YOLO 输入尺寸限制在 `32-4096`，confidence / NMS 限制在 `0.0-1.0`。

## 未纳入 Phase 8 的范围

- 不做配置热重载。
- 不做 UI 参数编辑界面。
- 不做模型下载器。
- 不做多 pipeline 并列激活。
- 不做按 stream 绑定不同 pipeline。
- 不做 OCR / 分割算法实现。
- 不做 ONNX Runtime / TensorRT 接入。
- 不做异步推理和降帧推理。

## 已知限制

- 配置文件路径第一版固定为 `config/vision_pipelines.json`。
- 配置变更需要重启程序后生效。
- labels 文件格式第一版约定为一行一个类别名。
- YOLO 推理仍同步执行，复杂模型可能导致播放卡顿。
- 配置文件只描述 pipeline 创建参数，不保存当前 active pipeline 或 UI 状态。

## 后续建议

Phase 9 建议进入视觉推理性能优化：

- 降帧分析：显示帧率与推理帧率分离。
- 异步推理：pipeline 在 worker thread 中执行，播放链路读取最新结果。
- 结果缓存：推理慢时复用上一帧检测框。
- 推理耗时统计：在 UI 或日志中暴露 pipeline 平均耗时。

如果优先业务能力扩展，也可以进入 OCR 或分割管线接入。
