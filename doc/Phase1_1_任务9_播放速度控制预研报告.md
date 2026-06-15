# Phase 1.1 任务 9: 播放速度控制预研报告

## 1. 目标

在不引入新第三方 DSP 库的前提下，为当前本地播放器补齐基础倍速播放能力。

本任务只支持固定档位：

- `0.5x`
- `1.0x`
- `1.5x`
- `2.0x`

当前 Phase 允许变调，不追求“不变调变速”。后续如果需要更高质量的音频变速，再评估 SoundTouch、Rubber Band 或 FFmpeg `atempo` filter。

## 2. 架构边界

继续遵循 `doc/架构指导.md`：

- `MainWindow` 只负责快捷键或 UI 事件转发。
- `MediaPlayController` 负责统一播放速度入口、状态保存和下发。
- `AudioPlayer` 负责音频 PCM 变速和音频主时钟刷新。
- `VideoRender` 负责视频帧渲染和自调度检查频率调整。
- `syncmanager` 保持主时钟存取职责，不引入倍速概念。
- 不新增全局变量。
- 不从 `AVFrame::time_base` 推导同步时间。

## 3. 当前链路

### 3.1 AudioPlayer

当前音频链路：

```text
AFrame queue
  -> Pop AVFrame
  -> swr_convert 重采样到 S16 PCM
  -> QAudioSink::write
  -> syncmanager::RefreshClock
```

当前主时钟刷新方式：

```cpp
double audioclock = tmp->pts * m_audioTimeBase * 1000;
m_syncManager->RefreshClock(audioclock);
```

这个方式在正常速度下可用，但在慢放时会有问题：如果同一个音频帧被重复输出，主时钟仍停留在该帧开头 PTS，视频会等待或抖动。因此倍速实现不能只改变 PCM 写出量，还要按输出进度刷新音频主时钟。

### 3.2 VideoRender

当前视频链路是 `QTimer::singleShot` 自调度：

```text
RenderFrame
  -> Front 查看队首帧
  -> videoPts - audioClock
  -> 太早则等待 diff
  -> 太晚则丢帧追赶
  -> 同步窗口内渲染
  -> ScheduleNextFrame
```

视频同步核心仍然是音频主时钟。倍速后，VideoRender 不应该直接修改主时钟，也不应该绕开 `videoPts - audioClock` 的同步判断。

### 3.3 syncmanager

`syncmanager` 只保存主时钟：

- `RefreshClock(double clock)`：更新主时钟，单位毫秒。
- `GetClock()`：返回主时钟，单位毫秒。
- `SeekTo(double clockMs)`：seek 后重置主时钟，单位毫秒。

倍速不应该让 `syncmanager` 变复杂。主时钟推进策略放在 AudioPlayer。

## 4. 修订后的方案

### 4.1 固定档位，不做任意精度

旧方案计划把速度截断到 `0.05` 精度，这会让 PCM 抽样、重复和时钟推进都变复杂。

本阶段只支持四个固定档位：

| 档位 | 策略 | 说明 |
|------|------|------|
| 0.5x | 重复采样帧 | 音调降低，可接受 |
| 1.0x | 原样输出 | 默认 |
| 1.5x | 每 3 个采样帧保留 2 个 | 音调升高，可接受 |
| 2.0x | 每 2 个采样帧保留 1 个 | 音调升高，可接受 |

输入值由 `MediaPlayController` 统一校验。可以选择“只接受完全匹配的档位”，也可以选择“归一化到最近档位并打印日志”。为了行为明确，推荐第一版直接拒绝不支持的值。

### 4.2 AudioPlayer 线程安全速度状态

`SetSpeed()` 通常由 UI 主线程经 Controller 调用，`AudioPlayer::Run()` 在音频线程中读取速度。速度成员必须线程安全。

推荐：

```cpp
std::atomic<double> m_speed {1.0};
```

如果当前 MSVC/Qt 环境对 `std::atomic<double>` 支持不理想，则改用 `QMutex` 保护。

`Run()` 中不要在一个 chunk 内多次读取速度。应在处理一个 PCM chunk 前读取一次 speed snapshot，保证该 chunk 内行为稳定。

### 4.3 PCM 必须按采样帧对齐

当前输出格式是交错 PCM。一次完整采样帧大小为：

```cpp
bytesPerSampleFrame = m_dstChannels * m_bytesPerSample;
```

所有写入、跳过、重复都必须按 `bytesPerSampleFrame` 对齐。不能直接用任意字节数做 `chunk / speed`，否则可能截断单个采样帧，造成噪声、爆音或声道错位。

建议先把重采样后的 PCM 按采样帧处理：

- `1.0x`：原样写出。
- `2.0x`：每 2 个采样帧保留 1 个。
- `1.5x`：每 3 个采样帧保留 2 个。
- `0.5x`：每个采样帧输出 2 次。

这不是高质量变速算法，但实现清晰、依赖干净，适合 Phase 1。

### 4.4 AudioPlayer 按输出进度刷新主时钟

倍速后的主时钟不能只取当前 AVFrame 的起始 PTS。建议在写 PCM 时估算当前媒体时间：

