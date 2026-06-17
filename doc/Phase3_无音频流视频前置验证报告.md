# Phase 3 无音频流视频前置验证报告

## 背景

用户当前手里已有无音频流视频文件。虽然摄像头硬件还未到货，但无音频流是摄像头、工业相机、RTSP 监控流、多路视频中非常常见的输入形态。

因此在正式抽取 `MediaPipeline` 前，必须先确认当前单路播放链路对“无音频流”的支持状态。否则后续把 `MediaPlayController` 内部资源迁移到 `MediaPipeline` 时，会把“音频必然存在”的假设一起固化进去，导致多路阶段返工。

本报告只做静态分析和架构预研，不修改代码。

## 当前结论

当前代码不能可靠支持无音频流视频。

主要原因不是 demuxer 不能识别视频包，而是播放管线初始化和运行控制默认认为音频链路一定存在：

- `MediaPlayController` 总是创建音频包队列、音频帧队列、音频 decoder、`AudioPlayer`、音频线程。
- `MediaPlayController` 总是把 `demuxer_->ReAcoderPar()` 传给音频 decoder 和 `AudioPlayer`。
- 无音频流时 `demuxer_->ReAcoderPar()` 返回 `nullptr`。
- `decoder::InitDecoder()` 没有对空 `AVCodecParameters*` 做保护，会继续访问 `par->codec_id`。
- `VideoRender` 当前以 `syncmanager` 中的音频主时钟为同步基准；无音频时没有稳定的音频时钟推进者。

因此无音频文件可能在初始化阶段直接崩溃，也可能在绕过初始化问题后出现视频不走、进度不走、seek 后同步异常等问题。

## 当前链路检查

### 1. 媒体源信息不足

位置：`src/domain/IMediaSource.h`

当前 `Fileinfo` 记录的信息主要是：

- 文件路径
- 视频宽高
- 一个 `CodecId_`
- 总时长
- 帧率
- 音频 time base
- 视频 time base

缺少以下关键能力描述：

- 是否存在视频流
- 是否存在音频流
- 视频流 index
- 音频流 index
- 视频 codec id 和音频 codec id 的区分
- 媒体源是否可 seek
- 媒体源是否为实时流

这会导致上层只能通过“音频 time base 是否为 0”或“音频 codec 参数是否为空”间接判断音频是否存在。这个判断方式不适合继续扩展摄像头、网络流和工业相机。

### 2. demuxer 可以读视频包，但缺少显式能力接口

位置：`src/infrastructure/demuxer.cpp`

demuxer 构造时会遍历流：

- 找到视频流时设置 `m_videoStreamIndex`、`m_V_coderPar`、`m_V_time_base`。
- 找到音频流时设置 `m_audioStreamIndex`、`m_A_coderPar`、`m_A_time_base`。

无音频流时：

- `m_audioStreamIndex` 保持 `-1`。
- `m_A_coderPar` 保持 `nullptr`。
- `m_A_time_base` 没有有效业务意义。

`StartDemuxer()` 读包时的流过滤逻辑是：

```cpp
if (packet->stream_index != m_audioStreamIndex &&
    packet->stream_index != m_videoStreamIndex) {
    av_packet_free(&packet);
    continue;
}
```

在无音频文件中，真实 packet 的 `stream_index` 不会等于 `-1`，所以视频包仍可以进入视频队列。这一段不是主要崩溃点。

但 demuxer 当前没有对外提供明确的 `HasAudio()` / `HasVideo()`，Controller 只能拿 `ReAcoderPar()` 是否为空来推断，这会让应用层依赖基础设施层细节。

### 3. Controller 初始化阶段强依赖音频

位置：`src/application/MediaPlayController.cpp`

当前 `InitializeOpenedMediaSource()` 固定执行以下动作：

