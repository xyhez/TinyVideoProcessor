# 队列 RAII 重构：所有权链路报告

## 结论

当前队列资源所有权模型可以运行，但仍然依赖“裸指针 + 约定 + 队列 deleter”共同维持：

- `AVPacket*` 链路相对简单，适合优先迁移到 `AVPacketPtr`。
- `AVFrame*` 链路跨过 `decoder`、`AudioPlayer`、`VideoFrameScheduler`、`IVideoFrameSink`、`VideoRender`、`MultiViewRenderer`，必须后迁移。
- `SyncQueue<T>` 当前既负责同步队列，又通过 `deleter_` 负责 FFmpeg 资源清理，职责不够纯。
- `Front()` 当前通过拷贝裸指针观察队首帧，后续迁移到 move-only RAII 类型前必须设计只读观察接口。

建议迁移顺序保持 TODO 中的规划：

```text
先引入 FFmpeg RAII 类型
  -> 让 SyncQueue 支持 move-only
    -> 迁移 packet 队列
      -> 单独设计 frame 队列迁移方案
        -> 迁移 frame 队列和 sink 所有权
          -> 删除 SyncQueue deleter 路径
```

## 当前队列类型

`MediaPipeline::CreatePipelineResources()` 创建四类队列：

```text
VPacket_ : SyncQueue<AVPacket*>
VFrame_  : SyncQueue<AVFrame*>
APacket_ : SyncQueue<AVPacket*>，仅有音频流时创建
AFrame_  : SyncQueue<AVFrame*>，仅有音频流时创建
```

这些队列都由 `std::shared_ptr<SyncQueue<...>>` 持有，并注入到 `demuxer`、`decoder`、`AudioPlayer`、`VideoFrameScheduler` 中。

队列清理策略：

- packet 队列使用 `packetDeleter`，内部调用 `av_packet_free()`。
- frame 队列使用 `frameDeleter`，内部调用 `av_frame_free()`。
- `SyncQueue::Clear()` 会遍历队列残留元素并调用 `deleter_`。
- `SyncQueue::Push()` 如果因为停止或等待中断失败，也会调用 `deleter_` 清理未入队元素。

## AVPacket 所有权链路

### 创建位置

`AVPacket*` 只在 `demuxer::StartDemuxer()` 中通过 `av_packet_alloc()` 创建。

每次循环读取前创建一个 packet：

```text
demuxer::StartDemuxer()
  -> av_packet_alloc()
  -> av_read_frame()
```

### 入队位置

读取成功后，`demuxer` 根据 `stream_index` 决定进入音频包队列或视频包队列：

```text
音频包:
demuxer -> m_APacket->Push(packet)

视频包:
demuxer -> m_VPacket->Push(packet)
```

入队成功后，packet 临时所有权进入对应 `SyncQueue<AVPacket*>`。

### 未入队释放位置

以下情况由 `demuxer` 立即释放 packet：

- `stop_flag` 已设置。
- `av_read_frame()` 返回错误或 EOF。
- 管线挂起请求已到达。
- packet 不属于音频流或视频流。
- 音频分支中发现停止状态。
- 视频分支中发现停止状态。

如果 `Push(packet)` 返回 false，当前代码不再手动释放 packet，而是依赖 `SyncQueue::Push()` 在失败分支调用 `packetDeleter`。

因此当前约定是：

```text
Push 前失败       -> demuxer 释放
Push 成功         -> 队列持有
Push 内部失败     -> 队列 deleter 释放
```

### 出队位置

`decoder::StartDecoder()` 从 packet 队列阻塞出队：

```text
AVPacket* packet = nullptr;
m_PacketVec->Pop(packet);
```

出队成功后，packet 所有权转移给 `decoder`。

### 最终释放位置

`decoder` 调用 `avcodec_send_packet(Ctx, packet)` 后立即释放：

```text
avcodec_send_packet(Ctx, packet);
av_packet_free(&packet);
```

如果出队后发现当前 decoder 已进入挂起请求，也由 `decoder` 释放：

```text
if (IsSuspendRequested()) {
    av_packet_free(&packet);
    continue;
}
```

### Packet 链路判断

packet 链路所有权相对单向：

```text
demuxer 创建
  -> demuxer 入队
    -> packet queue 持有
      -> decoder 出队
        -> decoder send_packet 后释放
```

这个链路没有进入 UI、sink 或渲染器，迁移成本低于 frame 队列，适合优先 RAII 化。

## AVFrame 所有权链路

### 解码临时帧

`decoder::StartDecoder()` 内部创建一个复用帧：

```text
frame = av_frame_alloc();
```

