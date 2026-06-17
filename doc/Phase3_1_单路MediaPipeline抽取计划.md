# Phase 3.1 单路 MediaPipeline 抽取计划

## 背景

Phase 3.0 已完成无音频流支持。当前单路播放管线已经具备以下基础：

- 媒体源能力由 `Fileinfo` 表达。
- 音频链路可以按 `hasAudio_` 可选创建。
- 同步模式可以在 AUDIO / VIDEO 之间切换。
- 无音频视频可以通过视频主时钟推进播放位置。

这意味着当前播放管线已经具备被抽取为单路 `MediaPipeline` 的前置条件。

Phase 3.1 的目标不是做多路播放，而是把当前 `MediaPlayController` 中属于“单路播放管线资源和生命周期”的内容迁移到 `MediaPipeline`，让 Controller 回到应用层协调角色。

## 阶段目标

第一版 `MediaPipeline` 只服务单路播放，不引入多路管理器，不改变 UI 行为。

目标结构：

```text
MainWindow
    -> PlaylistManager
    -> MediaPlayController
    -> MediaPipeline
    -> demuxer / decoder / AudioPlayer / VideoRender / syncmanager / queues / threads
```

Phase 3.1 完成后：

- `MediaPlayController` 不再直接持有队列、demuxer、decoder、AudioPlayer、VideoRender、syncmanager、QThread。
- `MediaPlayController` 仍负责对 UI 暴露播放状态和用户级控制接口。
- `MediaPipeline` 负责当前单路媒体源的播放资源、线程、初始化、释放、Start/Pause/Stop/Seek。
- UI 行为保持不变。

## 架构边界

### MainWindow

保持现状：

- 只处理 UI 事件。
- 只调用 `MediaPlayController`。
- 不直接访问 `MediaPipeline`。
- 不直接访问 demuxer、decoder、AudioPlayer、VideoRender、syncmanager。
- 不判断音频流是否存在。

### MediaPlayController

保留应用层职责：

- 管理用户可见播放状态：
  - `PlaybackState state_`
  - `SetState()`
- 保存用户级状态：
  - `playbackSpeed_`
  - `volume_`
  - `muted_`
- 对 UI 暴露统一接口：
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
  - `SetVolume()`
  - `SetMuted()`
  - `Duration()`
  - `Position()`
- 转发管线信号：
  - `PositionChanged`
  - `DurationChanged`
  - `PlaybackFinished`
  - `VolumeChanged`
  - `MuteChanged`

Controller 第一版只持有：

```cpp
std::unique_ptr<MediaPipeline> pipeline_;
```

### MediaPipeline

负责单路播放管线内部资源：

- 当前媒体源：
  - `mediaSource_`
- 队列：
  - `APacket_`
  - `VPacket_`
  - `AFrame_`
  - `VFrame_`
- 基础设施组件：
  - `demuxer_`
  - `Adecoder_`
  - `Vdecoder_`
  - `audioplay_`
  - `videoRender_`
  - `syncManager_`
- 线程：
  - `demuxerTH_`
  - `AdecoderTH_`
  - `VdecoderTH_`
  - `audioplayTH_`
- 管线内部生命周期：
  - `Initialize()`
  - `Shutdown()`
  - `ReleaseResources()`
  - `CreateConnects()`
  - `ClearQueues()`
  - `FlushDecoders()`
  - `SuspendAndWait()`
  - `ResetToStart()`
  - `HandleDemuxFinished()`

## 第一版接口设计

建议新增：

- `src/domain/MediaPipeline.h`
- `src/domain/MediaPipeline.cpp`

第一版接口：

