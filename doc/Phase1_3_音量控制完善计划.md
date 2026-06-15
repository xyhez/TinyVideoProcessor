# Phase 1.3：音量控制完善计划

## 目标

在 Phase 1.1 播放控制和 Phase 1.2 文件列表稳定之后，完善播放器音量控制能力。

本阶段目标是把当前“键盘上下键增减音量”扩展为完整、可显示、可同步的音量控制链路：

- 支持设置指定音量。
- 支持查询当前音量。
- 支持静音和取消静音。
- 支持音量变化信号。
- 支持 UI 音量滑块和静音按钮。

本阶段继续遵循 `doc/架构指导.md`：

- `MainWindow` 只负责 UI 交互和事件转发。
- `MediaPlayController` 对外提供统一音量控制入口。
- `AudioPlayer` 负责实际音量应用。
- UI 不直接依赖 `AudioPlayer` 或 `QAudioSink`。
- 不新增全局变量。
- 暂不做音量持久化，避免提前引入配置系统。

## 当前基础

- `MainWindow::VolumeUp()` / `VolumeDown()` 已转发到 `MediaPlayController`。
- `MediaPlayController::VolumeUp()` / `VolumeDown()` 已转发到 `AudioPlayer`。
- `AudioPlayer` 内部已有 `m_volume`，范围约为 `0.0` 到 `1.0`。
- `AudioPlayer` 已声明并发出 `VolumeChanged(qreal volume)`。

当前不足：

- `AudioPlayer::VolumeUp()` / `VolumeDown()` 只修改 `m_volume`，需要确认是否真正调用 `QAudioSink::setVolume()`。
- 没有 `SetVolume()` 统一入口。
- 没有 `Volume()` 查询接口。
- 没有静音状态。
- `MediaPlayController` 没有对外暴露音量值和静音信号。
- UI 没有音量滑块和静音按钮。

## 架构设计

### AudioPlayer

`AudioPlayer` 负责音频输出层的真实音量应用，建议收敛为以下接口：

```cpp
void SetVolume(qreal volume);
qreal Volume() const;
void VolumeUp();
void VolumeDown();
void SetMuted(bool muted);
bool IsMuted() const;
```

建议语义：

- `volume` 使用 `0.0` 到 `1.0`。
- `SetVolume()` 统一做 clamp。
- `VolumeUp()` / `VolumeDown()` 只调用 `SetVolume()`。
- `SetMuted(true)` 不丢失原音量值，只让实际输出为 `0.0`。
- 取消静音后恢复静音前的音量。
- `VolumeChanged(qreal)` 表示用户音量值变化。
- `MuteChanged(bool)` 表示静音状态变化。

实际输出音量建议通过内部方法统一应用：

```cpp
void ApplyOutputVolume();
```

`ApplyOutputVolume()` 根据 `m_muted` 决定给 `QAudioSink` 设置 `0.0` 还是 `m_volume`。

### MediaPlayController

`MediaPlayController` 是 UI 可见的统一入口，不让 `MainWindow` 直接接触 `AudioPlayer`。

建议新增：

```cpp
void SetVolume(double volume);
double Volume() const;
void SetMuted(bool muted);
bool IsMuted() const;

signals:
    void VolumeChanged(double volume);
    void MuteChanged(bool muted);
```

已有 `VolumeUp()` / `VolumeDown()` 继续保留，但内部转发到 `AudioPlayer` 的统一音量逻辑。

换源时，Controller 应保留当前音量和静音状态，并在新 `AudioPlayer` 初始化后下发，避免切换文件后音量重置。

### MainWindow

`MainWindow` 只做 UI 绑定：

- 音量滑块变化 -> `MediaPlayController::SetVolume()`。
- 静音按钮点击 -> `MediaPlayController::SetMuted()`。
- `VolumeChanged` -> 同步滑块位置。
- `MuteChanged` -> 同步静音按钮状态。
- 上下键继续调用 `VolumeUp()` / `VolumeDown()`。

## 任务拆分

### 1.3.1 梳理当前音量链路

**目标**：先确认现有实现，不直接改代码。

- [ ] 检查 `MainWindow::VolumeUp()` / `VolumeDown()`。
- [ ] 检查 `MediaPlayController::VolumeUp()` / `VolumeDown()`。
- [ ] 检查 `AudioPlayer::VolumeUp()` / `VolumeDown()` 是否真正作用到 `QAudioSink`。
- [ ] 检查 `AudioPlayer::VolumeChanged(qreal)` 是否已被 Controller 或 UI 使用。
- [ ] 明确当前音量单位：UI 用 0-100，内部用 0.0-1.0。

验收：

- [ ] 写清楚当前缺口。
- [ ] 后续改动不需要 `MainWindow` 直接依赖 `AudioPlayer`。

