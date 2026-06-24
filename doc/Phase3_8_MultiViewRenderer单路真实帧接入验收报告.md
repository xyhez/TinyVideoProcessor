# Phase 3.8 MultiViewRenderer 单路真实帧接入验收报告

## 验收结论

Phase 3.8 的目标是验证 `MultiViewRenderer` 能通过 `SingleStreamFrameSinkAdapter` 接收一路真实视频帧，并把该路画面绘制到 slot 0。

当前结论：

- 单路真实帧接入成立。
- 默认 `VideoRender` 路径仍然保留，可作为回退路径。
- `StreamId` 没有进入 `MediaPipeline`，仍由 `MultiStreamManager` / UI / adapter 外层管理。
- 当前只支持第一路，也就是 slot 0 的真实帧进入 `MultiViewRenderer`。
- 第二路及后续路仍走旧的 `VideoRender` / 独立 `QOpenGLWidget` 路径，这是 Phase 3.8 的范围限制，不是本阶段缺陷。

因此 Phase 3.8 可以收尾。下一阶段应进入多 slot 真实帧扩展。

## 当前真实链路

当前 slot 0 实验路径为：

```text
MediaPipeline
    -> VideoRender 帧调度
        -> IVideoFrameSink
            -> SingleStreamFrameSinkAdapter(streamId)
                -> MultiViewRenderer::PresentFrame(streamId, frame)
                    -> StreamRenderState.currentFrame
                    -> Y/U/V texture
                    -> slot 0 viewport
```

这里 `VideoRender` 仍然承担单路内部职责：

- 从 `VFrame_` 取帧。
- 根据 `syncmanager` 判断显示、等待、丢帧。
- 在帧可以显示时把所有权转交给当前输出 sink。

`MultiViewRenderer` 不访问：

- `VFrame_`
- `SyncManager`
- demuxer
- decoder
- audio player

该边界符合 `doc/架构指导.md` 中单路 `MediaPipeline` 与多路渲染器解耦的方向。

## 验收项

### 1. 默认 VideoRender 路径

状态：保留。

说明：

- `MediaPipeline::Initialize(...)` 只新增可选 `IVideoFrameSink*` 参数。
- 当外部 sink 为空时，`VideoRender` 仍把可显示帧交给自身 `PresentFrame()`。
- 旧的独立 `QOpenGLWidget` 渲染路径没有被删除。

结论：

默认路径仍可作为回退路径。

### 2. slot 0 实验路径普通视频

状态：通过用户验证。

现象：

- 启动默认视频或切换播放列表视频后，第一路可以进入实验区域显示。
- 修正前播放列表路径仍走默认 `SwitchSource()`，导致画面仍显示在旧 `QOpenGLWidget`。
- 修正后 `SwitchSourceWithFrameSinkFactory(...)` 会为切换源创建 `SingleStreamFrameSinkAdapter`，slot 0 可以进入 `MultiViewRenderer`。

结论：

slot 0 单路真实帧接入有效。

### 3. 无音频视频

状态：待完整人工复测。

已有前提：

- 项目当前已经支持无音频流以 VIDEO sync 方式播放。
- Phase 3.8 没有修改音频选择策略。
- 实验路径只替换视频帧输出 sink，不接管音频。

建议复测流程：

```text
1. 添加一个无音频视频作为第一路。
2. 确认画面进入 MultiViewRenderer slot 0。
3. 确认没有音频流时不会触发 active audio 异常。
4. 执行 stop / remove / shutdown。
```

结论：

从架构和修改范围看不应破坏无音频路径，但仍需要人工复测结果补齐。

### 4. seek / stop / remove stream / shutdown

状态：需要继续人工覆盖。

当前代码边界：

- `Stop()` / `Seek()` 仍由 `MediaPipeline` 控制 demuxer、decoder、queue、sync。
- `VideoRender::ClearOutputFrame()` 会在外部 sink 存在时调用 `SingleStreamFrameSinkAdapter::ClearFrame()`。
- `SingleStreamFrameSinkAdapter::ClearFrame()` 会转发到 `MultiViewRenderer::ClearFrame(streamId)`。
- 移除 stream 时，`MainWindow` 会调用 `MultiViewRenderer::DetachStream(id)` 清理渲染状态。

