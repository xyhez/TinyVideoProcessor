# TODO: Phase 3.0 无音频流支持

## 阶段目标

在抽取 `MediaPipeline` 之前，先让当前单路播放管线支持无音频流视频。

本阶段不是做多路播放，也不是做摄像头接入。核心目标是消除当前播放管线中的“音频必然存在”假设，让后续 `MediaPipeline` 可以继承一套正确的媒体源能力模型。

## 架构边界

- [ ] `MainWindow` 不直接判断文件是否有音频。
- [ ] `MainWindow` 不直接访问 demuxer、decoder、AudioPlayer、VideoRender、syncmanager。
- [ ] 媒体源能力由 `IMediaSource` / `Fileinfo` 表达。
- [ ] `MediaPlayController` 根据媒体源能力构建当前单路播放管线。
- [ ] 有音频时保持音频主时钟。
- [ ] 无音频时切换为视频主时钟。
- [ ] 不提前引入多路 UI。
- [ ] 不提前创建 `MultiStreamManager`。
- [ ] 不把多个 demuxer / decoder vector 塞进 `MediaPlayController`。
- [ ] 不用空 `AudioPlayer` 伪装无音频源。

## 参考文档

- `doc/架构指导.md`
- `doc/Phase3_多路播放架构预研报告.md`
- `doc/Phase3_无音频流视频前置验证报告.md`

## 3.0.1 扩展媒体源能力信息

**目标**：让媒体源显式声明自己有哪些流和能力，避免 Controller 通过空指针猜测。

- [x] 在 `src/domain/IMediaSource.h` 的 `Fileinfo` 中增加 `hasVideo_`。
- [x] 在 `Fileinfo` 中增加 `hasAudio_`。
- [x] 在 `Fileinfo` 中增加 `videoStreamIndex_`。
- [x] 在 `Fileinfo` 中增加 `audioStreamIndex_`。
- [x] 在 `Fileinfo` 中增加 `videoCodecId_`。
- [x] 在 `Fileinfo` 中增加 `audioCodecId_`。
- [x] 保留现有 `CodecId_` 字段，避免一次性扩大改动面。
- [x] 保留现有 `A_timeBase`、`V_timeBase` 字段，继续服务同步链路。
- [x] 暂不强行加入 `seekable_`、`live_`，但在报告中记录为 Phase 2/Phase 3 后续能力。

**验收标准**：

- [x] `Fileinfo` 能明确表达有视频无音频。
- [x] `Fileinfo` 能明确表达音视频都有。
- [x] 现有文件播放路径不需要从 UI 层额外传入音频判断。

## 3.0.2 demuxer 填充媒体源能力

**目标**：demuxer 扫描 FFmpeg stream 后，把实际流能力写回 `Fileinfo`。

- [x] 找到视频流时设置 `info.hasVideo_ = true`。
- [x] 找到视频流时设置 `info.videoStreamIndex_`。
- [x] 找到视频流时设置 `info.videoCodecId_`。
- [x] 找到视频流时继续设置宽高、帧率、视频 time base、duration。
- [x] 找到音频流时设置 `info.hasAudio_ = true`。
- [x] 找到音频流时设置 `info.audioStreamIndex_`。
- [x] 找到音频流时设置 `info.audioCodecId_`。
- [x] 找到音频流时继续设置音频 time base。
- [x] 无音频流时保持 `m_audioStreamIndex = -1`、`m_A_coderPar = nullptr`。
- [x] 无视频流时保持 `m_videoStreamIndex = -1`、`m_V_coderPar = nullptr`。

**验收标准**：

- [ ] 普通音视频文件：`hasVideo_ == true` 且 `hasAudio_ == true`。
- [ ] 无音频视频文件：`hasVideo_ == true` 且 `hasAudio_ == false`。
- [ ] demuxer 仍只向存在的音视频队列推送对应 packet。
- [ ] 不把媒体能力判断写到 `MainWindow`。

## 3.0.3 保护 decoder 初始化入口

**目标**：基础设施层对非法初始化参数保持防御，避免空 codec 参数直接崩溃。

