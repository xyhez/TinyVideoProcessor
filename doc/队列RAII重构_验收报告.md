# 队列 RAII 重构验收报告

## 1. 重构目标

本轮队列 RAII 重构的目标是把 `AVPacket` / `AVFrame` 的释放责任从 `SyncQueue` 和调用方手动约定中移出，交给 RAII 所有权类型表达。

最终所有权模型：

```text
demuxer / decoder 创建 FFmpeg 资源
  -> std::unique_ptr + 自定义 deleter 持有
    -> SyncQueue<T> 只负责 move 入队、move 出队和线程同步
      -> 消费者或 renderer 持有
        -> 离开作用域、清队列、替换显示帧或析构时自动释放
```

核心类型：

```cpp
using AVPacketPtr = std::unique_ptr<AVPacket, AVPacketDeleter>;
using AVFramePtr = std::unique_ptr<AVFrame, AVFrameDeleter>;
```

## 2. 已完成内容

### 2.1 FFmpeg RAII 类型

已在 `src/core/ffmpeg_raii.h` 中定义：

- `AVPacketDeleter`
- `AVFrameDeleter`
- `AVPacketPtr`
- `AVFramePtr`
- `MakeAVPacket()`
- `MakeAVFrame()`

释放 API 只在 RAII deleter 内集中出现：

- `AVPacket` 通过 `av_packet_free()` 释放。
- `AVFrame` 通过 `av_frame_free()` 释放。

### 2.2 SyncQueue 职责收口

`SyncQueue<T>` 当前只负责：

- 阻塞入队。
- 阻塞出队。
- 非阻塞出队。
- 队首只读观察。
- 清空队列。
- 停止、恢复和等待中断。

`SyncQueue<T>` 不再持有 `std::function<void(T)> deleter_`，也不再知道 `AVPacket`、`AVFrame` 或任何 FFmpeg 释放函数。

队列清空时直接清理容器：

```cpp
raw_queue_.clear();
```

对于 `AVPacketPtr` / `AVFramePtr`，容器元素析构会自动释放 FFmpeg 资源。

### 2.3 Packet 队列迁移

当前音频包队列、视频包队列均已迁移为：

```cpp
SyncQueue<AVPacketPtr>
```

所有权流向：

```text
demuxer
  -> MakeAVPacket()
  -> av_read_frame(packet.get())
  -> Push(std::move(packet))
    -> decoder Pop(packet)
      -> avcodec_send_packet(ctx, packet.get())
      -> packet 自动释放
```

packet 队列不再依赖外部 deleter。

### 2.4 Frame 队列迁移

当前音频帧队列、视频帧队列均已迁移为：

```cpp
SyncQueue<AVFramePtr>
```

音频帧所有权流向：

```text
decoder
  -> MakeAVFrame()
  -> Push(std::move(outFrame))
    -> AudioPlayer Pop(tmp)
      -> swr_convert(..., tmp->data, ...)
      -> tmp 自动释放
```

视频帧所有权流向：

```text
decoder
  -> MakeAVFrame()
  -> Push(std::move(outFrame))
    -> VideoFrameScheduler Pop(newFrame)
      -> PresentFrame(std::move(newFrame))
        -> VideoRender / MultiViewRenderer 持有当前显示帧
          -> reset / 替换 / 析构时自动释放
```

### 2.5 VideoFrameScheduler 队首观察

`VideoFrameScheduler` 不再通过 `Front()` 复制队首帧指针。

当前使用 `VisitFront()` 在锁内读取队首帧的 PTS 快照：

```text
VisitFront(const AVFramePtr&)
  -> 复制 pts
  -> 锁外只使用 pts 快照做同步判断
```

这样避免了 move-only 元素不可复制的问题，也避免在锁外长期持有队首帧裸指针。

### 2.6 Sink / Renderer 显示帧所有权

`IVideoFrameSink::PresentFrame()` 已迁移为：

```cpp
PresentFrame(AVFramePtr frame)
```

所有 sink 实现同步迁移：

