# Phase 3.2 多路管理实施计划

## 阶段定位

Phase 3.2 是从“单路播放管线”进入“多路播放管理”的过渡阶段。

本阶段不直接追求复杂多路 UI，而是先建立 `MultiStreamManager`，确保多路生命周期、id 管理、音频策略和 Controller 边界正确。

## 总体目标

```text
MainWindow
    -> MediaPlayController
    -> MultiStreamManager
        -> MediaPipeline #1
        -> MediaPipeline #2
        -> MediaPipeline #3
```

最终目标：

- `MediaPipeline` 保持单路资源管理职责。
- `MultiStreamManager` 管理多个 Pipeline。
- `MediaPlayController` 通过 Manager 管理单路或多路。
- `MainWindow` 不直接访问 Pipeline 或 Manager 内部资源。
- 第一版用本地文件源验证多路能力。

## 3.2.1 多路管理边界预研

**目标**：明确 `MultiStreamManager` 职责边界，不改代码。

任务：

- 梳理 Phase 3.1 后的单路架构现状。
- 明确 `MainWindow`、`MediaPlayController`、`MultiStreamManager`、`MediaPipeline` 职责。
- 明确 `StreamId` 只属于 Manager。
- 明确 `MediaPipeline` 不感知 id。
- 明确 active stream 和 active audio stream 概念。
- 明确第一版使用多个 `VideoRender`，不做 `MultiViewRenderer`。
- 明确文件源验证策略。
- 明确不做范围。

产出：

- `doc/Phase3_2_多路管理架构预研报告.md`

验收：

- 可以说明 Manager 和 Pipeline 的边界。
- 可以说明 Manager 和 Controller 的关系。
- 可以说明为什么不直接让 MainWindow 管多个 Pipeline。

## 3.2.2 MultiStreamManager 空壳

**目标**：新增类和接口，不接入 Controller，不创建真实 Pipeline。

建议新增：

- `src/domain/MultiStreamManager.h`
- `src/domain/MultiStreamManager.cpp`

第一版接口：

- `AddStream()`
- `RemoveStream()`
- `HasStream()`
- `StreamIds()`
- `ActiveStream()`
- `SetActiveStream()`
- `ActiveAudioStream()`
- `SetActiveAudioStream()`
- `ShutdownAll()`

信号：

- `StreamAdded`
- `StreamRemoved`
- `ActiveStreamChanged`
- `ActiveAudioStreamChanged`
- `StreamErrorOccurred`

验收：

- 新类加入构建系统。
- 不改变现有播放行为。
- 不引入 UI 改动。
- `MediaPipeline` 不出现 id 字段。

## 3.2.3 Manager 持有多个 Pipeline

**目标**：让 `MultiStreamManager` 可以创建和释放多个 `MediaPipeline`。

任务：

- 定义 `StreamSlot`。
- 使用 `QHash<StreamId, StreamSlot>` 或等价容器保存多路。
- `AddStream()` 创建 Pipeline。
- `RemoveStream()` Shutdown 并移除 Pipeline。
- `ShutdownAll()` 释放所有 Pipeline。
- 添加失败时回滚资源。

第一版 `StreamSlot`：

```cpp
struct StreamSlot {
    std::shared_ptr<IMediaSource> source;
    std::unique_ptr<MediaPipeline> pipeline;
    QWidget* rendererContainer = nullptr;
};
```

验收：

- 可以添加一路文件源。
- 可以移除一路文件源。
- 可以添加多路但暂不接 UI 播放。
- 移除不存在的 id 不崩溃。

## 3.2.4 Manager 转发 Pipeline 信号

**目标**：让 Manager 对外暴露带 `StreamId` 的信号。

任务：

- 连接每个 Pipeline 的 `PositionChanged`。
- 连接每个 Pipeline 的 `DurationChanged`。
- 连接每个 Pipeline 的 `PlaybackFinished`。
- 连接每个 Pipeline 的 `VolumeChanged`。
- 连接每个 Pipeline 的 `MuteChanged`。
- 连接每个 Pipeline 的 `ErrorOccurred`。
- 转发为带 `StreamId` 的 Manager 信号。

验收：

- 任意一路 Pipeline 的信号可以被 Manager 标识来源。
- RemoveStream 后旧信号不再回调到已删除 slot。
- 不影响当前单路 Controller 信号转发。

## 3.2.5 Manager 单路控制能力

**目标**：Manager 可以控制指定一路 Pipeline。

任务：

