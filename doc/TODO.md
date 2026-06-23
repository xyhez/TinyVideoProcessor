# TODO: Phase 3.6 MultiViewRenderer 空壳

## 阶段目标

在 Phase 3.1 单路 `MediaPipeline` 已稳定的基础上，引入 `MultiStreamManager`，让系统具备管理多路播放管线的架构能力。

本阶段第一目标不是复杂 UI，而是多路管理边界正确：

```text
MainWindow
    -> MediaPlayController
    -> MultiStreamManager
        -> MediaPipeline #1
        -> MediaPipeline #2
        -> MediaPipeline #3
```

## 架构边界

- [ ] `MainWindow` 不直接访问 `MediaPipeline`。
- [ ] `MainWindow` 不直接访问 `MultiStreamManager` 内部资源。
- [ ] `MediaPlayController` 作为 UI 对外入口。
- [ ] `MediaPlayController` 不直接持有多个 `MediaPipeline`。
- [ ] `MultiStreamManager` 管理多个 `MediaPipeline`。
- [ ] `MediaPipeline` 继续保持单路职责。
- [ ] `MediaPipeline` 不引入 id 字段。
- [ ] `StreamId` 只属于 `MultiStreamManager` 外层管理。
- [ ] 第一版不引入 `MultiViewRenderer`。
- [ ] 第一版不新增复杂多路 UI。
- [ ] 第一版使用文件源验证多路管理能力。

## 参考文档

- `doc/架构指导.md`
- `doc/Phase3_多路播放架构预研报告.md`
- `doc/Phase3_1_单路MediaPipeline抽取实现报告.md`
- `doc/Phase3_2_多路管理架构预研报告.md`
- `doc/Phase3_2_多路管理实施计划.md`

## 3.2.1 多路管理边界预研

**目标**：先确认 `MultiStreamManager` 的职责边界，不直接改代码。

- [x] 梳理 Phase 3.1 后的单路架构现状。
- [x] 明确 `MainWindow`、`MediaPlayController`、`MultiStreamManager`、`MediaPipeline` 职责。
- [x] 明确 `StreamId` 只属于 Manager。
- [x] 明确 `MediaPipeline` 不感知 id。
- [x] 明确 active stream 和 active audio stream 概念。
- [x] 明确第一版使用多个 `VideoRender`。
- [x] 明确第一版不做 `MultiViewRenderer`。
- [x] 明确文件源验证策略。
- [x] 明确不做范围。

**产出**：

- [x] `doc/Phase3_2_多路管理架构预研报告.md`
- [x] `doc/Phase3_2_多路管理实施计划.md`

**验收标准**：

- [x] 能说明 Manager 和 Pipeline 的边界。
- [x] 能说明 Manager 和 Controller 的关系。
- [x] 能说明为什么不直接让 MainWindow 管多个 Pipeline。

## 3.2.2 MultiStreamManager 空壳

**目标**：新增类和接口，不接入 Controller，不创建真实 Pipeline。

- [x] 新增 `src/domain/MultiStreamManager.h`。
- [x] 新增 `src/domain/MultiStreamManager.cpp`。
- [x] `MultiStreamManager` 继承 `QObject`。
- [x] 定义 `StreamId` 类型。
- [x] 定义无效 StreamId 常量。
- [x] 定义 `AddStream()`。
- [x] 定义 `RemoveStream()`。
- [x] 定义 `HasStream()`。
- [x] 定义 `StreamIds()`。
- [x] 定义 `ActiveStream()`。
- [x] 定义 `SetActiveStream()`。
- [x] 定义 `ActiveAudioStream()`。
- [x] 定义 `SetActiveAudioStream()`。
- [x] 定义 `ShutdownAll()`。
- [x] 定义 `StreamAdded` 信号。
- [x] 定义 `StreamRemoved` 信号。
- [x] 定义 `ActiveStreamChanged` 信号。
- [x] 定义 `ActiveAudioStreamChanged` 信号。
- [x] 定义 `StreamErrorOccurred` 信号。
- [x] 加入构建系统。
- [x] 暂不让 Controller 使用 `MultiStreamManager`。

