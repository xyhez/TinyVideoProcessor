# Phase 1.1 任务 1: 播放控制链路梳理报告

## 结论

当前播放控制链路已经完成了 Phase 0 的分层目标：`MainWindow` 只做 UI 事件转发，`MediaPlayController` 统一调度播放管线，基础设施层负责各自工作线程和媒体处理。

但 Phase 1.1 要实现真实 Seek 前，需要先承认一个核心事实：现在的 `Start/Pause/Stop/Rewind/Forward` 只是“状态和线程入口控制”，还不是完整的“可恢复播放管线控制”。尤其是 `Stop()` 会把队列 stop flag 置为 true，却没有清空队列、flush decoder、重置时钟、恢复队列可写状态；`Rewind()` 和 `Forward()` 也没有真正调用 FFmpeg seek。

因此后续不能直接在现有 `Rewind/Forward` 里硬塞 `av_seek_frame()`。正确方向是先建立一套 Controller 编排的 Seek 流程：暂停消费、定位 demuxer、清空队列、flush decoder、重置同步时钟，再按原播放状态恢复。

## 当前调用链

### UI 层

`MainWindow::keyReleaseEvent()` 只做键盘事件分发：

- Space: 根据 `MediaPlayController::State()` 调 `Start()` 或 `Pause()`。
- Up/Down: 调 `VolumeUp()` / `VolumeDown()`。
- Left/Right: 调 `Rewind()` / `Forward()`。

按钮槽也只转发：

- 播放按钮 -> `MainWindow::Start()` -> `MediaPlayController::Start()`。
- 暂停按钮 -> `MainWindow::Pause()` -> `MediaPlayController::Pause()`。

这个边界是对的，后续 Seek、Stop、进度条都不需要让 `MainWindow` 直接接触 `demuxer/decoder/queue/syncmanager`。进度条以后也应该只调用 `MediaPlayController::SeekTo()`。

### 应用层

`MediaPlayController` 当前负责：

- 创建四个队列：音频包、视频包、音频帧、视频帧。
- 创建 `demuxer`、音频 `decoder`、视频 `decoder`、`AudioPlayer`、`VideoRender`、`syncmanager`。
- 创建四个线程，并将 demuxer/decoder/audio player 移动到对应线程。
- 将线程 `started` 信号连接到工作函数：
  - demuxer thread -> `demuxer::StartDemuxer()`
  - audio decoder thread -> `decoder::StartDecoder()`
  - video decoder thread -> `decoder::StartDecoder()`
  - audio thread -> `AudioPlayer::StartAudioPlay()`

## Start 当前行为

`MediaPlayController::Start()` 当前顺序：

1. `SetState(Playing)`
2. `demuxerTH_->start()`
3. `AdecoderTH_->start()`
4. `VdecoderTH_->start()`
5. `audioplayTH_->start()`
6. `demuxer_->Start()`
7. `Adecoder_->Start()`
8. `Vdecoder_->Start()`
9. `audioplay_->Start()`
10. `videoRender_->StartRendering()`

实际效果：

- 第一次 Start 会启动线程，线程入口开始持续 demux/decode/play。
- 组件的 `Start()` 多数只是把 pause/stop 标志改回 false，并打印日志。
- `VideoRender::StartRendering()` 启动定时器，开始从视频帧队列按同步逻辑取帧。

风险点：

- 如果 Stop 后线程工作函数已经退出，再次 `QThread::start()` 不等于自动重新建立一个可靠的新播放管线。
- 队列 stop flag 如果还保持 true，demuxer push 会直接释放包，decoder/audio pop 可能直接退出。
- `Start()` 没有统一恢复四个队列的 `SetStopFlag(false)`。

结论：当前 Start 适合“首次播放”和“暂停后恢复”，不适合作为 Stop 或 Seek 后的通用恢复入口。

## Pause 当前行为

`MediaPlayController::Pause()` 当前顺序：

1. `SetState(Paused)`
2. `demuxer_->Pause()`
3. `Adecoder_->Pause()`
4. `Vdecoder_->Pause()`
5. `audioplay_->Pause()`
6. `videoRender_->Pause()`

实际效果：

- `AudioPlayer::Pause()` 设置 `m_pauseFlag = true`，并唤醒音频帧队列；音频播放循环会 suspend 音频输出并等待恢复。
- `VideoRender::Pause()` 等价于 `StopRendering()`，会停止渲染定时器。
- `demuxer::Pause()` 只设置 `pause_flag = true`。
- `decoder::Pause()` 只设置 `pause_flag = true`。