- [x] 在 `decoder::InitDecoder()` 开头检查 `coderPar` 是否为空。
- [x] `coderPar == nullptr` 时返回失败并输出清晰日志。
- [x] 检查 packet 队列和 frame 队列是否为空。
- [x] 队列为空时返回失败并输出清晰日志。
- [x] 保持视频 decoder 和音频 decoder 的正常初始化行为不变。

**验收标准**：

- [x] 即使上层误传空 codec 参数，也不会访问 `par->codec_id` 崩溃。
- [ ] 正常音视频文件初始化行为不变。

## 3.0.4 MediaPlayController 支持可选音频链路

**目标**：Controller 根据 `Fileinfo.hasAudio_` 创建播放管线资源，音频链路不再是必选项。

- [x] `InitializeOpenedMediaSource()` 始终创建视频包队列和视频帧队列。
- [x] `InitializeOpenedMediaSource()` 始终创建 demuxer、视频 decoder、VideoRender、syncmanager、position timer。
- [x] 仅当 `sourceInfo.hasAudio_ == true` 时创建音频包队列。
- [x] 仅当 `sourceInfo.hasAudio_ == true` 时创建音频帧队列。
- [x] 仅当 `sourceInfo.hasAudio_ == true` 时创建音频 decoder。
- [x] 仅当 `sourceInfo.hasAudio_ == true` 时创建 `AudioPlayer`。
- [x] 仅当 `sourceInfo.hasAudio_ == true` 时创建音频 decoder 线程。
- [x] 仅当 `sourceInfo.hasAudio_ == true` 时创建 AudioPlayer 线程。
- [x] 有音频时继续下发播放速度、音量、静音状态到 `AudioPlayer`。
- [x] 无音频时保留 Controller 内部音量、静音状态，但不创建音频输出。

**验收标准**：

- [x] 无音频视频不会初始化音频 decoder。
- [x] 无音频视频不会初始化 `AudioPlayer`。
- [x] 无音频视频不会创建音频线程。
- [ ] 普通音视频文件的音频链路行为不变。

## 3.0.5 调整 Controller 运行控制

**目标**：Start/Pause/Stop/Seek/Release 等控制逻辑按“必选视频链路 + 可选音频链路”运行。

- [x] `Start()` 的必选检查移除对音频组件的硬依赖。
- [x] `Start()` 有音频时启动音频 decoder 线程和 AudioPlayer 线程。
- [x] `Start()` 无音频时只启动 demuxer、视频 decoder、VideoRender。
- [x] `Pause()` 有音频时暂停 `AudioPlayer`。
- [x] `Pause()` 无音频时只暂停 demuxer、视频 decoder、VideoRender。
- [x] `SeekTo()` 不再要求 `Adecoder_` 必须存在。
- [x] `SeekTo()` 有音频时 flush 音频 decoder。
- [x] `SeekTo()` 无音频时只 flush 视频 decoder。
- [x] `SuspendPipelineAndWait()` 有音频线程时等待音频 decoder 安全点。
- [x] `SuspendPipelineAndWait()` 无音频时跳过音频 decoder。
- [x] `CreateConnects()` 有音频时连接音频线程和音量/静音信号。
- [x] `CreateConnects()` 无音频时不连接不存在的音频对象。
- [x] `ReleaseResources()` 保持所有音频对象空指针安全释放。

**验收标准**：

- [ ] 无音频视频可以 Start。
- [ ] 无音频视频可以 Pause。
- [ ] 无音频视频可以 Stop。
- [ ] 无音频视频可以 Seek。
- [ ] 无音频视频关闭窗口不阻塞。
- [ ] 普通音视频文件的 Start/Pause/Stop/Seek 行为不退化。

## 3.0.6 增加同步模式

**目标**：同步系统显式支持音频主时钟和视频主时钟。

- [x] 在 `syncmanager` 中增加 `SetSyncType(SYNCTYPE type)`。
- [x] 在 `syncmanager` 中增加 `SyncType() const`。
- [x] 保持默认同步模式为 `SYNCTYPE::AUDIO`。
- [x] Controller 初始化时，有音频设置为 `SYNCTYPE::AUDIO`。
- [x] Controller 初始化时，无音频设置为 `SYNCTYPE::VIDEO`。
- [x] `Position()` 继续读取 `syncManager_->GetClock()`，不把同步模式暴露给 UI。

**验收标准**：