**验收标准**：

- [x] 新类加入构建系统。
- [x] 当前单路播放行为不变。
- [x] `MediaPipeline` 不出现 id 字段。
- [x] 不新增 UI。

## 3.2.3 Manager 持有多个 Pipeline

**目标**：让 `MultiStreamManager` 可以创建和释放多个 `MediaPipeline`。

- [x] 定义 `StreamSlot`。
- [x] 使用容器保存 `StreamId -> StreamSlot`。
- [x] `AddStream()` 分配新的 `StreamId`。
- [x] `AddStream()` 创建 `MediaPipeline`。
- [x] `AddStream()` 初始化媒体源和 renderer container。
- [x] `AddStream()` 失败时完整回滚。
- [x] `RemoveStream()` 调用 Pipeline `Shutdown()`。
- [x] `RemoveStream()` 删除 slot。
- [x] `ShutdownAll()` 释放全部 Pipeline。
- [x] 删除 active stream 时更新 active stream。
- [x] 删除 active audio stream 时更新 active audio stream。

**验收标准**：

- [x] 可以添加一路文件源。
- [x] 可以移除一路文件源。
- [x] 可以添加多路但暂不接 UI 播放。
- [x] 移除不存在的 id 不崩溃。

## 3.2.4 Manager 转发 Pipeline 信号

**目标**：让 Manager 对外暴露带 `StreamId` 的信号。

- [x] 转发 Pipeline 的 `PositionChanged`。
- [x] 转发 Pipeline 的 `DurationChanged`。
- [x] 转发 Pipeline 的 `PlaybackFinished`。
- [x] 转发 Pipeline 的 `VolumeChanged`。
- [x] 转发 Pipeline 的 `MuteChanged`。
- [x] 转发 Pipeline 的 `ErrorOccurred`。
- [x] 所有转发信号携带 `StreamId`。
- [x] RemoveStream 后旧信号不会访问已删除 slot。

**验收标准**：

- [x] 任意一路 Pipeline 的信号能标识来源。
- [x] 删除一路后无悬空回调。
- [x] 当前单路 Controller 信号转发不受影响。

## 3.2.5 Manager 单路控制能力

**目标**：Manager 可以控制指定一路 Pipeline。

- [x] 定义 `StartStream(StreamId id)`。
- [x] 定义 `PauseStream(StreamId id)`。
- [x] 定义 `StopStream(StreamId id)`。
- [x] 定义 `SeekStreamTo(StreamId id, double seconds)`。
- [x] 定义 `SeekStreamRelative(StreamId id, double seconds)`。
- [x] 定义 `SetStreamPlaybackSpeed(StreamId id, double speed)`。
- [x] 定义 `SetStreamVolume(StreamId id, double volume)`。
- [x] 定义 `SetStreamMuted(StreamId id, bool muted)`。
- [x] id 不存在时安全返回。

**验收标准**：

- [x] 指定 id 存在时操作生效。
- [x] 指定 id 不存在时不崩溃。
- [x] 无音频视频设置音量和静音不崩溃。

## 3.2.6 Manager 全局控制能力

**目标**：Manager 可以对全部 Pipeline 执行批量控制。

- [x] 定义 `StartAll()`。
- [x] 定义 `PauseAll()`。
- [x] 定义 `StopAll()`。
- [x] 定义 `ShutdownAll()`。
- [x] 全局控制跳过无效 slot。

**验收标准**：

- [x] 多路启动不崩溃。
- [x] 多路暂停不阻塞。
- [x] 多路停止可以清空各自状态。
- [x] 程序关闭时可以释放全部资源。

## 3.2.7 Active Stream 状态设计

**目标**：确定 UI 当前显示哪一路状态。

- [x] Manager 保存 `activeStreamId_`。
- [x] `SetActiveStream()` 校验 id 是否存在。
- [x] active stream 被删除时自动选择下一个可用 stream 或置为无效。
- [x] Controller 后续从 active stream 读取 `Position()`。
- [x] Controller 后续从 active stream 读取 `Duration()`。
- [x] 没有 active stream 时返回 0 或无效状态。

