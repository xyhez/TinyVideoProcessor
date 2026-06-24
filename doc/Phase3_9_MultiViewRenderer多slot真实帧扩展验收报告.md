# Phase 3.9 MultiViewRenderer 多 slot 真实帧扩展验收报告

## 验收结论

Phase 3.9 的目标是把 Phase 3.8 已验证的 slot 0 单路真实帧路径扩展到多个可见 slot，让第二路及后续路也能通过 `SingleStreamFrameSinkAdapter` 进入 `MultiViewRenderer`。

当前结论：

- 多 slot 真实帧接入链路已经成立。
- `MultiViewRenderer` 不再只绘制 slot 0，而是按当前可见 viewport 的 `StreamId` 查找对应渲染状态并绘制。
- 多路文件和摄像头流都可以通过统一的 frame sink factory 创建对应 stream 的 adapter。
- 外部 sink 路径下，旧 `VideoRender` widget 不再作为可见显示区域加入布局，只保留单路帧调度职责。
- stop、seek、错误退出和资源释放路径已经补齐当前视频输出帧清理，避免统一渲染区域长期残留旧帧。
- `StreamId` 仍未进入 `MediaPipeline`，`MultiViewRenderer` 仍不访问 `VFrame_` 或 `SyncManager`。

因此 Phase 3.9 可以收尾。下一阶段建议进入 `VideoFrameScheduler` 抽取前的设计确认，或者先做一次多路统一渲染人工验收记录。

## 当前真实链路

当前多 slot 真实帧链路为：

```text
MultiStreamManager 分配 StreamId
    -> MainWindow 创建 SingleStreamFrameSinkAdapter(streamId)
        -> MediaPipeline 创建 VideoRender 并设置 external frame sink
            -> VideoRender 继续负责单路取帧、同步判断和丢帧追赶
                -> IVideoFrameSink::PresentFrame(frame)
                    -> SingleStreamFrameSinkAdapter::PresentFrame(frame)
                        -> MultiViewRenderer::PresentFrame(streamId, frame)
                            -> renderStates_[streamId]
                            -> viewSlots_[slot] -> streamId
                            -> DrawViewport(viewport)
                            -> DrawStreamFrame(viewport, state)
```

这里的职责边界保持不变：

- `MediaPipeline` 只负责单路播放管线。
- `VideoRender` 暂时负责单路视频帧调度。
- `SingleStreamFrameSinkAdapter` 负责把无 `StreamId` 的 sink 调用适配为带 `StreamId` 的统一渲染调用。
- `MultiViewRenderer` 只负责把已经允许显示的帧绘制到对应 slot。

## 验收项

### 1. 多路 frame sink 接入

状态：静态验收通过。

说明：

- `MainWindow::CreateMultiViewFrameSinkFactory()` 统一创建 `SingleStreamFrameSinkAdapter`。
- `AddMultiStreamFiles()` 为新增文件流使用 `AddStreamWithFrameSinkFactory(...)`。
- `AddCameraStream()` 为摄像头流使用 `AddStreamWithFrameSinkFactory(...)`。
- `SwitchToPlaylistIndex()` 为播放列表切换路径使用 `SwitchSourceWithFrameSinkFactory(...)`。

结论：

第二路及后续 stream 不再被 UI 层 slot 0 条件限制挡住，均可获得对应 `StreamId` 的统一渲染 sink。

### 2. 多 slot 绘制

状态：静态验收通过。

说明：

- `MultiViewRenderer::DrawViewport()` 当前按 `viewport.IsBound()` 判断是否查找 `renderStates_`。
- 已移除 `viewport.slotIndex == 0` 的真实帧绘制限制。
- 绘制入口仍按 `Viewports()` 遍历可见 slot，而不是直接遍历 `renderStates_`。

结论：

多 slot 绘制逻辑符合 Phase 3.9 方案：可见 slot 决定绘制区域，`StreamId` 决定取哪一路帧状态。

### 3. 默认 VideoRender 回退路径

状态：保留。

说明：

- `MediaPipeline::CreatePipelineResources(...)` 仅在 `videoFrameSink == nullptr` 时把 `VideoRender` widget 加入原有容器。
- 外部 sink 存在时，`VideoRender` 被隐藏，只作为调度组件存在。

结论：

默认无外部 sink 播放路径仍然可用；统一渲染路径不会造成同一路视频同时显示在旧 widget 和 `MultiViewRenderer`。

### 4. remove / stop / seek / shutdown 生命周期

状态：静态验收通过。

说明：

- stream remove 时，`MainWindow` 响应 `StreamRemoved` 并调用 `multiViewRenderer_->DetachStream(id)`。
- `DetachStream(streamId)` 会释放该 stream 当前帧、OpenGL 纹理并移除 `renderStates_`。
- stop/reset 路径中，`MediaPipeline` 调用 `videoRender_->ClearFrame()` 清理当前输出帧。
- seek 路径中，管线挂起成功后先清理旧输出帧，再清空队列并 seek。
- 资源释放和错误退出路径也会清理当前视频输出帧。
- external sink 路径下，`VideoRender::ClearFrame()` 会转发到 `SingleStreamFrameSinkAdapter::ClearFrame()`，最终调用 `MultiViewRenderer::ClearFrame(streamId)`。

