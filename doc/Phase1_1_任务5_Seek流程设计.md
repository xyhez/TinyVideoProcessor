# Phase 1.1 任务 5：MediaPlayController Seek 流程设计

## 目标

本任务的目标是在 `MediaPlayController` 中完成完整 Seek 编排，让快进、快退和后续进度条跳转都走同一套稳定流程。

Seek 不只是调用 `av_seek_frame()`。如果只移动 demuxer 的读取位置，而不处理旧队列、decoder 内部缓存和同步时钟，播放端仍可能继续消费 seek 前的数据，表现为：

- 快进/快退后仍播放旧画面。
- seek 后短时间内听到旧音频。
- 音视频主时钟和 demuxer 位置不一致。
- 暂停状态下 seek 后错误恢复播放。

因此任务 5 的职责是把任务 2、任务 3、任务 4 已完成的底层能力串起来：

- 队列层：清空、唤醒、恢复。
- demuxer 层：真实 seek。
- decoder 层：flush 解码器内部缓存。
- syncmanager 层：重置播放主时钟。
- Controller 层：维护播放状态并编排顺序。

## 架构边界

### MainWindow

`MainWindow` 仍然只负责 UI 事件转发。

允许：

- 按键触发 `mediaPlayController_->Forward()`。
- 按键触发 `mediaPlayController_->Rewind()`。
- 后续进度条释放时触发 `mediaPlayController_->SeekTo(seconds)`。

不允许：

- 直接访问 `demuxer`。
- 直接清空队列。
- 直接 flush decoder。
- 直接修改同步时钟。

### MediaPlayController

`MediaPlayController` 是本任务的核心修改点，负责完整 Seek 编排。

它应该提供：

```cpp
void SeekTo(double seconds);
void SeekRelative(double seconds);
void Rewind();
void Forward();
```

其中：

- `SeekTo(seconds)`：跳转到指定绝对时间点，单位为秒。
- `SeekRelative(seconds)`：以当前播放位置为基准，相对跳转指定秒数。
- `Rewind()`：调用 `SeekRelative(-10.0)`。
- `Forward()`：调用 `SeekRelative(10.0)`。

### demuxer

`demuxer` 只负责真实媒体读取位置跳转。

它已经具备：

```cpp
int SeekTo(double seconds);
int SeekRelative(double seconds);
```

建议额外暴露当前位置读取能力：

```cpp
double CurrentPositionSeconds() const;
```

或者新增同义接口：

```cpp
double Position() const;
```

这样 `MediaPlayController::SeekRelative()` 可以先计算目标时间，再统一调用 `SeekTo(targetSeconds)`，避免相对 seek 和绝对 seek 走两套不同恢复流程。

### decoder

`decoder` 只负责清理解码器内部缓存。

它已经具备：

```cpp
int Flush();
```

Seek 时必须同时 flush 音频 decoder 和视频 decoder。

### syncmanager

`syncmanager` 负责维护播放主时钟。

当前已有：

```cpp
int RefreshClock(double clock);
double GetClock() const;
int Forward();
```

任务 5 建议新增通用时钟重置接口：

```cpp
int SeekTo(double clockMs);
```

注意：`MediaPlayController::SeekTo(double seconds)` 的参数单位是秒，而 `syncmanager` 当前主时钟单位是毫秒。因此重置时钟时需要转换：

```cpp
syncManager_->SeekTo(seconds * 1000.0);
```

旧的 `syncmanager::Forward()` 不应该继续被 `MediaPlayController::Forward()` 使用，因为它写死增加 3000ms，无法表达真实 seek 目标。

### SyncQueue

队列层已经具备任务 5 需要的基础能力：

```cpp
void RequestStop();
void Resume();
void ClearAndWake();
```

Seek 编排中需要对四个队列统一处理：

- 音频包队列 `APacket_`
- 视频包队列 `VPacket_`
- 音频帧队列 `AFrame_`
- 视频帧队列 `VFrame_`

## 推荐接口改动

### MediaPlayController.h

新增 public 接口：

```cpp
void SeekTo(double seconds);
void SeekRelative(double seconds);
```

新增 private 工具函数：

```cpp
void ClearQueues();
void FlushDecoders();
```

这些工具函数只服务 Controller 内部编排，不暴露给 UI。

当前代码中 `SyncQueue::RequestStop()` 会让阻塞中的 `Pop()` 返回失败，进而让 decoder 和 audio player 的工作循环退出。任务 5 的 Seek 不负责重建工作循环，因此 Seek 流程不使用 `RequestStopQueues()`，只使用 `ClearAndWake()` 清理旧数据并唤醒队列等待。

### syncmanager.h

新增：

```cpp
int SeekTo(double clockMs);
```

### demuxer.h

建议将当前 private 的：

```cpp
double CurrentPositionSeconds() const;
```

移动到 public，或者新增：

```cpp
double Position() const;
```

