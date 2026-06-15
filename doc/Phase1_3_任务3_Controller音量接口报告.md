# Phase 1.3 任务 3：MediaPlayController 音量接口报告

## 目标

在 `MediaPlayController` 上暴露统一音量控制入口，让 UI 后续只依赖 Controller，不直接接触 `AudioPlayer` 或 `QAudioSink`。

本任务只修改 Controller，不接入 UI。

## 实现内容

### 新增 Controller 音量接口

新增：

```cpp
void SetVolume(double volume);
double Volume() const;
void SetMuted(bool muted);
bool IsMuted() const;
```

语义：

- `volume` 使用 `0.0` 到 `1.0`。
- `SetVolume()` 负责 clamp。
- `SetMuted(true)` 表示静音，但不改变 `volume_`。
- `SetMuted(false)` 恢复当前 `volume_` 对应的输出音量。
- 未初始化播放管线时调用 `SetVolume()` / `SetMuted()` 不会崩溃，Controller 会先保存状态并发出自身信号。

### 新增 Controller 信号

新增：

```cpp
void VolumeChanged(double volume);
void MuteChanged(bool muted);
```

Controller 连接 `AudioPlayer` 信号：

```cpp
AudioPlayer::VolumeChanged -> MediaPlayController::VolumeChanged
AudioPlayer::MuteChanged   -> MediaPlayController::MuteChanged
```

这样后续 UI 可以只监听 Controller。

### 保留快捷入口

`VolumeUp()` / `VolumeDown()` 保留，但不再直接调用 `AudioPlayer::VolumeUp()` / `VolumeDown()`。

当前实现为：

```cpp
VolumeUp()   -> SetVolume(volume_ + 0.05)
VolumeDown() -> SetVolume(volume_ - 0.05)
```

这样键盘快捷键和后续 UI 滑块都会走同一套 Controller 状态。

### 换源状态保持

Controller 新增状态：

```cpp
double volume_ = 1.0;
bool muted_ = false;
```

每次新建 `AudioPlayer` 后，Controller 会下发当前状态：

```cpp
audioplay_->SetVolume(volume_);
audioplay_->SetMuted(muted_);
```

因此切换文件或重建播放管线后，音量和静音状态不会回到默认值。

## 架构边界

- `MainWindow` 仍然不直接依赖 `AudioPlayer`。
- `MediaPlayController` 没有新增 UI 控件依赖。
- `AudioPlayer` 仍负责真实音量输出。
- Controller 只保存应用层可见的音量/静音状态，并负责换源后的状态恢复。

## 静态验收

- 已新增 `SetVolume(double)` / `Volume() const`。
- 已新增 `SetMuted(bool)` / `IsMuted() const`。
- 已新增 `VolumeChanged(double)` / `MuteChanged(bool)`。
- 已连接 `AudioPlayer` 音量和静音信号。
- `VolumeUp()` / `VolumeDown()` 已改为走 Controller 的 `SetVolume()`。
- 新 `AudioPlayer` 初始化后会恢复 Controller 保存的音量和静音状态。

## 未执行项

- 未编译验证。
- 未接入 UI 音量滑块和静音按钮。

## 结论

1.3.3 完成。

下一步应进入 1.3.4，在 `MainWindow` 中接入 `slider_volume` 和 `pushButton_mute`，并只通过 `MediaPlayController` 的音量接口交互。
