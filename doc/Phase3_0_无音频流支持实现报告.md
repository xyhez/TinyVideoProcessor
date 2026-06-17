# Phase 3.0 无音频流支持实现报告

## 背景

Phase 3 的目标是为多路视频、摄像头、网络流和后续 `MediaPipeline` 抽取做准备。

无音频流不是特殊边界，而是后续媒体源的常态之一：

- USB 摄像头可能只有视频流。
- 工业相机通常只有图像流。
- RTSP 监控流可能没有音频。
- 多路播放时也不能默认每一路都有音频输出。

因此本阶段先在当前单路播放管线中消除“音频必然存在”的假设，再进入 `MediaPipeline` 抽取。这样后续迁移的是正确的管线模型，而不是把现有问题复制到新类里。

## 实现目标

本阶段完成以下能力：

- 媒体源信息能明确表达是否存在音频流和视频流。
- demuxer 扫描流后填充媒体源能力字段。
- decoder 初始化入口能防御空 codec 参数。
- `MediaPlayController` 初始化播放管线时支持可选音频链路。
- `MediaPlayController` 的 Start/Pause/Stop/Seek 不再强依赖音频组件。
- `syncmanager` 显式支持音频主时钟和视频主时钟。
- `VideoRender` 在无音频场景下使用视频主时钟推进播放位置。

本阶段不改 UI，不引入多路播放，不抽取 `MediaPipeline`。

## 修改模块

### `src/domain/IMediaSource.h`

扩展 `Fileinfo`，新增媒体能力字段：

```cpp
bool hasVideo_ = false;
bool hasAudio_ = false;
int videoStreamIndex_ = -1;
int audioStreamIndex_ = -1;
AVCodecID videoCodecId_ = AV_CODEC_ID_NONE;
AVCodecID audioCodecId_ = AV_CODEC_ID_NONE;
```

字段含义：

- `hasVideo_`：当前媒体源是否存在视频流。
- `hasAudio_`：当前媒体源是否存在音频流。
- `videoStreamIndex_`：FFmpeg 中的视频流 index。
- `audioStreamIndex_`：FFmpeg 中的音频流 index。
- `videoCodecId_`：视频流 codec id。
- `audioCodecId_`：音频流 codec id。

保留旧字段 `CodecId_`，继续兼容现有代码。当前旧字段仍代表既有视频 codec 语义，后续可以在 `MediaPipeline` 抽取或媒体信息模型整理时再逐步收敛。

### `src/infrastructure/demuxer.cpp`

demuxer 扫描 FFmpeg streams 时写回 `Fileinfo`：

- 找到视频流：
  - `info.hasVideo_ = true`
  - `info.videoStreamIndex_ = i`
  - `info.videoCodecId_ = codec_id`
  - 继续填充宽高、帧率、视频 time base、duration
  - 继续填充旧字段 `CodecId_`

- 找到音频流：
  - `info.hasAudio_ = true`
  - `info.audioStreamIndex_ = i`
  - `info.audioCodecId_ = codec_id`
  - 继续填充音频 time base

无音频流时，`Fileinfo` 默认保持：

```cpp
hasAudio_ = false;
audioStreamIndex_ = -1;
audioCodecId_ = AV_CODEC_ID_NONE;
```

### `src/infrastructure/demuxer.h/.cpp`

新增：

```cpp
void SetAudioQueues(std::shared_ptr<SyncQueue<AVPacket*>> APacket,
                    std::shared_ptr<SyncQueue<AVFrame*>> AFrame);
```

原因：

`hasAudio_` 是 demuxer 扫描流后才能确定的。为了避免 Controller 重复扫描 FFmpeg stream，初始化顺序调整为：

```text
先创建 demuxer 扫描媒体能力
    -> Controller 读取 Fileinfo.hasAudio_
    -> 有音频时创建音频队列
    -> 通过 SetAudioQueues 绑定给 demuxer
```

这样能力识别仍由 demuxer 完成，Controller 只按媒体源能力构建管线。

### `src/infrastructure/decoder.cpp`

`decoder::InitDecoder()` 增加入口防御：

- `coderPar == nullptr` 时返回失败。
- `packetVec == nullptr` 时返回失败。
- `framevec == nullptr` 时返回失败。

这样即使上层误传空音频 codec 参数，也不会继续访问 `par->codec_id` 导致崩溃。

这不是无音频支持的主要架构方案，只是基础设施层应有的防御。

### `src/application/MediaPlayController.cpp`

`InitializeOpenedMediaSource()` 调整为按能力构建管线。

始终创建：

- 视频包队列
- 视频帧队列
- demuxer
- 视频 decoder
- `VideoRender`
- `syncmanager`
- position timer
- demuxer 线程
- 视频 decoder 线程

仅当 `sourceInfo.hasAudio_ == true` 时创建：

- 音频包队列
- 音频帧队列
- 音频 decoder
- `AudioPlayer`
- 音频 decoder 线程
- AudioPlayer 线程

仅有音频时执行：

- 音频 decoder 初始化
- `AudioPlayer` 初始化
- 音频播放速度下发
- 音量和静音状态下发

无音频时，Controller 仍保留自身的音量和静音状态，但不会创建无意义的音频输出。

