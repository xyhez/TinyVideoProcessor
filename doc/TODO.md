# TODO: Phase 3.1 单路 MediaPipeline 抽取

## 阶段目标

把当前 `MediaPlayController` 中属于单路播放管线的资源和生命周期迁移到 `MediaPipeline`。

本阶段只抽取单路管线，不做多路播放，不新增 UI，不引入 `MultiStreamManager`。

目标结构：

```text
MainWindow
    -> PlaylistManager
    -> MediaPlayController
    -> MediaPipeline
    -> demuxer / decoder / AudioPlayer / VideoRender / syncmanager / queues / threads
```

## 架构边界

- [ ] `MainWindow` 不直接访问 `MediaPipeline`。
- [ ] `MainWindow` 不直接访问 demuxer、decoder、AudioPlayer、VideoRender、syncmanager。
- [ ] `MediaPlayController` 保留应用层对外接口和播放状态。
- [ ] `MediaPlayController` 不再直接持有播放管线基础设施资源。
- [ ] `MediaPipeline` 只管理一路媒体源的播放资源。
- [ ] `MediaPipeline` 不引入 pipeline id。
- [ ] 不引入 `MultiStreamManager`。
- [ ] 不新增多路播放 UI。
- [ ] 保持 Phase 3.0 无音频流支持能力。

## 参考文档

- `doc/架构指导.md`
- `doc/Phase3_多路播放架构预研报告.md`
- `doc/Phase3_0_无音频流支持实现报告.md`
- `doc/Phase3_1_单路MediaPipeline抽取计划.md`

## 3.1.1 抽取边界预研

**目标**：先确认迁移边界，不直接改代码。

- [x] 梳理 `MediaPlayController` 当前字段。
- [x] 标记应迁入 `MediaPipeline` 的字段。
- [x] 标记应留在 `MediaPlayController` 的字段。
- [x] 梳理 `MediaPlayController` 当前方法。
- [x] 标记应迁入 `MediaPipeline` 的方法。
- [x] 标记应留在 `MediaPlayController` 的方法。
- [x] 明确 Pipeline 信号和 Controller 转发关系。
- [x] 明确音量/静音接口在单路 Pipeline 中的临时归属。
- [x] 明确后续多路阶段音频策略可能上移。
- [x] 形成边界报告。

**产出**：

- [x] `doc/Phase3_1_任务1_MediaPipeline边界报告.md`

**验收标准**：

- [x] 能清楚说明 Controller 和 Pipeline 的职责边界。
- [x] 能作为后续代码迁移依据。
- [x] 没有把多路概念提前引入单路抽取阶段。

## 3.1.2 新增 MediaPipeline 空壳

**目标**：新增类和接口，不迁移播放逻辑。

- [x] 新增 `src/domain/MediaPipeline.h`。
- [x] 新增 `src/domain/MediaPipeline.cpp`。
- [x] `MediaPipeline` 继承 `QObject`。
- [x] 定义 `Initialize()`。
- [x] 定义 `Shutdown()`。
- [x] 定义 `Start()`。
- [x] 定义 `Pause()`。
- [x] 定义 `Stop()`。
- [x] 定义 `SeekTo()`。
- [x] 定义 `SeekRelative()`。
- [x] 定义 `Duration()`。
- [x] 定义 `Position()`。
- [x] 定义 `SetPlaybackSpeed()`。
- [x] 定义 `SetVolume()`。
- [x] 定义 `SetMuted()`。
- [x] 定义 `PositionChanged` 信号。
- [x] 定义 `DurationChanged` 信号。
- [x] 定义 `PlaybackFinished` 信号。
- [x] 定义 `VolumeChanged` 信号。
- [x] 定义 `MuteChanged` 信号。
- [x] 定义 `ErrorOccurred` 信号。
- [x] 加入构建系统。
- [x] 暂不让 Controller 使用 `MediaPipeline`。

**验收标准**：

- [x] 新增空壳不改变现有播放行为。
- [x] `MediaPipeline` 不依赖 UI 层。
- [x] `MediaPipeline` 不引入多路 id。

## 3.1.3 资源承载准备

**目标**：先让 `MediaPipeline` 具备承载单路播放资源的成员结构，但暂不迁移 Controller 运行逻辑。

