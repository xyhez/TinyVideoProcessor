# Phase 3.6 MultiViewRenderer 空壳实现报告

## 阶段目标

Phase 3.6 的目标是在不替换当前稳定播放路径的前提下，先建立统一多画面渲染器的基础边界。

本阶段只实现 `MultiViewRenderer` 空壳能力：

```text
MainWindow
    -> MultiViewRenderer
        -> layout
        -> slot / StreamId binding
        -> viewport calculation
        -> empty OpenGL region drawing
        -> active stream highlight
        -> viewport click hit test
```

本阶段不接真实视频帧，不接解码器，不接音频策略，不接播放管线。

当前真实播放路径仍然保持为：

```text
MainWindow
    -> MediaPlayController
    -> MultiStreamManager
        -> MediaPipeline
            -> VideoRender
            -> QWidget container
```

因此 Phase 3.6 的核心价值不是“完成统一渲染”，而是先把后续统一渲染必需的 UI、布局、点击和职责边界稳定下来。

## 最终实现范围

本阶段新增并接入了 `MultiViewRenderer`：

- `src/infrastructure/multiviewrenderer.h`
- `src/infrastructure/multiviewrenderer.cpp`

`MultiViewRenderer` 当前继承 `QOpenGLWidget`，属于基础设施层。它只处理显示相关的底层能力，不参与业务控制。

MainWindow 中新增了正式的空壳接入口：

- 通过右侧 dock 显示“统一渲染视图”。
- 与当前多个 `VideoRender` 播放路径并存。
- 同步当前多路布局状态。
- 同步 slot 与 `StreamId` 的绑定关系。
- 同步 active stream 高亮。
- 接收 `StreamClicked(StreamId)` 信号，并通过 `MainWindow -> MediaPlayController` 切换 active stream 和 active audio stream。

## 空壳类职责边界

`MultiViewRenderer` 当前负责：

- 保存当前布局。
- 保存 view slot 与 `StreamId` 的绑定关系。
- 保存 active stream。
- 计算当前布局下的 viewport。
- 将 Qt 坐标系区域转换为 OpenGL viewport 坐标系区域。
- 绘制空背景。
- 绘制每个 slot 的空画面区域。
- 绘制普通边框和 active stream 高亮边框。
- 根据鼠标点击位置命中 viewport。
- 命中已绑定 slot 时发出 `StreamClicked(StreamId)`。

`MultiViewRenderer` 当前不负责：

- 打开媒体源。
- demux。
- decode。
- 管理 `MediaPipeline`。
- 管理 `MultiStreamManager`。
- seek / pause / stop / shutdown。
- 音频播放。
- active audio stream 策略。
- 播放列表。
- 真实视频纹理上传。
- 视频帧生命周期管理。

这符合 `doc/架构指导.md` 和 Phase 3.4 预研中的边界要求：渲染器只做渲染和命中，不成为播放控制中心。

## Viewport 布局计算方式

当前支持两种布局：

- `Single`
- `Grid2x2`

`Single` 布局：

```text
slot 0 -> 整个 MultiViewRenderer 区域
```

`Grid2x2` 布局：

```text
slot 0 -> 左上
slot 1 -> 右上
slot 2 -> 左下
slot 3 -> 右下
```

每个 viewport 同时保存两套区域：

- `qtRect`：Qt 坐标系，用于鼠标点击命中。
- `glViewportRect`：OpenGL 坐标系，用于 `glViewport` / `glScissor` 绘制。

Qt 坐标系原点在左上角，OpenGL viewport 坐标系原点在左下角，因此转换时需要翻转 Y 坐标。

当前最多支持 4 个逻辑 slot。不支持 3x3、4x4、1+N，也不做布局自动选择。

## Slot 与 StreamId 绑定策略

`MainWindow` 已有阶段性 UI 绑定表：

```text
StreamId -> video container index
```

为了让 `MultiViewRenderer` 复用同一套 UI 语义，MainWindow 会转换出：

```text
slot index -> StreamId
```

转换关系如下：

```text
video container index == MultiViewRenderer slot index
```

这样可以保证当前多个 `VideoRender` 容器路径和新的 `MultiViewRenderer` 空壳路径看到的是同一套多路视图关系。

该绑定只属于 UI 展示层，不进入 `MediaPipeline`，也不改变 `MultiStreamManager` 的职责。

## Active Stream 高亮策略

`MainWindow` 在刷新多路布局时，会同步当前 active stream 到 `MultiViewRenderer`。

`MultiViewRenderer` 在绘制每个 viewport 时判断：

```text
viewport.streamId == activeStreamId_
```

如果匹配，则绘制高亮边框；如果不匹配，则绘制普通边框。

空 slot 即使当前 active stream 无效，也不会绘制 active 高亮。

这一策略保持了与当前多个 `VideoRender` 容器边框高亮一致的语义：active stream 是 UI 当前关注的显示流，不等价于所有播放流。

## 点击命中策略

`MultiViewRenderer` 在 `mousePressEvent()` 中处理鼠标点击：

```text
鼠标点击
    -> 计算当前 Viewports
    -> 使用 Qt 坐标 qtRect 命中 slot
    -> 如果 slot 已绑定有效 StreamId
    -> emit StreamClicked(streamId)
```

