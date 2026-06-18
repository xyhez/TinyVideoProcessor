# Phase 3.3 多路显示布局实现报告

## 阶段目标

Phase 3.3 的目标是在 Phase 3.2 多路管理能力稳定后，完成第一版可观察、可操作的多路画面显示能力。

本阶段不追求统一 OpenGL 多纹理渲染，也不引入 `MultiViewRenderer`。当前选择低风险路线：

```text
MainWindow
    -> MediaPlayController
    -> MultiStreamManager
        -> MediaPipeline #1 -> VideoRender #1 -> QWidget container #1
        -> MediaPipeline #2 -> VideoRender #2 -> QWidget container #2
        -> MediaPipeline #N -> VideoRender #N -> QWidget container #N
```

核心目标是先验证多容器 UI、stream 与容器绑定、active stream 画面选择、active audio stream 策略和布局切换的稳定性。

## 最终布局策略

当前第一版支持两种布局：

- `Single`
- `Grid2x2`

`Single` 的语义是只显示当前 `active stream` 对应的视频容器。如果当前没有有效 `active stream`，则回退显示第一个视频容器作为空白显示区域。

`Grid2x2` 的语义是显示四个视频容器，用于承载最多四路已绑定视频流。空容器保持空白，不主动创建占位媒体流。

布局切换由 UI 控件手动触发，不根据 stream 数量自动切换。这样可以避免 UI 行为不可预测，也符合当前阶段“用户明确选择布局”的设计。

## 多容器 UI 实现方式

`MainWindow` 中准备了四个视频容器：

- `video_widget`
- `video_widget_1`
- `video_widget_2`
- `video_widget_3`

这些容器由 `.ui` 文件中的 `widget_videoArea` 承载，并使用 `QGridLayout` 组成 2x2 结构。

每一路 `MediaPipeline` 初始化时仍然接收一个 `QWidget* rendererContainer`，并在管线内部创建自己的 `VideoRender`。因此当前阶段没有改变 `MediaPipeline -> VideoRender -> rendererContainer` 的既有渲染链路。

`MainWindow` 只负责：

- 准备视频容器。
- 根据布局状态控制容器可见性。
- 根据 active stream 刷新容器高亮样式。
- 将选中的容器传给 `MediaPlayController::AddStream()`。

`MainWindow` 不直接访问 `MultiStreamManager`，也不直接持有或操作 `MediaPipeline`。

## Stream 与容器绑定策略

UI 层维护一张阶段性的绑定表：

```text
StreamId -> video container index
```

该绑定只存在于 `MainWindow`，用于表达“某一路逻辑流显示在哪个 UI 容器中”。它不进入 `MediaPipeline`，也不进入 `MultiStreamManager`。

绑定规则：

- 添加多路 stream 时，选择第一个空闲视频容器。
- 添加成功后记录 `StreamId -> container index`。
- 移除 stream 时释放对应容器。
- 容器满时拒绝继续添加显示流，并输出日志。
- 播放列表单路切换仍绑定到第一个容器，保持单路播放入口的既有体验。

这个设计让 `StreamId` 仍然属于多路管理语义，而 UI 容器索引只属于展示层。

## Active Stream 与 Active Audio 策略

点击某个视频容器时，UI 会根据容器索引找到对应 `StreamId`，然后通过 `MediaPlayController` 完成状态切换：

```text
点击视频容器
    -> MainWindow 找到 StreamId
    -> MediaPlayController::SetActiveStream(streamId)
    -> 根据目标流是否有音频决定 active audio
```

如果目标 stream 有音频：

```text
SetActiveAudioStream(streamId)
```

如果目标 stream 没有音频：

```text
SetActiveAudioStream(InvalidStreamId)
```

最终语义是：

```text
看哪一路，就听哪一路。
如果当前画面没有音频，则程序无声。
```

为支持 UI 判断目标 stream 是否有音频，`MediaPlayController` 暴露了只读查询能力 `StreamHasAudio(StreamId)`，UI 不直接访问 `MultiStreamManager`。

本阶段还修复了一个 active audio 切换顺序问题：从无音频流切回有音频流后仍然静音。原因是旧 active audio 被策略静音时，Controller 曾误把该信号记录成用户静音状态。修复后，`MultiStreamManager` 先切换 `activeAudioStreamId_`，再静音旧音频流，避免策略静音污染用户静音状态。

## Single 布局验证结果