- [ ] 有音频文件仍由 `AudioPlayer` 推进主时钟。
- [ ] 无音频视频由视频渲染链路推进主时钟。
- [x] UI 不需要知道当前同步模式。

## 3.0.7 VideoRender 支持视频主时钟

**目标**：无音频视频播放时，视频渲染负责推进主时钟和进度。

- [ ] `VideoRender::RenderFrame()` 判断当前同步模式。
- [ ] `SYNCTYPE::AUDIO` 模式下保持现有音频主时钟同步逻辑。
- [ ] `SYNCTYPE::VIDEO` 模式下不再等待音频时钟。
- [ ] `SYNCTYPE::VIDEO` 模式下根据视频帧 PTS 和 `m_videoTimeBase` 计算当前视频时间。
- [ ] `SYNCTYPE::VIDEO` 模式下渲染帧后调用 `syncManager->RefreshClock(videoPtsMs)`。
- [ ] `SYNCTYPE::VIDEO` 模式下下一帧调度继续使用帧率和播放速度。
- [ ] Seek 后视频主时钟从目标时间继续推进。

**验收标准**：

- [ ] 无音频视频播放时画面正常前进。
- [ ] 无音频视频播放时进度条正常前进。
- [ ] 无音频视频倍速播放时画面速度符合当前档位。
- [ ] 普通音视频文件音视频同步不退化。

## 3.0.8 文档和报告收口

**目标**：实现完成后留下可追溯的任务报告，方便后续 `MediaPipeline` 抽取。

- [ ] 新增 `doc/Phase3_0_无音频流支持实现报告.md`。
- [ ] 记录修改过的模块。
- [ ] 记录媒体源能力字段的含义。
- [ ] 记录音频链路可选后的 Controller 行为。
- [ ] 记录视频主时钟策略。
- [ ] 记录未做事项：音频-only、实时流 seekable/live、MediaPipeline 抽取、多路播放。

**验收标准**：

- [ ] 报告能解释为什么这一步必须在 `MediaPipeline` 前完成。
- [ ] 报告能作为 Phase 3.1 抽取单路管线的输入。

## 3.0 验收清单

### 无音频视频

- [ ] 打开无音频视频不崩溃。
- [ ] 点击播放后视频正常渲染。
- [ ] 进度条随视频推进。
- [ ] 暂停后画面停止。
- [ ] 暂停后继续播放可以恢复。
- [ ] Seek 后画面跳到目标位置附近。
- [ ] 0.5x 播放速度有效。
- [ ] 1.0x 播放速度有效。
- [ ] 1.5x 播放速度有效。
- [ ] 2.0x 播放速度有效。
- [ ] 播放完成后窗口可以关闭。
- [ ] 播放中关闭窗口不阻塞。
- [ ] 暂停后关闭窗口不阻塞。

### 普通音视频文件回归

- [ ] 打开普通音视频文件不崩溃。
- [ ] 音频正常播放。
- [ ] 视频正常渲染。
- [ ] 音视频同步不退化。
- [ ] 音量控制仍有效。
- [ ] 静音控制仍有效。
- [ ] Seek 后无旧音频残留。
- [ ] Seek 后无旧视频帧闪回。
- [ ] 播放速度控制仍有效。
- [ ] 播放列表切换不退化。
- [ ] 播放完成后窗口可以关闭。

## 3.0 不做范围

- [ ] 不支持音频-only 文件播放。
- [ ] 不做摄像头设备枚举。
- [ ] 不做 RTSP/RTMP 网络流。
- [ ] 不做多路播放 UI。
- [ ] 不抽取 `MediaPipeline`。
- [ ] 不引入 `MultiStreamManager`。
- [ ] 不做多路音频源选择。
- [ ] 不做音量持久化。

## Phase 3.0 完成标准

- [ ] 当前单路播放管线可以稳定播放无音频视频。
- [ ] 普通音视频文件播放能力不退化。
- [ ] `MainWindow` 不新增基础设施层依赖。
- [ ] `MediaPlayController` 的音频链路从必选变为可选。
- [ ] 同步模式从隐式音频主时钟变为显式音频/视频主时钟。
- [ ] 后续可以进入 Phase 3.1：单路 `MediaPipeline` 抽取。