### 1.3.2 收敛 AudioPlayer 音量能力

**目标**：让音频输出层拥有完整、单一的音量状态入口。

- [ ] 新增 `SetVolume(qreal volume)`。
- [ ] 新增 `qreal Volume() const`。
- [ ] `VolumeUp()` / `VolumeDown()` 改为调用 `SetVolume()`。
- [ ] `SetVolume()` 做 `0.0` 到 `1.0` clamp。
- [ ] `SetVolume()` 调用 `QAudioSink::setVolume()` 或统一的 `ApplyOutputVolume()`。
- [ ] 新增 `SetMuted(bool muted)`。
- [ ] 新增 `bool IsMuted() const`。
- [ ] 新增 `MuteChanged(bool muted)` 信号。
- [ ] 静音不丢失用户音量，取消静音恢复原音量。
- [ ] 保证音频线程和 UI 线程访问音量状态时线程安全。

验收：

- [ ] `VolumeUp()` / `VolumeDown()` 真实改变输出音量。
- [ ] `SetVolume(0.0)` 静音输出但不一定改变静音状态。
- [ ] `SetMuted(true)` 后实际输出为 0。
- [ ] `SetMuted(false)` 后恢复静音前音量。
- [ ] 音量和静音信号只在状态变化时发出。

### 1.3.3 MediaPlayController 增加统一音量接口

**目标**：应用层统一对外暴露音量控制，不让 UI 接触基础设施层。

- [ ] 新增 `SetVolume(double volume)`。
- [ ] 新增 `double Volume() const`。
- [ ] 新增 `SetMuted(bool muted)`。
- [ ] 新增 `bool IsMuted() const`。
- [ ] 新增 `VolumeChanged(double volume)` 信号。
- [ ] 新增 `MuteChanged(bool muted)` 信号。
- [ ] 连接 `AudioPlayer::VolumeChanged` 到 Controller 信号。
- [ ] 连接 `AudioPlayer::MuteChanged` 到 Controller 信号。
- [ ] `VolumeUp()` / `VolumeDown()` 继续作为快捷入口保留。
- [ ] Controller 持有当前音量和静音状态，换源后下发到新的 `AudioPlayer`。

验收：

- [ ] UI 只依赖 Controller 音量接口。
- [ ] 切换文件后音量状态保持。
- [ ] Controller 不新增 UI 控件依赖。

### 1.3.4 UI 接入音量滑块和静音按钮

**目标**：让用户可以通过界面设置音量和静音状态。

- [ ] 在 `mainwindow.ui` 增加音量滑块，建议命名为 `slider_volume`。
- [ ] 音量滑块范围使用 `0` 到 `100`。
- [ ] 在 `mainwindow.ui` 增加静音按钮，建议命名为 `pushButton_mute`。
- [ ] `slider_volume::valueChanged` 转换为 `0.0` 到 `1.0` 后调用 `MediaPlayController::SetVolume()`。
- [ ] `pushButton_mute::clicked` 调用 `MediaPlayController::SetMuted()`。
- [ ] `VolumeChanged` 同步滑块位置。
- [ ] `MuteChanged` 同步按钮状态。
- [ ] 上下键调音量后 UI 滑块同步更新。

验收：

- [ ] 拖动滑块后实际音量变化。
- [ ] 静音按钮能切换静音状态。
- [ ] 取消静音后恢复原音量。
- [ ] 键盘上下键和滑块显示保持一致。
- [ ] `MainWindow` 不直接 include 或操作 `AudioPlayer`。

### 1.3.5 边界检查和报告

**目标**：收口本阶段，确认没有破坏播放链路和架构边界。

- [ ] 音量范围小于 0 时归零。
- [ ] 音量范围大于 1 时归一。
- [ ] 未初始化播放管线时调用音量接口不 crash。
- [ ] 静音状态下拖动音量滑块，取消静音后使用新音量。
- [ ] 播放中、暂停中、切换文件后音量状态一致。
- [ ] 形成实现报告。

验收：

- [ ] 音量 UI 状态与实际输出一致。
- [ ] 静音 UI 状态与实际输出一致。
- [ ] 切换媒体源后音量和静音状态保持。
- [ ] `MainWindow` 仍不直接依赖基础设施层播放组件。

## 本阶段不做

- 不做音量持久化。
- 不做配置文件或 `QSettings`。
- 不做音频设备选择。
- 不做声道平衡。
- 不做均衡器。

这些能力后续可以放到配置管理或音频功能增强阶段。

## 完成标准

- 音量支持 UI 滑块控制。
- 音量支持键盘增减控制。
- 支持静音和取消静音。
- 音量状态和静音状态有 Controller 级信号。
- 切换文件后音量和静音状态保持。
- UI 不直接依赖 `AudioPlayer` 或 `QAudioSink`。
- 不新增全局变量。
