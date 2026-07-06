# Phase 7 多视觉处理器架构验收报告

## 验收结论

Phase 7 验收通过。

本阶段完成了从视频帧到多视觉处理管线的统一接入能力，已验证 OpenCV 传统视觉管线和 YOLO ONNX 目标检测管线可通过 UI 切换并完成端到端处理。

## 已完成能力

- `VisionFrame` / `VisionResultSet` 统一承载视觉输入和识别结果。
- `IVisionPipeline` 统一抽象视觉处理管线。
- `IFramePreprocessor` / `IVisionAnalyzer` / `IVisionPostprocessor` 支持管线内部组合。
- `VisionPipelineManager` 支持内置管线注册、单激活、懒加载、切换和关闭。
- `VisionProcessingFrameSink` 将 active stream 帧送入当前视觉管线，并通过 `VisionOverlayRenderer` 绘制结果。
- UI 通过识别模式下拉框切换 `Off` / OpenCV / YOLO。
- OpenCV 轮廓检测管线完成端到端验收。
- YOLO ONNX 检测管线完成端到端验收。

## YOLO 验收记录

- 模型文件：`models/yolo.onnx`
- 模型文件不进入 git，由 `.gitignore` 排除。
- 默认输入尺寸：`640x640`
- 默认推理后端：OpenCV DNN / CPU
- 默认阈值：confidence `0.35`，NMS `0.45`
- 用户已确认 YOLO 管线可以运行并绘制检测结果。

YOLO 管线链路：

```text
VisionFrame(BGR24)
    -> OpenCvLetterbox(640x640)
    -> OpenCvBgrToRgb
    -> YoloOnnxAnalyzer(OpenCV DNN)
    -> YoloNms
    -> CoordinateMapper
    -> VisionOverlayRenderer
```

## 代码复核结论

- 未发现阻断性问题。
- `YoloOnnxAnalyzer` 支持 YOLOv5 类输出 `[1, N, 85]` 和 YOLOv8 / YOLO11 类输出 `[1, 84, N]`。
- `YoloNms` 按 classId 分组执行 NMS，避免不同类别的重叠框互相抑制。
- `CoordinateMapper` 将 letterbox 坐标映射回原始视频坐标。
- 模型缺失或加载失败时，`Startup()` 返回 `false`，由上层切换逻辑回滚到无 active pipeline 状态。

## 未纳入 Phase 7 的范围

- 不做多条 pipeline 并列激活。
- 不做多路视频同时视觉分析，第一版只处理 active stream。
- 不做异步推理队列。
- 不做降帧分析。
- 不做模型路径、阈值、输入尺寸的 UI 配置。
- 不做检测结果持久化和统计报表。
- 不做 OCR / 分割管线的具体实现。

## 已知限制

- YOLO 推理当前同步执行，模型加载和单帧推理可能阻塞 UI 或降低播放帧率。
- 模型路径第一版固定为 `models/yolo.onnx`。
- 当前类别名按 COCO 80 类处理，业务自训练模型需要补充类别配置。
- YOLO26 默认 end-to-end NMS-free 输出格式未在本阶段接入；如使用 YOLO26，当前建议导出 `end2end=False` 的 ONNX。
- 模型缺失、模型损坏、反复切换、seek / stop / remove stream 等负向和压力场景需要后续回归测试。

## 后续建议

Phase 8 建议进入视觉任务配置化：

- 将模型路径、输入尺寸、置信度阈值、NMS 阈值从代码常量迁移到配置。
- 为不同 pipeline 提供独立配置项。
- 支持自训练模型的类别名配置。
- 为后续 OCR、分割、复合检测管线保留统一配置入口。
