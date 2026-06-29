# 队列 RAII 重构：Frame 迁移方案

## 1. 目标

本方案用于指导后续 `AVFrame*` 队列迁移到 `AVFramePtr`，范围覆盖音频帧队列、视频帧队列、视频帧调度器、音频播放器和视频帧 sink。

目标不是改变同步算法、渲染算法或音频播放策略，而是把帧资源释放责任从“调用方记得手动 `av_frame_free()`”迁移为“所有权类型离开作用域自动释放”。

最终目标模型：

```cpp
using AVFramePtr = std::unique_ptr<AVFrame, AVFrameDeleter>;

SyncQueue<AVFramePtr>
IVideoFrameSink::PresentFrame(AVFramePtr frame)
```

所有权流向：

```text
decoder 创建输出帧
  -> Push(std::move(frame))
    -> 帧队列持有
      -> AudioPlayer / VideoFrameScheduler Pop(std::move(frame))
        -> 音频帧处理后自动释放
        -> 视频可显示帧 PresentFrame(std::move(frame))
          -> sink / renderer 持有当前显示帧
            -> 替换、ClearFrame、析构时自动释放
```

## 2. 当前链路复盘

### 2.1 decoder 输出帧

当前 `decoder` 从 packet 队列取包，解码得到 `AVFrame* outFrame`，再推入 `m_FrameVec`：

```text
decoder
  -> av_frame_alloc()
  -> m_FrameVec->Push(outFrame)
```

当前释放责任分散在多个路径：

- 解码失败或格式转换失败时，`decoder` 手动释放 `outFrame`。
- `Push()` 失败时，裸指针队列的外部 deleter 或局部手动释放负责兜底。
- 队列残留帧由 `SyncQueue` 的 `frameDeleter` 清理。

迁移后，`decoder` 应使用 `AVFramePtr` 表达输出帧所有权，入队成功后不再持有该帧。

### 2.2 AudioPlayer 消费音频帧

当前 `AudioPlayer` 持有 `std::shared_ptr<SyncQueue<AVFrame*>>`，播放循环中 `Pop(tmp)` 后使用 `tmp` 完成重采样、变速和写入音频设备。

当前每个提前退出、写入失败、处理完成路径都需要手动释放 `tmp`。

迁移后，`tmp` 应改为 `AVFramePtr`：

```cpp
AVFramePtr tmp;
if (!m_audioFrameQueue->Pop(tmp)) {
    ...
}

// 使用 tmp.get() 调用 FFmpeg API
// 处理完成或提前退出时自动释放
```

音频播放不需要把帧继续转交给其他模块，因此迁移风险低于视频显示链路。

### 2.3 VideoFrameScheduler 调度视频帧

当前 `VideoFrameScheduler` 负责：

- `PeekNextFrame()`：通过 `frameQueue_->Front(frame)` 查看队首帧但不出队。
- `EvaluateFrameTiming()`：根据队首帧 PTS 和同步时钟判断显示、等待或丢弃。
- `DropLateFrame()`：`TryPop(drop)` 后手动 `av_frame_free(&drop)`。
- `PopDisplayableFrame()`：`TryPop(frame)` 弹出可显示帧。
- `PresentDisplayableFrame()`：把可显示帧交给 `IVideoFrameSink::PresentFrame(AVFrame*)`。

迁移难点是 `Front()` 不适合 move-only 类型。`AVFramePtr` 不能被复制，所以不能继续把队首元素复制给调用方。

## 3. 队首观察方案

推荐使用 `SyncQueue::VisitFront()` 完成队首只读观察，并在锁内复制调度所需的最小字段，不把裸指针泄漏到锁外长期使用。

建议新增调度快照结构：

```cpp
struct FrameTimingSnapshot {
    int64_t pts = AV_NOPTS_VALUE;
};
```

`PeekNextFrame()` 不再输出 `AVFrame*`，而是输出快照：

```cpp
bool VideoFrameScheduler::PeekNextFrameTiming(FrameTimingSnapshot& snapshot)
{
    snapshot = {};
    if (!frameQueue_) {
        return false;
    }

    return frameQueue_->VisitFront([&snapshot](const AVFramePtr& frame) {
        if (!frame) {
            return;
        }
        snapshot.pts = frame->pts;
    }) && snapshot.pts != AV_NOPTS_VALUE;
}
```

这样做的原因：

- 不复制 `AVFramePtr`，符合 move-only 语义。
- 不把队首帧裸指针保存到锁外，降低并发清队列导致悬空指针的风险。
- 调度判断实际只需要 PTS，不需要访问整帧数据。
- `VideoFrameScheduler` 仍只负责同步判断，不接触渲染和多路布局。

