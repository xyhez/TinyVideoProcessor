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

- [x] 确认 `SyncQueue::Clear()` 会正确释放队列内 AVPacket/AVFrame。
- [x] 确认清空队列时不会和阻塞中的 Push/Pop 死锁。
- [x] 必要时增加“清空并唤醒”的封装方法。
- [x] Seek 前后正确设置 `SetStopFlag()`，避免 demux/decode 线程卡住。

产出报告：`doc/Phase1_1_任务2_队列Seek能力报告.md`

验收：

- [x] Seek 时四个队列都能被清空。
- [x] 队列清空不泄漏 AVPacket/AVFrame。
- [x] 队列清空后可以重新接收新数据。

## 任务 3: demuxer 增加真实 Seek 能力

- [x] 在 `demuxer` 中保存可 seek 的 `AVFormatContext` 来源。
- [x] 新增 `SeekTo(double seconds)` 或等价接口。
- [x] 使用 `av_seek_frame()` 跳转到目标时间。
- [x] 处理目标时间边界：小于 0 时归零，大于总时长时限制到总时长。
- [x] Seek 后清理 demuxer 内部当前 PTS 状态。
- [x] 将 `Rewind()`、`Forward()` 改为调用通用 seek 逻辑。

产出报告：`doc/Phase1_1_任务3_demuxer真实Seek报告.md`

验收：

- [x] 左右方向键能触发真实位置跳转。
- [x] 目标时间不会越界。
- [x] seek 失败时能返回失败状态，不静默吞掉错误。

说明：任务 3 只完成 demuxer 层 seek 原语，旧包/旧帧清理、decoder flush、同步时钟重置和播放状态恢复等待任务 4/5 编排。

## 任务 4: decoder 增加 flush 能力

- [x] 为音频 decoder 增加 `Flush()`。
- [x] 为视频 decoder 增加 `Flush()`。
- [x] 内部调用 `avcodec_flush_buffers()`。
- [x] 确保硬解路径 flush 后仍能继续输出有效帧。

产出报告：`doc/Phase1_1_任务4_decoderFlush报告.md`

验收：

- [x] Seek 后不会继续输出 seek 前缓存帧。
- [x] 硬解和软解路径都走同一 flush 入口。

说明：任务 4 只提供 decoder 层 flush 能力，实际 Seek 时调用该能力由任务 5 的 `MediaPlayController` 编排完成。

## 任务 5: MediaPlayController 编排 Seek 流程

- [x] 新增 `SeekTo(double seconds)`。
- [x] 新增 `SeekRelative(double seconds)`。
- [x] `Rewind()` 调用 `SeekRelative(-10)`。
- [x] `Forward()` 调用 `SeekRelative(10)`。
- [x] Seek 流程中暂停播放管线。
- [x] 清空音频包队列、视频包队列、音频帧队列、视频帧队列。
- [x] flush 音频 decoder 和视频 decoder。
- [x] 重置同步时钟到目标时间。
- [x] 恢复播放状态。

验收：

- [ ] 快进/快退后音视频同步。
- [ ] Seek 后无旧画面闪回。
- [ ] Seek 后无旧音频残留。
- [ ] 暂停状态下 Seek 后仍保持暂停，播放状态下 Seek 后继续播放。

## 任务 6: Stop 行为回到起始位置

- [x] 明确 Stop 和 Pause 的状态差异。
- [x] Stop 时停止播放管线。
- [x] Stop 时清空所有队列。
- [x] Stop 时 flush decoder。
- [x] Stop 时同步时钟回到 0。
- [x] Stop 后再次 Start 从 0 开始播放。

验收：

- [ ] Stop 后画面和音频都停止。
- [ ] Stop 后再次播放从开头开始。
- [ ] Stop 不导致线程无法再次启动。

## 任务 7: 播放位置和总时长信号

> 拆解为 7 个子步骤，每步编译验证后再做下一步。

---

### 7.1 确认数据来源与单位（只读调研，不写代码）

**目标**：搞清楚两个数据的来源和单位，避免后续接口设计出错。

- [ ] 确认 `Fileinfo::duration_` 赋值来源：`demuxer.cpp:54` 处 `ic->duration/1000000`。`AVFormatContext::duration` 单位为微秒，除以 1,000,000 后单位为**秒**。`Fileinfo::duration_` 类型为 `int`。
- [ ] 确认 `syncmanager::GetClock()` 返回值单位：`mainclock` 成员注释写"单位毫秒(ms)"，`RefreshClock()` 调用处（`audioplayer.cpp:196`）传入 `tmp->pts * g_videoinfo.A_timeBase * 1000`，即毫秒。
- [ ] 确认 `syncmanager::SeekTo()` 参数单位：毫秒。
- [ ] **结论**：`Duration()` 对外返回秒（`int`→`double`），`Position()` 从 `syncmanager::GetClock()` 取毫秒 ÷ 1000 得到秒。Controller 接口统一用秒，UI 自行转换。

