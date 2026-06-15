# Phase1.1 任务9：播放速度控制实现报告

## 目标

按修订后的方案实现基础倍速播放能力，支持 `0.5x / 1.0x / 1.5x / 2.0x` 四个固定档位。

本任务继续遵循架构边界：

- `MainWindow` 只负责快捷键事件转发。
- `MediaPlayController` 统一保存和下发播放速度。
- `AudioPlayer` 负责音频 PCM 变速和主时钟推进。
- `VideoRender` 只调整自调度检查频率，不修改主时钟。
- `syncmanager` 不引入倍速概念。

## 实施内容

### AudioPlayer

- 增加 `SetSpeed(double speed)`。
- 增加线程安全速度成员 `std::atomic<double> m_speed {1.0}`。
- 仅接受固定档位：`0.5 / 1.0 / 1.5 / 2.0`。
- 新增 `BuildSpeedAdjustedPcm()`：
  - 按 `bytesPerSampleFrame = m_dstChannels * m_bytesPerSample` 对齐处理 PCM。
  - `1.0x` 原样输出。
  - `2.0x` 每 2 个采样帧保留 1 个。
  - `1.5x` 每 3 个采样帧保留 2 个。
  - `0.5x` 每个采样帧输出 2 次。
- 新增 `WritePcmBuffer()`：
  - 按 chunk 写入 `QAudioSink`。
  - 统计实际写入声卡的字节数。
  - 根据输出采样帧数和速度估算媒体时间推进。
  - 写出过程中持续刷新 `syncmanager` 主时钟。

### VideoRender

- 增加 `SetSpeed(double speed)`。
- 增加线程安全速度成员 `std::atomic<double> m_speed {1.0}`。
- 仅接受固定档位：`0.5 / 1.0 / 1.5 / 2.0`。
- 在 `RenderFrame()` 中读取速度快照。
- 使用 `scaledInterval = qMax(1, qRound(m_frameIntervalMs / speedSnapshot))` 调整普通自调度检查频率。
- `diff > SYNC_THRESHOLD_MS` 的精确等待仍按音视频时间差处理。
- 不修改 `syncmanager` 主时钟。

### MediaPlayController

- 增加 `SetPlaybackSpeed(double speed)`。
- 增加 `PlaybackSpeed() const`。
- 增加 `PlaybackSpeedChanged(double speed)` 信号。
- 增加 `playbackSpeed_` 状态，默认 `1.0`。
- 统一校验固定档位。
- 速度变化后下发到 `AudioPlayer` 和 `VideoRender`。
- `Initialize()` 构建播放管线后重新下发当前速度，保证后续切源或重建管线时速度状态可保持。

### MainWindow

- 连接 `PlaybackSpeedChanged` 信号，输出当前速度日志。
- `keyReleaseEvent` 绑定快捷键：
  - `1` -> `0.5x`
  - `2` -> `1.0x`
  - `3` -> `1.5x`
  - `4` -> `2.0x`
- UI 不直接依赖 `AudioPlayer`、`VideoRender` 或 `syncmanager`。

## 已知限制

- 当前实现会变调。
- `0.5x` 通过重复采样帧实现，音质一般。
- `1.5x / 2.0x` 通过丢弃采样帧实现，音质一般。
- 只支持固定档位，不支持任意倍速。
- 硬件延迟暂未纳入倍速时钟修正。

## 静态检查结论

- 已执行 `git diff --check`。
- 未做编译验证。
- 未做运行验收，运行项需要后续手动测试确认。
