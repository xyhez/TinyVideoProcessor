# Phase1.1 播放控制完成报告

## 目标

Phase1.1 的目标是补齐本地播放器的核心播放控制能力，包括真实 Seek、快进快退、Stop 回到起点、进度条、播放位置/时长信号和固定档位倍速播放。

本阶段继续遵循 `doc/架构指导.md`：

- `MainWindow` 只负责 UI 交互和事件转发。
- `MediaPlayController` 负责编排播放控制流程。
- `demuxer / decoder / audioplayer / videorender / syncmanager` 保持基础设施职责。
- 不新增全局变量。
- 不从 `AVFrame::time_base` 推导同步时间。

## 完成范围

### 任务 1：播放控制链路梳理

已完成当前播放链路梳理，明确 `MainWindow` 不应直接接触基础设施层，后续播放控制统一从 `MediaPlayController` 进入。

产出：

- `doc/Phase1_1_任务1_播放控制链路梳理报告.md`

### 任务 2：队列 Seek 能力

已补齐队列清空、唤醒和恢复能力，Seek/Stop 时可以清空音频包队列、视频包队列、音频帧队列、视频帧队列，并保证清空后可继续接收新数据。

产出：

- `doc/Phase1_1_任务2_队列Seek能力报告.md`

### 任务 3：demuxer 真实 Seek

`demuxer` 已增加真实 `SeekTo(double seconds)` 和 `SeekRelative(double seconds)` 能力，内部使用 `av_seek_frame()`，并处理目标时间越界和 seek 失败返回。

产出：

- `doc/Phase1_1_任务3_demuxer真实Seek报告.md`

### 任务 4：decoder Flush

音频 decoder 和视频 decoder 已提供统一 `Flush()` 入口，Seek/Stop 时可以清理解码器内部缓存，避免旧帧继续输出。

产出：

- `doc/Phase1_1_任务4_decoderFlush报告.md`

### 任务 5：MediaPlayController 编排 Seek

`MediaPlayController` 已完成完整 Seek 编排：

1. 暂停播放管线并等待安全点。
2. 清空四个队列。
3. 调用 demuxer seek。
4. flush 音频/视频 decoder。
5. 重置同步时钟。
6. 恢复 seek 前播放状态。

后续又补充了管线 Suspend 屏障和视频 preroll 丢弃逻辑，解决 seek 后旧帧、H264 参考帧和音视频同步问题。

产出：

- `doc/Phase1_1_任务5_Seek流程设计.md`

### 任务 6：Stop 回到起始位置

Stop 已从“停止线程”调整为“停止输出并回到起点”：

- Stop 不再直接让队列进入终止状态。
- Stop 会暂停管线、清空队列、seek 到 0、flush decoder、重置主时钟。
- Stop 后再次 Start 从开头播放。
- 暂停后退出卡住的问题已通过修正 demuxer/decoder 挂起屏障幂等性解决。

产出：

- `doc/Phase1_1_任务6_Stop行为报告.md`

### 任务 7：播放位置和总时长信号

`MediaPlayController` 已提供：

- `Duration()`
- `Position()`
- `PositionChanged(double seconds)`
- `DurationChanged(double seconds)`

位置更新由 Controller 内部 200ms 定时器驱动。Start/Stop/Seek/Pause/Initialize 会立即发射必要的位置信号或时长信号。

产出：

- `doc/Phase1_1_任务7_播放位置和时长信号报告.md`

### 任务 8：进度条 Seek

`MainWindow` 已接入进度条和时间标签：

- `label_current`
- `slider_progress`
- `label_duration`

UI 只通过 `MediaPlayController::PositionChanged`、`DurationChanged` 和 `SeekTo()` 交互。拖动过程中只更新预览时间，不触发高频 seek；释放后才执行一次 seek。

产出：

- `doc/Phase1_1_任务8_进度条Seek接口报告.md`

### 任务 9：播放速度控制

已实现固定档位倍速：

- `0.5x`
- `1.0x`
- `1.5x`
- `2.0x`

实现边界：

- `AudioPlayer` 负责 PCM 变速和主时钟推进。
- `VideoRender` 只调整自调度检查频率，不修改主时钟。
- `MediaPlayController` 保存并下发播放速度。
- `MainWindow` 只绑定数字键 `1/2/3/4` 转发速度切换。

产出：

- `doc/Phase1_1_任务9_播放速度控制预研报告.md`
- `doc/Phase1_1_任务9_播放速度控制实现报告.md`

## 验收结果

- 播放、暂停、停止行为边界清晰。
- Stop 后回到 0，再次播放从开头开始。
- 快进/快退是真实 seek。
- 进度条拖动是真实 seek，且不会高频触发。
- Seek 后音视频同步。
- Seek 后无旧包、旧帧、旧音频残留。
- 暂停状态下 seek 后保持暂停，播放状态下 seek 后继续播放。
- 播放位置和总时长可用于 UI 进度条。
- `0.5x / 1.0x / 1.5x / 2.0x` 可以切换。
- Pause/Seek/Stop 后倍速状态保持。
- 暂停后退出程序不再卡住。
- `MainWindow` 没有新增基础设施层依赖。
- 没有新增全局变量。

## 架构检查

### UI 层

`MainWindow` 只调用 `MediaPlayController`：

- `Start()`
- `Pause()`
- `Stop()`
- `SeekTo()`
- `Rewind()`
- `Forward()`
- `SetPlaybackSpeed()`

没有直接依赖 `demuxer`、`decoder`、`syncmanager`、`AudioPlayer` 或 `VideoRender`。

### 应用层

`MediaPlayController` 是播放控制流程的唯一编排者，负责：

- 管线生命周期。
- Seek/Stop/Pause/Start 编排。
- 位置和时长信号。
- 播放速度状态。

### 基础设施层

基础设施层只提供各自能力：

- `demuxer`：读取、真实 seek、挂起安全点。
- `decoder`：解码、flush、挂起安全点。
- `AudioPlayer`：音频输出、音量、倍速 PCM 输出、主时钟刷新。
- `VideoRender`：视频渲染、自调度、跟随音频主时钟。
- `syncmanager`：保存和刷新主时钟。

## 已知限制

- 播放速度控制当前会变调。
- 只支持固定倍速档位，不支持任意倍速。
- 倍速音频质量是基础实现，后续如需更高质量可考虑 SoundTouch、Rubber Band 或 FFmpeg `atempo`。
- Phase1.1 尚未处理播放列表、切换文件、循环模式，这些进入 Phase1.2。

## 静态检查

- 已执行 `git diff --check`。
- 未处理本地 `CMakeLists.txt.user`。

## 结论

Phase1.1 播放控制已完成，可以进入 Phase1.2 文件列表管理。