结论：

生命周期清理链路已经闭合，旧帧残留风险从管线输出侧和 renderer detach 侧同时收口。

### 5. renderer 销毁与 queued frame

状态：静态验收通过，但仍建议人工压力验证。

说明：

- `SingleStreamFrameSinkAdapter` 使用 `QPointer<MultiViewRenderer>` 保存 renderer。
- renderer 不存在或 `StreamId` 无效时，`PresentFrame()` 会释放传入 `AVFrame*`。
- renderer 不存在或 `StreamId` 无效时，`ClearFrame()` 直接返回，不访问悬空对象。

结论：

adapter 层具备基本悬空访问保护。后续如果引入更复杂跨线程 queued 投递，仍需要单独复核投递失败和对象销毁顺序。

## 当前限制

### 1. 暂只支持 YUV420P

`MultiViewRenderer` 当前只处理 `AV_PIX_FMT_YUV420P`，并使用 Y/U/V 三纹理 shader 绘制。

非 YUV420P、NV12、P010、10-bit、HDR、BT.2020、limited range 等格式仍不在 Phase 3.9 范围内。

### 2. 尚未抽出 VideoFrameScheduler

`VideoRender` 仍承担：

- 从 `VFrame_` 取帧。
- 根据 `SyncManager` 判断显示、等待、丢帧。
- 刷新视频时钟。
- 把可显示帧转交给外部 sink 或默认 widget。

这仍是过渡状态。后续要彻底统一渲染架构，应把这些调度能力抽成独立 `VideoFrameScheduler`，再让 scheduler 输出到 `IVideoFrameSink`。

### 3. 性能未专项优化

Phase 3.9 只完成多 slot 真实帧链路，不处理：

- 多路纹理复用策略优化。
- 零拷贝渲染。
- 硬解码帧直接渲染。
- 多路 1080P / 4K 压力指标。

### 4. 人工验收结果仍需补齐

本报告基于当前代码静态验收和用户前序运行反馈整理。建议补齐以下人工记录：

```text
1. 两路文件同时播放，确认两个 slot 均进入 MultiViewRenderer。
2. 三路或四路文件同时播放，确认各 slot 画面不串位。
3. 文件 + 摄像头混合播放，确认两类媒体源都进入统一渲染区域。
4. 删除第二路，确认对应 slot 不残留最后一帧。
5. stop all 后确认统一渲染区域清空。
6. pause all 后关闭程序，确认不阻塞。
7. seek 后确认旧帧不会长期残留。
8. 播放完成后关闭程序，确认不阻塞。
```

## 架构判断

Phase 3.9 的实现符合 `doc/架构指导.md` 的 Phase 3 多路渲染方向。

成立点：

- 多路显示能力向 `MultiViewRenderer` 收敛。
- 单路播放管线没有持有 `StreamId`。
- `MultiViewRenderer` 没有接管同步、取帧或队列消费。
- `IVideoFrameSink` 成为播放调度和最终渲染之间的稳定接口。
- 旧 `VideoRender` widget 的可见职责被收口，后续可以继续向 scheduler 拆分。

当前仍不建议让 `MultiViewRenderer` 直接读取 `VFrame_` 或 `SyncManager`。原因是这会把多路布局渲染和单路播放调度耦合起来，后续摄像头、网络流、无音频流和多路音频策略都会更难维护。

## 下一阶段建议

建议下一阶段不要继续堆 `MultiViewRenderer` 功能，而是先处理调度边界：

```text
Phase 3.10 VideoFrameScheduler 抽取预研与实施
```

推荐顺序：

1. 先写 `VideoFrameScheduler` 职责边界文档。
2. 明确它从 `VideoRender` 中接管哪些能力：取帧、同步判断、丢帧、视频时钟刷新。
3. 明确它不负责 OpenGL 绘制，不持有 `StreamId`。
4. 让 `MediaPipeline` 组合 scheduler + `IVideoFrameSink`。
5. 最后再让 `VideoRender` 回归纯默认 widget sink，或作为单路显示 sink 存在。

这样可以把当前“`VideoRender` 既是调度器又是 widget”的过渡状态拆干净。

## 结论

Phase 3.9 已完成其阶段目标：多路真实帧可以通过 `IVideoFrameSink` / `SingleStreamFrameSinkAdapter` 进入同一个 `MultiViewRenderer`，并按 slot 映射绘制到对应区域。

当前剩余问题不是多 slot 接入链路问题，而是更深一层的职责拆分问题：`VideoRender` 内部调度能力需要从可见 widget 中抽离。

因此 Phase 3.9 可以收尾，后续应进入 `VideoFrameScheduler` 抽取，或先进行一次多路统一渲染人工验收记录补充。
