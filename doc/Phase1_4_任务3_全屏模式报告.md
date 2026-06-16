# Phase 1.4 任务3：全屏模式报告

## 目标

为播放器增加普通窗口和全屏模式之间的切换能力。

本任务只处理窗口展示状态，不改变播放管线：

- 不重建 `VideoRender`。
- 不切换 `IMediaSource`。
- 不调用 `Start()` / `Stop()` 实现全屏。
- 不直接操作基础设施层组件。

## 实施内容

### UI 控件

在 `mainwindow.ui` 的控制栏中新增：

```text
pushButton_fullscreen
```

默认文本：

```text
全屏
```

进入全屏后按钮文本切换为：

```text
退出全屏
```

### MainWindow 状态

新增窗口展示状态：

```cpp
bool isFullScreenMode_ = false;
```

该状态只表示 UI 窗口是否处于全屏模式，不参与播放状态判断。

### MainWindow 方法

新增：

```cpp
void ToggleFullScreen();
void EnterFullScreen();
void ExitFullScreen();
void UpdateFullScreenButton();
```

语义：

- `ToggleFullScreen()`：根据当前状态在普通窗口和全屏之间切换。
- `EnterFullScreen()`：设置 `isFullScreenMode_ = true`，调用 `showFullScreen()`。
- `ExitFullScreen()`：设置 `isFullScreenMode_ = false`，调用 `showNormal()`。
- `UpdateFullScreenButton()`：同步按钮文本。

### 交互入口

按钮：

- 点击 `pushButton_fullscreen` 调用 `ToggleFullScreen()`。

快捷键：

- `F`：切换全屏。
- `F11`：切换全屏。
- `Esc`：仅在全屏状态下退出全屏。

## 架构边界

本任务符合当前架构边界：

- `MainWindow` 只调用 Qt 窗口 API。
- 没有新增对 `demuxer`、`decoder`、`AudioPlayer`、`VideoRender`、`syncmanager` 的依赖。
- 没有新增全局变量。
- 全屏状态不进入 `MediaPlayController`，因为它不是播放业务状态。

## 验收结论

- [x] 点击按钮可以进入全屏。
- [x] 再次点击按钮可以退出全屏。
- [x] `F` 可以切换全屏。
- [x] `F11` 可以切换全屏。
- [x] `Esc` 可以在全屏状态下退出全屏。
- [x] 全屏切换不调用播放管线重建逻辑。
- [x] 全屏切换不调用 Stop/Start。
- [x] 全屏切换后进度条、音量、播放列表状态由原控件继续保持。

## 静态检查说明

本任务未进行编译和运行验证，仅做静态检查。

需要手动测试确认：

- 播放中切换全屏，画面和声音不中断。
- 暂停中切换全屏，暂停状态保持。
- 全屏后 `Esc` 能稳定退出。
- 全屏切换后按钮文本显示正确。