该帧用于 `avcodec_receive_frame(Ctx, frame)` 接收解码器输出，然后通过 `av_frame_unref(frame)` 复用。

静态检查发现一个需要后续单独处理的风险：`decoder::~decoder()` 当前没有释放成员 `frame`。这不是队列元素，但属于 FFmpeg 裸指针资源，后续 RAII 重构时应一并收口。

### 输出帧创建位置

`decoder` 产出进入队列的 `outFrame`，来源有三类：

1. 硬解输出时，`TransferHWFrame(frame)` 创建系统内存帧。
2. 视频非 `YUV420P` 时，`av_frame_alloc()` 创建转换后帧，并用 `sws_scale()` 写入数据。
3. 音频或已支持的视频格式时，`av_frame_alloc()` 创建 `outFrame`，再通过 `av_frame_ref(outFrame, frame)` 引用解码帧数据。

如果中途失败，`decoder` 会立即 `av_frame_free(&outFrame)` 或释放中间帧。

### 入队位置

输出帧通过 decoder 进入音频帧队列或视频帧队列：

```text
decoder -> m_FrameVec->Push(outFrame)
```

入队成功后，`outFrame` 所有权进入对应 `SyncQueue<AVFrame*>`。

如果 `Push(outFrame)` 返回 false，当前代码依赖 `SyncQueue::Push()` 的 `frameDeleter` 释放该帧。

### 音频帧出队和释放

`AudioPlayer::Run()` 从 `AFrame_` 出队：

```text
AVFrame* tmp = nullptr;
m_audioFrameQueue->Pop(tmp);
```

出队成功后，音频帧所有权转移给 `AudioPlayer`。

`AudioPlayer` 在以下路径释放 `tmp`：

- 输入声道布局无效。
- 重采样器创建失败。
- 重采样器初始化失败。
- 重采样器为空。
- 重采样失败。
- PCM 播放处理完成。

释放方式基本为：

```text
av_frame_unref(tmp);
av_frame_free(&tmp);
```

### 视频帧队首观察

`VideoFrameScheduler::Tick()` 首先通过 `Front()` 观察队首帧：

```text
AVFrame* nextFrame = nullptr;
frameQueue_->Front(nextFrame);
```

此时帧仍属于 `VFrame_`，scheduler 不能释放，也不能交给 sink。它只读取 PTS 做同步判断。

这正是后续 RAII 迁移的关键点：`Front(T& item)` 当前靠复制裸指针完成观察，不能直接适配 `std::unique_ptr<AVFrame, Deleter>`。

### 视频帧丢弃路径

如果队首帧落后音频时钟，`VideoFrameScheduler::DropLateFrame()` 会从队列弹出并释放：

```text
frameQueue_->TryPop(drop);
av_frame_free(&drop);
```

此时所有权从队列转给 scheduler，并由 scheduler 立即释放。

### 视频帧显示路径

如果帧可以显示，scheduler 从队列弹出：

```text
frameQueue_->TryPop(frame);
```

然后转交给 sink：

```text
frameSink_->PresentFrame(frame);
```

调用 `PresentFrame()` 后，所有权转移给 sink，scheduler 不再释放或访问该帧。

如果 `frameSink_` 为空，scheduler 会兜底释放：

```text
av_frame_free(&frame);
```

### 默认 VideoRender sink

默认 fallback 路径下，`VideoRender::PresentFrame()` 接管帧：

```text
av_frame_free(&m_displayFrame);
m_displayFrame = frame;
```

释放位置：

- 新帧替换旧帧时释放旧 `m_displayFrame`。
- `ClearFrame()` / `ClearCurrentFrame()` 释放当前显示帧。
- `VideoRender::~VideoRender()` 释放当前显示帧。

### MultiViewRenderer sink

统一多路渲染路径下，链路为：

```text
VideoFrameScheduler
  -> SingleStreamFrameSinkAdapter::PresentFrame(frame)
    -> MultiViewRenderer::PresentFrame(streamId, frame)
```

如果 renderer 已销毁或 streamId 无效，adapter 会释放 frame。

`MultiViewRenderer::PresentFrame()` 接管帧：

```text
ReleaseCurrentFrame(state);
state.currentFrame = frame;
```

释放位置：

- 同一路新帧替换旧帧时释放旧 `currentFrame`。
- `ClearFrame(streamId)` 释放该 stream 当前显示帧。
- `DetachStream(streamId)` 释放该 stream 当前帧和纹理资源。
- `MultiViewRenderer::~MultiViewRenderer()` 遍历释放所有 stream 当前帧和纹理资源。

### Frame 链路判断

视频帧链路比 packet 复杂：

