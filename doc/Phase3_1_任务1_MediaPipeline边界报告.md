# Phase 3.1 任务 1：MediaPipeline 边界报告

## 目标

本报告只做静态边界梳理，不修改代码。

目标是明确：

- `MediaPlayController` 当前承担了哪些职责。
- 哪些字段和方法应迁入 `MediaPipeline`。
- 哪些字段、方法和信号应继续留在 `MediaPlayController`。
- Controller 和 Pipeline 的信号转发关系。
- 音量/静音在单路阶段和后续多路阶段的归属差异。

## 当前问题

当前 `MediaPlayController` 同时承担两类职责：

1. 应用层职责：
   - 暴露 UI 调用接口。
   - 维护用户可见播放状态。
   - 保存播放速度、音量、静音等用户级状态。
   - 向 UI 发出状态、进度、时长、音量、静音、播放完成信号。

2. 单路播放管线职责：
   - 创建音视频队列。
   - 创建 demuxer、decoder、AudioPlayer、VideoRender、syncmanager。
   - 创建并管理 QThread。
   - 初始化、释放、暂停、恢复、seek、flush、清空队列。
   - 处理 demux EOF。

Phase 3.1 要把第二类职责迁入 `MediaPipeline`，让 Controller 回到应用层协调角色。

## 当前字段清单

### 应迁入 MediaPipeline 的字段

以下字段属于单路播放管线内部资源，应迁入 `MediaPipeline`：

```cpp
std::shared_ptr<SyncQueue<AVPacket*>> APacket_;
std::shared_ptr<SyncQueue<AVPacket*>> VPacket_;
std::shared_ptr<SyncQueue<AVFrame*>>  AFrame_;
std::shared_ptr<SyncQueue<AVFrame*>>  VFrame_;
```

原因：队列是 demuxer、decoder、AudioPlayer、VideoRender 之间的内部数据通道，UI 和 Controller 不应该直接持有。

```cpp
std::shared_ptr<IMediaSource> mediaSource_;
```

原因：当前媒体源是单路管线的输入。Controller 只需要把媒体源交给 Pipeline 初始化，不应长期持有媒体源并参与管线释放。

```cpp
std::shared_ptr<syncmanager> syncManager_;
```

原因：同步时钟属于单路管线内部同步策略。Controller 只需要通过 Pipeline 查询 `Position()`。

```cpp
std::unique_ptr<demuxer> demuxer_;
std::unique_ptr<decoder> Adecoder_;
std::unique_ptr<decoder> Vdecoder_;
std::unique_ptr<AudioPlayer> audioplay_;
VideoRender* videoRender_;
```

原因：这些都是基础设施层播放组件，应由 `MediaPipeline` 统一管理。

```cpp
std::unique_ptr<QThread> demuxerTH_;
std::unique_ptr<QThread> AdecoderTH_;
std::unique_ptr<QThread> VdecoderTH_;
std::unique_ptr<QThread> audioplayTH_;
```

原因：线程生命周期和基础设施组件绑定，应随管线一起创建、停止、释放。

```cpp
std::unique_ptr<QTimer> positionTimer_;
```

原因：该定时器的职责是周期性从管线主时钟读取播放位置并发出 `PositionChanged`。它依赖 `syncManager_`，因此应迁入 Pipeline，由 Pipeline 发出位置变化信号。

### 应留在 MediaPlayController 的字段

以下字段属于应用层或用户级状态，应留在 Controller：

```cpp
PlaybackState state_ = PlaybackState::Stopped;
```

原因：播放状态是 UI 可见的应用状态。Pipeline 负责执行动作，Controller 负责将动作结果转成 UI 状态。

```cpp
double playbackSpeed_ = 1.0;
double volume_ = 1.0;
bool muted_ = false;
```

原因：这些是用户设置状态。切换媒体源或重建 Pipeline 后，应由 Controller 重新下发给新 Pipeline。

### Controller 新增字段

Phase 3.1 迁移完成后，Controller 应持有：

```cpp
std::unique_ptr<MediaPipeline> pipeline_;
```

Controller 头文件不应再 include 以下基础设施头：

- `core/queue.h`
- `infrastructure/audioplayer.h`
- `infrastructure/decoder.h`
- `infrastructure/demuxer.h`
- `infrastructure/syncmanager.h`
- `infrastructure/videorender.h`
- `QThread`
- `QTimer`

Controller 只需要 include：

- `QObject`
- `memory`
- `domain/IMediaSource.h`
- `domain/MediaPipeline.h`

## 当前方法清单

### 应迁入 MediaPipeline 的方法

以下方法本质上操作播放管线内部资源，应迁入 `MediaPipeline`。

