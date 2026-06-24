# Phase 3.7 MultiViewRenderer 单路真实帧接入计划

## 目标

本计划用于明确 `MultiViewRenderer` 后续如何先接入一路真实视频帧。

本阶段只写计划，不修改代码，不接真实帧。

## 当前基础

当前已经具备以下前置条件：

1. `VideoRender` 内部已经拆清取帧、同步判断、丢帧、显示帧接管和视频时钟刷新边界。
2. `IVideoFrameSink` 已经落地，提供单路可显示帧消费者接口。
3. `VideoRender` 已经作为默认 `IVideoFrameSink` 实现。
4. `MultiViewRenderer` 已经具备布局、slot、viewport 和点击命中能力。

当前 `MultiViewRenderer` 仍然只是统一渲染器空壳：

```text
MultiViewRenderer
    -> 管理 layout
    -> 管理 slot -> StreamId 映射
    -> 计算 viewport
    -> 绘制空 viewport
    -> 发出点击信号
```

它还没有：

- current frame 状态。
- OpenGL 纹理状态。
- `PresentFrame(streamId, frame)`。
- `ClearFrame(streamId)`。
- `DetachStream(streamId)`。
- YUV 纹理上传逻辑。

## 架构边界

单路真实帧接入必须继续保持以下边界：

```text
MediaPipeline
    -> 不知道 StreamId
    -> 不知道 MultiViewRenderer
    -> 只面对单路 IVideoFrameSink

SingleStreamFrameSinkAdapter
    -> 持有 StreamId
    -> 实现 IVideoFrameSink
    -> 把单路 PresentFrame(frame) 转成 MultiViewRenderer::PresentFrame(streamId, frame)

MultiViewRenderer
    -> 根据 StreamId 找 slot
    -> 持有每路 current frame / texture
    -> 在 OpenGL 上下文线程上传纹理并绘制到对应 viewport
```

不能出现：

```text
MediaPipeline
    -> StreamId

MultiViewRenderer
    -> VFrame_
    -> SyncManager
    -> demuxer / decoder
    -> active audio 策略
```

## 推荐最小路径

第一版单路真实帧接入推荐路径：

```text
MediaPipeline #1
    -> VideoFrameScheduler 或当前 VideoRender 内部调度逻辑
        -> IVideoFrameSink::PresentFrame(frame)
            -> SingleStreamFrameSinkAdapter(streamId=1)
                -> MultiViewRenderer::PresentFrame(streamId=1, frame)
                    -> 保存 current frame
                    -> 请求 update()
                    -> paintGL()
                        -> 找到 streamId 对应 slot
                        -> 上传纹理
                        -> 绘制到 viewport
```

注意：在真正引入 `VideoFrameScheduler` 前，当前 `VideoRender` 仍是默认路径。真实接入 `MultiViewRenderer` 时需要先决定如何让单路调度逻辑把可显示帧送到外部 sink。

## MultiViewRenderer 接帧入口

`MultiViewRenderer` 后续不应实现第一版单路 `IVideoFrameSink`。

原因：

- `IVideoFrameSink::PresentFrame(frame)` 不带 `StreamId`。
- `MultiViewRenderer` 是多路统一渲染器，必须知道帧属于哪一路。
- 如果让 `MultiViewRenderer` 直接继承单路 sink，就无法表达 stream 到 slot 的关系。

推荐给 `MultiViewRenderer` 增加多路语义入口：

```cpp
void PresentFrame(StreamId streamId, AVFrame* frame);
void ClearFrame(StreamId streamId);
void DetachStream(StreamId streamId);
```

语义：

- `PresentFrame(streamId, frame)`：接管该 stream 的可显示帧所有权，替换旧 current frame，并请求重绘。
- `ClearFrame(streamId)`：释放该 stream 当前显示帧，保留 stream 与 slot 绑定。
- `DetachStream(streamId)`：释放该 stream 当前显示帧和纹理状态，并移除该 stream 的渲染状态；slot 绑定由外层布局管理决定是否同步清除。

