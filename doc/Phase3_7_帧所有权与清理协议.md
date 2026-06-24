# Phase 3.7 帧所有权与清理协议

## 阶段目标

本文档用于明确真实视频帧接入前的 `AVFrame*` 所有权和清理顺序。

当前阶段仍然不改代码。目标是先把协议写清楚，后续实现 `IVideoFrameSink`、拆分 `VideoRender`、接入 `MultiViewRenderer` 时必须按这个协议推进。

需要解决的问题：

```text
1. AVFrame* 从 VFrame_ 弹出后由谁释放？
2. 可显示帧交给 sink 后所有权是否转移？
3. seek / stop / remove stream / shutdown 时如何避免旧帧回显？
4. 如何避免重复释放、漏释放和已释放对象访问？
```

## 总规则

第一版采用最简单、最明确的裸指针所有权规则：

```text
谁从 VFrame_ 弹出 AVFrame*，谁负责保证它最终被释放。
```

如果弹出的帧不再显示：

```text
调度器立即 av_frame_free(&frame)
```

如果弹出的帧要显示：

```text
调度器调用 sink->PresentFrame(frame)
PresentFrame 调用后，AVFrame* 所有权转移给 sink
```

这意味着：

- 调度器不能在 `PresentFrame(frame)` 后继续访问或释放该帧。
- sink 必须在替换、清理、detach 或析构时释放当前持有帧。
- 同一个裸 `AVFrame*` 不能同时被调度器和 sink 持有。
- 不允许多个 sink 共享同一个裸 `AVFrame*`。

本阶段不引入：

- `std::shared_ptr<AVFrame>`。
- 自定义 RAII `VideoFrame`。
- 引用计数帧包装。
- 多消费者共享帧。

这些可以作为后续独立任务，但不能混入第一版真实帧接入。

## 当前现状

当前 `VideoRender` 已经隐含了类似规则。

过晚帧：

```text
TryPop(drop)
av_frame_free(&drop)
```

可显示帧：

```text
TryPop(newFrame)
av_frame_free(&m_displayFrame)
m_displayFrame = newFrame
update()
```

析构：

```text
StopRendering()
av_frame_free(&m_displayFrame)
```

也就是说，当前 `VideoRender` 从队列弹出可显示帧后，会接管 `newFrame` 的所有权。后续 `IVideoFrameSink::PresentFrame()` 应保留这个语义。

## 单帧流转协议

### 队首检查

队首检查只允许查看，不允许接管所有权：

```text
Front(nextFrame)
```

规则：

- `Front()` 得到的指针仍属于队列。
- 如果帧过早，必须保持在队列中。
- 不能对 `Front()` 得到的帧调用 `av_frame_free()`。
- 不能把 `Front()` 得到的帧交给 sink。

原因：

```text
过早帧需要等待后重新判断，如果提前弹出会破坏调度语义。
```

### 过晚帧

过晚帧由调度器弹出并释放：

```text
TryPop(drop)
av_frame_free(&drop)
ScheduleNextFrame(0)
```

规则：

- 过晚帧不进入 sink。
- 过晚帧不触发绘制。
- 弹出方负责释放。
- 丢帧计数可以在释放后递增。

目的：

```text
追赶主时钟，避免视频落后时继续显示过期帧。
```

### 可显示帧

可显示帧由调度器弹出并转交 sink：

```text
TryPop(newFrame)
sink->PresentFrame(newFrame)
```

规则：

- `TryPop()` 成功后，调度器临时拥有该帧。
- `PresentFrame()` 调用完成后，所有权转移给 sink。
- 调度器不能再释放 `newFrame`。
- sink 必须负责释放旧 current frame。
- sink 必须保存或消费新 current frame。

推荐 sink 内部逻辑：

```text
PresentFrame(frame)
    av_frame_free(&currentFrame)
    currentFrame = frame
    hasFrame = true
    update()
```

## Sink 清理协议

### PresentFrame

`PresentFrame()` 的所有权协议：

```text
输入：调度器已经判定可显示的 AVFrame*
输出：sink 接管该 AVFrame*
```

必须满足：

- 接收 `nullptr` 时应直接忽略或断言失败，不能进入 current frame。
- 替换旧帧前必须释放旧 current frame。
- 新帧进入 current frame 后，sink 成为唯一释放责任方。
- `PresentFrame()` 不负责 seek、flush decoder 或清队列。

