# Phase 3.2 多路管理架构预研报告

## 背景

Phase 3.1 已完成单路 `MediaPipeline` 抽取。当前架构已经具备明确的单路播放边界：

```text
MainWindow
    -> PlaylistManager
    -> MediaPlayController
    -> MediaPipeline
    -> demuxer / decoder / AudioPlayer / VideoRender / syncmanager / queues / threads
```

这为多路播放提供了前置条件：每一路媒体源可以由一个独立 `MediaPipeline` 管理，外层只需要负责多路生命周期、布局关联、选择状态和音频策略。

Phase 3.2 不应直接把多个 `MediaPipeline` 塞进 `MainWindow` 或 `MediaPlayController`。正确方向是新增一个多路管理层，让多路逻辑有独立边界。

## 阶段目标

Phase 3.2 的目标是设计并逐步引入 `MultiStreamManager`。

第一版目标：

- 管理多个单路 `MediaPipeline`。
- 为每一路分配稳定的 `StreamId`。
- 支持添加、移除、查询、启动、暂停、停止多路 Pipeline。
- 保持 `MediaPipeline` 本身不感知 id。
- 保持 `MainWindow` 不直接访问 `MediaPipeline`。
- 保持 `MediaPlayController` 作为 UI 对外入口。
- 暂不引入复杂多路 UI。
- 暂不引入 `MultiViewRenderer`。

## 目标结构

```text
MainWindow
    -> MediaPlayController
    -> MultiStreamManager
        -> MediaPipeline #1
        -> MediaPipeline #2
        -> MediaPipeline #3
```

后续如果继续保留播放列表模式，则文件播放列表仍可作为单路控制入口存在：

```text
MainWindow
    -> PlaylistManager
    -> MediaPlayController
    -> MultiStreamManager
```

## 职责边界

### MainWindow

`MainWindow` 仍然只负责 UI：

- 响应按钮、列表、布局控件事件。
- 把用户意图转发给 `MediaPlayController`。
- 不直接创建 `MediaPipeline`。
- 不直接持有 `MultiStreamManager`。
- 不直接访问 demuxer、decoder、AudioPlayer、VideoRender、syncmanager。

### MediaPlayController

`MediaPlayController` 是应用层入口。

Phase 3.2 之后它可以从“单路 Controller”逐步变成“播放应用控制器”：

- 持有 `MultiStreamManager`。
- 保留当前对 UI 的单路播放兼容接口。
- 转发多路管理信号给 UI。
- 保存用户级全局参数。
- 管理当前选中流 `activeStreamId_`。
- 管理当前音频流 `activeAudioStreamId_`。

不应该让 Controller 直接持有多个 `MediaPipeline`。

### MultiStreamManager

`MultiStreamManager` 是 Phase 3.2 的核心。

它负责：

- 管理多个 `MediaPipeline` 实例。
- 分配和回收 `StreamId`。
- 添加一路媒体源。
- 移除一路媒体源。
- 查询一路 Pipeline 是否存在。
- 对单路 Pipeline 执行 Start/Pause/Stop/Shutdown。
- 对所有 Pipeline 执行 StartAll/PauseAll/StopAll/ShutdownAll。
- 维护当前活动流。
- 维护当前音频输出流。
- 转发 Pipeline 信号，并附带 `StreamId`。

它不负责：

- 直接处理 UI 控件。
- 直接创建 `FileMediaSource`、CameraMediaSource 或 NetworkMediaSource 的具体 UI 交互。
- 决定复杂布局渲染算法。
- 做图像处理。
- 直接访问 demuxer、decoder 等基础设施对象。

### MediaPipeline

`MediaPipeline` 继续保持单路职责：

- 不知道自己属于第几路。
- 不持有 pipeline id。
- 不知道多路布局。
- 不知道其他 Pipeline。
- 只负责自己的媒体源、队列、组件、线程、同步和渲染。

`StreamId` 只属于 `MultiStreamManager` 管理层。

## StreamId 设计

多路阶段需要一个稳定标识符，但不能让 `MediaPipeline` 内部出现 id。

建议：

```cpp
using StreamId = int;
```

规则：