验收：
- [ ] 单位结论有代码行号佐证，不靠猜测。

---

### 7.2 增加 `Duration()` 和 `Position()` 查询接口

**目标**：在 `MediaPlayController` 上增加两个 const 查询方法。

- [ ] `double Duration() const`：`return mediaSource_ ? static_cast<double>(mediaSource_->GetSourceInfo().duration_) : 0.0;`
- [ ] `double Position() const`：`return syncManager_ ? syncManager_->GetClock() / 1000.0 : 0.0;`
- [ ] 头文件 `MediaPlayController.h` 增加两个方法声明。
- [ ] 编译验证。

验收：
- [ ] `Duration()` 在 Initialize 成功后返回 >0 的秒数。
- [ ] `Position()` 在播放中返回递增的秒数。
- [ ] 未初始化时两个方法都返回 0.0，不 crash。

---

### 7.3 增加 `PositionChanged` / `DurationChanged` 信号

**目标**：声明两个信号，让 UI 层可以绑定。

- [ ] `MediaPlayController.h` 增加 `void PositionChanged(double seconds);`
- [ ] `MediaPlayController.h` 增加 `void DurationChanged(double seconds);`
- [ ] 编译验证（信号只声明不定义，moc 自动生成实现）。

验收：
- [ ] 编译通过，无 moc 相关链接错误。

---

### 7.4 增加位置更新定时器

**目标**：Controller 内部用 QTimer 周期发射 `PositionChanged`，MainWindow 无需主动轮询。

- [ ] 在 `MediaPlayController` 中添加 `QTimer* positionTimer_` 成员。
- [ ] 在 `Initialize()` 中创建定时器：`positionTimer_ = new QTimer(this);`
- [ ] 设置间隔 200ms（兼顾 UI 平滑与 CPU 开销）。
- [ ] `connect(positionTimer_, &QTimer::timeout, this, [this]() { emit PositionChanged(Position()); });`
- [ ] 在 `Start()` 中 `positionTimer_->start();`
- [ ] 在 `Stop()` 中 `positionTimer_->stop();`
- [ ] 在 `~MediaPlayController()` 或 `ReleaseResources()` 中停止定时器。
- [ ] `PositionChanged` 去重：仅在值与上次不同时才 emit（可选优化，初版可跳过）。
- [ ] 编译验证。

验收：
- [ ] 播放时 `PositionChanged` 持续发射，间隔约 200ms。
- [ ] Stop 后 `PositionChanged` 停止发射。
- [ ] 关闭窗口时定时器随 Controller 析构安全停止。

---

### 7.5 在状态切换点发射即时更新

**目标**：Start/Stop/Seek 操作后 Position 不等待下一个定时器周期，立即更新。

- [ ] `Stop()`：在 `SetState(Stopped)` 之前 emit `PositionChanged(0.0)`。
- [ ] `Start()`：在 `SetState(Playing)` 之后 emit `PositionChanged(Position())`。
- [ ] `SeekTo()`：在 `syncManager_->SeekTo()` 之后 emit `PositionChanged(targetSeconds)`。
- [ ] `Initialize()` 成功后：emit `DurationChanged(Duration())`。
- [ ] `Start()` 中如果首次播放，也 emit `DurationChanged(Duration())`（防止 Initialize 时 MediaSource 尚未 Open 导致 duration_=0 的情况；当前 Initialize 里已 Open，但留防御）。
- [ ] 编译验证。

验收：
- [ ] Stop 后 `PositionChanged` 发射值为 0.0。
- [ ] Seek 后 `PositionChanged` 发射值为目标秒数（不等到 200ms 定时器）。
- [ ] 初始化成功后 `DurationChanged` 发射总时长。

---

### 7.6 暂停时不推进位置

**目标**：Pause 状态 Position 冻结，不继续增长。

- [ ] `Pause()`：`positionTimer_->stop();`，然后 emit `PositionChanged(Position())`（冻结当前值）。
- [ ] `Start()`：已经在 7.4 中处理了 `positionTimer_->start();`，覆盖从 Pause 恢复的场景。
- [ ] 编译验证。
- [ ] 手动测试：播放 → 暂停 → 等 3 秒 → PositionChanged 不再发射新值。

验收：
- [ ] 暂停期间定时器停止，Position 冻结。
- [ ] 恢复播放后 Position 从冻结值继续增长。
- [ ] 暂停时 emit 了一次当前 Position（UI 可以据此显示精确的暂停位置）。

---

### 7.7 边界情况防御

**目标**：处理异常状态，不 crash、不发射荒谬值。

