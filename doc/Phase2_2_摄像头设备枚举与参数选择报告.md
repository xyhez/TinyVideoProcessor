# Phase 2.2 摄像头设备枚举与参数选择报告

## 1. 阶段目标

Phase 2.2 的目标是让用户不再手写摄像头设备名，提供一个基础的设备和参数选择入口。

本阶段仍然遵循既有架构边界：

- `CameraMediaSource` 只接收最终打开参数。
- 设备枚举不放进 `CameraMediaSource`。
- UI 只负责收集用户选择。
- 摄像头仍通过 `MediaPlayController::AddStream()` 进入多路播放体系。

## 2. 数据结构

新增摄像头参数结构：

- `CameraDeviceInfo`
- `CameraFormatOption`
- `CameraOpenOptions`

这些结构只描述设备、格式和用户选择，不依赖 UI 控件，也不持有 FFmpeg 运行期上下文。

`CameraOpenOptions` 是 UI 与 `CameraMediaSource` 之间的参数边界。

## 3. 设备枚举方案

已新增基础设施层服务：

```text
src/infrastructure/CameraDeviceEnumerator.h
src/infrastructure/CameraDeviceEnumerator.cpp
```

职责：

- 枚举摄像头设备。
- 枚举设备可选格式。
- 不打开播放流。
- 不创建 `MediaPipeline`。
- 不参与 active stream 策略。

第一版选择 FFmpeg dshow 枚举路线，原因：

- 与 `CameraMediaSource` 使用同一套 FFmpeg / libavdevice 能力。
- 不引入额外平台 SDK。
- 后续可向 Linux/macOS 的输入设备枚举扩展。

## 4. 能力枚举状态

当前能力枚举是部分完成状态：

- C920 已提供已知格式列表。
- UI 可获得 C920 的分辨率、帧率和编码选项。
- `CameraMediaSource` 只接收最终选择结果。

当前限制：

- 尚未动态解析真实 dshow 能力输出。
- 非 C920 设备仍可能复用 C920 已知格式列表。

这个限制不影响 C920 MVP 主链路，但后续支持任意 USB 摄像头时需要恢复处理。

## 5. UI 接入

已新增：

```text
src/ui/CameraSettingsDialog.h
src/ui/CameraSettingsDialog.cpp
```

UI 能力：

- 选择摄像头设备。
- 选择分辨率和编码格式。
- 根据格式选择帧率。
- 刷新设备列表。
- 确认后返回 `CameraOpenOptions`。

`MainWindow::AddCameraStream()` 只负责：

- 打开设置对话框。
- 获取 `CameraOpenOptions`。
- 创建 `CameraMediaSource`。
- 调用 `MediaPlayController::AddStream()`。

`MainWindow` 不直接打开摄像头，也不访问 FFmpeg context。

## 6. 架构边界

Phase 2.2 保持了职责分离：

- `CameraDeviceEnumerator` 属于基础设施层。
- `CameraSettingsDialog` 属于 UI 层。
- `CameraOpenOptions` 是跨层参数 DTO。
- `CameraMediaSource` 只负责按参数打开设备。
- `MediaPlayController` 仍是 UI 添加播放流的入口。

## 7. 后续建议

如果恢复 Phase 2 摄像头扩展，建议优先处理：

1. 动态解析 dshow 真实设备能力。
2. 非 C920 设备不再复用 C920 格式列表。
3. 多个同名摄像头的唯一身份识别。

这些内容当前不阻塞 C920 video-only 主链路，因此在 Phase 2 阶段性收尾中暂不继续展开。

## 8. 结论

Phase 2.2 已完成摄像头设备与参数选择的基础闭环。

虽然真实设备能力动态枚举仍未完全实现，但当前设计已经把设备枚举、参数 UI 和媒体源打开职责分离，为后续恢复扩展留下了清晰边界。
