# TODO: Phase 1.1 播放控制

## 目标

完善本地播放器的核心播放控制能力。当前优先级是先把快进、快退、停止和 Seek 做成真实功能，而不是只改变状态或调用空实现。

本阶段继续遵循 `doc/架构指导.md`：

- `MainWindow` 只负责 UI 交互和事件转发。
- `MediaPlayController` 负责播放控制流程。
- `demuxer/decoder/audioplayer/videorender/syncmanager` 保持基础设施职责。
- 不恢复全局变量。
- 不从 `AVFrame::time_base` 推导同步时间。

## 任务 1: 梳理当前播放控制链路

- [x] 检查 `MediaPlayController::Start()`、`Pause()`、`Stop()` 当前行为。
- [x] 检查 `demuxer::Rewind()`、`demuxer::Forward()` 空实现。
- [x] 检查 `syncmanager::Forward()` 是否应该被通用 Seek 时钟重置替代。
- [x] 明确当前队列停止标志、唤醒、清空的调用顺序。
- [x] 明确 decoder 是否已有 flush 能力，没有则计划新增。

产出报告：`doc/Phase1_1_任务1_播放控制链路梳理报告.md`

验收：

- [x] 写清楚当前 Seek 缺失点。
- [x] 确认后续改动不需要让 `MainWindow` 直接依赖基础设施层。

## 任务 2: 为队列补齐 Seek 所需能力

- [ ] 确认 `SyncQueue::Clear()` 会正确释放队列内 AVPacket/AVFrame。
- [ ] 确认清空队列时不会和阻塞中的 Push/Pop 死锁。
- [ ] 必要时增加“清空并唤醒”的封装方法。
- [ ] Seek 前后正确设置 `SetStopFlag()`，避免 demux/decode 线程卡住。

验收：

- [ ] Seek 时四个队列都能被清空。
- [ ] 队列清空不泄漏 AVPacket/AVFrame。
- [ ] 队列清空后可以重新接收新数据。

## 任务 3: demuxer 增加真实 Seek 能力

- [ ] 在 `demuxer` 中保存可 seek 的 `AVFormatContext` 来源。
- [ ] 新增 `SeekTo(double seconds)` 或等价接口。
- [ ] 使用 `av_seek_frame()` 跳转到目标时间。
- [ ] 处理目标时间边界：小于 0 时归零，大于总时长时限制到总时长。
- [ ] Seek 后清理 demuxer 内部当前 PTS 状态。
- [ ] 将 `Rewind()`、`Forward()` 改为调用通用 seek 逻辑。

验收：

- [ ] 左右方向键能触发真实位置跳转。
- [ ] 目标时间不会越界。
- [ ] seek 失败时能返回失败状态，不静默吞掉错误。

## 任务 4: decoder 增加 flush 能力

- [ ] 为音频 decoder 增加 `Flush()`。
- [ ] 为视频 decoder 增加 `Flush()`。
- [ ] 内部调用 `avcodec_flush_buffers()`。
- [ ] 确保硬解路径 flush 后仍能继续输出有效帧。

验收：

- [ ] Seek 后不会继续输出 seek 前缓存帧。
- [ ] 硬解和软解路径都走同一 flush 入口。

## 任务 5: MediaPlayController 编排 Seek 流程

- [ ] 新增 `SeekTo(double seconds)`。
- [ ] 新增 `SeekRelative(double seconds)`。
- [ ] `Rewind()` 调用 `SeekRelative(-10)`。
- [ ] `Forward()` 调用 `SeekRelative(10)`。
- [ ] Seek 流程中暂停播放管线。
- [ ] 清空音频包队列、视频包队列、音频帧队列、视频帧队列。
- [ ] flush 音频 decoder 和视频 decoder。
- [ ] 重置同步时钟到目标时间。
- [ ] 恢复播放状态。

验收：

- [ ] 快进/快退后音视频同步。
- [ ] Seek 后无旧画面闪回。
- [ ] Seek 后无旧音频残留。
- [ ] 暂停状态下 Seek 后仍保持暂停，播放状态下 Seek 后继续播放。

## 任务 6: Stop 行为回到起始位置

- [ ] 明确 Stop 和 Pause 的状态差异。
- [ ] Stop 时停止播放管线。
- [ ] Stop 时清空所有队列。
- [ ] Stop 时 flush decoder。
- [ ] Stop 时同步时钟回到 0。
- [ ] Stop 后再次 Start 从 0 开始播放。

验收：

- [ ] Stop 后画面和音频都停止。
- [ ] Stop 后再次播放从开头开始。
- [ ] Stop 不导致线程无法再次启动。

## 任务 7: 播放位置和总时长信号

- [ ] 从 `Fileinfo` 或媒体源信息中暴露总时长。
- [ ] `MediaPlayController` 增加 `Duration()`。
- [ ] `MediaPlayController` 增加 `Position()`。
- [ ] 增加 `PositionChanged(double seconds)` 信号。
- [ ] 增加 `DurationChanged(double seconds)` 信号。
- [ ] 先用定时器或音频主时钟更新当前位置。

验收：

- [ ] 播放过程中位置持续更新。
- [ ] Seek 后位置立即更新到目标附近。
- [ ] 总时长只由媒体源信息决定，不从 UI 硬编码。

## 任务 8: 为进度条 Seek 做接口准备

- [ ] `MainWindow` 只绑定进度条事件，不直接操作 demuxer。
- [ ] 拖动进度条时调用 `MediaPlayController::SeekTo()`。
- [ ] 拖动过程中避免频繁 seek，可以只在释放时 seek。
- [ ] 播放位置更新时刷新进度条。

验收：

- [ ] 进度条显示不反向驱动播放状态混乱。
- [ ] 用户拖动进度条后能跳到对应位置。
- [ ] 拖动进度条不会破坏音视频同步。

## 任务 9: 播放速度控制预研

- [ ] 确认 Qt 音频输出侧是否需要重采样/变速支持。
- [ ] 确认视频渲染定时器如何适配速度。
- [ ] 确认同步时钟是否以播放速度缩放。
- [ ] 暂不与 Seek 同时实现，避免风险叠加。

验收：

- [ ] 形成实现方案后再进入编码。

## 1.1 完成标准

- [ ] 播放、暂停、停止行为边界清晰。
- [ ] 快进/快退是真实 Seek。
- [ ] Seek 后音视频同步。
- [ ] Seek 后无旧包、旧帧、旧音频残留。
- [ ] 进度信息能用于 UI 进度条。
- [ ] `MainWindow` 没有新增基础设施层依赖。
- [ ] 没有新增全局变量。