建议复测流程：

```text
1. 播放第一路 slot 0 实验路径视频。
2. seek 到中间位置。
3. stop 后确认 slot 0 不残留错误帧。
4. remove stream 后确认不会访问已释放对象。
5. 播放中关闭程序。
6. 暂停后关闭程序。
```

结论：

静态链路已经具备清理路径，但必须用人工运行验证生命周期。

## 当前限制

### 1. 只支持 slot 0

当前代码有两个单路限制：

```text
MainWindow:
    只有 containerIndex == 0 时创建 SingleStreamFrameSinkAdapter

MultiViewRenderer:
    DrawViewport() 只在 viewport.slotIndex == 0 时绘制真实帧
```

结果：

```text
第一路 -> MultiViewRenderer slot 0
第二路 -> 默认 VideoRender / 旧 QOpenGLWidget
```

这符合 Phase 3.8 “只验证单路真实帧接入”的计划。

### 2. 只支持 YUV420P

当前 `MultiViewRenderer` 只上传和绘制 `AV_PIX_FMT_YUV420P`。

非 YUV420P 帧只输出诊断日志，不做格式转换。

后续如需支持 NV12、P010、YUVJ420P、RGB 等格式，应进入独立任务，不应混入 Phase 3.8 收尾。

### 3. 仍保留旧 VideoRender widget

即使 slot 0 使用外部 sink，`MediaPipeline` 当前仍创建 `VideoRender`，因为它还承担：

- 单路取帧调度。
- 音视频同步判断。
- 丢帧追赶。
- 默认回退渲染。

后续如果要彻底迁移到 `MultiViewRenderer`，需要先拆出独立 `VideoFrameScheduler`，不能直接删除 `VideoRender`。

### 4. queued 调用风险仍需后续强化

当前设计目标是验证单路实验路径。

后续多路真实帧扩展时需要重点确认：

- `PresentFrame(streamId, frame)` 是否总在 UI / OpenGL 线程调用。
- 如果跨线程投递，投递失败时谁释放 `AVFrame*`。
- renderer 销毁时是否可能还有未投递完成的 frame。

## 架构判断

当前 Phase 3.8 的架构方向是正确的：

- `MediaPipeline` 继续是单路管线。
- `StreamId` 没有进入 `MediaPipeline`。
- `MultiViewRenderer` 只接收已经完成调度判断的可显示帧。
- `SingleStreamFrameSinkAdapter` 承担单路 sink 到多路 renderer 的适配职责。
- `VideoRender` 默认路径保留，降低回退风险。

当前仍不应该直接让 `MultiViewRenderer` 访问 `VFrame_` 或 `SyncManager`。

原因：

- 取帧、同步、丢帧属于单路播放调度职责。
- 多路 renderer 应只负责把指定 stream 的显示帧绘制到指定 slot。
- 如果 renderer 直接拿队列，会把播放调度和多路布局耦合在一起，违背当前架构路线。

## 是否可以进入下一阶段

可以进入下一阶段，但下一阶段不应继续叫“单路接入”。

建议下一阶段目标：

```text
Phase 3.9 MultiViewRenderer 多 slot 真实帧扩展
```

建议拆分：

1. 取消 MainWindow 中 `containerIndex == 0` 的实验路径限制，让所有 stream 都可以创建 `SingleStreamFrameSinkAdapter`。
2. 取消 `MultiViewRenderer::DrawViewport()` 中 `slotIndex == 0` 的绘制限制。
3. 让 `MultiViewRenderer` 按 `viewport.streamId` 查找对应 `StreamRenderState` 并绘制到对应 slot。
4. 明确旧 `VideoRender` widget 在多路统一渲染阶段是隐藏、保留回退，还是后续逐步移除。
5. 补充多路 remove / shutdown / layout switch 的生命周期验证。

## 结论

Phase 3.8 已完成其本来目标：单路真实帧可以通过 adapter 进入 `MultiViewRenderer` 并绘制到 slot 0。

当前“第二路仍在旧区域播放”是阶段范围限制，不是 Phase 3.8 的失败。

下一步应从单路验证转入多路扩展，重点处理多 slot 绘制、旧 widget 过渡策略和跨线程帧投递生命周期。
