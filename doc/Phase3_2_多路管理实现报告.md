# Phase 3.2 多路管理实现报告

## 阶段目标

Phase 3.2 的目标是把播放器从单路播放管线推进到多路播放管理能力，同时保持既有单路播放 UI 不退化。

本阶段不追求完整多画面布局，也不引入 `MultiViewRenderer`。核心目标是先完成多路生命周期、流身份、活动显示流、活动音频流和 Controller 边界。

最终形成的主链路如下：

```text
MainWindow
    -> MediaPlayController
    -> MultiStreamManager
        -> MediaPipeline #1
        -> MediaPipeline #2
        -> MediaPipeline #N
```

播放列表链路仍然保留为单路使用方式：

```text
MainWindow
    -> PlaylistManager
    -> MediaPlayController
    -> MultiStreamManager
    -> active MediaPipeline
```

## Manager 最终职责

`MultiStreamManager` 是 Phase 3.2 的核心领域对象，负责多路播放管线的外层管理。

已落地职责：

- 分配和维护 `StreamId`。
- 持有多个 `MediaPipeline`。
- 通过 `AddStream()` 创建并初始化一路播放管线。
- 通过 `RemoveStream()` 移除并释放一路播放管线。
- 通过 `ShutdownAll()` 释放全部播放管线。
- 提供 `StreamIds()`、`HasStream()` 等查询能力。
- 提供单路控制能力：
  - `StartStream()`
  - `PauseStream()`
  - `StopStream()`
  - `SeekStreamTo()`
  - `SeekStreamRelative()`
  - `SetStreamPlaybackSpeed()`
  - `SetStreamVolume()`
  - `SetStreamMuted()`
- 提供全局控制能力：
  - `StartAll()`
  - `PauseAll()`
  - `StopAll()`
- 维护 active stream。
- 维护 active audio stream。
- 转发每一路 `MediaPipeline` 的状态信号，并附带 `StreamId`。

`MultiStreamManager` 不负责：

- 处理 UI 控件。
- 决定播放列表顺序。
- 创建具体 UI 交互流程。
- 直接访问 demuxer、decoder、AudioPlayer、VideoRender、syncmanager 等基础设施对象。
- 实现多画面布局算法。
- 实现统一 OpenGL 多纹理渲染。
- 处理摄像头、RTSP、工业相机 SDK 的具体接入细节。

## Controller 和 Manager 边界

`MediaPlayController` 是 UI 对外使用的应用层入口。Phase 3.2 后，UI 不直接访问 `MultiStreamManager`，而是通过 Controller 间接完成多路操作。

Controller 已落地职责：

- 持有 `std::unique_ptr<MultiStreamManager>`。
- 保留原有单路播放接口：
  - `Initialize()`
  - `SwitchSource()`
  - `Start()`
  - `Pause()`
  - `Stop()`
  - `SeekTo()`
  - `SeekRelative()`
  - `SetPlaybackSpeed()`
  - `SetVolume()`
  - `SetMuted()`
- 将原有单路接口委托给 active stream。
- 暴露正式多路控制入口：
  - `AddStream()`
  - `RemoveStream()`
  - `StreamIds()`
  - `ActiveStream()`
  - `SetActiveStream()`
  - `ActiveAudioStream()`
  - `SetActiveAudioStream()`
  - `StartAll()`
  - `PauseAll()`
  - `StopAll()`
- 转发 Manager 的多路信号给 UI。
- 对现有单路 UI 只转发 active stream / active audio stream 的状态。
- 保存用户级参数：
  - playback speed
  - volume
  - muted

边界结论：

- Controller 可以知道 Manager。
- Controller 不直接持有多个 `MediaPipeline`。
- UI 可以知道 Controller 暴露的 `StreamId`。
- UI 不知道 `MultiStreamManager` 的内部结构。
- UI 不直接拿 `MediaPipeline` 指针。

## Pipeline 单路职责确认

`MediaPipeline` 仍然保持单路播放管线职责。

当前确认：

- `MediaPipeline` 不持有 `StreamId`。
- `MediaPipeline` 不知道自己是第几路。
- `MediaPipeline` 不知道其他 Pipeline 的存在。
- `MediaPipeline` 不负责多路布局。
- `MediaPipeline` 不负责 active stream / active audio stream 策略。
- `MediaPipeline` 只负责单个 `IMediaSource` 的解复用、解码、同步、音频播放、视频渲染和资源释放。

这是本阶段最重要的架构约束之一：多路身份只存在于 `MultiStreamManager` 外层，不能下沉到 `MediaPipeline`。

