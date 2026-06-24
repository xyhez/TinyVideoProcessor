# Phase 3.10 UI 全面转向 MultiViewRenderer 复核报告

## 复核目标

本报告复核在 `VideoFrameScheduler` 抽取完成后，UI 是否具备从旧的多个 `VideoRender` / 多个视频容器路径，全面转向单个 `MultiViewRenderer` 的条件。

本轮只做静态复核，不修改播放代码，不运行程序。

## 当前结论

可以进入下一步“UI 全面转向 `MultiViewRenderer`”的实施规划，但不建议直接删除 `.ui` 中的四个旧视频容器。

原因是旧视频容器目前已经不再是统一渲染路径下的真实视频显示目标，但仍承担 UI 层的逻辑职责：

- 作为 `MainWindow` 中 slot 容量判断的依据。
- 作为 `streamContainerIndexes_` 中 `StreamId -> slotIndex` 的映射来源。
- 作为 single / 2x2 布局下显示隐藏、边框高亮和点击激活的事件载体。
- 作为未注入外部 sink 时默认 `VideoRender` 回退路径的容器。

因此，下一步可以替换旧 UI 容器，但必须先把这些逻辑职责迁出，而不是直接删控件。

## 当前播放与渲染链路

### 统一渲染路径

当前文件、多路和摄像头路径已经可以通过外部 `IVideoFrameSink` 进入 `MultiViewRenderer`：

```text
MainWindow
    -> CreateMultiViewFrameSinkFactory()
        -> SingleStreamFrameSinkAdapter(streamId)
            -> MultiViewRenderer::PresentFrame(streamId, frame)

MediaPlayController
    -> MultiStreamManager
        -> MediaPipeline
            -> VideoFrameScheduler
                -> IVideoFrameSink::PresentFrame(frame)
```

这个链路满足当前架构边界：

- `MediaPipeline` 不知道 `StreamId`。
- `VideoFrameScheduler` 不知道 `MultiViewRenderer`。
- `MultiViewRenderer` 不访问 `VFrame_` 或 `SyncManager`。
- `SingleStreamFrameSinkAdapter` 是 `StreamId` 注入统一渲染器的适配边界。

### 默认回退路径

当没有传入外部 sink 时，`MediaPipeline` 仍会创建 `VideoRender` 作为默认单路显示 sink：

```text
MediaPipeline
    -> VideoFrameScheduler
        -> VideoRender::PresentFrame(frame)
```

这个路径仍然有保留价值：它是单路默认显示能力和回退能力。但在主窗口现有流程里，`MultiViewRenderer` 初始化成功后，文件列表切换、多路文件和摄像头添加都会优先使用 `CreateMultiViewFrameSinkFactory()`。

## 场景复核

### 播放列表切换

`MainWindow::SwitchToPlaylistIndex()` 会优先调用 `SwitchSourceWithFrameSinkFactory(...)`。当 `multiViewRenderer_` 存在时，新流通过 `SingleStreamFrameSinkAdapter` 进入统一渲染器。

现状判断：

- 播放列表路径已具备统一渲染条件。
- 当前仍把 `PrimaryVideoContainer()` 传入 Controller，但在外部 sink 路径下 `MediaPipeline` 不再创建隐藏 `VideoRender`，该参数主要变成兼容参数。
- 后续可把 UI 层的默认容器依赖收口为可空或移除。

### 多路文件

`MainWindow::AddMultiStreamFiles()` 会通过 `AddStreamWithFrameSinkFactory(...)` 添加文件流。每个新增流都能获得对应 `StreamId` 的 `SingleStreamFrameSinkAdapter`。

现状判断：

- 多路文件已具备统一渲染条件。
- `FirstAvailableVideoContainerIndex()` 和 `BindStreamToVideoContainer()` 仍依赖旧四个容器来分配 slot。
- 后续应把 slot 分配从“找空闲 QWidget 容器”改为“找空闲逻辑 slot”。

### 摄像头

`MainWindow::AddCameraStream()` 与多路文件路径一致，也优先通过 `AddStreamWithFrameSinkFactory(...)` 注入统一 sink。

现状判断：

- 摄像头接入统一渲染的架构条件已经具备。
- 摄像头实际设备验证仍取决于硬件和驱动，不属于本轮静态复核范围。
- Phase2 早期文档中“摄像头第一版不接入 MultiViewRenderer”的判断已经被 Phase3 后续演进覆盖，后续应以当前 `IVideoFrameSink` / `VideoFrameScheduler` 边界为准。

### 无音频流

无音频流由 `MediaPipeline` 根据 `sourceInfo.hasAudio_` 设置 `syncManager` 为 `SYNCTYPE::VIDEO`，并且 `VideoFrameScheduler` 在 VIDEO sync 模式下刷新视频主时钟。

现状判断：

