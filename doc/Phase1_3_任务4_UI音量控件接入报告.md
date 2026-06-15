# Phase 1.3 任务 4：UI 音量控件接入报告

## 目标

在 `MainWindow` 接入音量滑块和静音按钮，让 UI 可以通过 `MediaPlayController` 控制音量和静音状态。

本任务只做 UI 绑定，不让 `MainWindow` 直接依赖 `AudioPlayer`。

## 实现内容

### UI 控件

在 `mainwindow.ui` 中新增：

```text
slider_volume
pushButton_mute
```

`slider_volume`：

- 范围：`0` 到 `100`
- 初始值：`100`
- 对应 Controller 内部音量：`0.0` 到 `1.0`

`pushButton_mute`：

- 未静音时显示：`静音`
- 静音时显示：`取消静音`

### 初始化

`MainWindow` 新增：

```cpp
void InitializeVolumeControls();
void UpdateMuteButton(bool muted);
```

初始化流程：

- 设置 `slider_volume` 范围为 `0-100`。
- 从 `MediaPlayController::Volume()` 读取当前音量并同步滑块。
- 从 `MediaPlayController::IsMuted()` 读取当前静音状态并同步按钮文本。

### UI -> Controller

音量滑块变化：

```cpp
slider_volume::valueChanged
    -> MediaPlayController::SetVolume(value / 100.0)
```

静音按钮点击：

```cpp
pushButton_mute::clicked
    -> MediaPlayController::SetMuted(!MediaPlayController::IsMuted())
```

### Controller -> UI

Controller 音量变化：

```cpp
MediaPlayController::VolumeChanged
    -> slider_volume->setValue(volume * 100)
```

同步滑块时使用 `QSignalBlocker`，避免 UI 被动更新时再次触发 `SetVolume()`。

Controller 静音状态变化：

```cpp
MediaPlayController::MuteChanged
    -> UpdateMuteButton(muted)
```

### 键盘同步

键盘上下键仍然调用：

```cpp
MainWindow::VolumeUp()
MainWindow::VolumeDown()
```

它们转发到 `MediaPlayController` 后，会通过 `VolumeChanged` 自动同步 `slider_volume`。

## 架构边界

- `MainWindow` 没有 include 或操作 `AudioPlayer`。
- `MainWindow` 只依赖 `MediaPlayController` 音量接口。
- 实际音量输出仍由 `AudioPlayer` 负责。
- 静音和音量状态仍由 Controller 对 UI 暴露。

## 静态验收

- 已新增 `slider_volume`。
- 已新增 `pushButton_mute`。
- `slider_volume` 范围为 `0-100`。
- `slider_volume::valueChanged` 已接入 `MediaPlayController::SetVolume()`。
- `pushButton_mute::clicked` 已接入 `MediaPlayController::SetMuted()`。
- `VolumeChanged` 已同步滑块。
- `MuteChanged` 已同步按钮文本。
- 上下键音量变化可通过 `VolumeChanged` 同步滑块。
- `MainWindow` 未直接依赖 `AudioPlayer`。

## 未执行项

- 未编译验证。
- 未运行验证 UI 行为。

## 结论

1.3.4 完成。

下一步应进入 1.3.5，做边界检查、运行验证和本阶段收口报告。