#### `InitializeOpenedMediaSource()`

迁入为：

```cpp
bool MediaPipeline::Initialize(std::shared_ptr<IMediaSource> mediaSource,
                               QWidget* rendererContainer);
```

原因：

- 创建队列。
- 创建 demuxer/decoder/audio/render/sync。
- 根据 `Fileinfo.hasAudio_` 创建可选音频链路。
- 设置同步模式。
- 创建线程。
- 初始化组件。
- 添加 `VideoRender` 到渲染容器。

这些都属于单路管线初始化，不属于应用层。

#### `CreateConnects()`

迁入 Pipeline。

原因：

- 连接 QThread started 到 demuxer/decoder/AudioPlayer。
- 连接 demux EOF 到 Pipeline 内部处理。
- 连接 position timer。
- 连接 AudioPlayer 音量/静音信号。

迁移后 Pipeline 发出自己的信号，Controller 只负责转发。

#### `ReleaseResources()`

迁入为：

```cpp
void MediaPipeline::Shutdown();
```

原因：

- 停止队列。
- 停止 demuxer/decoder/audio/render。
- quit/wait 线程。
- reset 基础设施组件。
- close 媒体源。

这些是管线生命周期，不应留在 Controller。

#### `HandleDemuxFinished()`

迁入 Pipeline。

原因：

- EOF 后停止队列和 position timer。
- Pipeline 应发出 `PlaybackFinished()` 和最终位置。
- Controller 接收后设置 `PlaybackState::Stopped` 并转发 UI 信号。

#### `SuspendPipelineAndWait()`

迁入 Pipeline。

原因：它直接控制 demuxer/decoder/render/audio 的暂停和安全点等待。

#### `ResetPipelineToStart()`

迁入 Pipeline。

原因：它直接操作 VideoRender、AudioPlayer、队列、demuxer seek、decoder flush、sync clock。

#### `ClearQueues()`

迁入 Pipeline。

原因：队列属于 Pipeline。

#### `FlushDecoders()`

迁入 Pipeline。

原因：decoder 属于 Pipeline。

### 需要拆分的方法

以下方法当前既有应用状态逻辑，又有管线执行逻辑，需要拆分。

#### `Start()`

Controller 保留：

- 设置 `PlaybackState::Playing`。
- 调用 `pipeline_->Start()`。
- 发出或转发必要状态信号。

Pipeline 承担：

- 启动 position timer。
- 启动 demuxer/video/audio 线程。
- 调用 demuxer/decoder/AudioPlayer/VideoRender 的 Start。

#### `Pause()`

Controller 保留：

- 设置 `PlaybackState::Paused`。
- 调用 `pipeline_->Pause()`。

Pipeline 承担：

- 暂停 demuxer。
- 暂停 decoder。
- 暂停 AudioPlayer。
- 暂停 VideoRender。
- 停止 position timer。

#### `Stop()`

Controller 保留：

- 设置 `PlaybackState::Stopped`。
- 调用 `pipeline_->Stop()`。

Pipeline 承担：

- 挂起管线。
- 清空队列。
- demuxer seek 到 0。
- flush decoder。
- sync clock 归零。
- 发出 `PositionChanged(0.0)`。

#### `SeekTo()`

Controller 保留：

- 根据当前 `PlaybackState` 记录 seek 前状态。
- 调用 `pipeline_->SeekTo(seconds)`。
- seek 后按原状态恢复播放或保持暂停。

Pipeline 承担：

- clamp 目标时间。
- 挂起管线。
- 清队列。
- demuxer seek。
- flush decoder。
- 设置视频丢帧阈值。
- sync clock seek。
- 发出 `PositionChanged()`。

#### `SeekRelative()`

可以由 Controller 保留为：

```cpp
SeekTo(Position() + seconds);
```

也可以委托：

```cpp
pipeline_->SeekRelative(seconds);
```

建议第一版保留在 Controller，降低迁移面。

### 应留在 MediaPlayController 的方法

以下方法属于应用层对外接口或用户级状态，第一版留在 Controller：

- `Initialize()`
- `SwitchSource()`
- `Start()`
- `Pause()`
- `Stop()`
- `Shutdown()`
- `SeekTo()`
- `SeekRelative()`
- `Rewind()`
- `Forward()`
- `SetPlaybackSpeed()`
- `PlaybackSpeed()`
- `SetVolume()`
- `Volume()`
- `SetMuted()`
- `IsMuted()`
- `State()`
- `SetState()`

但它们内部不应直接操作基础设施组件，而是调用 `pipeline_`。

### 应改为委托 Pipeline 的查询方法

```cpp
double Duration() const;
double Position() const;
```

