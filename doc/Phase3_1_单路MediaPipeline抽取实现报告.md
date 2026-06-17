# Phase 3.1 单路 MediaPipeline 抽取实现报告

## 目标

Phase 3.1 的目标是把 `MediaPlayController` 中属于单路播放管线的资源、线程、初始化、释放和运行控制迁移到 `MediaPipeline`，让 Controller 回到应用层协调角色。

本阶段只抽取单路管线，不引入多路播放、不新增多路 UI、不引入 `MultiStreamManager`，也不引入 pipeline id。

## 最终结构

```text
MainWindow
    -> PlaylistManager
    -> MediaPlayController
    -> MediaPipeline
    -> demuxer / decoder / AudioPlayer / VideoRender / syncmanager / queues / threads
```

## 已完成内容

### 1. 新增单路 MediaPipeline

新增 `src/domain/MediaPipeline.h` 和 `src/domain/MediaPipeline.cpp`。

`MediaPipeline` 当前负责：

- 持有当前媒体源。
- 创建和释放音视频包队列、帧队列。
- 创建和释放 `demuxer`、音视频 `decoder`、`AudioPlayer`、`VideoRender`、`syncmanager`。
- 创建和管理 demuxer、decoder、audio player 工作线程。
- 管理 position timer。
- 处理内部信号连接。
- 处理播放完成。
- 承接 `Start()`、`Pause()`、`Stop()`、`SeekTo()`、`SeekRelative()`。
- 承接播放速度、音量、静音参数下发。

### 2. Controller 回到应用层协调角色

`MediaPlayController` 当前只保留：

- `std::unique_ptr<MediaPipeline> pipeline_`
- `PlaybackState state_`
- `playbackSpeed_`
- `volume_`
- `muted_`
- 对 UI 暴露的播放控制接口。
- Pipeline 信号转发。

`MediaPlayController` 已不再直接持有：

- `SyncQueue`
- `IMediaSource` 实例状态
- `syncmanager`
- `demuxer`
- `decoder`
- `AudioPlayer`
- `VideoRender`
- `QThread`
- `QTimer`

`MediaPlayController.h` 也已移除对基础设施播放组件和队列的直接 include。

### 3. 初始化和释放迁移

原 Controller 中的初始化与释放职责已迁入 `MediaPipeline`：

- 队列创建
- demuxer 创建
- decoder 创建
- audio player 创建
- video render 创建
- syncmanager 创建
- position timer 创建
- 线程创建和对象 moveToThread
- 信号槽连接
- 停止组件
- 停止线程
- 释放对象
- 关闭媒体源

Controller 的 `Initialize()`、`SwitchSource()`、`Shutdown()` 已委托 Pipeline。

### 4. 播放控制迁移

以下管线控制逻辑已迁入 `MediaPipeline`：

- `Start()`
- `Pause()`
- `Stop()`
- `SeekTo()`
- `SeekRelative()`
- `SuspendPipelineAndWait()`
- `ResetPipelineToStart()`
- `ClearQueues()`
- `FlushDecoders()`

Controller 只负责：

- 播放状态编排。
- Seek 前后状态恢复。
- 对 UI 发出状态变化。
- 调用 Pipeline 执行实际管线动作。

### 5. 用户级参数迁移

Controller 继续保存用户级状态：

- 播放速度
- 音量
- 静音

Pipeline 负责下发到具体组件：

- 播放速度下发到 `AudioPlayer` 和 `VideoRender`。
- 音量下发到 `AudioPlayer`。
- 静音下发到 `AudioPlayer`。
- 无音频流时，音量和静音只发出 Pipeline 信号，不访问空音频链。

切换媒体源后，Controller 会重新下发当前 speed / volume / muted 到 Pipeline。

### 6. Phase 3.0 能力保持

本阶段保留了 Phase 3.0 的无音频流能力：

- 通过 `Fileinfo.hasAudio_` 判断是否创建音频链。
- 无音频流时不创建 audio packet queue、audio frame queue、audio decoder、AudioPlayer 和 audio thread。
- `syncmanager` 在无音频流时使用视频主时钟。
- 无音频视频可以播放、暂停、停止、seek、快进、快退和关闭。

## 架构边界验收

### MainWindow

验收结果：通过。

- 不直接访问 `MediaPipeline`。
- 不直接访问 demuxer、decoder、AudioPlayer、VideoRender、syncmanager。
- 仍只通过 `MediaPlayController` 和 `PlaylistManager` 工作。

### MediaPlayController

验收结果：通过。

- 不再直接 include 基础设施播放组件。
- 不再直接持有播放队列、组件和线程。
- 不再直接创建或连接基础设施播放对象。
- 保留应用层状态和用户级参数。
- 对 UI 的接口保持稳定。

### MediaPipeline

验收结果：通过。

- 只管理单路媒体源。
- 没有引入 pipeline id。
- 没有引入 `MultiStreamManager`。
- 没有引入多路 UI。
- 播放管线资源集中在 Pipeline 内部。

## 运行验收

用户已完成运行验证，结论通过。

已验证项目：

- 普通音视频文件播放正常。
- 无音频视频播放正常。
- Start / Pause / Stop 正常。
- Seek 正常。
- 快进 / 快退正常。
- 播放速度正常。
- 音量和静音正常。
- 播放列表切换正常。
- 播放完成后关闭正常。
- 播放中关闭不阻塞。
- 暂停后关闭不阻塞。
- 无音频视频设置音量和静音不崩溃。

## 不做范围确认

本阶段未做：

- 多路播放。
- 多路 UI。
- `MultiStreamManager`。
- pipeline id。
- 多路音频源选择。
- 摄像头枚举。
- RTSP / RTMP。
- 音频-only 文件。
- 渲染器重写。

## 结论

Phase 3.1 已完成。

当前代码已经具备清晰的单路播放管线边界：

- `MainWindow` 负责 UI。
- `MediaPlayController` 负责应用层状态和用户级控制。
- `MediaPipeline` 负责单路播放资源和生命周期。
- 基础设施层组件继续负责具体 demux、decode、audio、render、sync。

后续可以进入真正多路管理设计。在进入多路之前，建议先基于当前单路 Pipeline 设计 `MultiStreamManager` 的职责边界，避免直接把多个 Pipeline 塞进 UI 或 Controller。