### 3.1.3-a 在 MediaPipeline 中声明资源成员

- [x] 在 `MediaPipeline` 中声明 `APacket_`。
- [x] 在 `MediaPipeline` 中声明 `VPacket_`。
- [x] 在 `MediaPipeline` 中声明 `AFrame_`。
- [x] 在 `MediaPipeline` 中声明 `VFrame_`。
- [x] 在 `MediaPipeline` 中声明 `mediaSource_`。
- [x] 在 `MediaPipeline` 中声明 `syncManager_`。
- [x] 在 `MediaPipeline` 中声明 `demuxer_`。
- [x] 在 `MediaPipeline` 中声明 `Adecoder_`。
- [x] 在 `MediaPipeline` 中声明 `Vdecoder_`。
- [x] 在 `MediaPipeline` 中声明 `audioplay_`。
- [x] 在 `MediaPipeline` 中声明 `videoRender_`。
- [x] 在 `MediaPipeline` 中声明 `demuxerTH_`。
- [x] 在 `MediaPipeline` 中声明 `AdecoderTH_`。
- [x] 在 `MediaPipeline` 中声明 `VdecoderTH_`。
- [x] 在 `MediaPipeline` 中声明 `audioplayTH_`。
- [x] 在 `MediaPipeline` 中声明 `positionTimer_`。
- [x] 暂不删除 `MediaPlayController` 中的旧资源成员。
- [x] 暂不让 `MediaPlayController` 使用 `MediaPipeline`。

**验收标准**：

- [x] `MediaPipeline` 已具备单路播放资源承载结构。
- [x] Controller 行为不变。
- [x] UI 行为不变。

### 3.1.3-b 准备 Pipeline 侧队列常量和 deleter

- [x] 在 `MediaPipeline.cpp` 内部准备 `kPacketQueueMax`。
- [x] 在 `MediaPipeline.cpp` 内部准备 `kFrameQueueMax`。
- [x] 在 `MediaPipeline.cpp` 内部准备 packet deleter。
- [x] 在 `MediaPipeline.cpp` 内部准备 frame deleter。
- [x] 暂时保留 `MediaPlayController.h` 中旧常量和 deleter。

**验收标准**：

- [x] Pipeline 侧后续可以独立创建音视频队列。
- [x] Controller 侧现有逻辑不受影响。

### 3.1.3-c 最终清理 Controller 资源成员

> 注意：本小节必须等 3.1.4、3.1.5、3.1.6 完成后再做。

- [ ] 删除 `MediaPlayController` 中的队列成员。
- [ ] 删除 `MediaPlayController` 中的媒体源成员。
- [ ] 删除 `MediaPlayController` 中的 syncmanager 成员。
- [ ] 删除 `MediaPlayController` 中的基础设施组件成员。
- [ ] 删除 `MediaPlayController` 中的线程成员。
- [ ] 删除 `MediaPlayController` 中的 position timer 成员。
- [ ] 删除 `MediaPlayController.h` 对基础设施层播放组件的 include。
- [ ] Controller 新增并只保留 `std::unique_ptr<MediaPipeline> pipeline_`。

**验收标准**：

- [ ] `MediaPlayController.h` 不再 include 基础设施层播放组件。
- [ ] `MediaPlayController` 不再直接持有播放队列、组件和线程。
- [ ] UI 调用方式不变。

## 3.1.4 迁移初始化和释放

**目标**：把管线初始化、连接和释放迁入 `MediaPipeline`。

### 3.1.4-a 补齐 MediaPipeline 私有辅助接口

- [x] 声明资源创建入口。
- [x] 声明信号连接入口。
- [x] 声明资源释放入口。
- [x] 声明播放完成处理入口。
- [x] 声明队列清理入口。
- [x] 声明 decoder flush 入口。
- [x] 不迁移业务逻辑。

**验收标准**：

- [x] `MediaPipeline` 具备后续函数级迁移入口。
- [x] `MediaPlayController` 现有逻辑不受影响。
- [x] 当前播放行为不变。

### 3.1.4-b 迁移资源创建逻辑

