# Phase1.1 任务8：进度条 Seek 接口报告

## 目标

在 UI 层补齐进度条显示和拖动 Seek 能力，同时保持 `MainWindow` 只与 `MediaPlayController` 交互，不直接依赖基础设施层。

## 实施结果

- `mainwindow.ui` 增加进度条区域：
  - `label_current`：当前播放时间，初始值 `00:00`。
  - `slider_progress`：水平进度条，初始禁用，范围 `0..0`。
  - `label_duration`：总时长，初始值 `00:00`。
- `MainWindow::CreateConnects()` 连接 `DurationChanged`：
  - 设置进度条范围。
  - 根据总时长是否大于 0 控制进度条启用状态。
  - 更新总时长标签。
- `MainWindow::CreateConnects()` 连接 `PositionChanged`：
  - 非拖动状态下更新进度条位置。
  - 非拖动状态下更新当前时间标签。
- 拖动链路：
  - `sliderPressed` 设置 `isSliderDragging_ = true`，暂停自动位置覆盖。
  - `sliderMoved` 只更新当前时间预览，不触发 seek。
  - `sliderReleased` 先恢复 `isSliderDragging_ = false`，再调用 `MediaPlayController::SeekTo(target)`，确保 `SeekTo()` 内同步发出的 `PositionChanged` 不会被 UI 忽略。
- `MainWindow` 构造时先连接 Controller 信号，再调用 `Initialize()`，使 `Initialize()` 成功后发出的 `DurationChanged` / `PositionChanged` 可以被 UI 收到。

## 边界情况

- 总时长为 0 时，进度条保持禁用。
- Stop 后 Controller 发射 `PositionChanged(0.0)`，UI 自然回到 `00:00`。
- 拖动过程中不会高频触发 seek。
- 释放滑块时由 Controller 内部负责 seek 边界 clamp，UI 不重复实现业务规则。
- 暂停状态下拖动 seek 后，UI 可以收到 seek 后的位置更新，不会因为拖动标志仍为 true 而丢掉同步更新。

## 架构边界

- `MainWindow` 只依赖 `MediaPlayController::PositionChanged`、`DurationChanged` 和 `SeekTo()`。
- `MainWindow` 没有新增 `demuxer`、`decoder`、`syncmanager`、`Fileinfo` 依赖。
- 进度条只表达 UI 状态，不反向侵入播放管线内部。

## 静态检查结论

任务 8 的代码链路已按当前架构完成。按照本轮要求，未做编译验证和人工播放验证。