**验收标准**：

- [x] 切换 active stream 后 UI 状态语义明确。
- [x] 删除 active stream 不崩溃。
- [x] 没有 active stream 时行为可解释。

## 3.2.8 Active Audio Stream 策略

**目标**：避免多路同时输出音频。

- [x] Manager 保存 `activeAudioStreamId_`。
- [x] 第一条有音频流默认成为 active audio stream。
- [x] 非 active audio stream 默认静音。
- [x] 切换 active audio stream 时旧路静音。
- [x] 切换 active audio stream 时新路恢复当前 volume/muted。
- [x] 无音频流不能成为有效 active audio stream。

**验收标准**：

- [x] 多路播放时最多一路有声音。
- [x] 切换 active audio stream 后音频输出符合预期。
- [x] 无音频视频不影响音频策略。

## 3.2.9 Controller 接入 Manager

**目标**：让 `MediaPlayController` 从持有单个 Pipeline 过渡到持有 Manager。

### 3.2.9.1 Controller 持有 Manager

- [x] 引入 `MultiStreamManager` 头文件。
- [x] 将 `pipeline_` 替换为 `multiStreamManager_`。
- [x] 构造函数初始化 Manager。
- [x] 析构和 `Shutdown()` 释放 Manager 管理的全部 stream。
- [x] Controller 不直接持有 `MediaPipeline`。

### 3.2.9.2 Initialize / SwitchSource 接入 Manager

- [x] `Initialize()` 通过 Manager 添加第一路 stream。
- [x] `Initialize()` 成功后设置 active stream。
- [x] `Initialize()` 保留当前单路 UI 语义。
- [x] `SwitchSource()` 清理旧 stream。
- [x] `SwitchSource()` 添加新 stream。
- [x] `SwitchSource()` 根据 autoStart 恢复播放。

### 3.2.9.3 播放控制委托 active stream

- [x] `Start()` 委托 active stream。
- [x] `Pause()` 委托 active stream。
- [x] `Stop()` 委托 active stream。
- [x] `SeekTo()` 委托 active stream。
- [x] `SeekRelative()` 委托 active stream。
- [x] `SetPlaybackSpeed()` 委托 active stream。

### 3.2.9.4 音频控制接入 active audio stream

- [x] `SetVolume()` 委托 Manager 音频策略。
- [x] `Volume()` 返回 Controller 保存的 volume。
- [x] `SetMuted()` 委托 Manager 音频策略。
- [x] `IsMuted()` 返回 Controller 保存的 muted。
- [x] 切源后恢复当前音量和静音状态。

### 3.2.9.5 状态读取和信号转发

- [x] `Duration()` 读取 active stream duration。
- [x] `Position()` 读取 active stream position。
- [x] Manager position 信号转发为 Controller position 信号。
- [x] Manager duration 信号转发为 Controller duration 信号。
- [x] Manager playback finished 信号转发为 Controller playback finished。
- [x] Manager volume/mute 信号转发为 Controller volume/mute 信号。
- [x] 只转发 active stream / active audio stream 的 UI 信号。

### 3.2.9.6 静态验收

- [x] `MainWindow` 不直接访问 `MultiStreamManager`。
- [x] `MainWindow` 不直接访问 `MediaPipeline`。
- [x] `MediaPlayController` 不直接持有 `MediaPipeline`。
- [x] `MediaPipeline` 不出现 `StreamId`。
- [x] 当前单路 UI 接口保持不变。

**验收标准**：

- [x] 现有 UI 行为不变。
- [x] 播放列表切换不退化。
- [x] Controller 不直接持有多个 Pipeline。

## 3.2.10 最小多路文件源验证

**目标**：用本地文件源验证 Manager 能管理多路。