- 无音频流不依赖旧 UI 容器。
- 其显示目标只取决于当前 sink：有外部 sink 则进入 `MultiViewRenderer`，无外部 sink 则进入默认 `VideoRender`。

### seek / stop all / pause all / shutdown

`MediaPipeline` 当前已经显式持有并控制 `VideoFrameScheduler`：

- `Start()` 调用 scheduler `Start()`。
- `Pause()` 调用 scheduler `Pause()`。
- `SeekTo()` 调用 scheduler `ClearOutputFrame()` 清理当前 sink。
- `ReleaseResources()` 停止 scheduler、清理 sink，并将 sink 置空。

现状判断：

- 生命周期控制已经从 `VideoRender` 转移到 scheduler。
- 统一渲染路径下，`ClearOutputFrame()` 会通过 adapter 清理 `MultiViewRenderer` 中对应 stream 的当前帧。
- 旧 UI 容器不再参与这些生命周期动作。

## 旧 UI 容器当前职责

`.ui` 中的四个旧容器包括：

- `video_widget`
- `video_widget_1`
- `video_widget_2`
- `video_widget_3`

它们当前仍被以下逻辑使用：

- `VideoContainers()` 返回这四个容器。
- `FirstAvailableVideoContainerIndex()` 用它们判断是否还有可用 slot。
- `BindStreamToVideoContainer()` 将 `StreamId` 绑定到容器索引。
- `RefreshMultiViewLayout()` 根据 single / 2x2 布局显示隐藏容器。
- `UpdateVideoContainerStyles()` 通过容器边框显示 active stream。
- `InstallVideoContainerEventFilters()` 和 `HandleVideoContainerClick()` 通过容器点击切换 active stream。

这些职责不是视频渲染职责，而是 UI slot 管理职责。后续应迁移为：

- `MainWindow` 保存 `StreamId -> slotIndex` 的纯逻辑映射。
- `MultiViewRenderer` 负责显示布局、active 边框和点击命中。
- `MainWindow` 不再通过旧 QWidget 容器判断可用 slot。
- `MainWindow` 不再通过旧 QWidget 容器处理点击激活。

## 是否可以删除旧 UI 容器

当前不建议直接删除。

可以删除的前置条件：

- `FirstAvailableVideoContainerIndex()` 改为基于逻辑 slot，而不是 QWidget 容器。
- `VideoContainerAt()` 和 `VideoContainers()` 不再作为多路添加的必要入口。
- `RefreshMultiViewLayout()` 不再显示隐藏四个旧容器，而是只刷新 `MultiViewRenderer` 布局。
- `UpdateVideoContainerStyles()` 被 `MultiViewRenderer` 的 active stream 边框替代。
- `HandleVideoContainerClick()` / `ActivateStreamFromVideoContainer()` 被 `MultiViewRenderer::StreamClicked` 完全替代。
- 播放列表、文件多路、摄像头添加都不再需要给 `MediaPipeline` 传入旧容器。
- 默认单路回退路径仍保留在 `MediaPipeline + VideoRender`，但不作为主窗口常规路径。

## 建议的后续实施顺序

建议进入下一阶段，但要拆成小步：

1. 新增逻辑 slot 管理接口，替代 `VideoContainers()` 作为容量来源。
2. 让文件多路、摄像头和播放列表切换在统一渲染路径下传入空默认容器或不再依赖默认容器。
3. 让 `RefreshMultiViewLayout()` 只驱动 `MultiViewRenderer`，不再操作旧容器显示隐藏。
4. 用 `MultiViewRenderer::StreamClicked` 完全替代旧容器点击激活。
5. 清理 `.ui` 中四个旧视频容器及相关样式、事件过滤和辅助方法。
6. 保留 `VideoRender` 类作为默认 sink 回退能力，不从基础设施层删除。

## 剩余风险

- `MultiViewRenderer` 当前仍只支持 `AV_PIX_FMT_YUV420P`，不解决 4K MKV 色彩和格式覆盖问题。
- 全屏逻辑当前绑定 `MainWindow`，后续如果主显示区域变为 dock 中的 `MultiViewRenderer`，需要重新确认全屏目标。
- 旧容器删除会影响 UI 布局，需要一次性检查 `.ui`、`MainWindow` 布局初始化和控件启用逻辑。
- 默认 `VideoRender` 回退路径仍存在，后续文档需要明确它是 fallback，不是主 UI 显示路径。

## 复核结论

当前已经具备进入 UI 全面转向 `MultiViewRenderer` 的条件。

下一步不应继续修改 `VideoFrameScheduler` 或 `VideoRender`，而应进入“旧 UI 容器替换”任务：把旧 QWidget 容器承担的 slot 管理、布局显示、点击激活和 active 高亮职责迁移到 `MainWindow` 的逻辑 slot 状态与 `MultiViewRenderer`，最后再删除旧容器。