```cpp
frameStartClockMs = tmp->pts * m_audioTimeBase * 1000;
mediaElapsedMs = outputSampleFramesWritten * speed / m_dstRate * 1000;
currentClockMs = frameStartClockMs + mediaElapsedMs;
m_syncManager->RefreshClock(currentClockMs);
```

含义：

- `outputSampleFramesWritten` 是当前音频帧已经实际写给声卡的输出采样帧数。
- `speed` 用来把“输出时间”换算回“媒体时间”。
- 0.5x 时，输出 1 秒音频只推进 0.5 秒媒体时间。
- 2.0x 时，输出 0.5 秒音频推进 1 秒媒体时间。

这样慢放时主时钟会持续推进，不会长时间卡在同一个 PTS。

硬件延迟 `CalculateHardwareLatency()` 暂不纳入第一版倍速时钟修正，避免一次改动过大。如果运行测试发现画面稳定偏早，再单独处理延迟补偿。

### 4.5 VideoRender 只调整检查频率

VideoRender 可以增加 `SetSpeed(double)`，但它不负责同步语义。

它只做一件事：调整自调度检查频率。

```cpp
scaledInterval = qMax(1, qRound(m_frameIntervalMs / speedSnapshot));
```

使用范围：

- 队列空时的下一次检查。
- 正常渲染一帧后的下一次检查。
- 其他纯“回退等待”的调度。

不使用范围：

- `diff > SYNC_THRESHOLD_MS` 时的精确等待仍然使用 `diff`。
- `diff < -SYNC_THRESHOLD_MS` 时仍按原逻辑丢帧追赶。
- 不修改 `syncmanager` 主时钟。

这样可以让高倍速下视频检查更勤快，但不会抢走音频主时钟的同步职责。

## 5. 组件改动

### 5.1 AudioPlayer

新增：

```cpp
void SetSpeed(double speed);
```

新增线程安全成员：

```cpp
std::atomic<double> m_speed {1.0};
```

核心改动：

- `SetSpeed()` 校验固定档位。
- PCM 写出按采样帧对齐。
- 0.5x 重复采样帧。
- 1.5x/2.0x 丢弃采样帧。
- 每个输出 chunk 后按输出进度刷新主时钟。

### 5.2 VideoRender

新增：

```cpp
void SetSpeed(double speed);
```

新增线程安全成员：

```cpp
std::atomic<double> m_speed {1.0};
```

核心改动：

- `SetSpeed()` 校验固定档位。
- 在 `RenderFrame()` 内读取速度快照。
- 用速度快照缩放普通调度间隔。
- 保留现有音视频同步判断。

### 5.3 MediaPlayController

新增：

```cpp
void SetPlaybackSpeed(double speed);
double PlaybackSpeed() const;
```

新增信号：

```cpp
void PlaybackSpeedChanged(double speed);
```

新增状态：

```cpp
double playbackSpeed_ = 1.0;
```

职责：

- 校验固定档位。
- 保存当前速度。
- 下发到 `AudioPlayer` 和 `VideoRender`。
- 初始化播放管线后重新下发当前速度，保证 Stop/Start 或后续切换源时速度状态保持。
- 对外发射 `PlaybackSpeedChanged`。

### 5.4 MainWindow

只做事件转发：

- `1` -> `SetPlaybackSpeed(0.5)`
- `2` -> `SetPlaybackSpeed(1.0)`
- `3` -> `SetPlaybackSpeed(1.5)`
- `4` -> `SetPlaybackSpeed(2.0)`

`MainWindow` 不直接依赖 `AudioPlayer`、`VideoRender` 或 `syncmanager`。

## 6. 与现有播放控制的交互

| 操作 | 预期行为 |
|------|----------|
| Start | 使用当前 `playbackSpeed_` 播放 |
| Pause | 暂停后恢复，速度保持 |
| Stop | 回到起点，速度状态保持 |
| Seek | Seek 后速度保持 |
| Rewind/Forward | 只改变位置，不改变速度 |
| 后续 SwitchSource | 新管线初始化后重新下发当前速度 |

速度是播放状态的一部分，不因 Pause/Seek/Stop 自动重置。

## 7. 风险和限制

- 音频会变调，这是当前 Phase 的已知限制。
- 0.5x 通过重复采样帧实现，音质一般，但实现依赖干净。
- 1.5x/2.0x 通过丢弃采样帧实现，音质一般。
- 不支持任意倍速输入，避免抽样比例和时钟估算过早复杂化。
- 硬件延迟暂不纳入第一版倍速时钟修正，必要时后续单独优化。

## 8. 验收标准

- [ ] 0.5x/1.0x/1.5x/2.0x 可以切换。
- [ ] 无效速度输入被拒绝或明确归一化。
- [ ] PCM 写入始终按采样帧对齐。
- [ ] 0.5x 慢放时主时钟持续推进，视频不长时间冻结。
- [ ] 1.5x/2.0x 快放时视频能跟随音频主时钟。
- [ ] Pause/Seek/Stop 后速度状态保持。
- [ ] `MainWindow` 不直接依赖基础设施层。
