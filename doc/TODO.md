# TODO: Phase 3.2 多路管理设计

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

- [ ] 定义 `StreamSlot`。
- [ ] 使用容器保存 `StreamId -> StreamSlot`。
- [ ] `AddStream()` 分配新的 `StreamId`。
- [ ] `AddStream()` 创建 `MediaPipeline`。
- [ ] `AddStream()` 初始化媒体源和 renderer container。
- [ ] `AddStream()` 失败时完整回滚。
- [ ] `RemoveStream()` 调用 Pipeline `Shutdown()`。
- [ ] `RemoveStream()` 删除 slot。
- [ ] `ShutdownAll()` 释放全部 Pipeline。
- [ ] 删除 active stream 时更新 active stream。
- [ ] 删除 active audio stream 时更新 active audio stream。

**验收标准**：

- [ ] 可以添加一路文件源。
- [ ] 可以移除一路文件源。
- [ ] 可以添加多路但暂不接 UI 播放。
- [ ] 移除不存在的 id 不崩溃。

## 3.2.4 Manager 转发 Pipeline 信号

**目标**：让 Manager 对外暴露带 `StreamId` 的信号。

- [ ] 转发 Pipeline 的 `PositionChanged`。
- [ ] 转发 Pipeline 的 `DurationChanged`。
- [ ] 转发 Pipeline 的 `PlaybackFinished`。
- [ ] 转发 Pipeline 的 `VolumeChanged`。
- [ ] 转发 Pipeline 的 `MuteChanged`。
- [ ] 转发 Pipeline 的 `ErrorOccurred`。
- [ ] 所有转发信号携带 `StreamId`。
- [ ] RemoveStream 后旧信号不会访问已删除 slot。

**验收标准**：

- [ ] 任意一路 Pipeline 的信号能标识来源。
- [ ] 删除一路后无悬空回调。
- [ ] 当前单路 Controller 信号转发不受影响。

## 3.2.5 Manager 单路控制能力

**目标**：Manager 可以控制指定一路 Pipeline。

- [ ] 定义 `StartStream(StreamId id)`。
- [ ] 定义 `PauseStream(StreamId id)`。
- [ ] 定义 `StopStream(StreamId id)`。
- [ ] 定义 `SeekStreamTo(StreamId id, double seconds)`。
- [ ] 定义 `SeekStreamRelative(StreamId id, double seconds)`。
- [ ] 定义 `SetStreamPlaybackSpeed(StreamId id, double speed)`。
- [ ] 定义 `SetStreamVolume(StreamId id, double volume)`。
- [ ] 定义 `SetStreamMuted(StreamId id, bool muted)`。
- [ ] id 不存在时安全返回。

**验收标准**：

- [ ] 指定 id 存在时操作生效。
- [ ] 指定 id 不存在时不崩溃。
- [ ] 无音频视频设置音量和静音不崩溃。

## 3.2.6 Manager 全局控制能力

**目标**：Manager 可以对全部 Pipeline 执行批量控制。

- [ ] 定义 `StartAll()`。
- [ ] 定义 `PauseAll()`。
- [ ] 定义 `StopAll()`。
- [ ] 定义 `ShutdownAll()`。
- [ ] 全局控制跳过无效 slot。

**验收标准**：

- [ ] 多路启动不崩溃。
- [ ] 多路暂停不阻塞。
- [ ] 多路停止可以清空各自状态。
- [ ] 程序关闭时可以释放全部资源。

## 3.2.7 Active Stream 状态设计

**目标**：确定 UI 当前显示哪一路状态。

- [ ] Manager 保存 `activeStreamId_`。
- [ ] `SetActiveStream()` 校验 id 是否存在。
- [ ] active stream 被删除时自动选择下一个可用 stream 或置为无效。
- [ ] Controller 后续从 active stream 读取 `Position()`。
- [ ] Controller 后续从 active stream 读取 `Duration()`。
- [ ] 没有 active stream 时返回 0 或无效状态。

**验收标准**：

- [ ] 切换 active stream 后 UI 状态语义明确。
- [ ] 删除 active stream 不崩溃。
- [ ] 没有 active stream 时行为可解释。

## 3.2.8 Active Audio Stream 策略

**目标**：避免多路同时输出音频。

- [ ] Manager 保存 `activeAudioStreamId_`。
- [ ] 第一条有音频流默认成为 active audio stream。
- [ ] 非 active audio stream 默认静音。
- [ ] 切换 active audio stream 时旧路静音。
- [ ] 切换 active audio stream 时新路恢复当前 volume/muted。
- [ ] 无音频流不能成为有效 active audio stream。

**验收标准**：

- [ ] 多路播放时最多一路有声音。
- [ ] 切换 active audio stream 后音频输出符合预期。
- [ ] 无音频视频不影响音频策略。

## 3.2.9 Controller 接入 Manager

**目标**：让 `MediaPlayController` 从持有单个 Pipeline 过渡到持有 Manager。

- [ ] Controller 新增 `std::unique_ptr<MultiStreamManager> multiStreamManager_`。
- [ ] 保留当前单路 UI 接口不变。
- [ ] `Initialize()` 创建第一路 stream。
- [ ] `SwitchSource()` 替换 active stream。
- [ ] `Start()` 委托 Manager 操作 active stream。
- [ ] `Pause()` 委托 Manager 操作 active stream。
- [ ] `Stop()` 委托 Manager 操作 active stream。
- [ ] `SeekTo()` 委托 Manager 操作 active stream。
- [ ] `SeekRelative()` 委托 Manager 操作 active stream。
- [ ] Controller 继续保存 playbackSpeed / volume / muted。
- [ ] Controller 不直接持有多个 Pipeline。

**验收标准**：

- [ ] 现有 UI 行为不变。
- [ ] 播放列表切换不退化。
- [ ] Controller 不直接持有多个 Pipeline。

## 3.2.10 最小多路文件源验证

**目标**：用本地文件源验证 Manager 能管理多路。

- [ ] 添加两路普通文件。
- [ ] 添加一路普通文件和一路无音频文件。
- [ ] 启动所有路。
- [ ] 暂停所有路。
- [ ] 停止所有路。
- [ ] 移除其中一路。
- [ ] 关闭程序。
- [ ] 验证最多一路有声音。

**验收标准**：

- [ ] 多路生命周期稳定。
- [ ] 无音频流不崩溃。
- [ ] 音频最多一路输出。
- [ ] 关闭程序不阻塞。

## 3.2.11 Phase 3.2 验收报告

**目标**：收口多路管理阶段。

- [ ] 形成 `doc/Phase3_2_多路管理实现报告.md`。
- [ ] 记录 Manager 最终职责。
- [ ] 记录 Controller 和 Manager 边界。
- [ ] 记录 Pipeline 是否保持单路无 id。
- [ ] 记录文件源多路验证结果。
- [ ] 记录已知限制。
- [ ] 记录下一阶段建议。

**验收标准**：

- [ ] Manager 边界清晰。
- [ ] Controller 不直接持有多个 Pipeline。
- [ ] Pipeline 保持单路职责。
- [ ] 后续可以进入多路 UI 或多路渲染优化。

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

- [ ] `MultiStreamManager` 可以管理多个 `MediaPipeline`。
- [ ] `StreamId` 只存在于 Manager 层。
- [ ] `MediaPipeline` 继续保持单路无 id。
- [ ] Controller 可以通过 Manager 操作 active stream。
- [ ] 文件源多路生命周期验证通过。
- [ ] 无音频视频多路验证通过。
- [ ] 多路音频策略不混乱。
