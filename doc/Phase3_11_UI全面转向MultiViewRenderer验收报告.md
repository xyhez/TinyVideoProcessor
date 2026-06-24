# Phase 3.11 UI 全面转向 MultiViewRenderer 验收报告

## 验收结论

Phase 3.11 已完成。

主窗口常规视频显示路径已经从旧的多个 `QWidget` 视频容器切换为单个 `MultiViewRenderer` 统一承载。旧 `video_widget*` 容器及其辅助方法已经从 `MainWindow` 和 `.ui` 文件中清理，主窗口的多路画面布局、active 高亮和点击激活都已收口到 `MultiViewRenderer`。

本阶段符合 `doc/架构指导.md` 中 Phase 3 对“单个 OpenGL 上下文 + 多纹理”的演进方向，也保持了当前架构边界：`MultiViewRenderer` 只负责按 `StreamId -> slot -> viewport` 绘制画面，不负责同步、取帧、解码或队列消费。

## 最终视频显示链路

当前主窗口常规播放显示链路为：

```text
MainWindow
    -> 逻辑 slot 管理
    -> MediaPlayController
        -> MultiStreamManager
            -> MediaPipeline
                -> VideoFrameScheduler
                    -> IVideoFrameSink
                        -> SingleStreamFrameSinkAdapter
                            -> MultiViewRenderer
```

各层职责保持如下：

- `MainWindow`：负责 UI 交互、逻辑 slot 分配、active stream 状态同步和用户操作入口。
- `MediaPlayController`：继续作为 UI 层进入应用层播放能力的统一入口。
- `MultiStreamManager`：负责多路 stream / pipeline 的生命周期管理。
- `MediaPipeline`：负责单路媒体播放管线，不感知 UI slot 和 `StreamId`。
- `VideoFrameScheduler`：负责单路取帧、同步判断、丢帧和调度，不感知 UI slot。
- `SingleStreamFrameSinkAdapter`：负责把单路 scheduler 输出的帧适配到指定 `StreamId`。
- `MultiViewRenderer`：负责接收已允许显示的帧，并按 slot 绘制到统一 OpenGL 区域。

## 旧 UI 容器删除结果

本阶段已清理旧的多 `QWidget` 视频容器路径：

- `.ui` 中已删除 `video_widget`、`video_widget_1`、`video_widget_2`、`video_widget_3`。
- `MainWindow` 中已删除 `VideoContainers()`、`PrimaryVideoContainer()`、`VideoContainerAt()`。
- `MainWindow` 中已删除旧容器事件过滤、点击激活和 stylesheet 高亮逻辑。
- `MainWindow` 中的布局刷新不再通过显示/隐藏旧容器实现，而是同步 `MultiViewRenderer` 的布局、slot 映射和 active stream。

当前主视频区域由 `widget_videoArea` 承载 `MultiViewRenderer`。旧容器不再参与常规显示、容量判断、点击激活或 active 高亮。

## 逻辑 Slot 与 MultiViewRenderer Slot

当前逻辑 slot 仍由 `MainWindow` 管理，第一版保持 4 个逻辑 slot：

```text
slot 0
slot 1
slot 2
slot 3
```

`MainWindow` 维护 `StreamId -> slotIndex` 映射，并在刷新布局时转换为 `MultiViewRenderer::SetViewSlots()` 所需的 `slotIndex -> StreamId` 映射。

`MultiViewRenderer` 不决定哪个 stream 应该进入哪个 slot，也不参与容量判断。它只读取外部传入的 slot 映射，在当前布局下计算每个 slot 的 viewport，并把对应 `StreamId` 的当前帧绘制到对应区域。

## Active Stream 与音频策略

点击画面的激活链路已经从旧容器事件过滤迁移到 `MultiViewRenderer::StreamClicked`：

```text
MultiViewRenderer::mousePressEvent
    -> 命中已绑定 viewport
    -> emit StreamClicked(streamId)
    -> MainWindow::ActivateStreamFromView(streamId)
    -> MediaPlayController active stream / active audio stream 同步
```