点击空 slot 不发出有效 stream 信号。

点击后不由 `MultiViewRenderer` 直接调用 `SetActiveStream()`。当前状态切换链路是：

```text
MultiViewRenderer::StreamClicked(streamId)
    -> MainWindow::ActivateStreamFromView(streamId)
    -> MediaPlayController::SetActiveStream(streamId)
    -> MediaPlayController::SetActiveAudioStream(...)
```

如果目标 stream 有音频，则 active audio stream 同步切到该 stream；如果目标 stream 无音频，则 active audio stream 置为无效，保持“看哪一路，听哪一路；无音频则静音”的策略。

## 与当前多 VideoRender 路径的并存方式

当前阶段没有替换真实播放窗口。

实际视频仍由各自的 `MediaPipeline -> VideoRender -> QWidget container` 路径显示。

`MultiViewRenderer` 作为 MainWindow 中的正式空壳视图并存显示，只负责验证：

- 布局计算是否正确。
- 空画面绘制是否正确。
- active stream 高亮是否正确。
- 点击命中是否能回到 Controller 状态链路。
- 后续统一渲染器的 UI 接入点是否可用。

这样做的原因是当前真实视频帧接入还缺少更关键的前置边界：

- 视频帧输出接口。
- 帧所有权约定。
- 渲染线程和 UI 线程之间的数据交付方式。
- seek / stop / remove stream 时的旧帧清理策略。
- `VideoRender` 中帧调度、同步和纹理上传职责的拆分。

如果在这些边界未明确前直接接真实帧，容易把 `MultiViewRenderer` 做成新的播放管线中心，破坏架构路线。

## 验证结果

根据当前实现和用户手动验收，Phase 3.6 已通过以下验证：

- 渲染器空壳可以显示。
- `Single` / `Grid2x2` 空布局显示正常。
- 切换布局不崩溃。
- active stream 高亮正确。
- 切换 active stream 后高亮跟随变化。
- 空 slot 不显示 active 高亮。
- 点击不同 viewport 能识别对应 stream。
- 点击空区域不误触发。
- 渲染器不承担播放状态切换职责。
- 当前播放功能不退化。
- `MultiViewRenderer` 与当前多个 `VideoRender` 路径可以并存。
- 关闭程序不阻塞。

本轮没有进行编译验证，符合当前开发约定：用户运行验收通过后，文档收口只做静态检查。

## 架构边界确认

Phase 3.6 收口后，边界仍然成立：

- `MainWindow` 不直接访问 `MultiStreamManager`。
- `MainWindow` 不直接访问 `MediaPipeline`。
- `MediaPlayController` 仍是 UI 对外入口。
- `MultiStreamManager` 仍管理多路生命周期和 `StreamId`。
- `MediaPipeline` 继续保持单路职责。
- `MediaPipeline` 不感知多画面布局。
- `MultiViewRenderer` 不持有播放管线。
- `MultiViewRenderer` 不参与 active audio stream 策略。
- 当前多个 `VideoRender` 路径仍可作为回退路径。

## 已知限制

当前 Phase 3.6 仍有以下限制：

- `MultiViewRenderer` 只绘制空画面，不显示真实视频。
- 未实现视频纹理上传。
- 未实现多路纹理状态管理。
- 未定义视频帧输入接口。
- 未约定 `AVFrame` 所有权。
- 未拆分 `VideoRender` 的帧调度、同步和 OpenGL 绘制职责。
- 未处理真实视频帧接入后的 seek / stop / remove stream 清理。
- 未验证 4K、多路高负载下统一渲染性能。

这些限制不影响 Phase 3.6 的阶段目标，因为本阶段目标是统一渲染器空壳和 UI 边界，不是真实统一渲染。

## 下一阶段建议

Phase 3.6 完成后，不建议立即把真实视频帧直接塞进 `MultiViewRenderer`。

推荐下一步先进入真实帧接入前的边界设计，重点解决：

1. 定义视频帧输出接口。
2. 明确 `MediaPipeline` 如何把可显示帧交给外部消费者。
3. 明确 `VideoRender` 中帧调度、同步、纹理上传和绘制职责的拆分顺序。
4. 明确 `MultiViewRenderer` 接收帧后的所有权和释放责任。
5. 明确 stream 移除、seek、stop、shutdown 时旧帧如何清理。
6. 明确先接一路真实帧，再接多路真实帧的验收路径。

建议保持当前多 `VideoRender` 路径作为可运行基线，先做抽象和小步迁移。

## 验收结论

Phase 3.6 已完成统一多画面渲染器空壳阶段目标：

- `MultiViewRenderer` 类已建立。
- viewport 布局计算已完成。
- 空画面绘制已完成。
- active stream 高亮已完成。
- 点击命中与信号已完成。
- MainWindow 已正式接入空壳视图。
- 当前真实播放路径未被替换。
- 架构边界未被破坏。

阶段结论：可以收口 Phase 3.6。下一阶段应进入真实视频帧接入前的接口和职责拆分设计，而不是直接做完整统一 OpenGL 多纹理渲染。