- [ ] 将 `InitializeOpenedMediaSource()` 中的队列创建逻辑迁入 `MediaPipeline::Initialize()`。
- [ ] 将 demuxer 创建逻辑迁入 `MediaPipeline::Initialize()`。
- [ ] 将 audio/video decoder 创建逻辑迁入 `MediaPipeline::Initialize()`。
- [ ] 将 audio player 创建逻辑迁入 `MediaPipeline::Initialize()`。
- [ ] 将 video render 创建逻辑迁入 `MediaPipeline::Initialize()`。
- [ ] 将 syncmanager 创建逻辑迁入 `MediaPipeline::Initialize()`。
- [ ] 将 position timer 创建逻辑迁入 `MediaPipeline::Initialize()`。
- [ ] 将线程创建逻辑迁入 `MediaPipeline::Initialize()`。
- [ ] 保持无音频流初始化能力。

**验收标准**：

- [ ] `MediaPipeline` 能独立创建单路播放资源。
- [ ] 有音频文件初始化路径不退化。
- [ ] 无音频视频初始化路径不退化。

### 3.1.4-c 迁移信号连接逻辑

- [ ] 将 demuxer 线程启动连接迁入 `MediaPipeline`。
- [ ] 将 audio decoder 线程启动连接迁入 `MediaPipeline`。
- [ ] 将 video decoder 线程启动连接迁入 `MediaPipeline`。
- [ ] 将 audio player 线程启动连接迁入 `MediaPipeline`。
- [ ] 将 demux finished 连接迁入 `MediaPipeline`。
- [ ] 将 position timer 连接迁入 `MediaPipeline`。
- [ ] 将 AudioPlayer 音量/静音信号转发迁入 `MediaPipeline`。

**验收标准**：

- [ ] Controller 不再直接连接基础设施层播放组件。
- [ ] Pipeline 能向 Controller 转发位置、播放完成、音量、静音信号。
- [ ] 有音频和无音频路径连接完整。

### 3.1.4-d 迁移释放逻辑

- [ ] 将 `ReleaseResources()` 逻辑迁入 `MediaPipeline::Shutdown()`。
- [ ] 将停止 position timer 逻辑迁入 `MediaPipeline`。
- [ ] 将 RequestStop 逻辑迁入 `MediaPipeline`。
- [ ] 将 demuxer/decoder/audio/render 停止逻辑迁入 `MediaPipeline`。
- [ ] 将线程退出和等待逻辑迁入 `MediaPipeline`。
- [ ] 将基础设施对象 reset/delete 逻辑迁入 `MediaPipeline`。
- [ ] 将媒体源关闭逻辑迁入 `MediaPipeline`。
- [ ] 将 `HandleDemuxFinished()` 逻辑迁入 `MediaPipeline`。
- [ ] 将 `ClearQueues()` 逻辑迁入 `MediaPipeline`。
- [ ] 将 `FlushDecoders()` 逻辑迁入 `MediaPipeline`。

**验收标准**：

- [ ] 启动后关闭正常。
- [ ] 暂停后关闭正常。
- [ ] 播放中关闭正常。
- [ ] 播放完成后关闭正常。
- [ ] 切换文件时旧 Pipeline 能完整释放。

### 3.1.4-e Controller 接入 MediaPipeline

- [ ] Controller 新增 `std::unique_ptr<MediaPipeline> pipeline_`。
- [ ] Controller `Initialize()` 调用 `pipeline_->Initialize()`。
- [ ] Controller `SwitchSource()` 先关闭旧 Pipeline，再初始化新 Pipeline。
- [ ] Controller `Shutdown()` 调用 `pipeline_->Shutdown()`。
- [ ] Controller 转发 Pipeline 的 `DurationChanged`。
- [ ] Controller 转发 Pipeline 的 `PositionChanged`。
- [ ] Controller 转发 Pipeline 的 `PlaybackFinished`。
- [ ] Controller 转发 Pipeline 的 `VolumeChanged`。
- [ ] Controller 转发 Pipeline 的 `MuteChanged`。

**验收标准**：

- [ ] 打开文件正常。
- [ ] 切换文件正常。
- [ ] 关闭程序正常。
- [ ] 无音频视频仍可初始化。
- [ ] 普通音视频文件仍可初始化。

## 3.1.5 迁移播放控制

**目标**：把播放控制流程迁入 `MediaPipeline`，Controller 只保留应用层状态编排。