当前策略保持为：

- 点击有音频流的视频时，active stream 和 active audio stream 跟随切换。
- 点击无音频流的视频时，保持既有静音/无音频策略，不让无音频流强行产生不存在的音频播放路径。
- active 高亮由 `MultiViewRenderer` 绘制边框，不再依赖旧容器 stylesheet。

## VideoRender Fallback 保留原因

本阶段没有删除 `VideoRender`，原因如下：

- `VideoRender` 仍是基础设施层默认单路显示 sink。
- `MediaPipeline` 和 `VideoFrameScheduler` 的抽象不应强制依赖 `MultiViewRenderer`。
- 当外部没有提供 frame sink factory 时，默认播放路径仍需要一个明确的 renderer container 回退入口。
- 保留 `VideoRender` 可以降低本阶段 UI 迁移对播放管线的影响，符合渐进式演进原则。

因此，`MultiViewRenderer` 是主窗口常规多路显示路径，`VideoRender` 是默认 sink fallback，不是本阶段删除对象。

## 本阶段附带修正

在主窗口全面切换到 `MultiViewRenderer` 后，发现高 DPI 环境下 OpenGL 绘制区域集中在左下角。该问题已通过统一坐标边界解决：

- Qt 逻辑坐标继续用于布局计算和鼠标命中。
- OpenGL `glViewport()` / `glScissor()` 使用 framebuffer 物理像素坐标。
- `MultiViewRenderer` 新增 framebuffer 尺寸和坐标转换入口，避免高 DPI 下 viewport 尺寸与实际 framebuffer 不一致。

该修正属于 `MultiViewRenderer` 内部坐标职责，不改变播放管线、同步链路或 UI 业务逻辑。

## 验证结果

已完成的静态检查：

```powershell
git diff --check -- src\ui\mainwindow.h src\ui\mainwindow.cpp src\ui\mainwindow.ui doc\Phase3_11TODO.md
rg -n "video_widget|VideoContainers|PrimaryVideoContainer|VideoContainerAt|InstallVideoContainerEventFilters|UpdateVideoContainerStyles" src\ui
git diff --check -- src\infrastructure\multiviewrenderer.h src\infrastructure\multiviewrenderer.cpp
```

已完成人工验证：

- 主窗口常规播放画面显示在 `MultiViewRenderer` 区域。
- 多路画面不再显示在旧 `QWidget` 容器中。
- `Grid2x2` 布局下多路画面位置正常。
- 高 DPI 下画面不再集中到左下角。
- 点击画面可以切换 active stream。

本报告任务不执行编译验证。

## 剩余限制

Phase 3.11 明确不处理以下内容：

- 非 YUV420P 格式转换。
- 4K MKV 色彩异常和宽高显示异常。
- 多路渲染性能优化。
- 更多布局模式，例如 3x3、4x4、1+5。
- 单路全屏独占显示的完整体验。
- 摄像头硬件接入的完整实机验收。
- RTSP / RTMP / HTTP 网络流。
- OpenCV 图像处理能力。

这些问题不属于旧 UI 容器迁移本身，应进入后续 Phase 单独拆分。

## 下一阶段建议

Phase 3.11 收尾后，建议进入 `MultiViewRenderer` 后续能力完善，优先顺序如下：

1. 补齐 Phase 3.11 的最终提交，并保持当前代码为稳定基线。
2. 进入下一阶段前先更新 TODO，明确下一阶段只处理一个主题。
3. 优先考虑布局能力扩展或 `MultiViewRenderer` 渲染能力增强，不建议同时混入播放管线重构。

如果继续沿 Phase 3 推进，下一阶段应围绕 `MultiViewRenderer` 的布局能力、全屏能力或渲染格式能力选择一个方向，避免把 UI、OpenGL、解码格式和性能优化混在同一个提交中。
