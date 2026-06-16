# Phase 1.4 UI 改进任务报告

## 目标

Phase 1.4 的目标不是重新设计播放器界面，而是在已经完成的播放控制、播放列表和音量控制基础上，补齐本地播放器常见 UI 能力，并对已有 UI 进行收口验收。

本阶段继续遵循 `doc/架构指导.md`：

- `MainWindow` 只负责 UI 交互、控件状态同步和事件转发。
- 播放状态、播放速度、Seek、音量等能力继续由 `MediaPlayController` 提供。
- 播放列表规则继续由 `PlaylistManager` 提供。
- UI 不直接依赖 `demuxer`、`decoder`、`AudioPlayer`、`QAudioSink`、`VideoRender`、`syncmanager` 等基础设施细节。
- 不新增全局变量。
- 不为了 UI 功能绕过 `IMediaSource` 和 Controller 边界。

## 当前基础

虽然 `Phase1_完善功能计划.md` 中 1.4 原始清单仍显示未完成，但其中多项已经在 1.1、1.2、1.3 中完成：

- 进度条：已通过 `slider_progress` 接入 `PositionChanged` / `DurationChanged` / `SeekTo()`。
- 当前时间/总时长：已通过 `label_current` / `label_duration` 显示。
- 音量滑块：已通过 `slider_volume` 接入 `MediaPlayController::SetVolume()`。
- 静音按钮：已通过 `pushButton_mute` 接入 `MediaPlayController::SetMuted()`。
- 播放列表面板：已通过 `listWidget_files` 展示播放列表。
- 播放模式选择：已通过 `comboBox_mode` 接入 `PlaylistManager::SetPlayMode()`。

当前仍缺少或需要收口的 UI 能力：

- 播放速度 UI 选择控件。
- 全屏模式。
- 全屏时鼠标自动隐藏。
- Phase 1 UI 总体验收。

## 设计边界

### MainWindow 可以做

- 初始化 UI 控件。
- 连接 UI 信号到 `MediaPlayController` 或 `PlaylistManager`。
- 根据 Controller/PlaylistManager 信号更新控件状态。
- 管理窗口显示状态，例如普通窗口和全屏切换。
- 在全屏状态下处理鼠标隐藏定时器。

### MainWindow 不应该做

- 不直接访问解封装、解码、音频输出、视频渲染、同步时钟。
- 不根据 FFmpeg 内部状态计算播放位置或总时长。
- 不直接修改播放队列或帧队列。
- 不创建新的播放管线组件。
- 不保存播放业务状态的第二份副本。

## 任务拆分

### 1.4.1 同步 Phase 1 文档状态

**目标**：先把文档状态与当前代码进度对齐，避免后续重复规划已经完成的 UI 功能。

任务：

- [x] 更新 `doc/Phase1_完善功能计划.md` 中 1.1 已完成项。
- [x] 更新 1.2 文件列表已完成项，并保留“最近播放记录跳过”的说明。
- [x] 更新 1.3 音量控制已完成项，并保留“音量记忆暂不做”的说明。
- [x] 更新 1.4 UI 改进清单，把已完成的进度条、时间显示、音量滑块、播放列表面板标记为完成。
- [x] 明确 1.4 剩余任务只聚焦播放速度 UI、全屏模式和鼠标自动隐藏。

验收：

- [x] Phase 1 文档不再与当前代码进度冲突。
- [x] 后续任务不会重复实现已经完成的功能。

### 1.4.2 播放速度 UI 选择控件

**目标**：把当前仅靠快捷键 `1/2/3/4` 的播放速度控制暴露到 UI。

建议方案：

- 在 `mainwindow.ui` 中增加播放速度选择控件，建议命名为 `comboBox_speed`。
- 提供固定档位：
  - `0.5x`
  - `1.0x`
  - `1.5x`
  - `2.0x`
- 初始化时根据 `MediaPlayController::PlaybackSpeed()` 设置当前选中项。
- 用户切换选项时调用 `MediaPlayController::SetPlaybackSpeed(double)`。
- 监听 `MediaPlayController::PlaybackSpeedChanged(double)`，同步 `comboBox_speed` 当前项。
- 快捷键 `1/2/3/4` 保留，快捷键切换后 UI 也要同步。

架构要求：

- `MainWindow` 不直接操作 `AudioPlayer` 或 `VideoRender`。
- `MainWindow` 不保存独立播放速度状态，只从 Controller 读写。
- 播放速度仍由 Controller 下发给音频和视频组件。

验收：

- [x] UI 可以切换 0.5x / 1.0x / 1.5x / 2.0x。
- [x] 快捷键切换速度后，UI 选择项同步变化。
- [x] 切换文件后播放速度保持当前 Controller 状态。
- [x] 无效速度不会通过 UI 输入进入 Controller。