- [ ] 将 `Start()` 内部管线启动逻辑迁入 `MediaPipeline::Start()`。
- [ ] 将 `Pause()` 内部管线暂停逻辑迁入 `MediaPipeline::Pause()`。
- [ ] 将 `Stop()` 内部管线重置逻辑迁入 `MediaPipeline::Stop()`。
- [ ] 将 `SeekTo()` 内部管线 seek 逻辑迁入 `MediaPipeline::SeekTo()`。
- [ ] 将 `SeekRelative()` 内部位置计算迁入或委托给 `MediaPipeline`。
- [ ] 将 `SuspendPipelineAndWait()` 迁入 `MediaPipeline`。
- [ ] 将 `RestorePlaybackState()` 中对管线的恢复调用改为调用 Pipeline。
- [ ] 将 `ResetPipelineToStart()` 迁入 `MediaPipeline`。
- [ ] 将 `ClearQueues()` 迁入 `MediaPipeline`。
- [ ] 将 `FlushDecoders()` 迁入 `MediaPipeline`。
- [ ] Controller `Start()` 只设置状态并调用 `pipeline_->Start()`。
- [ ] Controller `Pause()` 只设置状态并调用 `pipeline_->Pause()`。
- [ ] Controller `Stop()` 只设置状态并调用 `pipeline_->Stop()`。
- [ ] Controller `SeekTo()` 只做目标边界和状态恢复编排，管线细节委托 Pipeline。

**验收标准**：

- [ ] Start/Pause/Stop 行为不变。
- [ ] Seek 行为不变。
- [ ] 快进/快退行为不变。
- [ ] 无音频视频行为不退化。
- [ ] 普通音视频文件行为不退化。

## 3.1.6 迁移用户级参数下发

**目标**：Controller 保存用户级状态，Pipeline 承接具体下发。

- [ ] Controller 继续持有 `playbackSpeed_`。
- [ ] Controller 继续持有 `volume_`。
- [ ] Controller 继续持有 `muted_`。
- [ ] `MediaPipeline::SetPlaybackSpeed()` 下发到 AudioPlayer 和 VideoRender。
- [ ] `MediaPipeline::SetVolume()` 下发到 AudioPlayer 或保存状态。
- [ ] `MediaPipeline::SetMuted()` 下发到 AudioPlayer 或保存状态。
- [ ] Controller 切换源后把当前 speed/volume/muted 重新下发给 Pipeline。

**验收标准**：

- [ ] 切换文件后播放速度保持。
- [ ] 切换文件后音量保持。
- [ ] 切换文件后静音状态保持。
- [ ] 无音频视频设置音量/静音不崩溃。

## 3.1.7 回归验收和报告

**目标**：收口 Phase 3.1，确认抽取没有改变外部行为。

- [ ] 普通音视频文件播放正常。
- [ ] 无音频视频播放正常。
- [ ] Start/Pause/Stop 正常。
- [ ] Seek 正常。
- [ ] 快进/快退正常。
- [ ] 播放速度正常。
- [ ] 音量和静音正常。
- [ ] 播放列表切换正常。
- [ ] 播放完成后关闭正常。
- [ ] 播放中关闭不阻塞。
- [ ] 暂停后关闭不阻塞。
- [ ] `MediaPlayController` 不直接持有基础设施层播放资源。
- [ ] 形成 `doc/Phase3_1_单路MediaPipeline抽取实现报告.md`。

**验收标准**：

- [ ] UI 行为不变。
- [ ] Phase 3.0 无音频能力不退化。
- [ ] Controller 和 Pipeline 边界清晰。
- [ ] 后续可以进入真正多路管理设计。

## 3.1 不做范围

- [ ] 不做多路播放。
- [ ] 不做多路 UI。
- [ ] 不引入 `MultiStreamManager`。
- [ ] 不引入 pipeline id。
- [ ] 不做多路音频源选择。
- [ ] 不做摄像头枚举。
- [ ] 不做 RTSP/RTMP。
- [ ] 不支持音频-only 文件。
- [ ] 不重写渲染器。

## Phase 3.1 完成标准

- [ ] `MediaPlayController` 回到应用层协调角色。
- [ ] 单路播放资源集中在 `MediaPipeline`。
- [ ] UI 行为不变。
- [ ] 有音频文件播放不退化。
- [ ] 无音频视频播放不退化。
- [ ] 后续可以进入多路管理设计。
