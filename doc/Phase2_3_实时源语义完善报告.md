# Phase 2.3 实时源语义完善报告

## 1. 目标

本阶段目标是把摄像头这类实时源和文件源的播放语义区分清楚，避免继续依赖 `MediaSourceType::Camera` 做分散判断。

核心语义：

- 文件源：有确定时长、支持 seek、不是实时源。
- 摄像头源：没有确定时长、不支持 seek、是实时源。

## 2. 能力接口

`IMediaSource` 已增加三个能力查询接口：

```cpp
virtual bool IsSeekable() const = 0;
virtual bool HasDuration() const = 0;
virtual bool IsRealtime() const = 0;
```

当前实现：

- `FileMediaSource`：`IsSeekable() = true`，`HasDuration() = true`，`IsRealtime() = false`。
- `CameraMediaSource`：`IsSeekable() = false`，`HasDuration() = false`，`IsRealtime() = true`。

## 3. 播放控制接入

`MediaPipeline`：

- `SeekTo()` 使用 `mediaSource_->IsSeekable()` 拒绝不可 seek 源。
- `SeekRelative()` 使用 `mediaSource_->IsSeekable()` 拒绝不可 seek 源。
- `Duration()` 使用 `mediaSource_->HasDuration()` 判断是否返回真实时长；实时源返回 0。

`MultiStreamManager`：

- 提供 `IsActiveStreamSeekable()`。
- 提供 `IsActiveStreamRealtime()`。
- 提供 `HasActiveStreamDuration()`。

`MediaPlayController`：

- 对 UI 暴露 active stream 能力查询。
- `SeekTo()` 先检查 active stream 是否可 seek。
- `SeekRelative()` 先检查 active stream 是否可 seek。

## 4. UI 行为

`MainWindow` 不直接访问 `MultiStreamManager` 或 `MediaPipeline`，只通过 `MediaPlayController` 查询 active stream 能力。

实时源 UI 行为：

- active stream 是摄像头时，进度条禁用。
- duration 显示 `LIVE`。
- current position 继续显示运行时长。
- 拖动释放时再次检查 `IsActiveStreamSeekable()`，不可 seek 源不触发 seek。

文件源 UI 行为：

- active stream 有确定 duration 且可 seek 时，进度条启用。
- duration 显示普通时间。
- 切回文件源后恢复原进度条语义。

## 5. 架构边界

本阶段保持了既定架构边界：

- `MainWindow` 只负责 UI 显示和用户交互。
- `MediaPlayController` 是 UI 查询播放能力的应用层入口。
- `MultiStreamManager` 按 active stream 读取媒体源能力。
- `MediaPipeline` 只使用当前单路媒体源能力做播放控制。
- `CameraMediaSource` 只声明自身能力，不参与 UI、Manager 或 Pipeline 策略。

## 6. 当前限制

- 实时源 `Stop()` 已在 Phase 2.4 前置修正中避免走文件 seek 回零路径。
- UI 只做最小 live 状态显示，未做更完整的 live 徽标或专门样式。
- 运行中断流、设备拔出等异常处理尚未进入本阶段。

## 7. 验收结论

Phase 2.3 已完成实时源能力接口、播放控制防护和 UI 最小 live 状态显示。

下一阶段建议进入 `Phase 2.4 摄像头异常与生命周期`，继续补齐打开失败、运行中断流、设备拔出和资源释放验证。
