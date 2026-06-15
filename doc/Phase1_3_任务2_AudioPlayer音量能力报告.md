# Phase 1.3 任务 2：AudioPlayer 音量能力报告

## 目标

收敛 `AudioPlayer` 内部音量能力，让音频输出层拥有统一的音量设置、查询、静音和实际输出应用入口。

本任务只修改 `AudioPlayer`，不接入 `MediaPlayController` 和 UI。

## 实现内容

### 统一音量入口

新增：

```cpp
void SetVolume(qreal volume);
qreal Volume() const;
```

`SetVolume()` 负责：

- 将输入音量限制在 `0.0` 到 `1.0`。
- 只在音量真实变化时更新状态并发出 `VolumeChanged(qreal)`。
- 调用 `ApplyOutputVolume()`，让音量真实作用到 `QAudioSink`。

`VolumeUp()` / `VolumeDown()` 已改为调用 `SetVolume()`，不再各自维护 clamp 逻辑。

### 静音能力

新增：

```cpp
void SetMuted(bool muted);
bool IsMuted() const;
void MuteChanged(bool muted);
```

静音语义：

- `SetMuted(true)` 不修改用户音量 `m_volume`，只让实际输出音量变为 `0.0`。
- `SetMuted(false)` 恢复到当前 `m_volume`。
- 静音状态真实变化时才发出 `MuteChanged(bool)`。
- 静音状态下调用 `SetVolume()` 会更新用户音量，但实际输出仍保持 `0.0`；取消静音后使用最新用户音量。

### 实际输出应用

新增内部方法：

```cpp
void ApplyOutputVolume();
```

该方法根据当前用户音量和静音状态计算实际输出音量：

```cpp
outputVolume = m_muted ? 0.0 : m_volume;
```

然后调用：

```cpp
m_audioSink->setVolume(outputVolume);
```

同时在 `InitAudioOutput()` 创建新的 `QAudioSink` 后立即调用 `ApplyOutputVolume()`，保证重新初始化音频输出设备时不会丢失当前音量/静音状态。

## 线程安全

新增独立的 `m_volumeMutex`，保护：

- `m_volume`
- `m_muted`

这样音量状态不会和播放暂停、队列等待等已有状态共用同一把锁，避免扩大锁粒度。

## 当前边界

本任务只完成 `AudioPlayer` 层能力。以下内容留到后续任务：

- `MediaPlayController` 级 `SetVolume()` / `Volume()` / `SetMuted()` / `IsMuted()`。
- Controller 级 `VolumeChanged(double)` / `MuteChanged(bool)`。
- 换源后由 Controller 下发音量和静音状态。
- UI 音量滑块和静音按钮。

## 静态验收

- `VolumeUp()` / `VolumeDown()` 已统一调用 `SetVolume()`。
- `SetVolume()` 已做 `0.0` 到 `1.0` clamp。
- `ApplyOutputVolume()` 已调用 `QAudioSink::setVolume()`。
- `SetMuted(true)` 后实际输出为 `0.0`。
- `SetMuted(false)` 后恢复用户音量。
- 音量和静音信号只在状态真实变化时发出。

## 未执行项

- 未编译验证。
- 未进行运行时听感测试。

## 结论

1.3.2 完成。

`AudioPlayer` 已具备完整的音量和静音基础能力。下一步应进入 1.3.3，在 `MediaPlayController` 上暴露统一音量接口和状态信号，并处理换源后的音量状态保持。