如果后续需要更多字段，可以扩展快照结构，例如 `duration`、`best_effort_timestamp`，但不建议直接在锁外持有队首裸指针。

## 4. Sink 接口迁移方案

### 4.1 不推荐 `.release()` 兼容旧接口

不建议在 `VideoFrameScheduler` 内部这样做：

```cpp
frameSink_->PresentFrame(frame.release());
```

原因：

- 这会重新把释放责任交给裸指针调用约定。
- 一旦某个 sink 分支忘记释放，类型系统无法兜底。
- 与本轮 RAII 重构目标冲突。

### 4.2 推荐升级 IVideoFrameSink

推荐把 sink 接口统一改为：

```cpp
virtual void PresentFrame(AVFramePtr frame) = 0;
```

调用方：

```cpp
void VideoFrameScheduler::PresentDisplayableFrame(AVFramePtr frame)
{
    if (!frameSink_) {
        return;
    }

    frameSink_->PresentFrame(std::move(frame));
}
```

接收方：

```cpp
void VideoRender::PresentFrame(AVFramePtr frame)
{
    if (!frame) {
        return;
    }

    m_displayFrame = std::move(frame);
    update();
}
```

这个方案的代价是需要同步修改所有 sink 实现，但修改范围已经在 `TODO` 的 1.6 中列出，属于可控范围。

## 5. Renderer 持帧方式

### 5.1 VideoRender

当前 `VideoRender` 使用：

```cpp
AVFrame* m_displayFrame = nullptr;
```

并在 `PresentFrame()`、`ClearFrame()`、析构中调用 `av_frame_free()`。

迁移后应改为：

```cpp
AVFramePtr m_displayFrame;
```

关键变化：

- `PresentFrame(AVFramePtr frame)` 中直接 `m_displayFrame = std::move(frame)`。
- `ClearCurrentFrame()` 中使用 `m_displayFrame.reset()`。
- `paintGL()`、`UploadTexture()`、`DrawFrame()` 中用 `m_displayFrame.get()` 取得观察指针。
- OpenGL 上传逻辑不接管帧，只读取当前显示帧数据。

### 5.2 MultiViewRenderer

当前每路状态使用：

```cpp
AVFrame* currentFrame = nullptr;
```

迁移后应改为：

```cpp
AVFramePtr currentFrame;
```

关键变化：

- `PresentFrame(StreamId streamId, AVFramePtr frame)` 接管帧所有权。
- `ReleaseCurrentFrame()` 使用 `state.currentFrame.reset()`。
- 绘制和纹理上传通过 `state.currentFrame.get()` 读取数据。
- `DetachStream()`、析构、`ClearFrame()` 不再手动调用 `av_frame_free()`。

### 5.3 SingleStreamFrameSinkAdapter

当前 adapter 只把 `AVFrame*` 转发给 `MultiViewRenderer`。迁移后应改为移动转发：

```cpp
void SingleStreamFrameSinkAdapter::PresentFrame(AVFramePtr frame)
{
    if (!renderer || invalid) {
        return;
    }

    renderer->PresentFrame(streamId_, std::move(frame));
}
```

如果 renderer 无效，局部 `AVFramePtr` 自动释放，不需要手动清理。

## 6. Decoder 输出帧迁移方式

`decoder` 是 frame 生产者，建议迁移原则：

- 解码器内部复用的接收帧可以继续是内部 `AVFramePtr` 或局部 `AVFramePtr`。
- 所有要推入 `m_FrameVec` 的输出帧必须是 `AVFramePtr`。
- 任何格式转换失败、丢弃、提前退出路径都依赖局部 `AVFramePtr` 自动释放。

推荐方向：

```cpp
AVFramePtr outFrame = MakeAVFrame();
...
if (!m_FrameVec->Push(std::move(outFrame))) {
    ...
}
```

硬解转换函数 `TransferHWFrame()` 当前返回 `AVFrame*`。1.6 实现时推荐同步改为返回 `AVFramePtr`，避免“转换函数返回裸指针、调用方再包装”的中间状态。

## 7. AudioPlayer 迁移方式

`AudioPlayer` 是音频帧终点消费者，迁移方式直接：

- `m_audioFrameQueue` 改为 `std::shared_ptr<SyncQueue<AVFramePtr>>`。
- `Run()` 中 `AVFramePtr tmp; m_audioFrameQueue->Pop(tmp);`。
- 所有 FFmpeg 重采样调用使用 `tmp.get()`。
- 删除 `tmp` 相关的所有 `av_frame_free(&tmp)`。

需要保留的内部帧：