## Active Stream 策略

active stream 表示当前 UI 关注的显示流。

当前策略：

- 第一条添加成功的 stream 自动成为 active stream。
- `SetActiveStream()` 只能设置已存在的 stream。
- active stream 被移除后，Manager 自动选择一个可用 stream 作为 fallback。
- 如果没有可用 stream，则 active stream 变为 `InvalidStreamId`。
- Controller 的 `Position()`、`Duration()` 读取 active stream。
- Controller 的单路 `Start()`、`Pause()`、`Stop()`、`SeekTo()`、`SeekRelative()` 操作 active stream。
- MainWindow 的进度条和时间显示仍跟随 active stream。

这个策略保证旧 UI 行为可以继续解释为“当前选中一路”的状态。

## Active Audio Stream 策略

active audio stream 表示当前允许输出声音的流。

当前策略：

- 第一条有音频的 stream 自动成为 active audio stream。
- 非 active audio stream 默认静音。
- 切换 active audio stream 时：
  - 旧 active audio stream 被静音。
  - 新 active audio stream 应用 Controller 当前音量和静音状态。
- 无音频流不能成为有效 active audio stream。
- 全局音量和静音状态由 Controller 保存，再下发给 active audio stream。

验收结果：

- 多路播放时最多一路有声音。
- 切换 active audio stream 后音频输出符合预期。
- 无音频文件不会破坏音频策略。

## 最小多路文件源验证

由于当前阶段没有摄像头设备，Phase 3.2 使用本地文件源验证多路管理能力。

已提供最小验证 UI：

- `Streams` 列表。
- `Add` 添加本地文件流。
- `Remove` 移除选中流。
- `View` 设置选中流为 active stream。
- `Audio` 设置选中流为 active audio stream。
- `Start All` 启动全部流。
- `Pause All` 暂停全部流。
- `Stop All` 停止全部流。

验证结果：

- 添加两路普通文件通过。
- 添加一路普通文件和一路无音频文件通过。
- 启动所有路通过。
- 暂停所有路通过。
- 停止所有路通过。
- 移除其中一路通过。
- 关闭程序通过。
- 验证最多一路有声音通过。
- 多路生命周期稳定。
- 无音频流不崩溃。
- 关闭程序不阻塞。

## 已知限制

Phase 3.2 当前仍有以下限制：

- 还没有完整多路 UI。
- 还没有多路布局切换。
- 还没有 `MultiViewRenderer`。
- 还没有统一 OpenGL 多纹理渲染。
- 多路验证 UI 仍是阶段性最小入口。
- 多路渲染暂时复用当前 `video_widget`，不代表最终多画面方案。
- 尚未验证 4 路、9 路或更高路数的性能。
- 尚未接入摄像头、RTSP、工业相机 SDK。
- 尚未抽象实时源能力，例如是否可 seek、是否有 duration、是否可暂停。
- 尚未提供每路独立的完整 UI 状态面板。

这些限制不影响 Phase 3.2 的目标，因为本阶段重点是多路管理边界，而不是多画面渲染和性能优化。

## 架构验收结论

Phase 3.2 已满足当前阶段验收：

- `MultiStreamManager` 边界清晰。
- `MediaPlayController` 仍是 UI 对外入口。
- `MainWindow` 不直接访问 `MultiStreamManager`。
- `MainWindow` 不直接访问 `MediaPipeline`。
- `MediaPlayController` 不直接持有多个 `MediaPipeline`。
- `MediaPipeline` 保持单路职责。
- `MediaPipeline` 不出现 `StreamId`。
- 文件源多路管理已完成最小运行验证。
- 无音频流样本已完成验证。
- active audio stream 策略有效。

## 下一阶段建议

Phase 3.2 已经完成多路管理基础，下一阶段可以进入多路显示和布局能力。

建议路线：

1. 先预研多路显示架构，不直接上复杂实现。
2. 明确 `VideoRender`、`rendererContainer`、`MultiViewRenderer` 的替换关系。
3. 设计多路布局模型，例如 1x1、2x2、3x3、1+N。
4. 决定第一版多路显示是继续多个 `VideoRender`，还是引入统一渲染入口。
5. 在进入统一 OpenGL 多纹理前，先保证多路容器生命周期和移除逻辑稳定。

推荐下一份文档：

```text
doc/Phase3_3_多路显示布局预研报告.md
```

不建议下一步直接写 `MultiViewRenderer` 代码。原因是当前 `MediaPipeline` 仍然直接创建 `VideoRender` 并挂到 `rendererContainer`，需要先确定渲染边界再动实现。