迁移后实现应为：

```cpp
return pipeline_ ? pipeline_->Duration() : 0.0;
return pipeline_ ? pipeline_->Position() : 0.0;
```

## 信号边界

### Pipeline 应发出的信号

```cpp
void PositionChanged(double seconds);
void DurationChanged(double seconds);
void PlaybackFinished();
void VolumeChanged(double volume);
void MuteChanged(bool muted);
void ErrorOccurred(QString message);
```

### Controller 应继续对 UI 暴露的信号

保持现有信号不变：

```cpp
void StateChanged(PlaybackState state);
void PositionChanged(double seconds);
void DurationChanged(double seconds);
void PlaybackSpeedChanged(double speed);
void PlaybackFinished();
void VolumeChanged(double volume);
void MuteChanged(bool muted);
```

### 转发关系

Controller 初始化 Pipeline 后建立连接：

- `pipeline_->PositionChanged` -> `MediaPlayController::PositionChanged`
- `pipeline_->DurationChanged` -> `MediaPlayController::DurationChanged`
- `pipeline_->PlaybackFinished` -> Controller 设置 `Stopped`，再 emit `PlaybackFinished`
- `pipeline_->VolumeChanged` -> 更新 Controller `volume_`，再 emit `VolumeChanged`
- `pipeline_->MuteChanged` -> 更新 Controller `muted_`，再 emit `MuteChanged`
- `pipeline_->ErrorOccurred` -> 暂时记录日志，后续可扩展 Controller 级错误信号

UI 仍只连接 Controller。

## 音量和静音归属

### 单路阶段

第一版允许 `MediaPipeline` 暴露：

```cpp
void SetVolume(double volume);
void SetMuted(bool muted);
```

原因：

- 当前 `AudioPlayer` 是单路 Pipeline 内部资源。
- Controller 保存用户级状态，Pipeline 负责把状态下发到 AudioPlayer。
- 无音频流时 Pipeline 可以保存或忽略下发，不创建空 AudioPlayer。

### 多路阶段

多路阶段音频策略不能继续简单放在单个 Pipeline 内：

- 多路播放通常只允许一路音频输出。
- 静音可能是全局静音，也可能是某路静音。
- 音频源选择需要上层策略。

因此后续可能引入：

```text
MultiStreamManager
AudioOutputPolicy
```

本阶段只为单路抽取服务，不提前设计多路音频策略。

## 禁止提前引入的内容

Phase 3.1 不应引入：

- pipeline id
- `MultiStreamManager`
- 多路布局
- 多个 `MediaPipeline`
- active audio pipeline
- 多路音频选择
- 多路渲染器
- 摄像头枚举
- RTSP/RTMP

这些属于后续阶段。

## 迁移后的目标依赖

### MainWindow

```text
MainWindow
    -> PlaylistManager
    -> FileMediaSource
    -> MediaPlayController
```

不依赖基础设施层。

### MediaPlayController

```text
MediaPlayController
    -> IMediaSource
    -> MediaPipeline
```

不直接依赖 demuxer/decoder/audio/render/sync/queue/thread。

### MediaPipeline

```text
MediaPipeline
    -> IMediaSource
    -> SyncQueue
    -> demuxer
    -> decoder
    -> AudioPlayer
    -> VideoRender
    -> syncmanager
    -> QThread/QTimer
```

负责单路管线内部资源。

## 迁移顺序建议

1. 新增 `MediaPipeline` 空壳。
2. 把资源字段复制到 `MediaPipeline`，先不删 Controller 字段。
3. 迁移初始化和释放，Controller 调 Pipeline。
4. 迁移 Start/Pause/Stop/Seek。
5. 迁移速度、音量、静音下发。
6. 迁移信号转发。
7. 删除 Controller 中已迁移的资源字段和私有方法。
8. 清理 Controller include。
9. 做回归验收。

## 验收标准

本任务完成标准：

- 已明确字段迁移边界。
- 已明确方法迁移边界。
- 已明确信号转发关系。
- 已说明音量/静音在单路阶段和多路阶段的归属差异。
- 未提前引入多路概念。

## 结论

`MediaPipeline` 第一版应只抽取当前单路播放管线，不改变 UI 语义，不引入多路概念。

`MediaPlayController` 后续应退回应用层：

```text
保存用户级状态
维护播放状态
调用 Pipeline
转发 Pipeline 信号
```

`MediaPipeline` 则成为单路播放资源的唯一所有者：

```text
媒体源 + 队列 + 基础设施组件 + 线程 + 同步 + 生命周期
```

这个边界可以支撑后续 Phase 3.1 的代码迁移，也为真正多路阶段留下清晰扩展点。
