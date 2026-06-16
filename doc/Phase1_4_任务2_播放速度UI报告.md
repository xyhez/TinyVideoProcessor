# Phase 1.4 任务2：播放速度 UI 报告

## 目标

为当前已经存在的播放速度控制能力增加 UI 入口，让用户不只依赖快捷键 `1/2/3/4`，也可以通过界面选择播放速度。

本任务继续遵循 `doc/架构指导.md`：

- `MainWindow` 只负责 UI 控件初始化、事件转发和状态同步。
- 播放速度状态由 `MediaPlayController` 管理。
- `MainWindow` 不直接操作 `AudioPlayer` 或 `VideoRender`。
- 不新增全局变量。

## 当前基础

`MediaPlayController` 已经具备播放速度能力：

- `SetPlaybackSpeed(double speed)`
- `PlaybackSpeed() const`
- `PlaybackSpeedChanged(double speed)`

快捷键也已经存在：

- `1` -> `0.5x`
- `2` -> `1.0x`
- `3` -> `1.5x`
- `4` -> `2.0x`

Controller 在播放管线初始化时已经把 `playbackSpeed_` 下发到 `AudioPlayer` 和 `VideoRender`，因此切换文件后播放速度可以保持。

## 实施内容

### UI 控件

在 `mainwindow.ui` 的控制栏中新增：

```text
comboBox_speed
```

固定选项：

- `0.5x`
- `1.0x`
- `1.5x`
- `2.0x`

每个选项的 `itemData` 保存对应的 `double` 速度值。

### MainWindow 初始化

新增：

```cpp
void InitializePlaybackSpeedComboBox();
void UpdatePlaybackSpeedComboBox(double speed);
```

初始化流程：

1. 清空 `comboBox_speed`。
2. 添加固定速度档位。
3. 根据 `MediaPlayController::PlaybackSpeed()` 设置当前选中项。

### UI 到 Controller

连接 `comboBox_speed::currentIndexChanged`：

1. 从当前项 `itemData` 读取速度值。
2. 转为 `double`。
3. 调用 `MediaPlayController::SetPlaybackSpeed(speed)`。

UI 不保存播放速度状态。

### Controller 到 UI

连接 `MediaPlayController::PlaybackSpeedChanged` 到 `UpdatePlaybackSpeedComboBox(double)`。

`UpdatePlaybackSpeedComboBox()` 使用 `QSignalBlocker` 同步当前选项，避免 Controller 信号同步 UI 时反向触发一次 `SetPlaybackSpeed()`。

这样快捷键 `1/2/3/4` 修改速度后，UI 下拉框也会同步变化。

## 验收结论

- [x] UI 可以选择 `0.5x / 1.0x / 1.5x / 2.0x`。
- [x] UI 选择速度时只调用 `MediaPlayController::SetPlaybackSpeed()`。
- [x] 快捷键切换速度后，`PlaybackSpeedChanged` 会同步 UI 选择项。
- [x] 切换文件后播放速度保持当前 Controller 状态。
- [x] UI 不直接依赖 `AudioPlayer` 或 `VideoRender`。
- [x] 没有新增全局变量。

## 静态检查说明

本任务未进行编译和运行验证，仅做静态检查。

需要手动测试确认：

- 下拉框切换速度后，实际播放速度变化。
- 快捷键 `1/2/3/4` 切换后，下拉框显示同步变化。
- 切换文件后速度档位保持。