- 创建 `APacket_`
- 创建 `AFrame_`
- 创建 `Adecoder_`
- 创建 `audioplay_`
- 创建 `AdecoderTH_`
- 创建 `audioplayTH_`
- 初始化音频 decoder
- 初始化 `AudioPlayer`
- 建立音频线程连接
- 下发音量和静音状态

其中高风险代码是：

```cpp
Adecoder_->InitDecoder(APacket_, AFrame_, demuxer_->ReAcoderPar(), demuxer_->ReAtimebase());
audioplay_->InitAudioPlay(demuxer_->ReAcoderPar(), AFrame_, syncManager_, sourceInfo.A_timeBase);
```

无音频流时 `demuxer_->ReAcoderPar()` 为 `nullptr`。`AudioPlayer::InitAudioPlay()` 对空参数有部分容错，但 `decoder::InitDecoder()` 当前没有空指针保护，会继续执行：

```cpp
codec = avcodec_find_decoder(par->codec_id);
```

这就是无音频流视频异常退出的直接风险点。

### 4. Start/Pause/Stop/Seek 默认音频组件必须存在

位置：`src/application/MediaPlayController.cpp`

`Start()` 当前判断：

```cpp
if (!demuxer_ || !Adecoder_ || !Vdecoder_ || !audioplay_ || !videoRender_ || !positionTimer_) {
    return;
}
```

这表示即使未来初始化阶段跳过音频链路，只要 `Adecoder_` 或 `audioplay_` 为空，`Start()` 也会拒绝启动。

同类问题还存在于：

- `Pause()`
- `Stop()`
- `SeekTo()`
- `CreateConnects()`
- `SuspendPipelineAndWait()`
- `FlushDecoders()`
- `ReleaseResources()`

其中部分函数已经有空指针保护，但整体语义仍是“完整播放管线 = 音频链路 + 视频链路”。这不适合无音频视频，也不适合后续多路摄像头。

### 5. 同步时钟默认音频主时钟

位置：

- `src/infrastructure/syncmanager.h`
- `src/infrastructure/audioplayer.cpp`
- `src/infrastructure/videorender.cpp`

`syncmanager` 当前默认同步类型是：

```cpp
SYNCTYPE currentSYNCTYPE = SYNCTYPE::AUDIO;
```

但这个字段当前没有真正参与策略分发。实际主时钟推进来自 `AudioPlayer`：

- 音频帧输出前后调用 `m_syncManager->RefreshClock(...)`。
- `MediaPlayController::Position()` 直接读取 `syncManager_->GetClock()`。
- `VideoRender::RenderFrame()` 用 `m_syncManager->GetClock()` 与视频 PTS 比较。

无音频流时没有 `AudioPlayer` 推进主时钟。如果只跳过音频链路，视频渲染仍然会使用一个不前进或只在 seek 时变化的时钟，结果可能是：

- 视频帧被误判为过早，持续等待。
- 视频帧被误判为过晚，大量丢帧。
- 进度条不前进。
- 播放完成状态和当前播放位置不稳定。

所以无音频支持不能只靠“音频为空就不创建音频对象”。必须同时补齐视频主时钟策略。

## 架构判断

无音频流支持不是 UI 功能，也不是播放列表功能。它属于播放管线对媒体源能力差异的基础适配。

按照 `doc/架构指导.md`，后续 Phase 2/Phase 3 会引入摄像头、网络流、多路播放。它们天然存在以下差异：

- 有视频无音频。
- 有音频无视频。
- 音视频都有。
- duration 不可用。
- seek 不可用。
- 帧率不稳定。
- EOF 语义和文件不同。

因此应该先在媒体源信息和播放管线层面建立能力判断，而不是在 `MainWindow` 或播放按钮逻辑里写特殊分支。

正确方向是：

```text
IMediaSource / Fileinfo 描述媒体源能力
    -> MediaPlayController 根据能力构建当前单路播放管线
    -> 有音频时使用 AudioMaster
    -> 无音频时使用 VideoMaster
    -> 后续 MediaPipeline 继承同一套能力模型
```

## 推荐实施方案