## SingleStreamFrameSinkAdapter

为了不让 `StreamId` 进入 `MediaPipeline`，后续应新增一个单路适配器：

```cpp
class SingleStreamFrameSinkAdapter : public IVideoFrameSink {
public:
    SingleStreamFrameSinkAdapter(MultiViewRenderer* renderer,
                                 MultiViewRenderer::StreamId streamId);

    void PresentFrame(AVFrame* frame) override;
    void ClearFrame() override;

private:
    MultiViewRenderer* renderer_ = nullptr;
    MultiViewRenderer::StreamId streamId_ = MultiViewRenderer::InvalidStreamId;
};
```

转发逻辑：

```text
PresentFrame(frame)
    -> renderer_->PresentFrame(streamId_, frame)

ClearFrame()
    -> renderer_->ClearFrame(streamId_)
```

该 adapter 是单路接口和多路渲染器之间的边界层。

## slot 0 的最小状态

第一版只接一路时，可以先固定使用 slot 0。

状态建议：

```text
streamId -> RenderState

RenderState
    -> AVFrame* currentFrame
    -> video width / height
    -> sample aspect ratio
    -> texture ids or QOpenGLTexture objects
    -> dirty flag
```

slot 0 显示逻辑：

```text
viewSlots_[0] = streamId
renderStates_[streamId].currentFrame != nullptr
paintGL()
    -> ViewportForSlot(0)
    -> 找到 streamId
    -> 找到 RenderState
    -> 上传或复用纹理
    -> 绘制到 viewport.glViewportRect
```

第一版不需要同时处理多个 stream 的真实帧，但数据结构应避免写死只能一路。可以先只验收一路，接口保留按 `StreamId` 索引的形式。

## OpenGL 线程要求

`QOpenGLWidget` 的 OpenGL 资源必须在其 OpenGL 上下文线程使用。

因此：

- `PresentFrame(streamId, frame)` 可以接管 `AVFrame*` 并保存到 pending/current 状态。
- OpenGL 纹理创建、释放、上传必须在 `initializeGL()` / `paintGL()` / 具备当前上下文的位置执行。
- 如果 `PresentFrame()` 从非 UI 线程调用，必须通过 Qt queued invocation 投递到 `MultiViewRenderer` 所在线程。

推荐约束：

```text
SingleStreamFrameSinkAdapter::PresentFrame(frame)
    -> 若当前线程不是 renderer 线程：
        -> queued invoke renderer->PresentFrame(streamId, frame)
        -> 所有权随 queued 调用转移
    -> 若当前线程是 renderer 线程：
        -> 直接调用 renderer->PresentFrame(streamId, frame)
```

这里存在一个所有权风险：如果 queued 调用投递后 renderer 已经销毁，frame 不能泄漏。

第一版实现前必须设计：

- renderer 销毁前 detach adapter。
- adapter 持有 `QPointer<MultiViewRenderer>`。
- 投递失败或 renderer 已销毁时释放 frame。

## 清理流程

### seek

seek 时应清理当前显示帧，避免旧帧回显。

推荐顺序：

```text
停止单路调度
清 packet/frame 队列
flush decoder
sink->ClearFrame()
重置 sync clock
恢复 demux / decode / schedule
```

对 `MultiViewRenderer` 路径：

```text
SingleStreamFrameSinkAdapter::ClearFrame()
    -> MultiViewRenderer::ClearFrame(streamId)
```

### stop

stop 时应释放当前显示帧，并让 slot 回到空画面或黑画面。

推荐：

```text
停止调度
停止音频
清队列
sink->ClearFrame()
重置时钟
保留 stream 与 slot 绑定
```

### remove stream

remove stream 不只是清当前帧，还要移除该路渲染状态。

推荐：

