# Phase 3 多路播放架构预研报告

## 背景

Phase 1 已完成本地文件播放器基础功能，包括播放控制、Seek、播放列表、音量、播放速度、全屏等能力。

用户后续会购买 USB 摄像头，但当前硬件还未到货。在没有摄像头硬件的情况下，不建议直接实现真正的多路摄像头播放。更合理的路线是先做多路播放的前置架构准备：把当前单路播放管线从 `MediaPlayController` 中抽离为 `MediaPipeline`。

本预研只做静态分析和架构设计，不改代码。

## 当前架构现状

### 当前分层

当前主链路是：

```text
MainWindow
    -> PlaylistManager
    -> FileMediaSource
    -> MediaPlayController
    -> demuxer / decoder / AudioPlayer / VideoRender / syncmanager / queues / threads
```

当前 `MainWindow` 已经不直接持有 demuxer/decoder/audio/render/sync 等基础设施层播放组件，播放管线资源主要集中在 `MediaPlayController`。

### MediaPlayController 当前承担的职责

`MediaPlayController` 目前承担两类职责：

一类是应用层对外控制职责：

- `Start()`
- `Pause()`
- `Stop()`
- `Shutdown()`
- `SeekTo()`
- `SeekRelative()`
- `SetPlaybackSpeed()`
- `SetVolume()`
- `SetMuted()`
- `SwitchSource()`
- 状态信号转发：`StateChanged`、`PositionChanged`、`DurationChanged`、`PlaybackFinished` 等

另一类是单路播放管线资源管理职责：

- 创建四个队列：
  - 音频包队列
  - 视频包队列
  - 音频帧队列
  - 视频帧队列
- 创建基础设施组件：
  - `demuxer`
  - 音频 `decoder`
  - 视频 `decoder`
  - `AudioPlayer`
  - `VideoRender`
  - `syncmanager`
- 创建线程：
  - demuxer 线程
  - 音频 decoder 线程
  - 视频 decoder 线程
  - audio player 线程
- 初始化各组件之间的队列、time base、frame rate、同步时钟。
- 管理管线暂停、恢复、flush、清空队列、释放资源。

### 当前多路扩展阻塞点

当前代码可以完成单路播放，但不适合直接扩展多路，原因是：

1. `MediaPlayController` 只能持有一套播放资源。
2. `VideoRender` 只有一个，绑定一个 `rendererContainer`。
3. `AudioPlayer` 只有一个，默认所有播放都按单音频链路设计。
4. `syncmanager` 只有一个，当前音视频同步围绕单路主时钟设计。
5. `PositionChanged`、`DurationChanged` 等状态信号默认表示“当前唯一媒体”。
6. `SwitchSource()` 的语义是替换当前管线，而不是新增一路管线。

因此不能直接在 `MediaPlayController` 里加 `vector<demuxer>`、`vector<decoder>` 这类改法。那会把当前已经稳定的单路 Controller 变成复杂资源容器，后续摄像头、RTSP、工业相机都会更难接入。

## 架构目标

Phase 3 的核心目标应是：

```text
一个 MediaPipeline 管理一路媒体源的完整播放资源。
MultiStreamManager 管理多个 MediaPipeline。
MediaPlayController 作为应用层入口，先持有一个 MediaPipeline，后续再委托 MultiStreamManager。
```

第一阶段不直接做多路 UI，而是先把当前单路播放管线抽象出来。

## 建议目标结构

### 单路阶段

第一阶段只抽单路：

```text
MainWindow
    -> PlaylistManager
    -> MediaPlayController
    -> MediaPipeline
    -> demuxer / decoder / AudioPlayer / VideoRender / syncmanager / queues / threads
```

此时外部行为必须保持和现在一致。

### 多路阶段

后续真正多路：

```text
MainWindow
    -> MediaPlayController
    -> MultiStreamManager
        -> MediaPipeline #1
        -> MediaPipeline #2
        -> MediaPipeline #3
```

每个 `MediaPipeline` 有独立：

- `IMediaSource`
- demuxer
- decoder
- queues
- syncmanager
- renderer
- threads

音频需要单独策略，不能每路都默认播放声音。

## MediaPipeline 职责设计

### 应该进入 MediaPipeline 的内容

以下内容当前在 `MediaPlayController` 中，但本质属于单路播放管线，应该迁入 `MediaPipeline`：

- 四个队列：
  - `APacket_`
  - `VPacket_`
  - `AFrame_`
  - `VFrame_`
- 当前媒体源：
  - `mediaSource_`
- 同步时钟：
  - `syncManager_`
- 基础设施组件：
  - `demuxer_`
  - `Adecoder_`
  - `Vdecoder_`
  - `audioplay_`
  - `videoRender_`
- 工作线程：
  - `demuxerTH_`
  - `AdecoderTH_`
  - `VdecoderTH_`
  - `audioplayTH_`