```text
decoder 创建 outFrame
  -> frame queue 持有
    -> scheduler Front 只观察，不接管
    -> scheduler TryPop
      -> 丢弃帧：scheduler 释放
      -> 可显示帧：sink 接管
        -> VideoRender 或 MultiViewRenderer 释放
```

音频帧链路相对简单：

```text
decoder 创建 outFrame
  -> AFrame_ 持有
    -> AudioPlayer Pop
      -> AudioPlayer 重采样/播放后释放
```

后续 frame RAII 迁移必须同时处理：

- `VideoFrameScheduler` 的队首观察接口。
- `IVideoFrameSink::PresentFrame()` 的所有权类型。
- `VideoRender` 和 `MultiViewRenderer` 当前显示帧持有方式。
- adapter 失败兜底释放语义。

## Clear / Stop / Seek / Shutdown 资源释放链路

### ClearQueues

`MediaPipeline::ClearQueues()` 调用四个队列的 `ClearAndWake()`：

```text
APacket_->ClearAndWake()
VPacket_->ClearAndWake()
AFrame_->ClearAndWake()
VFrame_->ClearAndWake()
```

`ClearAndWake()` 当前等同于 `Clear()`，释放队列内部残留元素并唤醒等待线程。

注意：`ClearQueues()` 只能释放仍在队列里的 packet/frame，不能释放已经被 decoder、AudioPlayer、scheduler 或 sink 弹出的对象。

### Stop

文件源 stop：

```text
SuspendPipelineAndWait()
  -> videoFrameScheduler_->Stop()
  -> videoFrameScheduler_->ClearOutputFrame()
  -> audioplay_->Pause()
  -> ClearQueues()
  -> demuxer_->SeekTo(0)
  -> FlushDecoders()
  -> ClearQueues()
  -> syncManager_->SeekTo(0)
```

实时源 stop：

```text
SuspendPipelineAndWait()
  -> videoFrameScheduler_->Stop()
  -> videoFrameScheduler_->ClearOutputFrame()
  -> audioplay_->Pause()
  -> ClearQueues()
  -> FlushDecoders()
  -> ClearQueues()
  -> syncManager_->SeekTo(0)
```

这里的关键点是：

- `ClearOutputFrame()` 释放 sink 当前显示帧。
- `ClearQueues()` 释放队列残留帧。
- `FlushDecoders()` 清理解码器内部缓存。

三者职责不同，不能互相替代。

### Seek

seek 流程：

```text
SuspendPipelineAndWait()
  -> videoFrameScheduler_->ClearOutputFrame()
  -> ClearQueues()
  -> demuxer_->SeekTo(targetSeconds)
  -> FlushDecoders()
  -> Vdecoder_->SetDiscardBefore(targetSeconds)
  -> ClearQueues()
  -> syncManager_->SeekTo(targetSeconds * 1000.0)
```

这条链路符合之前的帧所有权协议：seek 前先清掉 sink 当前帧，再清队列、seek、flush、设置丢弃边界。

### Shutdown

`MediaPipeline::ReleaseResources()` 的核心顺序：

```text
停止 position timer
  -> RequestStop 所有 packet/frame queue
  -> 停止 demuxer / decoder / audio player / scheduler
  -> scheduler ClearOutputFrame()
  -> scheduler SetFrameSink(nullptr)
  -> 等待线程退出
  -> reset demuxer / decoder / audio player / scheduler
  -> delete 默认 VideoRender
  -> reset queues
  -> reset syncManager
  -> mediaSource Close()
```

队列残留资源在 `APacket_ / VPacket_ / AFrame_ / VFrame_` reset 时触发 `SyncQueue::~SyncQueue()`，再由 `Clear()` 调用对应 deleter 释放。

sink 当前显示帧不在队列里，依赖 `ClearOutputFrame()` 或 sink 析构释放。

### 自然播放结束

`HandleDemuxFinished()` 当前只对 packet 队列 `RequestStop()`，让 decoder 退出取包等待。已经进入 frame 队列的帧继续由 AudioPlayer / VideoFrameScheduler 消费；后续关闭或切换时再按 stop/shutdown 链路清理。

### 移除多路 stream

`MultiStreamManager::RemoveStream()` 先调用：

```text
pipeline->Shutdown()
```

然后移除 stream，并发出 `StreamRemoved(id)`。UI 侧收到信号后调用：

```text
multiViewRenderer_->DetachStream(id)
```

因此当前设计中：

- 单路管线内部资源由 `MediaPipeline::Shutdown()` 清理。
- 多路渲染器中按 streamId 保存的当前显示帧和纹理，由 UI / renderer 路径通过 `DetachStream()` 清理。
- `MediaPipeline` 不感知 `StreamId`，符合架构边界。