### 第一步：补齐媒体源能力描述

建议在 `Fileinfo` 中增加明确字段：

```cpp
bool hasVideo_ = false;
bool hasAudio_ = false;
int videoStreamIndex_ = -1;
int audioStreamIndex_ = -1;
AVCodecID videoCodecId_ = AV_CODEC_ID_NONE;
AVCodecID audioCodecId_ = AV_CODEC_ID_NONE;
bool seekable_ = true;
bool live_ = false;
```

同时保留现有字段，避免一次性大改 UI 和 Controller。

`demuxer` 在扫描流时负责填充：

- 找到视频流：`hasVideo_ = true`
- 找到音频流：`hasAudio_ = true`
- 填充 stream index、codec id、time base

这样 Controller 不再通过空指针猜测媒体能力。

### 第二步：让音频链路成为可选链路

`MediaPlayController::InitializeOpenedMediaSource()` 应根据 `sourceInfo.hasAudio_` 决定是否创建：

- `APacket_`
- `AFrame_`
- `Adecoder_`
- `audioplay_`
- `AdecoderTH_`
- `audioplayTH_`

有音频时：

- 创建音频队列、音频 decoder、AudioPlayer、音频线程。
- 建立音频信号槽。
- 下发音量、静音、播放速度。

无音频时：

- 不创建音频 decoder。
- 不创建 AudioPlayer。
- 不创建音频线程。
- 音量和静音状态仍保留在 Controller 层，但不会下发给不存在的音频输出对象。

注意：不建议创建一个“空 AudioPlayer”来假装有音频。那会把无音频源伪装成有音频源，后续多路音频策略会更混乱。

### 第三步：Start/Pause/Seek/Release 改为按能力运行

需要把 Controller 中的“必选组件检查”拆成：

- 必选视频链路检查
- 可选音频链路检查

例如 `Start()` 的语义应变成：

- demuxer 必须存在。
- 视频 decoder 必须存在。
- video render 必须存在。
- position timer 必须存在。
- 如果有音频，则音频 decoder、AudioPlayer、音频线程必须存在。
- 如果无音频，则只启动视频链路和 demuxer。

同理：

- `Pause()`：有音频才 pause audio。
- `SeekTo()`：有音频才 flush audio decoder。
- `SuspendPipelineAndWait()`：有音频线程才等待音频 decoder 安全点。
- `CreateConnects()`：有音频组件才连接音频线程和音量信号。
- `ReleaseResources()`：已有空指针保护，可以继续保持，但语义上要承认音频组件可能不存在。

### 第四步：补齐视频主时钟

无音频流必须切换到视频主时钟。

推荐先做最小但架构正确的设计：

1. `syncmanager` 暴露同步模式：

```cpp
void SetSyncType(SYNCTYPE type);
SYNCTYPE SyncType() const;
```

2. Controller 初始化时设置：

```text
hasAudio_ == true  -> SYNCTYPE::AUDIO
hasAudio_ == false -> SYNCTYPE::VIDEO
```

3. `VideoRender` 在 `SYNCTYPE::VIDEO` 下承担主时钟推进：

- 取队首视频帧 PTS。
- 按 `videoTimeBase` 计算视频毫秒时间。
- 渲染该帧后调用 `syncManager->RefreshClock(videoPtsMs)`。
- 下一帧调度按帧率和播放速度控制。

4. `VideoRender` 在 `SYNCTYPE::AUDIO` 下继续沿用当前音频主时钟逻辑。

这样视频-only 文件可以正常播放和推进进度，同时不破坏现有音视频同步。

### 第五步：位置和时长语义保持不变

`MediaPlayController::Duration()` 可以继续读取 `Fileinfo.duration_`。

`MediaPlayController::Position()` 仍读取 `syncManager_->GetClock()`，但主时钟来源变为：

- 有音频：`AudioPlayer`
- 无音频：`VideoRender`