- `StartStream(StreamId id)`
- `PauseStream(StreamId id)`
- `StopStream(StreamId id)`
- `SeekStreamTo(StreamId id, double seconds)`
- `SeekStreamRelative(StreamId id, double seconds)`
- `SetStreamPlaybackSpeed(StreamId id, double speed)`
- `SetStreamVolume(StreamId id, double volume)`
- `SetStreamMuted(StreamId id, bool muted)`

验收：

- 指定 id 存在时操作生效。
- 指定 id 不存在时安全返回。
- 无音频视频设置音量和静音不崩溃。

## 3.2.6 Manager 全局控制能力

**目标**：Manager 可以对所有 Pipeline 执行批量控制。

任务：

- `StartAll()`
- `PauseAll()`
- `StopAll()`
- `ShutdownAll()`

验收：

- 多路启动不崩溃。
- 多路暂停不阻塞。
- 多路停止可以清空各自状态。
- 程序关闭时 `ShutdownAll()` 能释放全部资源。

## 3.2.7 Active Stream 状态设计

**目标**：确定 UI 当前显示哪一路状态。

任务：

- Manager 保存 `activeStreamId_`。
- `SetActiveStream()` 校验 id 是否存在。
- Active stream 被删除时自动选择下一个可用 stream 或置为无效。
- Controller 的 `Position()` / `Duration()` 后续读取 active stream。
- Controller 转发 active stream 的进度和时长给 UI。

验收：

- 切换 active stream 后 UI 进度语义明确。
- 删除 active stream 不崩溃。
- 没有 active stream 时返回 0 或无效状态。

## 3.2.8 Active Audio Stream 策略

**目标**：避免多路同时输出音频。

任务：

- Manager 保存 `activeAudioStreamId_`。
- 第一条有音频流默认成为 active audio stream。
- 非 active audio stream 默认静音。
- 切换 active audio stream 时旧路静音，新路恢复 Controller 当前 volume/muted。
- 无音频流不能成为有效 active audio stream。

验收：

- 多路播放时最多一路有声音。
- 切换 active audio stream 后音频输出符合预期。
- 无音频视频不影响音频策略。

## 3.2.9 Controller 接入 Manager

**目标**：让 `MediaPlayController` 从持有单个 Pipeline 过渡到持有 Manager。

任务：

- Controller 新增 `std::unique_ptr<MultiStreamManager> multiStreamManager_`。
- 保留当前单路接口不变。
- 当前单路接口暂时操作 active stream。
- `Initialize()` 可以创建第一路 stream。
- `SwitchSource()` 可以替换 active stream。
- `Start/Pause/Stop/Seek` 委托 Manager 操作 active stream。
- Controller 继续保存 playbackSpeed / volume / muted。

验收：

- 现有 UI 行为不变。
- 播放列表切换不退化。
- Controller 不直接持有多个 Pipeline。

## 3.2.10 最小多路文件源验证

**目标**：用本地文件源验证 Manager 能管理多路。

可选方式：

- 暂时通过内部测试入口添加两路文件源。
- 或后续新增最小 UI 容器。

验证项：

- 添加两路普通文件。
- 添加一路普通文件和一路无音频文件。
- 启动所有路。
- 暂停所有路。
- 停止所有路。
- 移除其中一路。
- 关闭程序。

验收：

- 多路生命周期稳定。
- 无音频流不崩溃。
- 音频最多一路输出。
- 关闭程序不阻塞。

## 3.2.11 Phase 3.2 验收报告

**目标**：收口多路管理阶段。

产出：

- `doc/Phase3_2_多路管理实现报告.md`

报告内容：

- Manager 最终职责。
- Controller 和 Manager 边界。
- Pipeline 是否保持单路无 id。
- 文件源多路验证结果。
- 已知限制。
- 下一阶段建议。

## 不做范围

Phase 3.2 暂不做：

- 完整多路 UI。
- 多路布局切换。
- `MultiViewRenderer`。
- 统一 OpenGL 多纹理渲染。
- 摄像头枚举。
- RTSP / RTMP。
- 工业相机 SDK。
- 图像处理。
- 多路性能优化。

## 推荐推进顺序

建议严格按以下顺序：

1. 先完成 `MultiStreamManager` 空壳。
2. 再让 Manager 持有多个 Pipeline。
3. 再做 Manager 信号转发。
4. 再做单路/全局控制接口。
5. 再处理 active stream 和 active audio stream。
6. 最后让 Controller 接入 Manager。

不要先改 UI，也不要先做复杂多路渲染。