### `MediaPlayController` 运行控制

运行控制改为“视频链路必选，音频链路可选”：

- `CreateConnects()`：音频线程、音量、静音信号只在音频对象存在时连接。
- `Start()`：不再要求音频 decoder 和 `AudioPlayer` 存在；有音频时才启动音频线程和音频组件。
- `Pause()`：有音频时暂停音频；无音频时只暂停 demuxer、视频 decoder、VideoRender。
- `Stop()`：不再要求音频 decoder 存在。
- `SeekTo()`：不再要求音频 decoder 存在。
- `SuspendPipelineAndWait()`：已有空指针判断，继续保持。
- `ReleaseResources()`：已有空指针安全释放，继续保持。

这一步解决的是“无音频时 Controller 不应该访问不存在的音频对象”。

### `src/infrastructure/syncmanager.h/.cpp`

新增同步模式接口：

```cpp
void SetSyncType(SYNCTYPE type);
SYNCTYPE SyncType() const;
```

默认同步模式仍为：

```cpp
SYNCTYPE::AUDIO
```

Controller 初始化时根据媒体能力设置：

```text
hasAudio_ == true  -> SYNCTYPE::AUDIO
hasAudio_ == false -> SYNCTYPE::VIDEO
```

UI 仍然只读取 `MediaPlayController::Position()`，不需要知道当前同步模式。

### `src/infrastructure/videorender.cpp`

`VideoRender::RenderFrame()` 根据同步模式选择策略：

#### AUDIO 模式

保持原有行为：

- 使用 `syncmanager->GetClock()` 作为音频主时钟。
- 视频帧过早则等待。
- 视频帧过晚则丢帧追赶。
- 同步窗口内渲染。

#### VIDEO 模式

用于无音频流：

- 不等待音频时钟。
- 不按音频时钟判断丢帧。
- 正常取出视频帧渲染。
- 渲染后使用视频 PTS 刷新主时钟：

```cpp
videoPtsMs = frame->pts * m_videoTimeBase * 1000.0;
syncManager->RefreshClock(videoPtsMs);
```

下一帧仍按帧率和播放速度调度。

这样 `MediaPlayController::Position()` 继续读取同一个主时钟，但无音频时钟来源变为视频渲染链路。

## 架构边界检查

本阶段符合当前架构路线：

- `MainWindow` 没有新增对 demuxer、decoder、AudioPlayer、VideoRender、syncmanager 的直接依赖。
- UI 没有判断文件是否有音频。
- 媒体能力由 `Fileinfo` 表达。
- demuxer 负责从 FFmpeg stream 中填充媒体能力。
- Controller 只根据媒体能力构建当前单路播放管线。
- 音频输出没有被伪装为空播放器。
- 同步模式由播放管线内部处理，不暴露给 UI。

## 为什么必须在 MediaPipeline 前完成

`MediaPipeline` 的职责是管理一路媒体源的完整播放资源。

如果在抽取前不解决无音频问题，后续迁移时会把以下错误假设搬进 `MediaPipeline`：

- 每路都有音频队列。
- 每路都有音频 decoder。
- 每路都有 `AudioPlayer`。
- 每路都用音频主时钟。
- Start/Pause/Seek 都依赖音频链路。

这些假设在多路视频、摄像头、工业相机和 RTSP 场景下都不成立。

因此 Phase 3.0 的价值不是“兼容一个无音频文件”，而是为后续单路 `MediaPipeline` 抽取提供正确的管线模型：

```text
媒体源能力
    -> 按能力构建音视频链路
    -> 有音频走 AUDIO 主时钟
    -> 无音频走 VIDEO 主时钟
    -> UI 不关心底层差异
```

## 未做范围

本阶段明确不做：

- 音频-only 文件播放。
- 摄像头设备枚举。
- 摄像头实时预览。
- RTSP/RTMP 网络流。
- 多路播放 UI。
- `MediaPipeline` 抽取。
- `MultiStreamManager`。
- 多路音频源选择。
- 实时流 `seekable_` / `live_` 能力字段落地。
- 音量持久化。

这些能力应在 Phase 2、Phase 3.1 之后继续推进。

## 需要人工验证的内容

无音频视频：

- 打开不崩溃。
- 播放后视频正常渲染。
- 进度条随视频推进。
- 暂停和恢复正常。
- Seek 后画面跳到目标位置附近。
- 0.5x、1.0x、1.5x、2.0x 播放速度有效。
- 播放完成后窗口可以关闭。
- 播放中关闭窗口不阻塞。
- 暂停后关闭窗口不阻塞。

普通音视频文件回归：

- 音频正常播放。
- 视频正常渲染。
- 音视频同步不退化。
- 音量和静音仍有效。
- Seek 后无旧音频残留。
- Seek 后无旧视频帧闪回。
- 播放速度控制仍有效。
- 播放列表切换不退化。

## 后续建议

如果用户测试通过，Phase 3.0 可以收口。

下一步建议进入：

```text
Phase 3.1：单路 MediaPipeline 抽取
```

Phase 3.1 的重点不是新增功能，而是把当前已经稳定的单路播放管线资源从 `MediaPlayController` 中迁移到 `MediaPipeline`，同时保持 UI 和用户行为不变。
