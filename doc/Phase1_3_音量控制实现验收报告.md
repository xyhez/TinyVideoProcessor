# Phase 1.3 音量控制实现验收报告

## 目标

本阶段目标是把原有键盘音量增减能力扩展为完整音量控制链路：

- `AudioPlayer` 负责实际音频输出音量和静音应用。
- `MediaPlayController` 负责对外提供统一音量接口和状态信号。
- `MainWindow` 只负责 UI 控件绑定和事件转发。
- 不引入全局变量，不让 UI 直接依赖 `AudioPlayer` 或 `QAudioSink`。

本次验收只做静态检查，不做编译和运行验证。

## 当前链路

音量设置链路：

```text
MainWindow::slider_volume::valueChanged
    -> MediaPlayController::SetVolume(double)
    -> AudioPlayer::SetVolume(qreal)
    -> AudioPlayer::ApplyOutputVolume()
    -> QAudioSink::setVolume()
```

静音设置链路：

```text
MainWindow::pushButton_mute::clicked
    -> MediaPlayController::SetMuted(bool)
    -> AudioPlayer::SetMuted(bool)
    -> AudioPlayer::ApplyOutputVolume()
    -> QAudioSink::setVolume(0.0 或当前用户音量)
```

状态回传链路：

```text
AudioPlayer::VolumeChanged(qreal)
    -> MediaPlayController::VolumeChanged(double)
    -> MainWindow 同步 slider_volume

AudioPlayer::MuteChanged(bool)
    -> MediaPlayController::MuteChanged(bool)
    -> MainWindow 同步 pushButton_mute 文本
```

## 边界检查

### 音量范围小于 0 时归零

结论：通过静态检查。

依据：

- `MediaPlayController::SetVolume(double)` 使用 `std::clamp(volume, 0.0, 1.0)`。
- `AudioPlayer::SetVolume(qreal)` 也使用 `std::clamp(volume, 0.0, 1.0)`。

Controller 和基础设施层都有边界保护，外部传入负数不会继续向下扩散为非法音量。

### 音量范围大于 1 时归一

结论：通过静态检查。

依据：

- `MediaPlayController::SetVolume(double)` 将输入限制到 `0.0-1.0`。
- `AudioPlayer::SetVolume(qreal)` 再次将输入限制到 `0.0-1.0`。

即使后续新增调用方绕过 UI 直接调用 Controller 或 AudioPlayer，也不会让 `QAudioSink` 收到超出预期范围的音量值。

### 未初始化播放管线时调用音量接口不 crash

结论：通过静态检查。

依据：

- `MediaPlayController::SetVolume()` 在 `audioplay_ == nullptr` 时只更新 `volume_` 并发出 `VolumeChanged(volume_)`。
- `MediaPlayController::SetMuted()` 在 `audioplay_ == nullptr` 时只更新 `muted_` 并发出 `MuteChanged(muted_)`。
- `MediaPlayController::Volume()` 和 `IsMuted()` 只读取 Controller 自身状态，不依赖播放管线。
- `MainWindow::InitializeVolumeControls()` 在播放管线初始化前调用是安全的。

因此 UI 可以先初始化音量控件，再初始化媒体源。

### 静音状态下拖动音量滑块，取消静音后使用新音量

结论：通过静态检查。

依据：

- `AudioPlayer::SetMuted(true)` 不修改 `m_volume`，只修改 `m_muted`。
- 静音状态下调用 `AudioPlayer::SetVolume()` 会更新 `m_volume`。
- `AudioPlayer::ApplyOutputVolume()` 使用 `m_muted ? 0.0 : m_volume` 计算实际输出。
- 取消静音时 `SetMuted(false)` 会重新调用 `ApplyOutputVolume()`，此时实际输出恢复为当前 `m_volume`。

也就是说静音不是把用户音量改成 0，而是只把实际输出临时置 0。

### 播放中、暂停中、切换文件后音量状态一致

结论：通过静态检查。

依据：

- `MediaPlayController` 持有 `volume_` 和 `muted_`，这是应用层当前音量状态。
- `VolumeUp()` / `VolumeDown()` 统一转发到 `SetVolume()`，不会形成第二套音量状态。
- 新建播放管线后，`InitializeOpenedMediaSource()` 调用：

```cpp
audioplay_->SetVolume(volume_);
audioplay_->SetMuted(muted_);
```

因此切换文件或重建 `AudioPlayer` 后，Controller 保存的音量和静音状态会重新下发到新的音频输出对象。

## 架构边界检查

结论：符合当前架构路线。

检查结果：

- `MainWindow` 没有 include `AudioPlayer`。
- `MainWindow` 没有 include `QAudioSink`。
- `MainWindow` 只调用 `MediaPlayController::SetVolume()`、`SetMuted()`、`Volume()`、`IsMuted()`。
- `AudioPlayer` 的 `QAudioSink` 细节仍封装在基础设施层。
- 没有新增全局变量。

当前分层仍是：

```text
UI 层 MainWindow
    -> 应用层 MediaPlayController
    -> 基础设施层 AudioPlayer
    -> Qt 音频输出 QAudioSink
```

## 静态检查结论

Phase 1.3 已完成当前计划内能力：

- 支持 UI 滑块调节音量。
- 支持键盘上下键调节音量。
- 支持静音和取消静音。
- Controller 暴露统一音量和静音接口。
- Controller 提供 `VolumeChanged` 和 `MuteChanged` 信号。
- 切换媒体源后音量和静音状态保持。
- UI 没有反向依赖基础设施层音频组件。
- 没有新增全局变量。

## 仍需运行时确认的点

本报告只做静态检查，以下现象仍建议由手动测试确认：

- 滑块拖动时实际听感音量是否连续变化。
- 静音后取消静音的实际听感是否恢复为最新滑块音量。
- 播放中切换文件后，新文件音量是否保持一致。
- 暂停状态下调整音量，再恢复播放后音量是否立即生效。