- 管线内部操作：
  - `InitializeOpenedMediaSource()`
  - `CreateConnects()`
  - `ReleaseResources()`
  - `SuspendPipelineAndWait()`
  - `ResetPipelineToStart()`
  - `ClearQueues()`
  - `FlushDecoders()`
  - `HandleDemuxFinished()`

### 应该留在 MediaPlayController 的内容

以下内容属于应用层对外语义，第一阶段建议仍留在 `MediaPlayController`：

- 播放状态：
  - `PlaybackState state_`
  - `SetState()`
- 用户级持久状态：
  - `playbackSpeed_`
  - `volume_`
  - `muted_`
- UI 对外接口：
  - `Start()`
  - `Pause()`
  - `Stop()`
  - `SeekTo()`
  - `SeekRelative()`
  - `SetPlaybackSpeed()`
  - `SetVolume()`
  - `SetMuted()`
  - `SwitchSource()`
- UI 对外信号：
  - `StateChanged`
  - `PositionChanged`
  - `DurationChanged`
  - `PlaybackSpeedChanged`
  - `VolumeChanged`
  - `MuteChanged`
  - `PlaybackFinished`

Controller 第一阶段只持有：

```cpp
std::unique_ptr<MediaPipeline> pipeline_;
```

并把用户级状态下发给 pipeline。

## MediaPipeline 第一版接口建议

第一版 `MediaPipeline` 只服务单路，不急于暴露多路复杂接口。

建议接口：

```cpp
class MediaPipeline : public QObject {
    Q_OBJECT
public:
    explicit MediaPipeline(QObject* parent = nullptr);
    ~MediaPipeline();

    bool Initialize(std::shared_ptr<IMediaSource> mediaSource,
                    QWidget* rendererContainer);

    void Start();
    void Pause();
    void Stop();
    void Shutdown();

    bool SeekTo(double seconds);
    bool SeekRelative(double seconds);

    double Duration() const;
    double Position() const;

    void SetPlaybackSpeed(double speed);
    void SetVolume(double volume);
    void SetMuted(bool muted);

signals:
    void PositionChanged(double seconds);
    void DurationChanged(double seconds);
    void PlaybackFinished();
    void VolumeChanged(double volume);
    void MuteChanged(bool muted);
    void ErrorOccurred(QString message);
};
```

### 需要谨慎的接口

`SetVolume()` / `SetMuted()` 是否进入 `MediaPipeline` 需要谨慎。

单路阶段可以进入，因为当前 audio player 属于管线资源。

多路阶段可能需要重新定义音频策略：

- 多路视频只允许一路音频输出。
- 静音可能是全局静音，也可能是某路静音。
- 音频源选择应由上层策略管理。

因此第一版可以让 `MediaPipeline` 暴露音量接口，但要在报告和代码注释里说明：多路阶段音频策略可能上移到 `MultiStreamManager` 或专门的 `AudioOutputPolicy`。

## MultiStreamManager 后续设计

真正多路阶段再引入：

```cpp
class MultiStreamManager : public QObject {
    Q_OBJECT
public:
    int AddPipeline(std::shared_ptr<IMediaSource> source, QWidget* renderContainer);
    void RemovePipeline(int pipelineId);
    MediaPipeline* Pipeline(int pipelineId);

    void StartAll();
    void PauseAll();
    void StopAll();

    void SetActiveAudioPipeline(int pipelineId);
};
```

多路阶段关键策略：

- 每路独立视频渲染。
- 每路独立同步时钟。
- 默认只开启一路音频。
- 布局管理和 pipeline 管理分离。
- 文件、USB 摄像头、RTSP、工业相机都通过媒体源抽象或专门帧源抽象接入。

## 渲染策略预研

### 方案 A：多个 VideoRender

每一路一个 `VideoRender`，每个 `VideoRender` 是独立 `QOpenGLWidget`。

优点：

- 最容易从当前代码演进。
- 每路资源边界清楚。
- 适合第一版多路验证。

缺点：

- 多个 OpenGL widget 可能带来上下文和性能成本。
- 布局切换、单路放大、多路缩放时需要额外管理。

### 方案 B：MultiViewRenderer

一个统一 `QOpenGLWidget` 管理多路纹理。

优点：

- 更适合 4 路、9 路、16 路视频墙。
- 渲染性能和布局控制更好。

缺点：

- 需要重写渲染抽象。
- 需要让每路 pipeline 输出帧或纹理到统一渲染器。
- 改动大，不适合作为第一步。

### 建议

第一阶段使用方案 A：

```text
一个 MediaPipeline 对应一个 VideoRender
```

等多路跑通后，再预研 `MultiViewRenderer`。

## 与摄像头 Phase 2 的关系

