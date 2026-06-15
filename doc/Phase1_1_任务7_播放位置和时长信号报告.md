# Phase1.1 任务7：播放位置和总时长信号报告

## 目标

为 `MediaPlayController` 补齐播放位置和总时长的应用层接口，让 UI 层只通过 Controller 获取播放进度，不直接依赖 `syncmanager`、`demuxer` 或 `Fileinfo` 内部细节。

## 数据来源和单位

- `Fileinfo::duration_` 来源于 `demuxer.cpp:66`，由 `AVFormatContext::duration / 1000000` 得到，单位是秒。
- `syncmanager::mainclock` 在 `syncmanager.h:57` 标注为毫秒。
- `syncmanager::GetClock()` 在 `syncmanager.cpp:22` 返回主时钟，单位保持毫秒。
- `syncmanager::SeekTo(double clockMs)` 在 `syncmanager.cpp:27` 接收毫秒。
- `AudioPlayer` 在 `audioplayer.cpp:196` 用音频 PTS 和音频 time base 计算音频时钟后刷新主时钟。

因此 Controller 对外统一使用秒：

- `Duration()` 返回媒体源总时长，单位秒。
- `Position()` 返回 `syncManager_->GetClock() / 1000.0`，单位秒。
- `PositionChanged(double seconds)` 和 `DurationChanged(double seconds)` 的参数也统一为秒。

## 实施结果

- `MediaPlayController` 增加 `Duration()` 和 `Position()` 查询接口。
- `MediaPlayController` 增加 `PositionChanged(double seconds)` 和 `DurationChanged(double seconds)` 信号。
- `MediaPlayController` 内部增加 200ms 位置更新定时器，播放时周期发射 `PositionChanged`。
- `Start()` 会启动位置定时器，并立即发射当前 `PositionChanged` 和 `DurationChanged`。
- `Stop()` 会停止位置定时器，并立即发射 `PositionChanged(0.0)`。
- `Pause()` 会停止位置定时器，并发射一次当前 `PositionChanged`，使 UI 冻结在暂停位置。
- `SeekTo()` 在同步时钟重置后立即发射 `PositionChanged(Position())`，避免 UI 等待下一个 200ms 周期。
- `Initialize()` 成功后立即发射 `DurationChanged(Duration())` 和 `PositionChanged(Position())`，用于初始化 UI 进度信息。
- `MediaPlayController.h` 直接 include `<QTimer>`，避免依赖其他头文件的间接包含。
- `Start()` / `Pause()` 增加未初始化保护，避免未初始化调用导致空指针访问。

## 定时器间隔选择

位置定时器间隔为 200ms，即 5Hz。进度条不需要逐帧刷新，200ms 可以在 UI 平滑度和 CPU 开销之间取得比较稳妥的平衡。

## 架构边界

- `MainWindow` 不直接访问 `syncmanager`。
- `MainWindow` 不直接访问 `demuxer`。
- `MainWindow` 不直接访问 `Fileinfo`。
- 总时长和播放位置都由 `MediaPlayController` 统一暴露。
- 没有新增全局变量。

## 静态检查结论

任务 7 的代码链路已按当前架构完成。按照本轮要求，未做编译验证和人工播放验证。