- [x] 通过 `MediaPlayController` 暴露正式多路控制入口。
- [x] 提供最小多路文件源验证 UI。
- [x] 添加两路普通文件。
- [x] 添加一路普通文件和一路无音频文件。
- [x] 启动所有路。
- [x] 暂停所有路。
- [x] 停止所有路。
- [x] 移除其中一路。
- [x] 关闭程序。
- [x] 验证最多一路有声音。

**验收标准**：

- [x] 多路生命周期稳定。
- [x] 无音频流不崩溃。
- [x] 音频最多一路输出。
- [x] 关闭程序不阻塞。

## 3.2.11 Phase 3.2 验收报告

**目标**：收口多路管理阶段。

- [x] 形成 `doc/Phase3_2_多路管理实现报告.md`。
- [x] 记录 Manager 最终职责。
- [x] 记录 Controller 和 Manager 边界。
- [x] 记录 Pipeline 是否保持单路无 id。
- [x] 记录文件源多路验证结果。
- [x] 记录已知限制。
- [x] 记录下一阶段建议。

**验收标准**：

- [x] Manager 边界清晰。
- [x] Controller 不直接持有多个 Pipeline。
- [x] Pipeline 保持单路职责。
- [x] 后续可以进入多路 UI 或多路渲染优化。

## Phase 3.2 不做范围

- [ ] 不做完整多路 UI。
- [ ] 不做多路布局切换。
- [ ] 不引入 `MultiViewRenderer`。
- [ ] 不做统一 OpenGL 多纹理渲染。
- [ ] 不做摄像头枚举。
- [ ] 不做 RTSP/RTMP。
- [ ] 不做工业相机 SDK。
- [ ] 不做图像处理。
- [ ] 不做多路性能优化。

## Phase 3.2 完成标准

- [x] `MultiStreamManager` 可以管理多个 `MediaPipeline`。
- [x] `StreamId` 只存在于 Manager 层。
- [x] `MediaPipeline` 继续保持单路无 id。
- [x] Controller 可以通过 Manager 操作 active stream。
- [x] 文件源多路生命周期验证通过。
- [x] 无音频视频多路验证通过。
- [x] 多路音频策略不混乱。

## Phase 3.3 多路显示布局

**目标**：在多路管理能力稳定后，设计多路画面显示、布局切换和后续渲染优化路线。

## 3.3.1 多路显示布局预研

**目标**：先明确多路显示方案，不直接改代码。

- [x] 梳理当前 `MediaPipeline -> VideoRender -> rendererContainer` 链路。
- [x] 分析多个 `VideoRender` + 多个 QWidget 容器方案。
- [x] 分析统一 `MultiViewRenderer` 方案。
- [x] 明确第一步不直接实现 `MultiViewRenderer`。
- [x] 明确 active stream 和多画面选中态关系。
- [x] 明确第一版布局范围。

**产出**：

- [x] `doc/Phase3_3_多路显示布局预研报告.md`

**验收标准**：

- [x] 能说明当前渲染链路的问题。
- [x] 能说明为什么第一步先用多个 QWidget 容器。
- [x] 能说明后续引入 `MultiViewRenderer` 前需要抽象哪些边界。

## 3.3.2 多路显示布局实施计划

**目标**：将 Phase 3.3 拆成可执行小步。

- [x] 形成 `doc/Phase3_3_多路显示布局实施计划.md`。
- [x] 拆分布局模型任务。
- [x] 拆分多容器 UI 任务。
- [x] 拆分 stream 与 container 绑定任务。
- [x] 拆分 active stream 画面选择任务。
- [x] 拆分 2x2 布局验证任务。
- [x] 记录不做范围。

**验收标准**：

- [x] 每个任务都能独立验证。
- [x] 不直接跳到统一 OpenGL 多纹理。
- [x] 不破坏 Phase 3.2 多路管理边界。

## 3.3.3 布局模型定义

**目标**：先定义多路显示布局枚举和基础语义，不接入 UI，不创建容器。

- [x] 定义 `MultiViewLayout`。
- [x] 定义当前布局状态。
- [x] 定义布局切换入口。
- [x] 明确 `Single` 和 `Grid2x2` 的最大可见路数。
- [x] 不引入 `MultiViewRenderer`。