如果担心暴露内部命名，可以优先使用 `Position()`，让 `CurrentPositionSeconds()` 保持 private，由 `Position()` 转调。

## SeekTo 完整流程

`MediaPlayController::SeekTo(double seconds)` 建议按以下顺序执行：

1. 检查关键组件是否存在。
2. 将目标时间裁剪到 `[0, duration]` 范围内。
3. 保存 seek 前播放状态。
4. 暂停当前播放管线。
5. 清空四个队列里的旧包、旧帧，并唤醒等待中的队列操作。
6. 调用 `demuxer_->SeekTo(targetSeconds)`。
7. 如果 demuxer seek 失败，恢复 seek 前状态。
8. flush 音频 decoder。
9. flush 视频 decoder。
10. 给视频 decoder 设置目标时间前输出帧丢弃边界。
11. 再次清空四个队列，丢弃 seek 过程中可能残留的旧数据。
12. 将同步主时钟重置到目标时间。
13. 根据 seek 前状态恢复播放或暂停。

伪代码：

```cpp
void MediaPlayController::SeekTo(double seconds) {
    if (!demuxer_ || !Adecoder_ || !Vdecoder_ || !syncManager_) {
        return;
    }

    const double targetSeconds = NormalizeSeekTarget(seconds);
    const PlaybackState previousState = State();

    if (previousState != PlaybackState::Stopped &&
        !SuspendPipelineAndWait()) {
        RestorePlaybackState(previousState);
        return;
    }

    ClearQueues();

    const int seekResult = demuxer_->SeekTo(targetSeconds);
    if (seekResult != 0) {
        RestorePlaybackState(previousState);
        return;
    }

    FlushDecoders();
    Vdecoder_->SetDiscardBefore(targetSeconds);
    ClearQueues();
    syncManager_->SeekTo(targetSeconds * 1000.0);
    RestorePlaybackState(previousState);
}
```

其中 `RestorePlaybackState(previousState)` 可以先不单独抽函数，直接写在 `SeekTo()` 尾部：

```cpp
if (previousState == PlaybackState::Playing) {
    Start();
} else if (previousState == PlaybackState::Paused) {
    SetState(PlaybackState::Paused);
} else {
    SetState(PlaybackState::Stopped);
}
```

如果 seek 前是 `Paused`，seek 后不应该调用 `Start()`。

如果 seek 前是 `Stopped`，seek 后也不应该主动启动播放。

## SeekRelative 流程

`SeekRelative(double seconds)` 只负责计算目标时间，不负责重复写 seek 编排。

推荐：

```cpp
void MediaPlayController::SeekRelative(double seconds) {
    if (!demuxer_) {
        return;
    }

    const double targetSeconds = demuxer_->Position() + seconds;
    SeekTo(targetSeconds);
}
```

也可以使用：

```cpp
const double targetSeconds = demuxer_->CurrentPositionSeconds() + seconds;
```

但命名上 `Position()` 更适合作为对 Controller 暴露的只读接口。

目标时间越界不需要 Controller 重复处理，交给 `demuxer::SeekTo()` 兜底即可。当前 demuxer 已经处理小于 0 和大于总时长的边界。

## Rewind 和 Forward

旧逻辑：

```cpp
void MediaPlayController::Rewind() {
    Pause();
    demuxer_->Rewind();
    Start();
}

void MediaPlayController::Forward() {
    Pause();
    syncManager_->Forward();
    demuxer_->Forward();
    Start();
}
```

新逻辑：

```cpp
void MediaPlayController::Rewind() {
    SeekRelative(-10.0);
}

void MediaPlayController::Forward() {
    SeekRelative(10.0);
}
```

这样快进、快退、后续进度条拖动都会复用同一个 `SeekTo()` 主流程。

## 队列处理顺序

Seek 时队列处理顺序很关键。

推荐顺序：

```cpp
SuspendPipelineAndWait();
ClearQueues();
demuxer_->SeekTo(seconds);
FlushDecoders();
Vdecoder_->SetDiscardBefore(targetSeconds);
ClearQueues();
syncManager_->SeekTo(seconds * 1000.0);
```

原因：

- `ClearQueues()` 清掉 seek 前已经积压的旧包、旧帧。
- `demuxer_->SeekTo()` 移动媒体读取位置。
- `FlushDecoders()` 清掉解码器内部尚未输出的旧帧。
- `Vdecoder_->SetDiscardBefore()` 让视频 decoder 消化关键帧预滚，但不输出目标时间前的视频帧。
- 第二次 `ClearQueues()` 丢弃 seek 过程中可能被生产出来的旧数据。
- `syncManager_->SeekTo()` 让播放主时钟和目标位置对齐。

不推荐在 seek 前只 `Clear()`，因为工作线程可能正阻塞在队列条件变量上。

不推荐在 Seek 流程中使用 `RequestStop()`，因为当前 decoder 和 audio player 会把 `Pop()` 返回 false 视为工作结束。