### ClearFrame

`ClearFrame()` 的清理协议：

```text
释放 sink 当前持有帧，并清空可见帧状态。
```

必须满足：

- `av_frame_free(&currentFrame)`。
- `currentFrame = nullptr`。
- `hasFrame = false`。
- 请求重绘空画面。

当前 `VideoRender` 后续可对应：

```text
av_frame_free(&m_displayFrame)
m_hasFrame = false
update()
```

后续 `MultiViewRenderer` 对某一路可对应：

```text
释放 stream current frame
标记 stream texture invalid
请求重绘对应 viewport
```

### 析构

sink 析构时必须：

- 停止自身调度或取消后续绘制请求。
- 释放 current frame。
- 释放自身纹理或渲染资源。

如果 sink 是 `QOpenGLWidget`，OpenGL 资源释放仍需遵守 Qt OpenGL 上下文规则，但 `AVFrame*` 释放不依赖 OpenGL 上下文。

## seek 清理顺序

seek 是最容易出现旧帧回显的场景。推荐顺序如下：

```text
1. 暂停 demuxer / decoder 管线并等待安全点
2. 停止视频帧调度
3. 调用 sink->ClearFrame()
4. ClearQueues()
5. demuxer->SeekTo(targetSeconds)
6. FlushDecoders()
7. 设置 decoder discardBefore
8. ClearQueues()
9. syncManager->SeekTo(targetMs)
10. 恢复 demuxer / decoder / 调度
```

关键约束：

- `ClearFrame()` 必须发生在恢复调度前。
- `ClearFrame()` 必须清掉当前显示帧，避免 seek 后旧画面继续停留。
- `ClearQueues()` 只能清掉队列内帧，不能清掉 sink 当前显示帧。
- `FlushDecoders()` 只能清掉解码器内部缓存，不能清掉 sink 当前显示帧。
- seek 失败时也要保证已暂停的管线能进入可解释状态，不能让 sink 持有与目标时刻无关的旧帧。

当前代码已有：

```text
SuspendPipelineAndWait()
ClearQueues()
demuxer->SeekTo()
FlushDecoders()
SetDiscardBefore()
ClearQueues()
syncManager->SeekTo()
```

后续真实 sink 协议落地时，需要补齐显式 `ClearFrame()` 调用点。

## stop 清理顺序

普通文件源 stop 推荐顺序：

```text
1. 停止视频帧调度
2. 暂停或停止音频输出
3. 调用 sink->ClearFrame()
4. ClearQueues()
5. demuxer->SeekTo(0)
6. FlushDecoders()
7. ClearQueues()
8. syncManager->SeekTo(0)
9. 保持 pipeline 可再次 Start
```

实时源 stop 推荐顺序：

```text
1. 停止视频帧调度
2. 暂停或停止音频输出
3. 调用 sink->ClearFrame()
4. ClearQueues()
5. FlushDecoders()
6. ClearQueues()
7. syncManager->SeekTo(0)
8. 不执行 demuxer seek
```

关键约束：

- stop 后不能继续显示 stop 前的 current frame。
- 清队列不能替代 `ClearFrame()`。
- decoder flush 不能替代 `ClearFrame()`。
- stop 后再次 start 时，第一帧必须来自新的解码输出。

当前代码已有 `StopRendering()`、`ClearQueues()`、`FlushDecoders()` 和时钟重置。后续需要在 sink 协议落地后补齐显式 current frame 清理。

## remove stream 清理顺序

remove stream 发生在多路管理场景中。推荐顺序：

```text
1. Controller / Manager 发起 RemoveStream(streamId)
2. 停止对应 MediaPipeline 的 demuxer / decoder / frame scheduler
3. 调用该路 sink->ClearFrame()
4. 如果是 MultiViewRenderer，调用 DetachStream(streamId)
5. 释放该路纹理状态和 current frame
6. 释放 MediaPipeline 资源
7. 从 Manager 删除 StreamId 映射
8. MainWindow 释放 UI slot 绑定
```

关键约束：

- `MediaPipeline` 不应知道自己的 `StreamId`。
- `DetachStream(streamId)` 不应进入单路 `MediaPipeline`。
- 多路 `MultiViewRenderer` 的 stream 清理由 Manager / Controller / 渲染适配层触发。
- remove stream 后，不能再有该路 queued frame 更新访问已释放 renderer 状态。

