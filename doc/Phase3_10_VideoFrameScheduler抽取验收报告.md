# Phase 3.10 VideoFrameScheduler 抽取验收报告

## 验收结论

Phase 3.10 已完成。

本阶段已经把原本留在 `VideoRender` 内部的单路视频帧调度职责抽取为独立 `VideoFrameScheduler`，并把调度器上移到 `MediaPipeline` 显式组合。`VideoRender` 已收口为默认视频帧 sink 和 OpenGL widget；`MultiViewRenderer` 仍只负责多路绘制，不访问帧队列、同步管理器或播放管线。

本阶段未完成、也不应该直接完成的内容是：删除主窗口中的旧视频容器并让 `MultiViewRenderer` 成为 UI 唯一显示区域。该工作属于下一阶段 UI 替换任务。

## 最终架构边界

当前视频显示链路分为调度、sink、绘制三层：

```text
MediaPipeline
    -> VideoFrameScheduler
        -> VFrame_ / syncmanager / frameRate / videoTimeBase / playbackSpeed
        -> IVideoFrameSink::PresentFrame(frame)
            -> 默认路径：VideoRender
            -> 统一渲染路径：SingleStreamFrameSinkAdapter -> MultiViewRenderer
```

职责边界如下：

- `VideoFrameScheduler` 负责取帧、同步判断、丢帧追赶、VIDEO sync 时钟刷新和下一轮调度。
- `MediaPipeline` 负责创建和控制 `VideoFrameScheduler`，并把当前 `IVideoFrameSink` 注入 scheduler。
- `IVideoFrameSink` 只表达“接收可显示帧”和“清理当前显示帧”的语义。
- `VideoRender` 只作为默认单路显示 sink，负责 OpenGL widget 绘制和当前帧持有。
- `SingleStreamFrameSinkAdapter` 负责把无 `StreamId` 的单路 sink 调用适配到 `MultiViewRenderer`。
- `MultiViewRenderer` 负责按照 `StreamId -> slot -> viewport` 映射绘制多路画面。

## 完成内容

### 1. 调度器抽取方案完成

已明确 `VideoFrameScheduler` 的输入、输出和不做范围：

- 输入：`VFrame_`、`syncmanager`、帧率、视频 time base、播放速度。
- 输出：`IVideoFrameSink::PresentFrame(frame)` 和 `IVideoFrameSink::ClearFrame()`。
- 不持有 `StreamId`。
- 不依赖 `MultiViewRenderer`。
- 不做 OpenGL 绘制。
- 不管理 UI 布局。

### 2. 新增并落地 VideoFrameScheduler

`VideoFrameScheduler` 已接管以下逻辑：

- 查看队首帧但不提前弹出。
- AUDIO sync 下按音频主时钟判断视频帧早晚。
- 帧过早时等待。
- 帧过晚时弹出并释放。
- 帧可显示时弹出并转交 sink。
- VIDEO sync 下刷新视频主时钟。
- 按播放速度修正帧间隔。
- 通过 `QTimer::singleShot()` 进行下一轮自调度。

### 3. MediaPipeline 显式组合调度器和 sink

`MediaPipeline` 当前持有：

- `std::unique_ptr<VideoFrameScheduler> videoFrameScheduler_`
- `IVideoFrameSink* activeVideoFrameSink_`
- 默认回退路径需要时创建的 `VideoRender* videoRender_`

关键控制点已经转移到 scheduler：

- `Start()` 调用 scheduler `Start()`。
- `Pause()` 调用 scheduler `Pause()`。
- `Stop()` / 实时源 stop 调用 scheduler `Stop()` 和 `ClearOutputFrame()`。
- `SeekTo()` 在清队列和 seek 前调用 scheduler `ClearOutputFrame()`。
- `SetPlaybackSpeed()` 调用 scheduler `SetSpeed()`。
- `ReleaseResources()` 停止 scheduler、清理当前 sink，并将 scheduler sink 置空。

外部统一渲染路径下，`MediaPipeline` 不再创建隐藏 `VideoRender` 来驱动调度。

### 4. VideoRender 收口为默认 sink

`VideoRender` 已删除调度兼容接口和状态：

- 不再暴露 `InitVideoRender()`。
- 不再暴露 `StartRendering()` / `StopRendering()` / `Pause()`。
- 不再处理播放速度。
- 不再持有 `VFrame_` 或 `syncmanager`。
- 不再内部组合 `VideoFrameScheduler`。
- 不再转发外部 sink。

当前只保留：

- `PresentFrame(AVFrame*)`
- `ClearFrame()`
- OpenGL 初始化、纹理上传、绘制、显示矩形计算和资源释放。

### 5. MultiViewRenderer 多路路径保持独立

