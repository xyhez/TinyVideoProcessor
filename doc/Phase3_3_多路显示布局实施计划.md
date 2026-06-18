# Phase 3.3 多路显示布局实施计划

## 阶段定位

Phase 3.3 在 Phase 3.2 多路管理稳定之后推进，目标是让多路视频真正具备可观察的多画面显示能力。

本阶段仍然遵循现有架构边界：

```text
MainWindow
    -> MediaPlayController
    -> MultiStreamManager
        -> MediaPipeline
        -> VideoRender
```

本阶段第一版不直接实现 `MultiViewRenderer`，而是先走低风险路线：

```text
多个 VideoRender + 多个 QWidget 容器
```

## 已确认决策

### 1. 第一版渲染路线

确认采用：

```text
一个 stream 对应一个 MediaPipeline
一个 MediaPipeline 对应一个 VideoRender
一个 VideoRender 放入一个独立 QWidget 容器
```

暂不采用：

```text
多个 stream -> 统一 MultiViewRenderer
```

原因：

- 当前 `MediaPipeline::Initialize(mediaSource, rendererContainer)` 已经适配 QWidget 容器。
- 低风险，能优先验证多画面 UI 和容器生命周期。
- 不需要立即重构 `VideoRender` 和帧输出边界。
- `MultiViewRenderer` 涉及统一 OpenGL 上下文和多纹理输入，适合后续单独预研。

### 2. 第一版布局范围

确认第一版只做：

- `Single`
- `2x2`

暂不做：

- `3x3`
- `4x4`
- `1+N`
- 单路全屏
- 统一 OpenGL 多纹理渲染

原因：

- `Single` 用于兼容当前单路显示语义。
- `2x2` 足够验证 4 路显示。
- 当前阶段重点是布局和生命周期，不是性能压测。

### 3. 点击画面行为

确认点击画面时：

- 切换 active stream 到被点击 stream。
- 如果该 stream 有音频，则同时切换 active audio stream。
- 如果该 stream 无音频，则清空 active audio stream，表现为无声。

语义：

```text
看哪一路，就听哪一路。
当前画面没有音频，就无声。
```

这与单独播放无音频视频的体验一致，也避免“画面 A、声音 B”的误解。

## 不做范围

Phase 3.3 第一版不做：

- `MultiViewRenderer`
- 统一 OpenGL 多纹理渲染
- 3x3 / 4x4 / 1+N 布局
- 单路全屏
- 摄像头枚举
- RTSP / RTMP
- 工业相机 SDK
- 图像处理
- 多路性能优化
- 多路录制

## 3.3.3 布局模型定义

**目标**：先定义多路显示布局枚举和基础语义，不接入 UI，不创建容器。

建议新增或放置：

- 可以先在 UI 层附近定义阶段性布局枚举。
- 如果后续布局逻辑扩大，再抽到独立领域/应用层类型。

建议枚举：

```cpp
enum class MultiViewLayout {
    Single,
    Grid2x2
};
```

任务：

- 定义 `MultiViewLayout`。
- 定义当前布局状态。
- 定义布局切换入口。
- 明确 `Single` 和 `Grid2x2` 的最大可见路数。
- 不引入 `MultiViewRenderer`。

验收：

- 能在代码中表示当前布局。
- 能在 `Single` 和 `Grid2x2` 之间切换状态。
- 不改变现有播放行为。

## 3.3.4 多容器 UI 骨架

**目标**：在 MainWindow 中准备多个视频容器，用于承载多个 `VideoRender`。

建议：

- 第一版准备 4 个 QWidget 容器。
- `Single` 模式只显示第 1 个或 active stream 容器。
- `Grid2x2` 模式显示 4 个容器。
- 容器命名要清晰，例如：
  - `videoContainer_0`
  - `videoContainer_1`
  - `videoContainer_2`
  - `videoContainer_3`

任务：

- 修改 `.ui` 或通过代码创建 4 个容器。
- 添加布局切换控件。
- 保留现有播放控制区。
- 保留 `Streams` 验证列表。
- 不让 UI 直接访问 `MultiStreamManager`。

验收：

- 程序启动后 UI 正常。
- `Single` 模式显示一个视频区域。
- `2x2` 模式显示四个视频区域。
- 切换布局不崩溃。
- 尚未要求每个容器都有视频。