- `MultiStreamManager` 分配 `StreamId`。
- `StreamId` 只用于外层索引和信号标识。
- `MediaPipeline` 构造和运行不接收 `StreamId`。
- UI 只拿 `StreamId` 调用 Controller，不直接拿 Pipeline 指针。

信号示例：

```cpp
void StreamAdded(StreamId id);
void StreamRemoved(StreamId id);
void StreamPositionChanged(StreamId id, double seconds);
void StreamDurationChanged(StreamId id, double seconds);
void StreamPlaybackFinished(StreamId id);
void StreamErrorOccurred(StreamId id, QString message);
```

## 第一版 MultiStreamManager 接口建议

```cpp
class MultiStreamManager : public QObject {
    Q_OBJECT
public:
    using StreamId = int;

    explicit MultiStreamManager(QObject* parent = nullptr);
    ~MultiStreamManager();

    StreamId AddStream(std::shared_ptr<IMediaSource> mediaSource,
                       QWidget* rendererContainer);
    bool RemoveStream(StreamId id);
    bool HasStream(StreamId id) const;
    QList<StreamId> StreamIds() const;

    bool InitializeStream(StreamId id);
    void ShutdownStream(StreamId id);
    void ShutdownAll();

    void StartStream(StreamId id);
    void PauseStream(StreamId id);
    void StopStream(StreamId id);

    void StartAll();
    void PauseAll();
    void StopAll();

    bool SeekStreamTo(StreamId id, double seconds);
    bool SeekStreamRelative(StreamId id, double seconds);

    void SetActiveStream(StreamId id);
    StreamId ActiveStream() const;

    void SetActiveAudioStream(StreamId id);
    StreamId ActiveAudioStream() const;

signals:
    void StreamAdded(StreamId id);
    void StreamRemoved(StreamId id);
    void ActiveStreamChanged(StreamId id);
    void ActiveAudioStreamChanged(StreamId id);
    void StreamPositionChanged(StreamId id, double seconds);
    void StreamDurationChanged(StreamId id, double seconds);
    void StreamPlaybackFinished(StreamId id);
    void StreamErrorOccurred(StreamId id, QString message);
};
```

第一版可以把 AddStream 和 Initialize 合并，也可以先拆开。建议第一版先合并为一个动作，降低使用复杂度：

```cpp
StreamId AddStream(mediaSource, rendererContainer);
```

如果初始化失败，返回无效 id。

## 内部数据结构建议

```cpp
struct StreamSlot {
    std::shared_ptr<IMediaSource> source;
    std::unique_ptr<MediaPipeline> pipeline;
    QWidget* rendererContainer = nullptr;
};

QHash<StreamId, StreamSlot> streams_;
StreamId nextStreamId_ = 1;
StreamId activeStreamId_ = -1;
StreamId activeAudioStreamId_ = -1;
```

说明：

- `StreamSlot` 是管理层结构，不进入 `MediaPipeline`。
- `rendererContainer` 第一版可以是外部 UI 提供的容器。
- 后续如果引入 `RenderSurface` 抽象，可以替换 `QWidget*`。

## 音频策略

多路阶段不能默认每一路都输出音频。

第一版建议：

- 只允许一路 active audio stream。
- 新增一路时，如果当前没有 active audio stream，则该路成为 active audio stream。
- 非 active audio stream 默认静音。
- 切换 active audio stream 时：
  - 旧 active audio stream 静音。
  - 新 active audio stream 恢复当前 Controller 音量和静音状态。

当前 `MediaPipeline` 已有 `SetVolume()` 和 `SetMuted()`，因此第一版可以通过这些接口完成音频策略。

后续可以抽象为：

```text
AudioOutputPolicy
```

但 Phase 3.2 第一版不建议过早新增该类。

## 渲染策略

第一版建议继续使用多个 `VideoRender`：

```text
一个 MediaPipeline 对应一个 VideoRender
一个 VideoRender 放入一个 rendererContainer
```

原因：

- 符合当前 `MediaPipeline::Initialize(mediaSource, rendererContainer)` 接口。
- 不需要重写 OpenGL 渲染器。
- 有利于先验证多 Pipeline 生命周期。

暂不做：