**验收标准**：

- [x] 能在代码中表示当前布局。
- [x] 能在 `Single` 和 `Grid2x2` 之间切换状态。
- [x] 不改变现有播放行为。

## 3.3.4 多容器 UI 骨架

**目标**：在 MainWindow 中准备多个视频容器，用于承载多个 `VideoRender`。

- [x] 准备 4 个视频容器。
- [x] 添加布局切换控件。
- [x] 保留现有播放控制区。
- [x] 保留 `Streams` 验证列表。
- [x] 不让 UI 直接访问 `MultiStreamManager`。

**验收标准**：

- [x] 程序启动后 UI 正常。
- [x] `Single` 模式显示一个视频区域。
- [x] `2x2` 模式显示四个视频区域。
- [x] 切换布局不崩溃。

## 3.3.5 Stream 与容器绑定

**目标**：添加 stream 时为其分配一个可用视频容器。

- [x] 在 `MainWindow` 维护 `StreamId -> 视频容器索引` 的 UI 绑定表。
- [x] 添加 stream 时分配第一个空闲容器。
- [x] 添加 stream 时把对应容器传给 `MediaPlayController::AddStream()`。
- [x] 移除 stream 后释放容器占用。
- [x] 容器满时拒绝继续添加显示流并输出日志。

**验收标准**：

- [x] 添加第 1 路显示在一个容器。
- [x] 添加第 2 路显示在另一个容器。
- [x] 最多添加 4 路显示流。
- [x] 移除一路后容器可再次复用。
- [x] UI 仍只通过 Controller 添加和移除 stream。

## 3.3.6 Active Stream 画面选择

**目标**：点击某个视频容器时切换 active stream，并同步音频策略。

- [x] 为每个视频容器添加点击处理。
- [x] 根据容器找到对应 `StreamId`。
- [x] 点击画面时调用 `SetActiveStream(streamId)`。
- [x] 有音频流点击后调用 `SetActiveAudioStream(streamId)`。
- [x] 无音频流点击后调用 `SetActiveAudioStream(InvalidStreamId)`。
- [x] UI 标记 active stream。

**验收标准**：

- [x] 点击有音频画面后，进度条和声音都切换到该路。
- [x] 点击无音频画面后，进度条切换到该路，程序无声。
- [x] 不出现画面和声音来自不同路的情况。
- [x] `MainWindow` 不直接访问 `MultiStreamManager`。

## 3.3.7 Single 布局行为

**目标**：定义并实现 `Single` 布局的最小行为。

- [x] `Single` 只显示 active stream 对应容器。
- [x] 非 active stream 容器隐藏。
- [x] active stream 变化时刷新容器可见性。
- [x] 没有 active stream 时显示空白容器。

**验收标准**：

- [x] 单路播放体验不退化。
- [x] 多路存在时只显示 active stream。
- [x] 切换 active stream 后画面区域跟随变化。

## 3.3.8 Grid2x2 布局行为

**目标**：实现第一版 2x2 多画面显示。

- [x] 实现 `Grid2x2` 容器布局。
- [x] 切换到 `Grid2x2` 后显示所有已绑定容器。
- [x] 空容器保持空白。
- [x] active stream 变化后刷新视觉标记。

**验收标准**：

- [x] 两路文件可以同时显示。
- [x] 四路文件可以同时显示。
- [x] 布局切换不影响播放生命周期。
- [x] 移除任意一路后布局保持稳定。
- [x] 无音频流显示正常。

## 3.3.9 Phase 3.3 验收报告

**目标**：收口多路显示布局第一版。

- [x] 形成 `doc/Phase3_3_多路显示布局实现报告.md`。
- [x] 记录最终布局策略。
- [x] 记录多容器 UI 实现方式。
- [x] 记录 stream 与 container 绑定策略。
- [x] 记录 active stream / active audio stream 点击策略。
- [x] 记录 Single 和 2x2 验证结果。
- [x] 记录已知限制。
- [x] 记录是否具备进入 `MultiViewRenderer` 预研的条件。