- `VideoRender`
- `SingleStreamFrameSinkAdapter`
- `MultiViewRenderer`

`VideoRender` 当前显示帧：

```cpp
AVFramePtr m_displayFrame;
```

`MultiViewRenderer` 单路显示状态：

```cpp
AVFramePtr currentFrame;
```

`MultiViewRenderer` 内部 `renderStates_` 使用 `std::map`，避免 `QMap` 对 move-only value 的拷贝要求。

## 3. 为什么选择 unique_ptr 而不是 shared_ptr

本项目当前媒体资源链路是明确的单一所有权转移：

- packet 从 `demuxer` 转移到 packet 队列，再转移到 `decoder`。
- audio frame 从 `decoder` 转移到 frame 队列，再转移到 `AudioPlayer`。
- video frame 从 `decoder` 转移到 frame 队列，再转移到 `VideoFrameScheduler`，最后转移到当前 sink。

这些路径没有“多个模块同时拥有同一帧并共同延长生命周期”的需求。

使用 `unique_ptr` 的收益：

- 所有权方向清晰，符合当前播放管线。
- 编译器禁止拷贝，能暴露错误的共享或复制路径。
- 队列清空、提前返回、异常分支和替换显示帧时自动释放。
- 不需要引用计数开销。

不使用 `shared_ptr` 的原因：

- `shared_ptr` 会弱化所有权边界，容易掩盖谁负责消费和释放。
- 播放管线中如果出现循环引用或长期持有，问题更隐蔽。
- 当前架构中 `VideoFrameScheduler`、sink、renderer 的职责边界已经能用 move 表达，不需要共享所有权。

如果后续图像处理或截图功能确实需要跨模块共享帧，应优先考虑复制必要图像数据或引入明确的只读共享帧对象，而不是把底层 `AVFrame` 默认改成共享所有权。

## 4. 静态验收结果

已完成的静态验收目标：

- `SyncQueue` 不再包含 `deleter_`。
- `SyncQueue` 不再包含 `std::function<void(T)>`。
- 播放管线中不再存在 `SyncQueue<AVPacket*>`。
- 播放管线中不再存在 `SyncQueue<AVFrame*>`。
- `AVPacket` / `AVFrame` 队列元素均为 RAII 类型。
- `IVideoFrameSink` 和所有实现均使用 `AVFramePtr` 接收显示帧。
- 出队 packet / frame 的手动释放路径已移除。
- 显示帧替换和清理由 `AVFramePtr::reset()` 或移动赋值完成。

保留的 `av_frame_free()` / `av_packet_free()` 属于以下合理路径：

- `src/core/ffmpeg_raii.h` 中的 RAII deleter。
- 非队列内部临时资源的既有释放路径，例如 `AudioPlayer::m_frame`。

## 5. 未执行的人工验收

本轮按约定只做静态检查，未编译、未运行。

建议人工验证：

- 普通有音频 MP4：播放、暂停、seek、stop、关闭。
- 无音频视频：播放、seek、关闭。
- 多路 2x2：添加多路、切换 active stream、remove stream、关闭。
- 播放中关闭、暂停后关闭、播放完后关闭。

## 6. 剩余风险

- `SyncQueue<T>` 删除 deleter 后，不再适合承载需要手动释放的裸指针资源。后续新增队列元素时必须优先使用值类型或 RAII 类型。
- `AudioPlayer::m_frame` 仍是内部裸指针临时资源，不属于队列元素；后续可以单独清理，但不影响本轮队列所有权收口。
- 本轮未覆盖运行期验证，仍需通过用户侧编译和播放场景确认行为不退化。

## 7. 结论

队列 RAII 重构的核心目标已完成：

```text
SyncQueue 只负责同步和元素移动
FFmpeg 资源释放由 AVPacketPtr / AVFramePtr 负责
播放管线通过 move 表达单一所有权转移
```

后续如果静态和人工运行均通过，可以把 `TODO-队列RAII重构.md` 中运行类验收项补充勾选。
