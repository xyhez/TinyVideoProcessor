# Phase 1.4 UI 改进验收报告

## 目标

对 Phase 1.4 的 UI 改进进行静态验收，确认新增 UI 能力没有破坏当前分层架构，也没有把播放管线细节拉回 `MainWindow`。

本次验收只做静态检查，不做编译和运行验证。

## 已完成能力

Phase 1.4 当前已完成：

- 进度条和时间显示。
- 音量滑块和静音按钮。
- 播放列表面板。
- 播放速度 UI 选择控件。
- 全屏模式。
- 全屏时鼠标自动隐藏。

## UI 链路验收

### 进度条和时间显示

结论：通过静态检查。

当前链路：

```text
MediaPlayController::PositionChanged(double)
    -> MainWindow 更新 slider_progress 和 label_current

MediaPlayController::DurationChanged(double)
    -> MainWindow 更新 slider_progress 范围和 label_duration

slider_progress::sliderReleased
    -> MediaPlayController::SeekTo(double)
```

`MainWindow` 没有直接读取 `syncmanager`，也没有直接访问 demuxer/decoder 队列。

### 音量 UI

结论：通过静态检查。

当前链路：

```text
slider_volume::valueChanged
    -> MediaPlayController::SetVolume(double)

pushButton_mute::clicked
    -> MediaPlayController::SetMuted(bool)

MediaPlayController::VolumeChanged(double)
    -> MainWindow 同步 slider_volume

MediaPlayController::MuteChanged(bool)
    -> MainWindow 同步 pushButton_mute
```

`MainWindow` 没有直接 include 或操作 `AudioPlayer` / `QAudioSink`。

### 播放速度 UI

结论：通过静态检查。

当前链路：

```text
comboBox_speed::currentIndexChanged
    -> MediaPlayController::SetPlaybackSpeed(double)

MediaPlayController::PlaybackSpeedChanged(double)
    -> MainWindow 同步 comboBox_speed
```

快捷键 `1/2/3/4` 仍然调用 `MediaPlayController::SetPlaybackSpeed()`，并通过 Controller 信号同步 UI。

`MainWindow` 没有直接操作 `AudioPlayer` 或 `VideoRender`。

### 播放列表 UI

结论：通过静态检查。

当前链路：

```text
listWidget_files / prev / next / comboBox_mode
    -> PlaylistManager
    -> MainWindow 根据 PlaylistItem 构造 FileMediaSource
    -> MediaPlayController::SwitchSource(std::shared_ptr<IMediaSource>, ...)
```

当前换源仍通过 `IMediaSource` 抽象进入 `MediaPlayController`，没有新增 `QString` 路径型换源接口。

需要注意：`MainWindow` 当前仍 include `domain/FileMediaSource.h` 并负责把播放列表条目转换成文件媒体源。这属于既有实现方式，不是本次 1.4 UI 改进新增的问题。它没有越过 Controller 直接触碰基础设施层播放组件。

### 全屏模式

结论：通过静态检查。

当前链路：

```text
pushButton_fullscreen / F / F11 / Esc
    -> MainWindow::ToggleFullScreen()
    -> showFullScreen() / showNormal()
```

全屏切换只调用 Qt 窗口 API：

- `showFullScreen()`
- `showNormal()`

没有重建 `VideoRender`，没有切换 `IMediaSource`，没有调用 `Start()` / `Stop()` 实现全屏。

### 全屏鼠标自动隐藏

结论：通过静态检查。

当前逻辑：

- `cursorHideTimer_` 只存在于 `MainWindow`。
- 进入全屏后启动 2000ms 单次定时器。
- 定时器超时后，如果仍处于全屏状态，则设置 `Qt::BlankCursor`。
- 鼠标移动后恢复鼠标并重启定时器。
- 退出全屏或关闭窗口时停止定时器并恢复鼠标。

该逻辑没有进入 `MediaPlayController`，也不依赖视频帧、渲染器或同步时钟。

## 架构边界验收

### MainWindow 依赖

结论：通过静态检查。

`MainWindow` 当前直接依赖：

- Qt UI 类型。
- `MediaPlayController`。
- `PlaylistManager`。
- `FileMediaSource`。

没有新增以下基础设施层依赖：

- `demuxer`
- `decoder`
- `AudioPlayer`
- `QAudioSink`
- `VideoRender`
- `syncmanager`
- `SyncQueue`

### 全局状态

结论：通过静态检查。

本阶段新增状态均为 `MainWindow` 成员：

- `isFullScreenMode_`
- `isCursorHidden_`
- `cursorHideTimer_`

没有新增全局变量。

## 验收结论

- [x] 进度条只依赖 Controller 的位置、时长和 Seek 接口。
- [x] 音量 UI 只依赖 Controller 的音量和静音接口。
- [x] 播放速度 UI 只依赖 Controller 的播放速度接口。
- [x] 播放列表切换仍通过 `IMediaSource` 抽象进入 Controller。
- [x] 全屏和鼠标隐藏不影响播放管线。
- [x] `MainWindow` 没有新增基础设施层播放组件依赖。
- [x] 没有新增全局变量。

## 仍需手动验证

本报告不替代运行测试，建议手动确认：

- 播放、暂停、Seek、音量、播放速度、播放列表操作均可用。
- 快捷键和 UI 控件状态一致。
- 播放中切换全屏不中断。
- 全屏 2 秒后鼠标隐藏，移动鼠标后恢复。
- 关闭程序时不会因为 UI 定时器或播放管线状态卡住。