- `m_frame` 如果只是内部临时缓冲，可以单独迁移为 `AVFramePtr`，也可以留到后续清理。
- 本次核心目标是队列帧所有权，不要求一次性移除所有内部裸指针。

## 8. 最小风险迁移顺序

推荐 1.6 按以下顺序实现：

1. 修改接口类型：
   - `IVideoFrameSink::PresentFrame(AVFramePtr frame)`
   - `VideoRender::PresentFrame(AVFramePtr frame)`
   - `SingleStreamFrameSinkAdapter::PresentFrame(AVFramePtr frame)`
   - `MultiViewRenderer::PresentFrame(StreamId, AVFramePtr frame)`

2. 修改 renderer 持帧成员：
   - `VideoRender::m_displayFrame` 改为 `AVFramePtr`
   - `MultiViewRenderer::StreamRenderState::currentFrame` 改为 `AVFramePtr`
   - 清理函数改为 `reset()`
   - 绘制函数只用 `.get()` 观察

3. 修改 `VideoFrameScheduler`：
   - `frameQueue_` 改为 `SyncQueue<AVFramePtr>`
   - 队首观察改为 `VisitFront()` + 快照
   - `DropLateFrame()` 改为弹出 `AVFramePtr` 后自动释放
   - `PopDisplayableFrame()` 改为 move 出 `AVFramePtr`
   - `PresentDisplayableFrame()` move 给 sink

4. 修改 `AudioPlayer`：
   - 音频帧队列改为 `SyncQueue<AVFramePtr>`
   - 出队帧使用 `.get()` 处理
   - 删除手动释放出队帧的路径

5. 修改 `decoder`：
   - `m_FrameVec` 改为 `SyncQueue<AVFramePtr>`
   - 输出帧创建、格式转换、硬解转换返回 `AVFramePtr`
   - 成功入队时 `Push(std::move(outFrame))`

6. 修改 `MediaPipeline`：
   - `AFrame_` / `VFrame_` 改为 `SyncQueue<AVFramePtr>`
   - 创建帧队列时不再传入 `frameDeleter`
   - 删除 `MediaPipeline` 中的 frame 外部 deleter

7. 定向清理：
   - 搜索并清理业务链路里的 `SyncQueue<AVFrame*>`
   - 搜索并确认 `av_frame_free()` 只保留在 RAII deleter 和非队列内部临时资源必要位置

## 9. 验收重点

静态验收：

- `SyncQueue<AVFrame*>` 不再出现在播放管线队列类型中。
- `IVideoFrameSink` 和所有实现都使用 `AVFramePtr`。
- `VideoFrameScheduler` 不再手动释放出队视频帧。
- `AudioPlayer` 不再手动释放出队音频帧。
- `VideoRender` / `MultiViewRenderer` 当前显示帧由 `AVFramePtr` 持有。
- `SyncQueue` 对 frame 队列不再需要外部 deleter。

人工运行验收：

- 普通有音频 MP4：播放、暂停、seek、stop、关闭。
- 无音频流视频：播放、seek、关闭。
- 多路 2x2：添加多路、切换 active stream、remove stream、关闭。
- 播放中关闭、暂停后关闭、播放完后关闭。

## 10. 风险与处理

- **风险：sink 接口改动影响范围较大。**
  - 处理：一次性同步修改所有 sink 实现，不保留 `.release()` 中间态。

- **风险：队首观察拿到悬空裸指针。**
  - 处理：用 `VisitFront()` 复制 PTS 快照，不在锁外持有队首指针。

- **风险：renderer 绘制时持帧被清理。**
  - 处理：继续保留现有帧互斥锁，绘制时只在锁保护下读取或复制必要状态。

- **风险：硬解转换路径仍返回裸指针。**
  - 处理：1.6 中同步把 `TransferHWFrame()` 返回值迁移为 `AVFramePtr`。

- **风险：AudioPlayer 早退路径多，容易漏改。**
  - 处理：先改出队变量为 `AVFramePtr`，再删除所有针对出队帧的 `av_frame_free()`，最后定向搜索。

## 11. 结论

Frame 队列迁移不建议采用“scheduler `.release()` 后继续交给旧 sink”的兼容方式。更合适的方案是沿播放管线统一推进 `AVFramePtr`：

```text
decoder -> SyncQueue<AVFramePtr> -> VideoFrameScheduler -> IVideoFrameSink -> Renderer
```

这样修改范围更明确，所有权表达更一致，也符合当前架构中 `VideoFrameScheduler` 只负责调度、`IVideoFrameSink` 只负责接收可显示帧、renderer 只负责持有和绘制当前帧的职责边界。