- `MultiViewRenderer`
- 多纹理统一渲染
- 复杂布局算法
- 4/9/16 分屏性能优化

这些属于多路跑通后的渲染优化阶段。

## 文件源验证策略

当前没有摄像头硬件，但可以用文件源验证多路管理：

- 同时添加多个本地视频文件。
- 至少一个普通音视频文件。
- 至少一个无音频视频文件。
- 每一路独立 Start/Pause/Stop。
- 移除任意一路后其他路继续稳定。
- 切换 active stream 后，UI 状态跟随选中流。
- 切换 active audio stream 后，只有一路有声音。

无音频视频可以作为多路验证的重要样本，因为摄像头/工业相机经常没有音频流。

## 与现有 PlaylistManager 的关系

`PlaylistManager` 是文件播放列表，不等于多路流管理。

区别：

```text
PlaylistManager:
    管理一组待播放文件，同一时间当前主播放器播放其中一个。

MultiStreamManager:
    管理多个同时存在的播放管线，每一路都有独立生命周期。
```

Phase 3.2 不应把 PlaylistManager 改造成 MultiStreamManager。

后续可以让 UI 从播放列表选中文件后调用 Controller 添加一路流，但领域职责仍然不同。

## 与 Camera / RTSP 的关系

Phase 3.2 第一版只要求多路文件源验证。

摄像头和网络流后续通过 `IMediaSource` 接入：

- `CameraMediaSource`
- `NetworkMediaSource`
- 工业相机适配源

对 `MultiStreamManager` 来说，它们都只是 `IMediaSource`。

实时源差异需要后续能力描述：

- 是否 seekable。
- 是否有 duration。
- 是否有音频。
- 是否可暂停。
- 是否 EOF。

这些不在 Phase 3.2 第一版落地。

## 风险点

### 1. QWidget 生命周期

当前 `MediaPipeline` 创建 `VideoRender` 并添加到 `rendererContainer`。多路时每一路都需要独立容器。

风险：

- 移除某一路时，VideoRender 释放顺序必须稳定。
- rendererContainer 如果先被 UI 删除，Pipeline 再释放可能出现悬空。

第一版应保证 UI 先让 manager RemoveStream / ShutdownAll，再销毁容器。

### 2. 多路线程数量

每一路可能创建：

- demuxer thread
- video decoder thread
- audio decoder thread
- audio player thread

4 路文件源可能创建较多线程。第一版先验证 2 路，再扩展到 4 路。

### 3. 音频冲突

如果多个 Pipeline 同时拥有 AudioPlayer 并输出声音，会混乱。

第一版必须定义 active audio stream，非 active audio stream 默认静音。

### 4. 状态语义变化

当前 Controller 的 `State()`、`Position()`、`Duration()` 表示唯一当前媒体。

多路后需要区分：

- active stream 状态。
- 单路 stream 状态。
- 全局 all playing / partial playing 状态。

第一版建议 UI 仍显示 active stream 状态，避免一次性改太多 UI。

### 5. Pipeline 初始化失败回滚

添加一路失败时，必须释放已经创建的 Pipeline 和已打开媒体源，不能留下半初始化流。

## 不做范围

Phase 3.2 第一版不做：

- 多路复杂 UI。
- `MultiViewRenderer`。
- 多纹理统一渲染。
- 摄像头枚举。
- RTSP / RTMP。
- 工业相机 SDK。
- 图像处理。
- 多路录制。
- 音频源选择 UI。
- 多路性能优化。

## 结论

Phase 3.2 可以进入多路管理设计，但第一步必须是 `MultiStreamManager` 边界设计和空壳引入。

推荐路线：

1. 新增 `MultiStreamManager` 空壳。
2. 让它管理多个 `MediaPipeline`，但先不接 UI。
3. 用静态接口和少量内部逻辑验证生命周期边界。
4. 再考虑 UI 如何提供多个 renderer container。
5. 最后才做真正多路文件源播放。

核心原则：

```text
MediaPipeline 保持单路、无 id。
MultiStreamManager 管理多路和 id。
MediaPlayController 继续作为 UI 对外入口。
MainWindow 不直接接触 Pipeline。
```