摄像头硬件未到货时，可以先抽 `MediaPipeline`，因为文件源也能验证单路管线抽取。

等 C920 到货后，再新增：

```text
UsbCameraMediaSource / CameraMediaSource
```

并通过同一个 `MediaPipeline` 播放。

需要提前注意：摄像头实时流和文件源不同：

- duration 可能为 0。
- Seek 不可用。
- 可能没有音频流。
- EOF 语义不同。
- 帧率可能不稳定。

这些差异应由媒体源能力描述或 pipeline 查询接口暴露，不能在 UI 中硬编码判断。

## 当前已知风险

### 1. 无音频流视频

之前测试发现无音频流视频会异常。多路和摄像头都可能遇到无音频源，所以抽 `MediaPipeline` 前应记录该风险。

后续应支持：

- 只有视频流。
- 只有音频流。
- 音视频都有。
- 实时视频无 duration。

### 2. MainWindow 当前构造 FileMediaSource

当前 `MainWindow` 会根据播放列表项构造 `FileMediaSource`，然后传入 `MediaPlayController::SwitchSource()`。

这在文件播放阶段可以接受，但到摄像头/RTSP/工业相机阶段，需要引入媒体源创建策略，例如：

```text
MediaSourceFactory
```

或者由应用层根据用户选择创建不同 `IMediaSource`。

### 3. 音频策略

当前每个播放管线都有自己的 `AudioPlayer`。多路时不能默认每路都播放音频。

第一版多路建议：

- 只允许一个 active audio pipeline。
- 其他 pipeline 禁用音频输出或静音。
- UI 后续提供音频源选择。

### 4. Controller 状态语义

当前 `State()`、`Position()`、`Duration()` 都表示唯一当前媒体。

多路后需要区分：

- 全局状态。
- 当前选中 pipeline 状态。
- 单个 pipeline 状态。

因此第一阶段只做单路 pipeline 抽取，不立刻改变 UI 和状态语义。

## 推荐实施顺序

### 3.1 单路 MediaPipeline 抽取预研

只写文档，不改代码：

- 明确迁移字段。
- 明确迁移方法。
- 明确 Controller 和 Pipeline 边界。
- 明确信号转发方式。

### 3.2 新增 MediaPipeline 空壳

新增类，但不迁移逻辑：

- `src/domain/MediaPipeline.h`
- `src/domain/MediaPipeline.cpp`
- 加入 CMake。
- 暂时只定义接口和空实现。

### 3.3 迁移资源所有权

把队列、组件、线程从 `MediaPlayController` 迁移到 `MediaPipeline`。

验收标准：

- UI 不变。
- 单文件播放正常。
- 播放列表切换正常。
- 关闭窗口正常。

### 3.4 迁移播放控制编排

把以下逻辑迁入 `MediaPipeline`：

- `Start`
- `Pause`
- `Stop`
- `SeekTo`
- `SeekRelative`
- `Shutdown`
- `ReleaseResources`
- `ClearQueues`
- `FlushDecoders`
- `SuspendPipelineAndWait`

Controller 只做状态协调和信号转发。

### 3.5 迁移状态信号

Pipeline 发出底层状态：

- `PositionChanged`
- `DurationChanged`
- `PlaybackFinished`
- `VolumeChanged`
- `MuteChanged`
- `ErrorOccurred`

Controller 转发给 UI。

### 3.6 单路回归验收

在没有摄像头硬件的情况下，只用文件源验收：

- 播放、暂停、停止。
- 快进、快退、进度条 Seek。
- 播放速度。
- 音量和静音。
- 播放列表切换。
- 全屏和鼠标隐藏。
- 播放完成后关闭。
- 暂停后关闭。
- 播放中关闭。

### 3.7 真正多路管理预研

在单路 pipeline 稳定后，再设计：

- `MultiStreamManager`
- 多路布局。
- 多路音频策略。
- 多路 UI。
- 多路资源上限和性能目标。

## 不建议现在做的事

- 不建议直接做 4 路播放 UI。
- 不建议直接创建多个 `MediaPlayController`。
- 不建议把 `MediaPlayController` 改成持有多个 demuxer/decoder vector。
- 不建议先做 `MultiViewRenderer`。
- 不建议在摄像头到货前写死摄像头设备名。
- 不建议把工业相机 SDK 接入和多路播放混在同一阶段。

## 结论

可以开始多路播放方向，但第一步必须是架构前置，而不是直接多路功能。

最合理的下一步是：

```text
Phase 3.1：单路 MediaPipeline 抽取预研
```

核心目标是把当前 `MediaPlayController` 中的播放管线资源和资源生命周期迁移到 `MediaPipeline`，同时保持 UI 和用户行为完全不变。

只有单路 `MediaPipeline` 稳定后，才进入真正的 `MultiStreamManager` 和多路 UI。