这样 UI 层不用知道当前是音频主时钟还是视频主时钟，仍符合当前架构边界。

### 第六步：后续抽取 MediaPipeline 时继承该设计

无音频支持应在抽 `MediaPipeline` 前完成，或作为 `MediaPipeline` 抽取的前置任务之一完成。

原因是 `MediaPipeline` 的第一版职责就是管理一路媒体源的完整播放资源。如果它一开始仍默认音频必选，后续摄像头和多路视频会继续踩同一个坑。

建议顺序：

```text
3.0 无音频流支持
3.1 单路 MediaPipeline 抽取预研
3.2 新增 MediaPipeline 空壳
3.3 迁移单路播放管线资源
3.4 多路管理预研
```

## 不建议的做法

不建议只在崩溃点加空指针判断：

```cpp
if (demuxer_->ReAcoderPar()) {
    Adecoder_->InitDecoder(...);
}
```

这个做法只能绕开初始化崩溃，不能解决 Start/Pause/Seek 对音频对象的强依赖，也不能解决无音频时钟不推进的问题。

不建议让 `AudioPlayer` 在无音频时使用默认采样率启动空音频输出：

- 会创建无意义的音频设备资源。
- 会把“没有音频”伪装成“有静音音频”。
- 多路阶段无法清晰判断哪一路具备真实音频。

不建议在 `MainWindow` 中判断文件是否有音频：

- UI 层不应该理解 FFmpeg 流能力。
- 后续摄像头、网络流、工业相机都不应该把判断逻辑散落在 UI 事件里。

## 需要改动的代码范围

如果进入实现，建议改动范围控制在以下文件：

- `src/domain/IMediaSource.h`
  - 扩展 `Fileinfo` 媒体能力字段。

- `src/infrastructure/demuxer.h`
  - 可选增加 `HasAudio()` / `HasVideo()`，或只通过 `Fileinfo` 提供能力。

- `src/infrastructure/demuxer.cpp`
  - 扫描流时填充 `Fileinfo` 的音视频能力。
  - 保持只向存在的队列 push 对应包。

- `src/infrastructure/syncmanager.h`
  - 增加同步模式设置和读取接口。

- `src/infrastructure/syncmanager.cpp`
  - 实现同步模式接口。

- `src/infrastructure/videorender.cpp`
  - 在视频主时钟模式下按视频 PTS 推进 `syncmanager`。
  - 在音频主时钟模式下保持当前同步逻辑。

- `src/application/MediaPlayController.cpp`
  - 根据 `Fileinfo.hasAudio_` 条件创建和启动音频链路。
  - 调整 `Start()`、`Pause()`、`SeekTo()`、`SuspendPipelineAndWait()`、`CreateConnects()` 的必选组件判断。

暂不建议改 `MainWindow`。

## 验收用例

使用用户手里的无音频流视频验证：

- 打开无音频视频不崩溃。
- 点击播放后视频正常渲染。
- 进度条随视频推进。
- 暂停后画面停止，继续播放后恢复。
- seek 后画面跳转到目标位置附近。
- 0.5x、1.0x、1.5x、2.0x 下视频速度符合预期。
- 播放完成后窗口可以关闭。
- 播放中关闭窗口不阻塞。
- 暂停后关闭窗口不阻塞。

同时需要回归普通有音频视频：

- 音视频同步不退化。
- 音量和静音仍有效。
- seek 后无旧音频残留。
- 播放列表切换不退化。

## 最终结论

无音频流支持应该作为进入 `MediaPipeline` 抽取前的前置任务。

它的核心不是“兼容一个特殊文件”，而是建立媒体源能力驱动的播放管线构建方式：

```text
媒体源声明能力
    -> Controller 按能力构建音视频链路
    -> 有音频走音频主时钟
    -> 无音频走视频主时钟
    -> UI 不关心底层差异
```

这条路线符合 `doc/架构指导.md` 的分层方向，也能为后续摄像头、多路视频和网络流减少返工。