**验收标准**：

- [x] 单路 UI 不退化。
- [x] 2 路显示通过。
- [x] 4 路显示通过。
- [x] 点击画面切换 active stream 和音频符合预期。
- [x] 无音频流点击后无声。
- [x] 关闭程序不阻塞。

## Phase 3.4 MultiViewRenderer 预研

**目标**：在不改代码的前提下，明确从“多个 `VideoRender` + 多个 `QWidget` 容器”迁移到“单个 `MultiViewRenderer` + 多纹理渲染”的架构边界和实施路径。

## 3.4.1 当前渲染链路复盘

**目标**：确认现有渲染职责分布，找出进入统一渲染前必须拆分的边界。

- [x] 梳理 `MediaPipeline -> VideoRender -> rendererContainer` 当前链路。
- [x] 梳理 `VideoRender` 当前承担的职责。
- [x] 梳理 `MediaPipeline` 当前直接创建 `VideoRender` 的影响。
- [x] 梳理多个 `QOpenGLWidget` 方案的优势和限制。

**产出**：

- [x] `doc/Phase3_4_MultiViewRenderer预研报告.md`

## 3.4.2 MultiViewRenderer 边界判断

**目标**：明确 `MultiViewRenderer` 应属于哪个层，以及它不应该承担哪些职责。

- [x] 判断 `MultiViewRenderer` 所属层级。
- [x] 明确它是否持有解码器、demuxer、音频播放器。
- [x] 明确它是否参与 active audio 策略。
- [x] 明确它是否负责布局、点击命中和 active stream 视觉标记。

## 3.4.3 帧输出抽象预研

**目标**：明确 `MediaPipeline` 是否应从“直接创建渲染控件”改为“输出视频帧给外部渲染器”。

- [x] 分析是否需要新增视频帧输出接口。
- [x] 分析 `VideoRender` 可拆分的职责。
- [x] 分析帧所有权和释放责任。
- [x] 分析 seek、stop、remove stream 时的帧清理要求。

## 3.4.4 迁移路线判断

**目标**：给出后续可执行的最小迁移路径，不直接推翻当前稳定链路。

- [x] 明确不建议直接实现完整 `MultiViewRenderer`。
- [x] 给出先抽象帧输出、再接入统一渲染的迁移顺序。
- [x] 明确每一步的验收标准。
- [x] 明确保留当前多 `VideoRender` 方案作为回退路径。

## 3.4.5 Phase 3.4 验收标准

- [x] 能说明为什么当前多个 `QOpenGLWidget` 不是最终方案。
- [x] 能说明为什么不能直接让 `MultiViewRenderer` 管理解码管线。
- [x] 能说明 `MediaPipeline` 后续需要抽象出的渲染边界。
- [x] 能说明从当前方案迁移到统一渲染的最小安全路径。
- [x] 不直接修改播放代码。

## Phase 3.5 音视频格式归一化预研

**目标**：在继续推进 `MultiViewRenderer` 和视频帧输出边界前，先明确音频输出格式、视频解码输出格式和渲染输入格式的归一化策略。

## 3.5.1 音频输出格式链路复盘

**目标**：确认当前 `AudioPlayer` 的采样率、声道布局、采样格式和 Qt 输出格式是否一致。

- [x] 梳理 `codecPar -> QAudioFormat -> SwrContext -> PCM bytes` 链路。
- [x] 分析固定 stereo outLayout 与 `m_dstChannels` 不一致的风险。
- [x] 分析设备 preferred format fallback 对重采样参数的影响。
- [x] 明确音频格式归一化的推荐策略。

## 3.5.2 视频帧格式链路复盘

**目标**：确认当前 decoder 输出帧和 `VideoRender` 输入格式之间的约束。

- [x] 梳理软解和硬解输出路径。
- [x] 分析 `VideoRender` 只支持 `AV_PIX_FMT_YUV420P` 的限制。
- [x] 分析 4K MKV / 10-bit / HDR / BT.2020 / limited range 的风险。
- [x] 明确视频帧格式归一化的推荐策略。