统一渲染路径仍通过 `SingleStreamFrameSinkAdapter` 接入：

```text
MainWindow::CreateMultiViewFrameSinkFactory()
    -> SingleStreamFrameSinkAdapter(multiViewRenderer_, streamId)
        -> MultiViewRenderer::PresentFrame(streamId, frame)
```

该路径满足当前架构要求：

- `StreamId` 不进入 `MediaPipeline`。
- `StreamId` 不进入 `VideoFrameScheduler`。
- `MultiViewRenderer` 不访问 `VFrame_`。
- `MultiViewRenderer` 不访问 `SyncManager`。
- `MultiViewRenderer` 不控制 demuxer、decoder、audio player 或 scheduler。

### 6. UI 全面迁移条件已复核

已完成 `doc/Phase3_10_UI全面转向MultiViewRenderer复核报告.md`。

结论是：可以进入下一步“旧 UI 容器替换”任务，但不能直接删除 `.ui` 中的四个旧视频容器。旧容器目前仍承担 slot 分配、布局显示、点击激活和 active 高亮职责，需要先迁移这些 UI 逻辑。

## 生命周期复核

### start / pause

播放启动和暂停已经由 `MediaPipeline` 统一控制 scheduler，`VideoRender` 不再参与播放状态转换。

### seek

seek 前会暂停播放管线并清理当前 sink 显示帧。统一渲染路径下，清理动作会经 `SingleStreamFrameSinkAdapter` 到达 `MultiViewRenderer::ClearFrame(streamId)`。

### stop

stop 会暂停管线、停止 scheduler、清理当前 sink，并根据媒体源是否可 seek 选择回到起点或实时源停止流程。

### shutdown

资源释放顺序已经收口：

1. 停止 position timer。
2. 请求队列停止。
3. 停止 demuxer / decoder / audio player。
4. 停止 `VideoFrameScheduler`。
5. 清理当前 sink。
6. 将 scheduler sink 置空。
7. 等待线程退出。
8. 释放基础设施对象、队列、同步管理器和媒体源。

该顺序避免 scheduler 在释放过程中继续向已销毁 sink 投递帧。

## 验收判断

### Phase 3.10 是否完成

完成。

原因：

- 调度职责已经从 `VideoRender` 移到 `VideoFrameScheduler`。
- `MediaPipeline` 已成为 scheduler 和 sink 的组合者。
- `VideoRender` 已降级为默认 sink。
- `MultiViewRenderer` 多路路径仍通过 `IVideoFrameSink` 接收可显示帧。
- UI 全面替换的前置条件和剩余风险已经单独复核。

### 当前调度、sink、绘制边界是否清晰

清晰。

- 调度：`VideoFrameScheduler`
- 单路播放管线组合：`MediaPipeline`
- sink 接口：`IVideoFrameSink`
- 默认绘制：`VideoRender`
- 多路绘制：`MultiViewRenderer`
- StreamId 适配：`SingleStreamFrameSinkAdapter`

### 是否可以进入 UI 全面替换旧容器

可以进入下一阶段规划和小步实施。

但第一步不应直接删除旧容器，而应先把旧容器承担的逻辑职责迁移为 MainWindow 的逻辑 slot 状态，并让 `MultiViewRenderer` 承担显示、点击和 active 高亮。

## 剩余限制

- `MultiViewRenderer` 和 `VideoRender` 当前仍主要支持 `AV_PIX_FMT_YUV420P`，不解决 4K MKV 色彩、格式覆盖和硬件解码输出格式问题。
- 主窗口中的四个旧视频容器仍存在，且仍参与 slot 分配和 UI 布局逻辑。
- `MediaPipeline::Initialize(..., QWidget* rendererContainer, IVideoFrameSink*)` 仍保留默认容器参数，用于默认 `VideoRender` 回退路径。
- 本报告只做静态验收，不替代用户运行验证。

## 下一步建议

进入 UI 全面转向 `MultiViewRenderer` 的下一阶段，建议拆成以下顺序：

1. 用逻辑 slot 管理替代 `VideoContainers()` 容量判断。
2. 让主窗口常规播放路径不再依赖旧视频容器参数。
3. 让 `RefreshMultiViewLayout()` 只刷新 `MultiViewRenderer`。
4. 用 `MultiViewRenderer::StreamClicked` 完全替代旧容器点击激活。
5. 用 `MultiViewRenderer` active 边框替代旧容器 stylesheet 高亮。
6. 最后删除 `.ui` 中旧四容器和相关辅助方法。

## 静态检查

本验收任务应执行：

```powershell
git diff --check -- doc\Phase3_10_VideoFrameScheduler抽取验收报告.md doc\Phase3_10TODO.md
```

不进行编译和运行验证。