后续如果使用 adapter：

```text
SingleStreamFrameSinkAdapter(streamId)
    -> ClearFrame()
    -> MultiViewRenderer::ClearFrame(streamId)
    -> DetachStream(streamId)
```

adapter 析构前必须保证没有未处理的 `PresentFrame()` 投递。

## shutdown 清理顺序

shutdown 是释放范围最大的场景。推荐顺序：

```text
1. 停止 position timer
2. RequestStop 所有 packet/frame queue
3. 停止 demuxer
4. 停止 audio decoder / video decoder
5. 停止 audio player
6. 停止 video frame scheduler / VideoRender 调度
7. 调用 sink->ClearFrame()
8. 等待工作线程退出
9. 删除或 reset demuxer / decoder / audio player
10. 删除 sink / renderer
11. reset queues
12. reset syncmanager
13. close media source
```

关键约束：

- `RequestStop()` 和线程退出保证后续不再产生新帧。
- `ClearFrame()` 保证 renderer 不再持有旧帧。
- 删除 sink 前必须确保没有排队的 `PresentFrame()` 会访问它。
- reset `VFrame_` 前，队列内剩余帧由队列 deleter 释放。
- sink 当前帧不在 `VFrame_` 内，必须由 sink 释放。

当前代码中 `ReleaseResources()` 已经按“请求停止 -> 停基础设施 -> 停渲染 -> 等线程 -> 删除渲染器 -> reset 队列”的方向执行。后续 sink 协议落地时，应把 `ClearFrame()` 放在删除渲染器前，并确保它不会与 queued `PresentFrame()` 交叉。

## 队列内帧与 current frame 的区别

必须明确区分两类帧：

```text
VFrame_ 内的帧
    -> 由队列或弹出方释放

sink current frame
    -> 已经从 VFrame_ 弹出
    -> 不再属于队列
    -> 由 sink 释放
```

因此：

- `ClearQueues()` 只能释放队列内帧。
- `FlushDecoders()` 只能清理解码器内部缓存。
- `ClearFrame()` 才能释放 sink 当前显示帧。

这三者不能互相替代。

## queued 调用约束

第一版建议调度器和 sink 同线程调用，避免 queued 调用所有权问题。

如果后续必须跨线程投递：

```text
调度器弹出 frame
queued PresentFrame(frame)
```

必须额外设计：

- 投递后 frame 所有权归谁。
- sink 在投递执行前销毁时谁释放 frame。
- seek / stop 发生时如何取消旧投递。
- remove stream 后如何丢弃该 stream 的旧投递。
- 队列积压时是否丢弃旧帧。

在这些设计完成前，不允许后台线程直接投递裸 `AVFrame*` 给 UI sink。

## 不允许的做法

后续实现中应避免：

- `Front()` 后直接释放帧。
- `PresentFrame()` 后调度器继续访问 frame。
- sink 和调度器同时保存同一个裸 `AVFrame*`。
- 用 `ClearQueues()` 代替 `ClearFrame()`。
- 用 `FlushDecoders()` 代替 `ClearFrame()`。
- remove stream 后 renderer 仍接收该 stream 的旧帧。
- `MediaPipeline` 内部持有 `StreamId` 来调用多路 detach。
- `MultiViewRenderer` 直接从多个 `VFrame_` 取帧。

## 验收判断

后续代码实现必须满足：

- 每个 `AVFrame*` 任意时刻只有一个所有权责任方。
- 过晚帧由调度器释放。
- 可显示帧由 sink 释放。
- seek 后不会显示 seek 前的 current frame。
- stop 后不会停留旧画面。
- remove stream 后不会访问已释放帧或纹理。
- shutdown 后没有 queued frame 更新访问已释放对象。

## 协议结论

第一版真实帧接入应遵循：

```text
队列内帧由队列或弹出方负责。
过晚帧由调度器释放。
可显示帧通过 PresentFrame 转移给 sink。
sink current frame 只能由 sink 释放。
seek / stop / remove / shutdown 必须显式 ClearFrame。
```

下一步可以进入 `VideoRender` 适配方案设计，明确如何在不破坏当前播放路径的前提下，让现有 `VideoRender` 先承接 `PresentFrame()` / `ClearFrame()` 语义。