- [ ] `Duration()` null guard（已在 7.2 中处理）。
- [ ] `Position()` null guard（已在 7.2 中处理）。
- [ ] 总时长为 0（直播流或未 Open）：`Duration()` 返回 0.0，UI 据此隐藏进度条或显示不确定状态。
- [ ] `positionTimer_` 回调中 double-check `syncManager_` 非空（极端情况：定时器触发时 Controller 正在析构）。
- [ ] `SeekTo()` 空值检查维持现有逻辑不变（已有 null guard）。
- [ ] 编译验证。

验收：
- [ ] 未 Initialize 时调用 `Duration()` / `Position()` 不 crash。
- [ ] 总时长为 0 时 UI 不会除零。
- [ ] 析构期间定时器不会访问已释放的 syncManager_（通过 `QTimer` 绑定 parent 自动管理生命周期）。

---

### 7.8 形成报告

- [ ] 汇总以上 7 个子步骤的实施结果。
- [ ] 记录 `Duration()` / `Position()` 的精确语义和单位。
- [ ] 记录定时器间隔选择理由（200ms = 5Hz，小于人眼感知延迟 100ms 的两倍，适合进度条刷新）。
- [ ] 记录边界情况处理方式。
- [ ] 形成报告：`doc/Phase1_1_任务7_播放位置和时长信号报告.md`

---

验收：

- [ ] 播放过程中位置持续更新。
- [ ] Seek 后位置立即更新到目标附近。
- [ ] 总时长只由媒体源信息决定，不从 UI 硬编码。
- [ ] Stop 后位置显示为 0。
- [ ] 暂停时位置不继续前进。
- [ ] `MainWindow` 不直接依赖 `syncmanager`、`demuxer` 或 `Fileinfo` 内部细节。

## 任务 8: 为进度条 Seek 做接口准备

- [ ] 明确进度条显示单位：UI 使用毫秒或百分比，Controller 接口继续使用秒。
- [ ] `MainWindow` 只绑定进度条事件，不直接操作 demuxer/decoder/syncmanager。
- [ ] 播放位置更新时，由 `PositionChanged(double seconds)` 刷新进度条。
- [ ] 总时长更新时，由 `DurationChanged(double seconds)` 设置进度条范围。
- [ ] 用户按下进度条时暂停 UI 自动回写，避免信号互相驱动。
- [ ] 用户拖动过程中只更新 UI 预览位置，不连续调用 seek。
- [ ] 用户释放进度条时调用 `MediaPlayController::SeekTo(targetSeconds)`。
- [ ] Seek 调用完成后恢复进度条跟随 `PositionChanged`。
- [ ] 处理总时长为 0 或媒体源不可 seek 的边界。
- [ ] 形成报告：`doc/Phase1_1_任务8_进度条Seek接口报告.md`

验收：

- [ ] 进度条显示不反向驱动播放状态混乱。
- [ ] 用户拖动进度条后能跳到对应位置。
- [ ] 拖动进度条不会破坏音视频同步。
- [ ] 拖动过程中不会触发高频 seek。
- [ ] `MainWindow` 没有新增基础设施层依赖。

## 任务 9: 播放速度控制预研

- [ ] 调研当前 `AudioPlayer` 输出链路是否支持不变调变速。
- [ ] 确认是否需要引入音频变速处理：重采样、丢/插样、或后续接入专用算法。
- [ ] 确认 `VideoRender` 的帧间隔如何按速度缩放。
- [ ] 确认 `syncmanager` 主时钟是否需要引入 playback speed 概念。
- [ ] 设计 `MediaPlayController::SetPlaybackSpeed(double speed)` 对外接口。
- [ ] 设计 `PlaybackSpeedChanged(double speed)` 信号。
- [ ] 明确支持范围：优先 0.5x、1.0x、1.5x、2.0x。
- [ ] 明确异常输入处理：小于等于 0、过大速度、非有限数。
- [ ] 明确播放速度与 Seek/Stop/Pause 的交互关系。
- [ ] 暂不编码实现速度控制，只产出方案，避免和 Seek 稳定性风险叠加。
- [ ] 形成报告：`doc/Phase1_1_任务9_播放速度控制预研报告.md`

验收：

- [ ] 形成实现方案后再进入编码。
- [ ] 方案明确音频、视频、同步时钟三方职责。
- [ ] 方案不要求 `MainWindow` 直接控制基础设施层。

## 1.1 完成标准

- [ ] 播放、暂停、停止行为边界清晰。
- [ ] 快进/快退是真实 Seek。
- [ ] Seek 后音视频同步。
- [ ] Seek 后无旧包、旧帧、旧音频残留。
- [ ] 进度信息能用于 UI 进度条。
- [ ] `MainWindow` 没有新增基础设施层依赖。
- [ ] 没有新增全局变量。
