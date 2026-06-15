# Phase 1.3 任务 1：音量控制链路梳理报告

## 目标

梳理当前音量控制链路，确认现有 `VolumeUp()` / `VolumeDown()` 是否真正改变音频输出，并明确后续 1.3.2/1.3.3/1.3.4 需要补齐的能力。

本任务只做静态检查，不修改代码，不编译验证。

## 当前调用链路

### UI 层：MainWindow

当前键盘事件在 `MainWindow::keyReleaseEvent()` 中处理：

```cpp
} else if (ev->key() == Qt::Key_Up) {
    VolumeUp();
} else if (ev->key() == Qt::Key_Down) {
    VolumeDown();
}
```

`MainWindow::VolumeUp()` / `VolumeDown()` 只转发给 Controller：

```cpp
void MainWindow::VolumeUp() {
    mediaPlayController_->VolumeUp();
}

void MainWindow::VolumeDown() {
    mediaPlayController_->VolumeDown();
}
```

结论：

- `MainWindow` 当前没有直接依赖 `AudioPlayer`。
- UI 层职责符合架构要求。
- 当前只有键盘入口，没有音量滑块和静音按钮。

### 应用层：MediaPlayController

`MediaPlayController::VolumeUp()` / `VolumeDown()` 当前直接调用 `AudioPlayer`：

```cpp
void MediaPlayController::VolumeUp() {
    audioplay_->VolumeUp();
}

void MediaPlayController::VolumeDown() {
    audioplay_->VolumeDown();
}
```

结论：

- Controller 是 UI 到音频输出层之间的转发入口。
- 当前没有 `SetVolume()` / `Volume()` 查询接口。
- 当前没有 `SetMuted()` / `IsMuted()` 静音接口。
- 当前没有 Controller 级 `VolumeChanged` / `MuteChanged` 信号。
- 当前没有空指针保护，未初始化播放管线时调用 `VolumeUp()` / `VolumeDown()` 存在崩溃风险。
- 当前没有保存 Controller 级音量状态，切换媒体源后无法保证音量保持。

### 基础设施层：AudioPlayer

`AudioPlayer` 内部已有音量成员：

```cpp
qreal m_volume = 1.0;
```

并已有信号：

```cpp
void VolumeChanged(qreal volume);
```

当前 `VolumeUp()` / `VolumeDown()` 实现：

```cpp
void AudioPlayer::VolumeUp() {
    if(m_volume>0.95) {
        m_volume = 1.0;
    }else{
        m_volume+=0.05;
    }
    emit VolumeChanged(m_volume);
}

void AudioPlayer::VolumeDown(){
    if(m_volume>0.05){
        m_volume-=0.05;
    }else{
        m_volume = 0.0;
    }
    emit VolumeChanged(m_volume);
}
```

结论：

- 当前只修改了 `m_volume`。
- 当前没有调用 `QAudioSink::setVolume()`。
- 因此当前音量变化信号会发出，但实际音频输出音量不会随之变化。
- `VolumeChanged(qreal)` 当前没有被 `MediaPlayController` 或 `MainWindow` 连接使用。
- 没有 `SetVolume(qreal)`，音量 clamp 逻辑分散在 `VolumeUp()` / `VolumeDown()` 中。
- 没有 `Volume() const` 查询接口。
- 没有静音状态，也没有 `MuteChanged(bool)`。
- `m_volume` 当前没有线程保护；UI 线程调用音量方法，而 `AudioPlayer` 对象已被移动到音频线程后，后续需要明确线程安全策略。

## 音量单位结论

建议后续统一语义：

- `AudioPlayer` 内部使用 `qreal`，范围 `0.0` 到 `1.0`。
- `MediaPlayController` 对外可以使用 `double`，范围 `0.0` 到 `1.0`。
- `MainWindow` 的音量滑块使用整数 `0` 到 `100`。
- UI 和 Controller 之间转换规则：

```cpp
double volume = sliderValue / 100.0;
int sliderValue = qRound(volume * 100.0);
```

## 当前主要缺口

1. 音量没有真正作用到 `QAudioSink`。
2. 没有统一 `SetVolume()` 入口。
3. 没有音量查询接口。
4. 没有静音/取消静音能力。
5. `AudioPlayer::VolumeChanged` 没有向 Controller/UI 传播。
6. Controller 没有保存音量状态，换源后音量无法保持。
7. Controller 音量接口缺少空指针保护。
8. 音量状态缺少线程安全保护。

## 后续实施建议

### 1.3.2

优先在 `AudioPlayer` 内收敛音量状态：

- 新增 `SetVolume(qreal)`。
- 新增 `Volume() const`。
- 新增 `SetMuted(bool)` / `IsMuted() const`。
- 新增 `MuteChanged(bool)`。
- 新增内部 `ApplyOutputVolume()`，统一调用 `QAudioSink::setVolume()`。
- `VolumeUp()` / `VolumeDown()` 改为调用 `SetVolume()`。
- 用 `QMutex` 或等价方式保护音量和静音状态。

### 1.3.3

在 `MediaPlayController` 增加 UI 可用的统一入口：

- `SetVolume(double)`。
- `Volume() const`。
- `SetMuted(bool)`。
- `IsMuted() const`。
- `VolumeChanged(double)`。
- `MuteChanged(bool)`。
- 初始化或切换媒体源后，将 Controller 保存的音量和静音状态下发到新的 `AudioPlayer`。

### 1.3.4

UI 只绑定 Controller：

- `slider_volume` 范围 `0` 到 `100`。
- `pushButton_mute` 切换静音状态。
- `VolumeChanged` 同步滑块。
- `MuteChanged` 同步按钮。

## 验收结论

1.3.1 完成。

当前音量链路已经从 UI 转发到 `AudioPlayer`，但链路未闭合：音量变化没有作用到真实音频输出。后续应先补齐 `AudioPlayer` 的统一音量和静音能力，再向上暴露 Controller 级接口，最后接 UI。