## 3.5.3 后续实施边界

**目标**：确定哪些问题应在进入统一渲染前解决，哪些可以留给 `MultiViewRenderer` 阶段。

- [x] 明确音频重采样应先保证输出格式一致。
- [x] 明确视频解码输出应先归一化到 renderer 可消费格式。
- [x] 明确当前阶段不直接实现 HDR tone mapping。
- [x] 明确格式归一化应作为帧输出边界抽象的前置条件。

**产出**：

- [x] `doc/Phase3_5_音视频格式归一化预研报告.md`

## 3.5.4 音频输出格式归一化实现

**目标**：先修正音频输出格式链路，确保重采样输出、Qt 音频设备格式、PCM 字节计算和音频时钟推进使用同一套最终输出参数。

- [x] 统一最终输出采样率、声道数、声道布局和采样格式。
- [x] 使用最终输出格式初始化 `QAudioFormat`。
- [x] 使用同一套最终输出格式初始化 `SwrContext`。
- [x] 修正 PCM 字节数和 `bytesPerSampleFrame` 计算。
- [x] 为输入 `AVChannelLayout` 缺失场景增加兜底。
- [x] 检查 `swr_alloc_set_opts2()` 和 `swr_init()` 返回值。
- [x] 输出音频输入格式、重采样输出格式和 Qt 设备格式日志。

**验收标准**：

- [ ] AAC 44100Hz 文件无撕裂。
- [ ] 普通 MP4 音频正常。
- [ ] 无音频流视频不受影响。
- [ ] 音画同步不退化。

## 3.5.5 VideoRender 输入格式能力梳理与最小修正

**目标**：明确 `VideoRender` 当前支持的输入帧格式，对不支持格式输出可诊断日志，避免静默失败。本任务不修改 decoder 输出格式，不做 `sws_scale` 统一转换。

- [x] 将支持格式判断抽成 `IsSupportedFrameFormat()`。
- [x] 明确当前仅支持 `AV_PIX_FMT_YUV420P`。
- [x] 新增帧格式签名记录，避免每帧重复刷日志。
- [x] 对支持格式首次输出 accepted 日志。
- [x] 对不支持格式输出 unsupported 日志。
- [x] 日志包含 pix_fmt、width、height、linesize、color_range、colorspace、color_primaries、color_trc。
- [x] 非支持格式不进行纹理上传和绘制。
- [x] 不修改 decoder 输出格式。
- [x] 不实现 `sws_scale` 统一转换。
- [x] 不实现 HDR tone mapping。

**验收标准**：

- [ ] 普通 MP4 可看到 accepted `yuv420p` 日志并正常播放。
- [ ] 4K MKV 可看到实际 pix_fmt 和色彩信息日志。
- [ ] 不支持格式不再静默失败。
- [ ] 当前已支持格式播放不退化。

## Phase 3.6 MultiViewRenderer 空壳

**目标**：先建立统一渲染器的布局、绘制和点击命中边界，不接真实视频帧，不替换当前多个 `VideoRender` 的稳定播放路径。

**OpenGL 学习路线**：

- [x] `doc/Phase3_6_MultiViewRenderer_OpenGL学习路线.md`

## 3.6.1 MultiViewRenderer 空壳设计

**目标**：新增统一渲染器类的最小接口和职责边界。

- [x] 新增 `MultiViewRenderer` 类。
- [x] `MultiViewRenderer` 继承 `QOpenGLWidget`。
- [x] 明确该类属于基础设施层。
- [x] 不持有 `MediaPipeline`。
- [x] 不访问 demuxer / decoder / audio player。
- [x] 不参与 active audio stream 策略。
- [x] 不接真实视频帧。

**验收标准**：

- [x] 类可以独立加入构建。
- [x] 不影响当前播放路径。
- [x] 当前多个 `VideoRender` 方案仍可作为回退路径。

## 3.6.2 Viewport 布局计算

**目标**：让 `MultiViewRenderer` 能根据布局和 stream 列表计算每路画面区域。