## 3.3.5 Stream 与容器绑定

**目标**：添加 stream 时为其分配一个可用视频容器。

第一版建议：

- 最多同时显示 4 路。
- 添加 stream 时选择第一个空闲容器。
- 移除 stream 时释放容器占用。
- 如果容器已满，拒绝添加更多显示流，并给出日志。

任务：

- 在 `MainWindow` 维护 `StreamId -> QWidget*` 的 UI 绑定表。
- 在 `AddMultiStreamFiles()` 中把分配到的容器传给 `MediaPlayController::AddStream()`。
- 在 `RemoveSelectedStream()` 后释放绑定。
- 在 `RefreshStreamView()` 中显示容器/布局状态。

验收：

- 添加第 1 路显示在一个容器。
- 添加第 2 路显示在另一个容器。
- 最多添加 4 路显示流。
- 移除一路后容器可再次复用。
- UI 仍只通过 Controller 添加和移除 stream。

## 3.3.6 Active Stream 画面选择

**目标**：点击某个视频容器时切换 active stream，并同步音频策略。

任务：

- 为每个视频容器安装事件过滤或点击处理。
- 根据容器找到对应 `StreamId`。
- 调用 `MediaPlayController::SetActiveStream(streamId)`。
- 如果目标流有音频，调用 `SetActiveAudioStream(streamId)`。
- 如果目标流无音频，调用 `SetActiveAudioStream(InvalidStreamId)`。
- 在 UI 上标记 active stream。

前置条件：

- Controller 或 UI 需要能判断某个 stream 是否有音频。
- 如果当前 API 不足，应优先在 `MediaPlayController` 暴露只读查询能力，而不是让 UI 访问 Manager。

验收：

- 点击有音频画面后，进度条跟随该路，声音也切换到该路。
- 点击无音频画面后，进度条跟随该路，程序无声。
- 不出现画面和声音来自不同路的情况。
- `MainWindow` 不直接访问 `MultiStreamManager`。

## 3.3.7 Single 布局行为

**目标**：定义并实现 `Single` 布局的最小行为。

建议语义：

- `Single` 显示 active stream 对应容器。
- 非 active stream 容器隐藏。
- 切换 active stream 后，显示区域切换到对应容器。

任务：

- 实现 `Single` 布局刷新逻辑。
- active stream 变化时刷新容器可见性。
- 如果没有 active stream，显示空白容器。

验收：

- 单路播放体验不退化。
- 多路存在时只显示 active stream。
- 切换 active stream 后画面区域跟随变化。

## 3.3.8 Grid2x2 布局行为

**目标**：实现第一版 2x2 多画面显示。

建议语义：

- 最多显示 4 路。
- 每路固定占一个容器。
- active stream 用边框或列表标记表示。
- active audio stream 可继续用列表标记表示。

任务：

- 实现 `Grid2x2` 容器布局。
- 添加布局切换控件。
- 切换到 `Grid2x2` 后显示所有已绑定容器。
- 空容器保持空白。
- active stream 变化后刷新视觉标记。

验收：

- 两路文件可以同时显示。
- 四路文件可以同时显示。
- 布局切换不影响播放生命周期。
- 移除任意一路后布局保持稳定。
- 无音频流显示正常。

## 3.3.9 Phase 3.3 验收报告

**目标**：收口多路显示布局第一版。

产出：

- `doc/Phase3_3_多路显示布局实现报告.md`

报告内容：

- 最终布局策略。
- 多容器 UI 实现方式。
- stream 与 container 绑定策略。
- active stream / active audio stream 点击策略。
- Single 和 2x2 验证结果。
- 已知限制。
- 是否具备进入 `MultiViewRenderer` 预研的条件。

验收：

- 单路 UI 不退化。
- 2 路显示通过。
- 4 路显示通过。
- 点击画面切换 active stream 和音频符合预期。
- 无音频流点击后无声。
- 关闭程序不阻塞。

## 推荐推进顺序

严格按以下顺序推进：

1. 布局模型定义。
2. 多容器 UI 骨架。
3. stream 与容器绑定。
4. active stream 画面选择。
5. Single 布局行为。
6. Grid2x2 布局行为。
7. Phase 3.3 验收报告。

不要先做统一渲染器，也不要把布局逻辑塞进 `MediaPipeline`。
