# Phase 1.4 任务4：全屏鼠标隐藏报告

## 目标

在全屏播放时自动隐藏鼠标，减少鼠标指针对画面的遮挡。

本任务只处理 `MainWindow` 窗口交互：

- 不进入 `MediaPlayController`。
- 不修改播放状态。
- 不访问视频帧、渲染器或同步时钟。
- 不新增全局变量。

## 实施内容

### 鼠标隐藏定时器

在 `MainWindow` 中新增：

```cpp
std::unique_ptr<QTimer> cursorHideTimer_;
```

通过 `InitializeCursorHideTimer()` 初始化：

- 间隔：`2000ms`
- 单次触发：`setSingleShot(true)`
- 超时后调用 `HideCursorIfFullScreen()`

### 全屏进入流程

`EnterFullScreen()` 进入全屏后：

1. 调用 `ShowCursor()` 确保鼠标先可见。
2. 调用 `RestartCursorHideTimer()` 启动隐藏计时。

### 全屏退出流程

`ExitFullScreen()` 退出全屏时：

1. 停止 `cursorHideTimer_`。
2. 调用 `ShowCursor()` 恢复普通鼠标。
3. 调用 `showNormal()` 返回普通窗口。

### 鼠标活动处理

新增：

```cpp
void mouseMoveEvent(QMouseEvent *event) override;
bool eventFilter(QObject *watched, QEvent *event) override;
void HandleFullScreenMouseActivity();
```

处理逻辑：

- 全屏状态下检测到鼠标移动：
  - 调用 `ShowCursor()`。
  - 调用 `RestartCursorHideTimer()`。
- 普通窗口状态下不处理自动隐藏。

为了覆盖视频区域和控件区域，`MainWindow` 给以下对象安装事件过滤器：

- `centralwidget`
- `video_widget`
- `widget_func`
- `listWidget_files`

### 鼠标显示和隐藏

新增：

```cpp
void ShowCursor();
void HideCursorIfFullScreen();
void RestartCursorHideTimer();
```

隐藏鼠标使用：

```cpp
setCursor(Qt::BlankCursor);
```

恢复鼠标使用：

```cpp
unsetCursor();
```

并通过 `isCursorHidden_` 防止重复设置。

## 架构边界

本任务符合当前架构路线：

- 鼠标隐藏只属于 UI 展示行为。
- 没有新增对基础设施层组件的依赖。
- 没有修改 `MediaPlayController`。
- 没有新增全局变量。
- 不影响播放、暂停、Seek、换源和播放列表逻辑。

## 验收结论

- [x] 普通窗口模式下鼠标不自动隐藏。
- [x] 全屏模式下启动鼠标隐藏定时器。
- [x] 全屏模式下鼠标静止 `2000ms` 后隐藏。
- [x] 全屏模式下移动鼠标后恢复显示。
- [x] 退出全屏后停止隐藏定时器并恢复鼠标。
- [x] 关闭窗口时停止定时器并恢复鼠标。

## 静态检查说明

本任务未进行编译和运行验证，仅做静态检查。

需要手动测试确认：

- 全屏静止鼠标 2 秒后鼠标隐藏。
- 全屏移动鼠标后鼠标恢复显示。
- 退出全屏后鼠标稳定恢复。
- 关闭程序时不会因为定时器导致卡住。