- [x] 定义渲染器内部布局枚举或复用现有布局语义。
- [x] 支持 `Single` 布局 viewport。
- [x] 支持 `Grid2x2` 布局 viewport。
- [x] 支持最多 4 个逻辑 view slot。
- [x] 空 slot 保持空白。
- [x] 不引入 3x3 / 4x4 / 1+N。

**验收标准**：

- [x] `Single` 能计算一个全区域 viewport。
- [x] `Grid2x2` 能计算四个 viewport。
- [x] 空 slot 不影响布局计算。

## 3.6.3 空画面绘制

**目标**：先验证统一 OpenGL widget 可以绘制多画面区域，不接纹理。

- [x] 初始化 OpenGL 状态。
- [x] 绘制空背景。
- [x] 绘制 `Single` 空画面区域。
- [x] 绘制 `Grid2x2` 四个空画面区域。
- [x] 绘制 slot 边界。
- [x] 不上传视频纹理。

**验收标准**：

- [ ] 渲染器空壳显示正常。
- [ ] `Single` / `Grid2x2` 空布局显示正常。
- [ ] 切换布局不崩溃。

## 3.6.4 Active Stream 视觉标记

**目标**：让统一渲染器能根据 active stream 绘制选中态。

- [ ] 支持设置 slot 与 `StreamId` 的绑定关系。
- [ ] 支持设置 active stream。
- [ ] active stream 对应 viewport 绘制高亮边框。
- [ ] 无 active stream 时不绘制高亮。

**验收标准**：

- [ ] active stream 高亮正确。
- [ ] 切换 active stream 后高亮跟随变化。
- [ ] 空 slot 不显示 active 高亮。

## 3.6.5 点击命中与信号

**目标**：将鼠标点击坐标映射到 viewport，并对外发出 stream 点击信号。

- [ ] 实现坐标命中 viewport。
- [ ] 命中已绑定 slot 时发出 `StreamClicked(StreamId)` 信号。
- [ ] 点击空 slot 不发出有效 stream 信号。
- [ ] `MultiViewRenderer` 不直接调用 `SetActiveStream()`。
- [ ] active stream 切换仍由 `MainWindow -> MediaPlayController` 完成。

**验收标准**：

- [ ] 点击不同 viewport 能识别对应 stream。
- [ ] 点击空区域不误触发。
- [ ] 渲染器不承担播放状态切换职责。

## 3.6.6 MainWindow 实验性接入入口

**目标**：以并存方式验证 `MultiViewRenderer` 空壳，不替换当前真实播放显示链路。

- [ ] 在 UI 中预留实验性渲染器挂载入口。
- [ ] `MultiViewRenderer` 与当前多个 `VideoRender` 路径并存。
- [ ] 默认仍使用当前多个 `VideoRender` 播放路径。
- [ ] 实验入口只用于验证空布局、active 高亮和点击命中。
- [ ] 不把真实视频帧接入 `MultiViewRenderer`。

**验收标准**：

- [ ] 当前播放功能不退化。
- [ ] 可以打开或显示 `MultiViewRenderer` 空壳。
- [ ] 空壳布局、active 高亮、点击信号可验证。
- [ ] 关闭程序不阻塞。

## 3.6.7 Phase 3.6 验收报告

**目标**：收口统一渲染器空壳阶段，决定是否进入真实视频帧接入。

- [ ] 形成 `doc/Phase3_6_MultiViewRenderer空壳实现报告.md`。
- [ ] 记录空壳类职责边界。
- [ ] 记录 viewport 布局计算方式。
- [ ] 记录 active stream 高亮策略。
- [ ] 记录点击命中策略。
- [ ] 记录与当前多 `VideoRender` 路径的并存方式。
- [ ] 记录下一阶段接真实帧前的风险点。

**验收标准**：

- [ ] `MultiViewRenderer` 不管理播放管线。
- [ ] 当前播放路径不退化。
- [ ] 空布局绘制通过。
- [ ] 点击命中通过。
- [ ] 具备进入真实帧接入预研的条件。