```text
停止 pipeline
adapter->ClearFrame()
MultiViewRenderer::DetachStream(streamId)
移除 streamId -> slot 绑定
删除 adapter
删除 pipeline
```

### shutdown

shutdown 时必须确保没有 queued 的 `PresentFrame` 再访问已销毁 renderer。

推荐：

```text
停止所有 pipeline 调度
使 adapter 失效
清理 queued 风险
DetachStream 所有 stream
销毁 MultiViewRenderer
```

## 单路接入的回退方案

第一版真实帧接入必须保留当前 `VideoRender` 路径作为回退。

推荐实现开关：

```text
默认路径：MediaPipeline -> VideoRender
实验路径：MediaPipeline -> SingleStreamFrameSinkAdapter -> MultiViewRenderer
```

如果实验路径出现：

- 黑屏。
- 旧帧回显。
- seek 后错帧。
- 关闭时崩溃。
- OpenGL 上下文错误。
- queued frame 泄漏。

应立即切回 `VideoRender` 路径定位，而不是继续推进多路真实帧。

## 真实实现前预计修改文件

后续真正实现单路真实帧接入时，预计至少会涉及：

- `src/infrastructure/multiviewrenderer.h`
- `src/infrastructure/multiviewrenderer.cpp`
- `src/infrastructure/singlestreamframesinkadapter.h`
- `src/infrastructure/singlestreamframesinkadapter.cpp`
- `CMakeLists.txt`
- `src/domain/MediaPipeline.h`
- `src/domain/MediaPipeline.cpp`
- `src/domain/MultiStreamManager.h`
- `src/domain/MultiStreamManager.cpp`
- `src/application/MediaPlayController.h`
- `src/application/MediaPlayController.cpp`
- `doc/TODO.md`

是否修改 `MediaPipeline::Initialize(...)` 需要单独确认。

推荐不要直接把 `StreamId` 塞进 `MediaPipeline`。如果 `MediaPipeline` 需要接外部 sink，应接收 `std::shared_ptr<IVideoFrameSink>` 或等价单路 sink，而不是接收 `StreamId` 或 `MultiViewRenderer`。

## 为什么先接一路

先接一路的原因：

1. 可以单独验证 OpenGL 纹理上传和 viewport 绘制。
2. 可以单独验证 `AVFrame*` 所有权转移。
3. 可以单独验证 queued 调用和关闭生命周期。
4. 可以避免同时引入多路 slot、active stream、active audio 和布局切换问题。
5. 可以保留当前多个 `VideoRender` 方案作为稳定回退。

一路稳定后，再扩展到多路才有明确基线。

## 验收路线

第一阶段验收：

- 只接一路文件视频。
- 只显示到 slot 0。
- `StreamId` 不进入 `MediaPipeline`。
- `MultiViewRenderer` 不访问 `VFrame_`。
- `MultiViewRenderer` 不访问 `SyncManager`。
- seek / stop / remove stream 不显示旧帧。
- 关闭程序不访问已释放 renderer。

第二阶段验收：

- 两路文件视频分别进入两个 slot。
- active stream 切换只影响 UI 和 active audio 策略。
- `MultiViewRenderer` 只按 streamId/slot 绘制。
- 多路同步仍由各自 pipeline 的调度器负责。

## 结论

`MultiViewRenderer` 单路真实帧接入不应从“把 `VFrame_` 交给统一渲染器”开始。

正确路线是：

```text
单路调度器
    -> IVideoFrameSink
        -> SingleStreamFrameSinkAdapter(streamId)
            -> MultiViewRenderer::PresentFrame(streamId, frame)
                -> streamId -> slot
                -> current frame / texture
                -> viewport 绘制
```

这条路线可以保持：

- `MediaPipeline` 单路职责。
- `StreamId` 留在多路管理和渲染适配层。
- `MultiViewRenderer` 只负责统一 OpenGL 绘制。
- 当前 `VideoRender` 路径可以作为回退基线。