```cpp
class MediaPipeline : public QObject {
    Q_OBJECT
public:
    explicit MediaPipeline(QObject* parent = nullptr);
    ~MediaPipeline();

    bool Initialize(std::shared_ptr<IMediaSource> mediaSource,
                    QWidget* rendererContainer);
    void Shutdown();

    void Start();
    void Pause();
    void Stop();

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

说明：

- `MediaPipeline` 第一版可以暴露 `SetVolume()` / `SetMuted()`，因为当前 `AudioPlayer` 是管线内部资源。
- 多路阶段音频策略可能上移到 `MultiStreamManager` 或 `AudioOutputPolicy`，本阶段先不处理。
- `MediaPipeline` 不负责 UI 状态枚举，播放状态仍由 Controller 维护。

## 迁移原则

### 不一次性大搬迁

迁移顺序必须可验证：

1. 新增空壳，不改变行为。
2. 迁移资源所有权，但 Controller 仍可通过 pipeline 调用。
3. 迁移初始化和释放。
4. 迁移运行控制。
5. 迁移信号转发。
6. 最后清理 Controller 成员。

### 不改变 UI 行为

Phase 3.1 不新增 UI，也不改变按键、按钮、播放列表、音量、进度条行为。

### 不引入多路概念

本阶段不出现：

- `MultiStreamManager`
- pipeline id
- 多个 renderer
- 多路布局
- active audio pipeline

这些属于 Phase 3.2 之后。

### 保持 Phase 3.0 能力

抽取后必须继续支持：

- 有音频视频：音频主时钟。
- 无音频视频：视频主时钟。
- 音频链路可选。
- 播放速度。
- 音量和静音。
- Seek。
- 播放列表切换。
- 播放完成后关闭。

## 推荐实施步骤

### 3.1.1 抽取边界预研

只做静态分析和报告，确认：

- 哪些字段从 Controller 迁移到 Pipeline。
- 哪些方法从 Controller 迁移到 Pipeline。
- 哪些信号由 Pipeline 发出，Controller 转发。
- 哪些状态仍由 Controller 持有。

产出：

- `doc/Phase3_1_任务1_MediaPipeline边界报告.md`

### 3.1.2 新增 MediaPipeline 空壳

新增文件：

- `src/domain/MediaPipeline.h`
- `src/domain/MediaPipeline.cpp`

只定义接口和空实现，加入构建系统。

验收：

- 现有播放行为不变。
- Controller 暂不使用 Pipeline。

### 3.1.3 迁移资源所有权

把以下成员从 `MediaPlayController` 迁入 `MediaPipeline`：

- 队列
- 媒体源
- syncmanager
- demuxer
- decoder
- AudioPlayer
- VideoRender
- QThread
- position timer

Controller 改为持有：

```cpp
std::unique_ptr<MediaPipeline> pipeline_;
```

验收：

- Controller 头文件不再 include 基础设施层播放组件。
- Controller 不再直接持有播放管线资源。

### 3.1.4 迁移初始化和释放

把以下逻辑迁入 `MediaPipeline`：

- `InitializeOpenedMediaSource()`
- `CreateConnects()`
- `ReleaseResources()`
- `HandleDemuxFinished()`

Controller 的 `Initialize()` / `SwitchSource()` 调用 Pipeline 的 `Initialize()` / `Shutdown()`。

验收：

- 打开文件正常。
- 切换文件正常。
- 关闭程序正常。
- 无音频视频仍正常。

### 3.1.5 迁移播放控制

把以下逻辑迁入 `MediaPipeline`：

- `Start()`
- `Pause()`
- `Stop()`
- `SeekTo()`
- `SeekRelative()`
- `ClearQueues()`
- `FlushDecoders()`
- `SuspendPipelineAndWait()`
- `ResetPipelineToStart()`

Controller 只负责：

- 设置 `PlaybackState`
- 调用 `pipeline_->Start()` / `Pause()` / `Stop()` / `SeekTo()`
- 根据调用结果决定是否恢复状态

验收：

- Start/Pause/Stop 行为不变。
- Seek 行为不变。
- 播放速度行为不变。
- 有音频和无音频都正常。

### 3.1.6 迁移状态信号

Pipeline 发出内部状态信号：

- `PositionChanged`
- `DurationChanged`
- `PlaybackFinished`
- `VolumeChanged`
- `MuteChanged`
- `ErrorOccurred`

Controller 连接这些信号并转发给 UI。

验收：

- UI 仍只连接 Controller。
- 进度条、时间、音量、静音、播放完成行为不变。

### 3.1.7 回归验收和报告

产出：

- `doc/Phase3_1_单路MediaPipeline抽取实现报告.md`

验收：

- 普通音视频文件播放正常。
- 无音频视频播放正常。
- Seek 正常。
- 播放速度正常。
- 音量和静音正常。
- 播放列表切换正常。
- 播放完成后关闭正常。
- 播放中关闭和暂停后关闭不阻塞。
- `MediaPlayController` 不直接持有基础设施层播放资源。

## 风险点

### 1. 线程生命周期迁移风险

QThread 的 quit/wait 顺序必须保持稳定。迁移时不能把释放顺序打散。

### 2. Qt 对象线程归属风险

demuxer、decoder、AudioPlayer 移动线程后，销毁时要确保线程已停止。

### 3. VideoRender 父子关系风险

`VideoRender` 是 QWidget，仍需要绑定 `rendererContainer`。迁移到 Pipeline 后要保持：

- 创建在 UI 线程。
- 添加到 renderer container。
- 释放顺序安全。

### 4. 信号重复连接风险

切换媒体源时 Pipeline 重建，必须避免旧信号残留或重复连接。

### 5. 播放状态语义风险

Pipeline 不应反向拥有 UI 层播放状态。`PlaybackState` 第一版仍留在 Controller。

## 不做范围

本阶段不做：

- 多路播放。
- 多路 UI。
- `MultiStreamManager`。
- 多路音频源选择。
- 摄像头枚举。
- RTSP/RTMP。
- 音频-only 文件。
- 实时流 seekable/live 能力落地。
- 重写渲染器。

## 完成标准

- `MediaPlayController` 回到应用层协调角色。
- 单路播放资源集中在 `MediaPipeline`。
- UI 行为不变。
- Phase 3.0 无音频能力不退化。
- 后续可以进入真正多路管理设计。