关键问题：

- `demuxer::StartDemuxer()` 循环没有检查 `pause_flag`。
- `decoder::StartDecoder()` 循环没有检查 `pause_flag`。
- 所以 Pause 后，demuxer 和 decoder 仍可能继续工作，直到队列满后被 backpressure 卡住。

结论：当前 Pause 对音频输出和视频显示有效，但对 demux/decode 不是严格暂停。实现 Seek 前需要明确：Seek 时不能只依赖 `Pause()` 阻塞管线。

## Stop 当前行为

`MediaPlayController::Stop()` 当前顺序：

1. `SetState(Stopped)`
2. 四个队列 `SetStopFlag(true)`
3. 四个队列 `WakeAll()`
4. `demuxer_->Stop()`
5. `Adecoder_->Stop()`
6. `Vdecoder_->Stop()`
7. `audioplay_->Stop()`
8. `videoRender_->StopRendering()`

实际效果：

- 队列 stop flag 为 true 后：
  - `Push()` 会释放新入队的数据并返回。
  - `Pop()` 在队列为空时返回 false。
  - 阻塞中的线程会被 `WakeAll()` 唤醒。
- `AudioPlayer::Stop()` 还会再次对音频帧队列设置 stop flag 并停止 `QAudioSink`。
- `VideoRender::StopRendering()` 只停定时器，不清空当前显示帧。

缺失点：

- Stop 不清空四个队列。
- Stop 不 flush decoder。
- Stop 不 seek 回 0。
- Stop 不重置同步时钟。
- Stop 不恢复队列 stop flag 到 false。
- Stop 后再次 Start 的行为不可靠。

结论：当前 Stop 更像“让线程退出/停止消费”的 teardown 操作，不是播放器语义里的“停止并回到开头”。Phase 1 要重写 Stop 语义，不能继续沿用现在的半 teardown 行为。

## Rewind / Forward 当前行为

`MediaPlayController::Rewind()` 当前流程：

1. `Pause()`
2. `demuxer_->Rewind()`
3. `Start()`

`MediaPlayController::Forward()` 当前流程：

1. `Pause()`
2. `syncManager_->Forward()`
3. `demuxer_->Forward()`
4. `Start()`

`demuxer::Rewind()` 和 `demuxer::Forward()` 当前都只是：

```cpp
return 0;
```

也就是说：

- Rewind 不会改变文件读取位置。
- Forward 不会改变文件读取位置。
- Forward 只会让 `syncmanager` 的主时钟加 3000ms。

这个 `syncmanager::Forward()` 目前不是 Seek，它只是把音频主时钟向前推。由于视频渲染用 `videoPts - audioClock` 判断等待或丢帧，单独推进时钟可能导致视频帧被误判为落后并被丢弃，但 demuxer 的真实读取位置没有变化。

结论：`syncmanager::Forward()` 应该被通用的 `SetClock()` 或 `ResetClock()` 替代。Seek 后时钟应被设置到目标时间，而不是用固定 +3000ms 做模拟快进。

## 队列状态

`SyncQueue` 当前能力：

- `Push()`：队列满时阻塞；stop flag 为 true 时释放 item 并返回。
- `Pop()`：队列空时阻塞；stop flag 为 true 且队列空时返回 false。
- `TryPop()` / `Front()`：非阻塞取数据或窥视数据。
- `Clear()`：释放队列内数据，并 `notify_all` not_full。
- `WakeAll()`：唤醒 not_empty 和 not_full。
- `SetStopFlag(bool)`：设置停止标志。

适合 Seek 的部分：

- `Clear()` 已经能用 deleter 释放 AVPacket/AVFrame。
- `SetStopFlag()` + `WakeAll()` 可以打断阻塞中的 Push/Pop。

不够的部分：

- `Clear()` 不唤醒 not_empty。
- `Clear()` 不重置 stop flag。
- 没有“清空并恢复可写状态”的原子语义。
- 没有明确 Seek 场景下的顺序约定。

建议后续增加一个 Controller 层的固定顺序，而不是让各组件各自随意清队列：

1. 设置 seek 中状态，阻止继续消费旧数据。
2. 队列 `SetStopFlag(true)` + `WakeAll()`，打断阻塞。
3. `Clear()` 四个队列。
4. decoder `Flush()`。
5. demuxer seek。
6. 队列 `SetStopFlag(false)`。
7. 恢复播放或保持暂停。