## 当前风险点

### 1. SyncQueue 同时负责同步和资源释放

`SyncQueue` 当前通过 `std::function<void(T)> deleter_` 释放元素。这让队列模板感知了元素清理策略，后续应收口为：

```text
SyncQueue 只负责同步、容量、等待和元素移动。
FFmpeg 资源释放由 AVPacketPtr / AVFramePtr 的 deleter 自动完成。
```

### 2. Front 不适合 move-only RAII 元素

当前 `Front(T& item)` 会复制队首元素。对 `AVFrame*` 可行，但对 `std::unique_ptr<AVFrame, Deleter>` 不成立。

后续应新增只读观察接口，例如：

```text
VisitFront(lambda)
PeekRaw()
FrontRef()
```

具体命名可在 1.3 设计时确定，但原则是：观察队首不转移所有权。

### 3. frame 队列不能先于 sink 协议迁移

如果直接把 `VFrame_` 改成 `SyncQueue<AVFramePtr>`，当前 `IVideoFrameSink::PresentFrame(AVFrame*)` 会迫使 scheduler 使用 `.release()` 兼容旧接口。

这种做法可以作为过渡，但必须非常明确：

```text
release 后 frame 所有权必须立即转移给 sink。
失败分支必须重新接管或释放。
```

否则会形成新的裸指针泄漏点。

### 4. decoder 成员 frame 存在独立释放风险

`decoder::StartDecoder()` 中成员 `frame` 使用 `av_frame_alloc()` 创建，用于复用接收解码输出。当前静态检查没有看到 `decoder::~decoder()` 中释放该成员。

该对象不是队列元素，但属于同类 FFmpeg 裸指针资源。建议后续在 RAII 类型引入或 decoder 收口时一并处理。

### 5. shutdown 依赖线程退出顺序

`ReleaseResources()` 当前先 `RequestStop()`、再 stop 各组件、再等待线程退出，整体方向正确。后续 RAII 迁移不能改变这个顺序，否则可能出现：

- decoder 仍在持有 packet/frame 时队列先析构。
- scheduler 仍可能调 sink 时 sink 已销毁。
- renderer 已 detach 后仍收到旧帧。

## 优先迁移判断

### 适合先迁移

packet 队列适合先迁移：

- 只在 `demuxer` 和 `decoder` 间流转。
- 没有 `Front()` 观察需求。
- 没有 UI / sink / renderer 接入。
- 出队后立即用于 `avcodec_send_packet()` 并释放。

建议先迁移：

```text
APacket_ / VPacket_
demuxer::StartDemuxer()
decoder::StartDecoder()
```

### 必须后迁移

frame 队列必须后迁移：

- 视频帧存在 `Front()` 观察。
- 可显示帧会跨过 `IVideoFrameSink`。
- `VideoRender` 和 `MultiViewRenderer` 都持有 current frame。
- remove stream / shutdown 依赖 sink 当前帧清理。

建议先写 `Frame迁移方案`，再改：

```text
AFrame_ / VFrame_
AudioPlayer
VideoFrameScheduler
IVideoFrameSink
VideoRender
SingleStreamFrameSinkAdapter
MultiViewRenderer
```

## 与架构指导的一致性

本报告不改变现有代码，只确认后续重构边界：

- `MediaPipeline` 仍是单路播放管线，不引入 `StreamId`。
- `MultiStreamManager` 仍只管理多路身份、active stream 和多条 pipeline。
- `VideoFrameScheduler` 仍负责同步、取帧、丢帧和可显示帧转交，不参与 OpenGL。
- `MultiViewRenderer` 仍只负责按 streamId/slot 绘制已交付的可显示帧，不访问队列或 sync manager。
- `VideoRender` fallback 保留，不在队列 RAII 重构中删除。

## 验收结论

1. packet 队列当前由 `demuxer` 创建 packet，由队列临时持有，由 `decoder` 出队后释放。
2. frame 队列当前由 `decoder` 创建 frame，由队列临时持有，音频由 `AudioPlayer` 出队释放，视频由 `VideoFrameScheduler` 出队后丢弃释放或转交 sink。
3. 队列清空、seek、stop、shutdown 时，队列残留资源由 `SyncQueue::Clear()` + deleter 释放，sink 当前显示帧由 `ClearFrame()` 或 sink 析构释放。
4. 后续迁移应先处理 packet 队列；frame 队列必须先解决队首观察和 sink 所有权接口。
5. 当前发现一个非队列但相关的裸指针风险：`decoder::frame` 成员分配后未在析构中释放，建议后续 RAII 化时纳入修正范围。
