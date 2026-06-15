# Phase 1.2：文件列表管理验收报告

## 验收结论

Phase 1.2 主线验收通过。

本阶段已完成播放列表数据管理、文件切换、上一项/下一项、播放模式 UI，以及 EOF 后按播放模式处理。最近播放记录功能按当前计划跳过，不纳入本次收口范围。

本次只做静态检查和文档验收，未进行编译验证。

## 功能验收

### 播放列表数据管理

- `PlaylistManager` 已提供 `AddFile()`、`AddFiles()`、`RemoveAt()`、`Clear()`、`CurrentIndex()`、`CurrentItem()`、`SetCurrentIndex()`。
- `AddFile()` 会规范化路径并去重。
- `RemoveAt()` 已覆盖删除当前项、删除当前项之前条目、删除末尾条目、清空后 `currentIndex_ = -1` 等规则。
- `PlaylistManager` 只管理列表数据和选择规则，不创建 `FileMediaSource`，不依赖 FFmpeg 基础设施。

### 文件切换

- `MediaPlayController::SwitchSource()` 继续接收 `std::shared_ptr<IMediaSource>`，没有退回 `QString filePath` 型接口。
- 应用层根据 `PlaylistItem::filePath` 创建 `FileMediaSource`，再传入 Controller。
- 换源流程由 Controller 停止旧管线、释放资源、初始化新媒体源，并根据 `autoStart` 决定是否立即播放。
- 换源失败时 `MainWindow::SwitchToPlaylistIndex()` 会恢复原播放列表索引，避免 UI 选中项与实际播放源不一致。

### UI 绑定

- `mainwindow.ui` 已加入左侧 `listWidget_files` 播放列表区域。
- `MainWindow` 通过 `PlaylistManager::PlaylistChanged` 刷新列表展示。
- 文件添加通过 `QFileDialog::getOpenFileNames()` 获取路径后转发给 `PlaylistManager::AddFiles()`。
- 删除、清空、双击切换、上一项、下一项、播放模式选择都在 `MainWindow` 内做 UI 事件转发。
- `MainWindow` 没有直接 include 或操作 `demuxer`、`decoder`、`SyncQueue`、`AudioPlayer`、`VideoRender`、`syncmanager`。

### 上一项、下一项和播放模式

- `PlaylistManager::MoveNext()` / `MovePrevious()` 已支持 `Sequential`、`ListLoop`、`SingleLoop`、`Random`。
- `pushButton_prev` / `pushButton_next` 已接入上一项和下一项播放。
- `Ctrl+Left` / `Ctrl+Right` 已接入上一项和下一项快捷键。
- `comboBox_mode` 已接入顺序播放、单曲循环、列表循环、随机播放。
- `comboBox_mode::currentIndexChanged` 只调用 `PlaylistManager::SetPlayMode()`。

### EOF 后行为

- `MediaPlayController` 通过 `PlaybackFinished()` 对外报告当前媒体源自然播放结束。
- `MainWindow::HandlePlaybackFinished()` 根据 `PlaylistManager::GetPlayMode()` 分发行为：
  - 顺序播放：保持结束状态，不跳转。
  - 单曲循环：重建当前媒体源并播放。
  - 列表循环：调用 `PlayNext()`，末尾回绕到第一项。
  - 随机播放：调用 `PlayNext()`，由 `PlaylistManager` 随机选择下一项。

## 架构验收

- `MainWindow` 仍然是 UI 交互和应用层协调入口，没有反向侵入基础设施层。
- `MediaPlayController` 只控制当前媒体源和当前播放管线，没有持有播放列表状态。
- `PlaylistManager` 位于领域层，只负责列表数据、当前索引和播放模式规则。
- 文件切换继续通过 `IMediaSource` 抽象完成，后续摄像头或网络流可以复用 Controller 的换源入口。
- 未新增全局变量。
- 未引入最近播放记录、拖拽排序、缩略图、持久化等 Phase 1.2 外扩功能。

## 静态检查

- 已检查 `MainWindow` include：未发现 `demuxer`、`decoder`、`queue` 等基础设施层直接依赖。
- 已检查 `MediaPlayController::SwitchSource()`：参数仍为 `std::shared_ptr<IMediaSource>`。
- 已检查 `PlaylistManager`：未依赖 UI 控件、`FileMediaSource` 或 FFmpeg 组件。
- 已执行 `git diff --check`，无空白错误。

## 未执行项

- 未编译验证。
- 未做自动化测试。
- `CMakeLists.txt.user` 是本地 Qt 配置文件改动，不属于本阶段代码验收内容。

## 后续建议

Phase 1.2 可以收口。下一阶段建议进入 Phase 1.3 音量控制，先完善音量滑块、静音/取消静音和音量状态同步，再考虑持久化。