具体顺序还要结合线程生命周期实现细化，但核心原则是：旧包旧帧必须先断流并清掉，再允许新数据进入。

## Decoder 状态

当前 `decoder` 没有 `Flush()` 方法，也没有调用 `avcodec_flush_buffers()`。

`decoder::StartDecoder()` 的工作循环主要受队列控制：

- `m_PacketVec->Pop(packet)` 返回 false 时退出循环。
- 成功取包后 `avcodec_send_packet()`。
- 再循环 `avcodec_receive_frame()` 输出帧。
- 输出帧入 `m_FrameVec`。

注意：

- `decoder::Pause()` 只设置 `pause_flag`，但解码循环没有检查。
- `decoder::Stop()` 只设置 `stop_flag`，但解码循环没有检查。
- 实际让解码线程退出的是包队列 stop flag 导致 `Pop()` 返回 false。

Seek 必须补充：

- `decoder::Flush()`，内部调用 `avcodec_flush_buffers(Ctx)`。
- seek 前清空 frame queue，避免旧帧继续渲染/播放。
- seek 后清空 codec 内部缓存，避免 receive 到 seek 前的缓存帧。

结论：decoder flush 是真实 Seek 的硬前置，不做 flush 会有旧帧闪回和音视频错位风险。

## SyncManager 状态

当前 `syncmanager` 只有：

- `RefreshClock(double clock)`：设置主时钟。
- `GetClock()`：读取主时钟。
- `Forward()`：主时钟加 3000ms。

当前音频播放时，`AudioPlayer` 根据音频帧 pts 和 `m_audioTimeBase` 计算毫秒级 `audioclock`，然后调用 `RefreshClock(audioclock)`。视频渲染时，根据视频帧 pts 和 `m_videoTimeBase` 算 `videoPts`，再和 `GetClock()` 比较。

Seek 后应该做的是：

- 将主时钟设置为目标 seek 时间。
- 不再保留 `Forward()` 这种固定加 3000ms 的模拟控制。

建议新增：

```cpp
int SetClock(double clockMs);
int ResetClock();
```

`Forward()` 可以暂时保留，但后续 `MediaPlayController::Forward()` 不应直接调用它。

## 当前 Seek 缺失点

粗略地说，当前缺的是“真实 seek”。细看分成这些点：

1. `demuxer::Rewind()` / `demuxer::Forward()` 是空实现，没有 `av_seek_frame()`。
2. 没有 `SeekTo(seconds)` / `SeekRelative(seconds)` 这样的统一接口。
3. 没有目标时间边界处理。
4. 没有清空四个队列的 seek 编排。
5. 没有 decoder flush。
6. 没有重置同步时钟到目标时间。
7. 没有区分暂停态 seek 和播放态 seek。
8. 没有 Stop 回到 0 的语义。
9. 队列 stop flag 只会被置 true，没有 seek 后恢复为 false 的明确流程。
10. `Pause()` 不能真正暂停 demux/decode 循环，不能单独作为 seek 屏障。

## 后续实施建议

Phase 1.1 下一步建议不要直接做 UI 进度条，先做底层 Seek 能力。

推荐顺序：

1. 给 `syncmanager` 增加 `SetClock(double clockMs)` / `ResetClock()`。
2. 给 `decoder` 增加 `Flush()`。
3. 给 `demuxer` 增加 `SeekTo(double seconds)`，内部用 `av_seek_frame()`。
4. 在 `MediaPlayController` 增加 `SeekTo(double seconds)` 和 `SeekRelative(double seconds)`。
5. `Rewind()` / `Forward()` 改成调用 `SeekRelative(-10)` / `SeekRelative(10)`。
6. 明确 Stop 是否复用 `SeekTo(0)`，但 Stop 不能再只做 stop flag teardown。
7. 最后再加 `PositionChanged` / `DurationChanged`，给进度条准备数据。

## MainWindow 边界确认

后续改动不需要让 `MainWindow` 直接依赖基础设施层。

`MainWindow` 只需要：

- 响应按键/按钮/进度条。
- 调用 `MediaPlayController::Start/Pause/Stop/SeekTo/SeekRelative`。
- 监听 `StateChanged/PositionChanged/DurationChanged` 更新 UI。

`demuxer/decoder/queue/syncmanager` 的所有细节都应该留在 Controller 和基础设施层内。