### 1.4.3 全屏模式

**目标**：支持播放器窗口在普通模式和全屏模式之间切换。

建议方案：

- 增加全屏按钮，建议命名为 `pushButton_fullscreen`。
- 增加快捷键：
  - `F` 或 `F11`：进入/退出全屏。
  - `Esc`：全屏状态下退出全屏。
- `MainWindow` 内部保存窗口显示状态，例如 `bool isFullScreenMode_`。
- 切换逻辑只调用 Qt 窗口 API：
  - `showFullScreen()`
  - `showNormal()`
- 全屏状态下，播放列表面板是否隐藏应单独决策。第一版建议不隐藏结构，只先保证窗口级全屏稳定。

架构要求：

- 全屏是 UI 展示状态，不应影响播放管线。
- 不重建 `VideoRender`。
- 不切换 `IMediaSource`。
- 不调用 Stop/Start 来实现全屏。

验收：

- [x] 点击按钮可以进入全屏。
- [x] 再次点击可以退出全屏。
- [x] `F` 或 `F11` 可以切换全屏。
- [x] `Esc` 可以退出全屏。
- [x] 全屏切换过程中播放不中断。
- [x] 全屏切换后进度条、音量、播放列表状态保持。

### 1.4.4 全屏时鼠标自动隐藏

**目标**：在全屏播放时减少鼠标遮挡视频画面。

建议方案：

- 在 `MainWindow` 中增加一个 `QTimer`，例如 `cursorHideTimer_`。
- 仅在全屏状态下启用该定时器。
- 鼠标移动时：
  - 显示鼠标。
  - 重启隐藏定时器。
- 定时器超时后：
  - 如果仍处于全屏状态，则设置空白鼠标或隐藏鼠标。
- 退出全屏时：
  - 停止定时器。
  - 恢复普通鼠标。

建议时间：

- 鼠标静止 `2000ms` 后隐藏。

架构要求：

- 该能力只属于窗口交互，不应进入 `MediaPlayController`。
- 不影响播放状态。
- 不依赖视频帧、渲染器或同步时钟。

验收：

- [ ] 普通窗口模式下鼠标不自动隐藏。
- [ ] 全屏模式下鼠标静止一段时间后隐藏。
- [ ] 全屏模式下移动鼠标后鼠标恢复显示。
- [ ] 退出全屏后鼠标恢复显示。
- [ ] 关闭窗口时定时器安全释放。

### 1.4.5 UI 总体验收

**目标**：确认 Phase 1 的 UI 能力没有破坏架构边界，也没有引入重复状态。

检查项：

- [ ] 进度条只依赖 `PositionChanged` / `DurationChanged` / `SeekTo()`。
- [ ] 音量 UI 只依赖 `SetVolume()` / `SetMuted()` / `VolumeChanged` / `MuteChanged`。
- [ ] 播放速度 UI 只依赖 `SetPlaybackSpeed()` / `PlaybackSpeedChanged`。
- [ ] 播放列表 UI 只依赖 `PlaylistManager` 和 `MediaPlayController::SwitchSource()`。
- [ ] 全屏和鼠标隐藏不影响播放管线。
- [ ] `MainWindow` 没有新增基础设施层 include。
- [ ] 没有新增全局变量。

验收：

- [ ] 播放、暂停、停止、Seek、播放速度、音量、播放列表功能都可以从 UI 操作。
- [ ] 快捷键和 UI 控件状态保持一致。
- [ ] 普通窗口和全屏之间切换稳定。
- [ ] 关闭程序时不因 UI 定时器或播放管线状态卡住。
- [ ] 形成 Phase 1.4 UI 改进验收报告。

## 本阶段不做

- 不重做整体视觉设计。
- 不引入复杂皮肤系统。
- 不做最近播放记录。
- 不做播放配置持久化。
- 不做摄像头或网络流 UI。
- 不引入 `MediaPipeline`，该能力仍留到后续多源/多路阶段。
- 不让 UI 直接控制基础设施层组件。

## 推荐推进顺序

1. 先同步 `Phase1_完善功能计划.md` 的完成状态。
2. 实现播放速度 UI 选择控件。
3. 实现全屏切换。
4. 实现全屏鼠标自动隐藏。
5. 做 Phase 1.4 UI 总体验收报告。

## 完成标准

- 已完成 UI 功能在文档中准确标记。
- 播放速度可以通过 UI 和快捷键控制，二者状态一致。
- 全屏模式可进入、退出，播放不中断。
- 全屏时鼠标可以自动隐藏并在移动后恢复。
- `MainWindow` 仍只做 UI 交互和事件转发。
- 没有新增基础设施层反向依赖。
- 没有新增全局变量。
