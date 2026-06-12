# Phase 1.1 任务 6：Stop 行为回到起始位置报告

## 目标

本任务将 `MediaPlayController::Stop()` 从“终止工作循环”调整为“停止播放并回到起始位置”。

Stop 的用户语义是：

- 当前音视频输出停止。
- 播放位置回到 0。
- 状态变为 `Stopped`。
- 再次 `Start()` 时从 0 开始播放。

Stop 不应该等同于释放资源，也不应该让 demuxer、decoder、audio player 的工作循环永久退出。

## 修改前问题

原 `Stop()` 直接调用四个队列的 `RequestStop()`：

```cpp
APacket_->RequestStop();
VPacket_->RequestStop();
AFrame_->RequestStop();
VFrame_->RequestStop();
```

这会让阻塞中的 `Pop()` 返回 false。当前 decoder 和 audio player 会把 `Pop()` 返回 false 视为工作循环结束，因此 Stop 后再 `Start()` 存在管线无法重新生产/消费数据的风险。

这种行为更接近“释放资源”或“关闭管线”，不符合播放器 Stop 按钮的语义。

## 当前实现

新的 `Stop()` 流程由 `MediaPlayController` 编排：

```text
Stop()
  -> SuspendPipelineAndWait()
  -> ResetPipelineToStart()
  -> SetState(Stopped)
```

其中 `ResetPipelineToStart()` 负责：

```text
停止视频渲染
暂停音频输出
清空音频包队列、视频包队列、音频帧队列、视频帧队列
demuxer seek 到 0
flush 音频 decoder 和视频 decoder
再次清空队列
syncmanager 主时钟重置到 0
```

## 职责边界

### MediaPlayController

负责 Stop 的业务编排：

- 决定 Stop 和 Pause 的状态差异。
- 进入管线挂起安全点。
- 清空队列。
- 调用 demuxer seek 到起点。
- 调用 decoder flush。
- 重置同步时钟。
- 设置播放状态为 `Stopped`。

### demuxer

只负责媒体读取位置跳转：

```cpp
demuxer_->SeekTo(0.0);
```

demuxer 不负责清队列、flush decoder 或修改 UI 状态。

### decoder

只负责清理解码器内部缓存：

```cpp
Adecoder_->Flush();
Vdecoder_->Flush();
```

同时清除 seek 预滚丢弃边界，避免 Stop 后再次播放仍携带旧的丢弃条件。

### SyncQueue

Stop 回到起始位置只使用 `ClearAndWake()` 清空旧数据。

`RequestStop()` 仍保留给 `ReleaseResources()`，用于真正关闭管线和释放资源。

## Stop 和 Pause 的差异

| 行为 | Pause | Stop |
| --- | --- | --- |
| 播放状态 | `Paused` | `Stopped` |
| 播放位置 | 保持当前位置 | 回到 0 |
| 队列 | 不要求清空 | 清空 |
| decoder | 不要求 flush | flush |
| 同步时钟 | 保持当前位置 | 重置到 0 |
| 再次 Start | 从暂停处继续 | 从 0 开始 |
| 工作循环 | 保持可恢复 | 保持可恢复 |

## 验收点

- Stop 后画面和音频停止。
- Stop 后再次 Start 从开头播放。
- Stop 不导致 demuxer/decoder/audio player 工作循环退出。
- Stop 不新增 `MainWindow` 对基础设施层的依赖。
- Stop 不新增全局变量。

## 后续观察

当前 Stop 不调用 `AudioPlayer::Stop()`，因为该接口会设置 `m_stopFlag` 并请求音频帧队列停止，语义偏向释放资源。

如果后续测试发现 Stop 后音频设备内部仍有极短缓存残留，应该单独为 `AudioPlayer` 增加“非终止式清空音频输出缓冲”的接口，而不是在 `MediaPlayController::Stop()` 中调用释放型 Stop。