## Pipeline Suspend 屏障

Seek 前必须先进入管线挂起屏障，而不是只设置普通暂停标志。

挂起屏障的职责：

- demuxer 停在 `av_read_frame()` 前后的安全边界，不再向包队列写入 seek 前 packet。
- decoder 停在取下一个 packet 前的安全边界，不再继续消费 seek 前 packet。
- `SyncQueue::InterruptWait()` 只唤醒阻塞中的 `Push()` / `Pop()`，不设置 stop flag，不让工作循环退出。
- Controller 等 demuxer 和 decoder 都确认进入安全点后，才允许清队列、seek 和 flush。

`RequestStop()` 只用于 Stop/释放资源，不用于 Seek。

## 视频预滚帧处理

Seek 到目标时间时，demuxer 通常会落到目标点之前的关键帧。视频 decoder 必须从该关键帧继续解码，才能得到目标点附近的画面。因此 seek 后视频帧队列里可能短时间出现一批早于音频主时钟的旧视频帧。

这类帧不应该交给 `VideoRender` 兜底处理。`VideoRender` 的职责是正常同步判断和渲染，不应该承担 seek 后从关键帧追到目标点的预滚清理。

更合适的处理位置是视频 decoder 输出侧：

- `MediaPlayController::SeekTo(targetSeconds)` 在 demuxer seek 成功后，给视频 decoder 设置 `SetDiscardBefore(targetSeconds)`。
- 视频 decoder 继续从关键帧开始解码，保证参考帧链路完整。
- 视频 decoder 在输出帧入队前检查 `frame->pts * stream_time_base`。
- 小于 `targetSeconds` 的视频帧直接释放，不进入 `VFrame_`。
- 第一帧达到或超过 `targetSeconds` 后，decoder 自动清除丢弃边界，恢复正常输出。

这样旧视频帧不会进入渲染队列，`VideoRender` 不需要打补丁式批量丢帧。

## 播放状态恢复规则

Seek 前状态决定 seek 后状态：

| Seek 前状态 | Seek 后行为 |
| --- | --- |
| Playing | seek 完成后继续播放 |
| Paused | seek 完成后保持暂停 |
| Stopped | seek 完成后保持停止 |

这条规则是任务 5 的重要验收点。

如果后续 UI 需要更精细的状态显示，可以新增 `Seeking` 状态。但当前阶段不建议新增，因为会扩大 MainWindow 状态处理范围。

## 异常处理

### demuxer seek 失败

如果 `demuxer_->SeekTo(seconds)` 返回失败：

1. 必须恢复 seek 前状态。
2. 不应该 flush decoder 和重置时钟，因为实际读取位置没有改变。

### 组件未初始化

如果 `demuxer_`、`Adecoder_`、`Vdecoder_`、`syncManager_` 任意为空，`SeekTo()` 应直接返回。

后续可以补日志，但本任务不需要扩大日志系统。

### 暂停中的 seek

暂停状态下 seek 后不要调用 `Start()`。

这意味着 seek 后队列可以恢复为可用状态，但 demux/decode/render/play 的 pause flag 仍应保持暂停语义。

## 和后续任务的关系

### 任务 6：Stop 回到起始位置

Stop 可以复用本任务的部分工具函数：

- `ClearQueues()`
- `FlushDecoders()`
- `syncManager_->SeekTo(0.0)`

但 Stop 还需要处理线程停止、状态设为 `Stopped`、再次 Start 从 0 开始等语义，不应在任务 5 中顺手完成。

### 任务 7：播放位置和总时长信号

本任务建议暴露 demuxer 当前位置，是为了让 `SeekRelative()` 能计算目标时间。

任务 7 后续会把位置能力正式提升为 Controller 接口：

```cpp
double Position() const;
double Duration() const;
```

并通过信号通知 UI：

```cpp
void PositionChanged(double seconds);
void DurationChanged(double seconds);
```

### 任务 8：进度条 Seek

进度条释放时只需要调用：

```cpp
mediaPlayController_->SeekTo(targetSeconds);
```

不需要知道队列、decoder、demuxer 和同步时钟细节。

## 验收标准

任务 5 完成后，需要重点验证：

- 快进后确实跳到新位置。
- 快退后确实跳到新位置。
- 快进/快退后音视频同步。
- seek 后不继续显示 seek 前的旧画面。
- seek 后不继续播放 seek 前的旧音频。
- 播放状态下 seek 后继续播放。
- 暂停状态下 seek 后仍保持暂停。
- seek 失败时播放管线不会卡死。

## 本任务不处理的内容

为了保持修改边界清晰，本任务不处理：

- Stop 回到起始位置。
- 进度条 UI。
- 时间显示 UI。
- 播放位置定时信号。
- 播放速度。
- 删除 `syncmanager::Forward()`。
- 改造 `MainWindow`。

这些内容分别属于任务 6、任务 7、任务 8 和任务 9。