`Single` 布局当前行为：

- 单路播放体验不退化。
- 多路存在时只显示 active stream 对应容器。
- 切换 active stream 后，画面区域跟随变化。
- 没有 active stream 时显示默认空白容器。

验证结果：

- 单路播放通过。
- 多路下切换 active stream 后 Single 显示跟随通过。
- 从 2x2 切回 Single 后显示当前 active stream 通过。
- 隐藏非 active 容器不影响对应 stream 的播放生命周期。

## Grid2x2 布局验证结果

`Grid2x2` 布局当前行为：

- 四个视频容器稳定显示。
- 已绑定 stream 显示在各自容器中。
- 空容器保持空白。
- active stream 变化后，容器边框高亮跟随变化。
- 布局切换不影响 stream 生命周期。

验证结果：

- 两路文件同时显示通过。
- 四路文件同时显示通过。
- 添加无音频流显示通过。
- 点击无音频流后程序无声通过。
- 从无音频流切回有音频流后声音恢复通过。
- 移除任意一路后布局保持稳定通过。
- 关闭程序不阻塞通过。

## 架构边界确认

本阶段保持以下边界：

- `MainWindow` 不直接访问 `MultiStreamManager`。
- `MainWindow` 不直接访问 `MediaPipeline`。
- `MediaPlayController` 仍是 UI 对外入口。
- `MultiStreamManager` 管理多路 `MediaPipeline` 和 `StreamId`。
- `MediaPipeline` 仍然是单路播放管线。
- `MediaPipeline` 不感知 `StreamId`。
- `MediaPipeline` 不感知多画面布局。
- 第一版不引入 `MultiViewRenderer`。

当前 UI 中的 `StreamId -> container index` 绑定属于展示层临时状态，不破坏领域层边界。

## 已知限制

当前 Phase 3.3 仍有以下限制：

- 仍然使用多个 `VideoRender` 和多个 `QWidget` 容器。
- 每路都有自己的 `QOpenGLWidget`，不是统一 OpenGL 上下文。
- 尚未做多路性能优化。
- 尚未验证 4 路高分辨率长时间播放的 CPU/GPU/内存指标。
- 尚未支持 3x3、4x4、1+N、单路全屏等高级布局。
- active stream 的视觉标记当前是容器边框高亮，尚未做复杂 overlay。
- 播放列表仍是单路播放入口，不等价于多路流列表。
- 摄像头、RTSP、RTMP、工业相机 SDK 尚未接入。

这些限制不影响 Phase 3.3 的阶段目标，因为本阶段重点是多路显示布局语义和生命周期稳定性，不是最终渲染性能。

## 是否具备进入 MultiViewRenderer 预研

当前已经具备进入 `MultiViewRenderer` 预研的条件，但不建议立即直接实现。

已经具备的基础：

- 多路生命周期由 `MultiStreamManager` 管理。
- `MediaPipeline` 已保持单路职责。
- UI 已验证多个视频容器和多路显示关系。
- active stream / active audio stream 语义已经明确。
- 无音频流策略已经验证。
- Single / Grid2x2 的第一版布局语义已经验证。

进入 `MultiViewRenderer` 前建议先预研以下问题：

1. `VideoRender` 是否需要抽象为帧输出接口，而不是直接拥有 `QOpenGLWidget`。
2. `MediaPipeline` 是否应输出解码后视频帧，由外部渲染器统一消费。
3. 多路纹理上传由谁负责。
4. 音频仍然保持单路 active audio，还是抽象成独立音频策略对象。
5. 统一渲染器如何处理 stream 移除、seek、暂停、停止和关闭。
6. 多路高分辨率下的性能目标和验收方法。

建议下一阶段先写 `MultiViewRenderer` 预研报告，再决定是否进入渲染架构改造。

## 验收结论

Phase 3.3 已完成第一版多路显示布局目标：

- 单路 UI 不退化。
- 2 路显示通过。
- 4 路显示通过。
- Single 布局跟随 active stream。
- Grid2x2 布局稳定显示多容器。
- 点击画面切换 active stream 和 active audio stream 符合预期。
- 无音频流点击后程序无声。
- 无音频流切回有音频流后声音恢复。
- 移除 stream 后容器可复用。
- 关闭程序不阻塞。

阶段结论：可以收口 Phase 3.3，下一步进入 `MultiViewRenderer` 预研或 Phase 3 后续多路渲染优化规划。
