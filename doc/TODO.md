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

- [x] 快进/快退后音视频同步。
- [x] Seek 后无旧画面闪回。
- [x] Seek 后无旧音频残留。
- [x] 暂停状态下 Seek 后仍保持暂停，播放状态下 Seek 后继续播放。

## 任务 6: Stop 行为回到起始位置

- [x] 明确 Stop 和 Pause 的状态差异。
- [x] Stop 时停止播放管线。
- [x] Stop 时清空所有队列。
- [x] Stop 时 flush decoder。
- [x] Stop 时同步时钟回到 0。
- [x] Stop 后再次 Start 从 0 开始播放。

验收：

- [x] Stop 后画面和音频都停止。
- [x] Stop 后再次播放从开头开始。
- [x] Stop 不导致线程无法再次启动。

## 任务 7: 播放位置和总时长信号

> 拆解为 7 个子步骤，每步编译验证后再做下一步。

---

### 7.1 确认数据来源与单位（只读调研，不写代码）

**目标**：搞清楚两个数据的来源和单位，避免后续接口设计出错。

- [x] 确认 `Fileinfo::duration_` 赋值来源：`demuxer.cpp:66` 处 `ic->duration/1000000`。`AVFormatContext::duration` 单位为微秒，除以 1,000,000 后单位为**秒**。`Fileinfo::duration_` 类型为 `int`。
- [x] 确认 `syncmanager::GetClock()` 返回值单位：`mainclock` 成员注释写"单位毫秒(ms)"，`RefreshClock()` 调用处（`audioplayer.cpp:196`）传入音频时钟，即毫秒。
- [x] 确认 `syncmanager::SeekTo()` 参数单位：毫秒。
- [x] **结论**：`Duration()` 对外返回秒（`int`→`double`），`Position()` 从 `syncmanager::GetClock()` 取毫秒 ÷ 1000 得到秒。Controller 接口统一用秒，UI 自行转换。

验收：
- [x] 单位结论有代码行号佐证，不靠猜测。

---

### 7.2 增加 `Duration()` 和 `Position()` 查询接口

**目标**：在 `MediaPlayController` 上增加两个 const 查询方法。

- [x] `double Duration() const`：`return mediaSource_ ? static_cast<double>(mediaSource_->GetSourceInfo().duration_) : 0.0;`
- [x] `double Position() const`：`return syncManager_ ? syncManager_->GetClock() / 1000.0 : 0.0;`
- [x] 头文件 `MediaPlayController.h` 增加两个方法声明。
- [x] 编译验证。

验收：
- [x] `Duration()` 在 Initialize 成功后返回 >0 的秒数。
- [x] `Position()` 在播放中返回递增的秒数。
- [x] 未初始化时两个方法都返回 0.0，不 crash。

---

### 7.3 增加 `PositionChanged` / `DurationChanged` 信号

**目标**：声明两个信号，让 UI 层可以绑定。

- [x] `MediaPlayController.h` 增加 `void PositionChanged(double seconds);`
- [x] `MediaPlayController.h` 增加 `void DurationChanged(double seconds);`
- [x] 编译验证（信号只声明不定义，moc 自动生成实现）。

验收：
- [x] 编译通过，无 moc 相关链接错误。

---

### 7.4 增加位置更新定时器

**目标**：Controller 内部用 QTimer 周期发射 `PositionChanged`，MainWindow 无需主动轮询。

- [x] 在 `MediaPlayController` 中添加 `positionTimer_` 成员。
- [x] 在 `Initialize()` 中创建定时器。
- [x] 设置间隔 200ms（兼顾 UI 平滑与 CPU 开销）。
- [x] `connect(positionTimer_, &QTimer::timeout, this, [this]() { emit PositionChanged(Position()); });`
- [x] 在 `Start()` 中 `positionTimer_->start();`
- [x] 在 `Stop()` 中 `positionTimer_->stop();`
- [x] 在 `ReleaseResources()` 中释放定时器。
- [ ] `PositionChanged` 去重：仅在值与上次不同时才 emit（可选优化，初版可跳过）。
- [x] 编译验证。

验收：
- [x] 播放时 `PositionChanged` 持续发射，间隔约 200ms。
- [x] Stop 后 `PositionChanged` 停止发射。
- [x] 关闭窗口时定时器随 Controller 析构安全停止。

---

### 7.5 在状态切换点发射即时更新

**目标**：Start/Stop/Seek 操作后 Position 不等待下一个定时器周期，立即更新。

- [x] `Stop()`：在 `SetState(Stopped)` 之前 emit `PositionChanged(0.0)`。
- [x] `Start()`：在 `SetState(Playing)` 之后 emit `PositionChanged(Position())`。
- [x] `SeekTo()`：在 `syncManager_->SeekTo()` 之后 emit `PositionChanged(Position())`。
- [x] `Initialize()` 成功后：emit `DurationChanged(Duration())`。
- [x] `Start()` 中如果首次播放，也 emit `DurationChanged(Duration())`（防止 Initialize 时 MediaSource 尚未 Open 导致 duration_=0 的情况；当前 Initialize 里已 Open，但留防御）。
- [x] 编译验证。

验收：
- [x] Stop 后 `PositionChanged` 发射值为 0.0。
- [x] Seek 后 `PositionChanged` 发射值为目标秒数（不等到 200ms 定时器）。
- [x] 初始化成功后 `DurationChanged` 发射总时长。

---

### 7.6 暂停时不推进位置

**目标**：Pause 状态 Position 冻结，不继续增长。

- [x] `Pause()`：`positionTimer_->stop();`，然后 emit `PositionChanged(Position())`（冻结当前值）。
- [x] `Start()`：已经在 7.4 中处理了 `positionTimer_->start();`，覆盖从 Pause 恢复的场景。
- [x] 编译验证。
- [x] 手动测试：播放 → 暂停 → 等 3 秒 → PositionChanged 不再发射新值。

验收：
- [x] 暂停期间定时器停止，Position 冻结。
- [x] 恢复播放后 Position 从冻结值继续增长。
- [x] 暂停时 emit 了一次当前 Position（UI 可以据此显示精确的暂停位置）。

---

### 7.7 边界情况防御

**目标**：处理异常状态，不 crash、不发射荒谬值。

- [x] `Duration()` null guard（已在 7.2 中处理）。
- [x] `Position()` null guard（已在 7.2 中处理）。
- [x] 总时长为 0（直播流或未 Open）：`Duration()` 返回 0.0，UI 据此禁用进度条或显示不确定状态。
- [x] `positionTimer_` 回调通过 `Position()` 间接检查 `syncManager_` 非空。
- [x] `SeekTo()` 空值检查维持现有逻辑不变（已有 null guard）。
- [x] 编译验证。

验收：
- [x] 未 Initialize 时调用 `Duration()` / `Position()` 不 crash。
- [x] 总时长为 0 时 UI 不会除零。
- [x] 析构期间定时器不会访问已释放的 syncManager_。

---

### 7.8 形成报告

- [x] 汇总以上 7 个子步骤的实施结果。
- [x] 记录 `Duration()` / `Position()` 的精确语义和单位。
- [x] 记录定时器间隔选择理由（200ms = 5Hz）。
- [x] 记录边界情况处理方式。
- [x] 形成报告：`doc/Phase1_1_任务7_播放位置和时长信号报告.md`

---

验收：

- [x] 播放过程中位置持续更新。
- [x] Seek 后位置立即更新到目标附近。
- [x] 总时长只由媒体源信息决定，不从 UI 硬编码。
- [x] Stop 后位置显示为 0。
- [x] 暂停时位置不继续前进。
- [x] `MainWindow` 不直接依赖 `syncmanager`、`demuxer` 或 `Fileinfo` 内部细节。

## 任务 8: 为进度条 Seek 做接口准备

> 拆解为 5 个子步骤，每步编译验证。

**设计原则**：MainWindow 只认 Controller 的 `PositionChanged` / `DurationChanged` / `SeekTo()`，不直接依赖基础设施层。

---

### 8.1 UI 添加进度条和时间标签

**目标**：在 `mainwindow.ui` 中加入 `QSlider` + 两个时间 `QLabel`，控件就位但先不接线。

- [x] 在 `mainwindow.ui` 中增加一行水平布局：
  - `QLabel("label_current")` — 当前时间，默认显示 `00:00`
  - `QSlider("slider_progress")` — 水平进度条（Orientation: Horizontal）
  - `QLabel("label_duration")` — 总时长，默认显示 `00:00`
- [x] `.ui` 文件手动编辑 XML，不引入 Qt Designer 依赖。
- [x] `mainwindow.cpp` 中通过 `ui->label_current`、`ui->slider_progress`、`ui->label_duration` 可访问。
- [x] 编译验证。

验收：
- [x] 窗口启动后看到进度条和两个时间标签。
- [x] 进度条初始禁用或值为 0。

---

### 8.2 连接信号：DurationChanged → 设置滑块范围 + 更新总时长标签

**目标**：拿到总时长后，设置 `QSlider::setRange()` 和总时长标签文本。

- [x] 在 `CreateConnects()` 中增加 `DurationChanged` 连接：
  ```cpp
  connect(controller, &MediaPlayController::DurationChanged, this, [this](double seconds) {
      int total = static_cast<int>(seconds);
      ui->slider_progress->setRange(0, total);
      ui->slider_progress->setEnabled(total > 0);
      ui->label_duration->setText(formatTime(total));
  });
  ```
- [x] 增加私有辅助函数 `QString formatTime(double totalSeconds)` → `"MM:SS"`。
- [x] 编译验证。

验收：
- [x] 播放开始后，总时长标签显示正确 MM:SS。
- [x] 滑块范围随总时长设置。
- [x] 总时长为 0 时滑块禁用。

---

### 8.3 连接信号：PositionChanged → 更新滑块位置 + 当前时间标签

**目标**：播放时滑块跟随位置前进。

- [x] 在 `CreateConnects()` 中增加 `PositionChanged` 连接：
  ```cpp
  connect(controller, &MediaPlayController::PositionChanged, this, [this](double seconds) {
      if (!m_isSliderDragging) {
          int pos = static_cast<int>(seconds);
          ui->slider_progress->setValue(pos);
          ui->label_current->setText(formatTime(pos));
      }
  });
  ```
- [x] `mainwindow.h` 增加 `bool isSliderDragging_ = false;` 成员。
- [x] 编译验证。

验收：
- [x] 播放时滑块平滑前进。
- [x] 当前时间标签持续更新。
- [x] Stop 后滑块回到 0，标签显示 00:00。
- [x] Pause 时滑块冻结不变。

---

### 8.4 连接滑块信号：按下/拖动/释放

**目标**：用户拖动滑块时暂停自动更新，释放时触发 Seek。

- [x] 连接 `slider_progress::sliderPressed`：设置 `isSliderDragging_ = true;`
- [x] 连接 `slider_progress::sliderMoved`（拖动中）：更新当前时间标签预览值 `ui->label_current->setText(formatTime(value));`，不调 SeekTo。
- [x] 连接 `slider_progress::sliderReleased`：
  ```cpp
  double target = static_cast<double>(ui->slider_progress->value());
  controller->SeekTo(target);
  m_isSliderDragging = false;
  ```
  当前实现先恢复 `isSliderDragging_ = false`，再调用 `SeekTo()`，确保 SeekTo 内同步发射的 `PositionChanged` 不会被忽略。
- [x] 编译验证。

验收：
- [x] 拖动滑块时当前时间标签跟随预览值变化。
- [x] 拖动过程中不调用 SeekTo（不会触发视频跳转）。
- [x] 释放滑块后视频跳到目标位置。
- [x] 释放后进度条恢复跟随 PositionChanged。

---

### 8.5 边界情况处理 + 形成报告

**目标**：防止异常情况导致 crash 或行为异常。

- [x] 总时长为 0：`DurationChanged` 中 `total > 0` 判断已处理滑块禁用（8.2）。
- [x] Stop 后滑块归零：`PositionChanged(0.0)` 已在 Controller 中 emit，8.3 中 `setValue(0)` 自然处理。
- [x] 滑块值范围校验：`sliderReleased` 中 `targetSeconds` 已由 Controller `SeekTo()` 内部 clamp（无需 MainWindow 重复）。
- [x] 快速双击空格（Pause→Play→Pause）时 `isSliderDragging_` 不会卡在 true：因为 `sliderPressed` 只有鼠标/触控触发，键盘操作不影响。
- [x] 初始状态：`label_current` = `00:00`，`label_duration` = `00:00`，`slider_progress` 禁用。
- [x] 汇总以上，形成报告：`doc/Phase1_1_任务8_进度条Seek接口报告.md`

验收：
- [x] 进度条显示不反向驱动播放状态混乱。
- [x] 用户拖动进度条后能跳到对应位置。
- [x] 拖动进度条不会破坏音视频同步。
- [x] 拖动过程中不会触发高频 seek。
- [x] `MainWindow` 没有新增基础设施层依赖。

---

验收：

- [x] 进度条显示不反向驱动播放状态混乱。
- [x] 用户拖动进度条后能跳到对应位置。
- [x] 拖动进度条不会破坏音视频同步。
- [x] 拖动过程中不会触发高频 seek。
- [x] `MainWindow` 没有新增基础设施层依赖。

## 任务 9: 播放速度控制

> 预研已修订，方案见 `doc/Phase1_1_任务9_播放速度控制预研报告.md`。
> 核心思路：固定档位变速 → 音频 PCM 按采样帧对齐处理 → AudioPlayer 按实际输出进度刷新主时钟 → VideoRender 继续以音频主时钟同步，只调整自调度检查频率。
> 每步编译验证。

---

### 9.1 明确支持范围和时钟策略

**目标**：先固定任务边界，避免任意倍速导致 PCM 处理和时钟推进不可控。

- [x] 支持固定档位：`0.5x / 1.0x / 1.5x / 2.0x`。
- [x] 不支持任意 `0.05` 精度倍速；输入值由 Controller 拒绝。
- [x] 明确当前 Phase 允许变调，不引入 SoundTouch/RubberBand/AVFilterGraph。
- [x] 明确音频写出必须按采样帧对齐，不能按任意字节截断。
- [x] 明确主时钟由 AudioPlayer 根据“当前音频帧 PTS + 已输出媒体进度”刷新，不能只在每个 AVFrame 开头刷新一次。
- [x] 编译验证。

验收：
- [x] 文档记录固定档位和变调限制。
- [x] 文档记录主时钟推进策略，慢放时不会长期停在同一个 PTS。

---

### 9.2 AudioPlayer 增加线程安全速度控制

**目标**：新增 `SetSpeed(double)` 方法，提供线程安全的播放速度状态。

- [x] `audioplayer.h` 增加线程安全速度成员，优先使用 `std::atomic<double> m_speed {1.0};`；如编译环境不适合，则使用 `QMutex` 保护。
- [x] `audioplayer.h` 增加方法声明 `void SetSpeed(double speed);`
- [x] `audioplayer.cpp` 实现 `SetSpeed()`：
  - 边界保护：speed <= 0 或非有限数直接 return
  - 仅接受 `0.5 / 1.0 / 1.5 / 2.0` 四个归一化后的值
  - 赋值给线程安全速度成员
- [x] `AudioPlayer::Run()` 在处理每个 PCM chunk 前读取一次当前速度快照，避免一次写循环中速度反复变化。
- [x] 编译验证。

验收：
- [x] UI 线程调用 `SetSpeed()` 与音频线程读取速度不存在数据竞争。
- [x] 无效速度不会改变当前速度。
- [x] 切换速度不 crash。

---

### 9.3 AudioPlayer 按采样帧对齐处理 PCM

**目标**：真正改变音频输出速度，同时保证 PCM 不被截断到单个采样帧中间。

- [x] 计算 `bytesPerSampleFrame = m_dstChannels * m_bytesPerSample`。
- [x] 每次写入、跳过、重复的字节数都向下对齐到 `bytesPerSampleFrame` 的整数倍。
- [x] `1.0x`：保持现有写出逻辑。
- [x] `2.0x`：按采样帧丢弃一半 PCM，例如每 2 个采样帧保留 1 个采样帧。
- [x] `1.5x`：按稳定比例丢弃约 1/3 PCM，例如每 3 个采样帧保留 2 个采样帧。
- [x] `0.5x`：按采样帧重复输出，使实际输出时长约为原来的 2 倍。
- [x] 保持 `m_totalBytesWritten` / `m_bytesWrittenTotal` 统计实际写入声卡的字节数。
- [x] 编译验证。

验收：
- [x] 0.5x/1.5x/2.0x 都不会写出非采样帧对齐的 PCM。
- [x] 变速时无明显爆音、声道错位或重采样链路异常。
- [x] 统计字段仍反映实际写入量。

---

### 9.4 AudioPlayer 按输出进度刷新主时钟

**目标**：避免慢放时主时钟长时间停留在同一个 AVFrame PTS，导致视频等待或抖动。

- [x] 在处理每个音频 AVFrame 时记录 `frameStartClockMs = tmp->pts * m_audioTimeBase * 1000`。
- [x] 在写 PCM 的过程中，根据已写出的输出采样帧数和当前速度估算媒体时间推进：
  - `mediaElapsedMs = outputSampleFramesWritten * speed / m_dstRate * 1000`
  - `currentClockMs = frameStartClockMs + mediaElapsedMs`
- [x] 写出每个 chunk 后刷新 `m_syncManager->RefreshClock(currentClockMs)`。
- [x] 保留现有硬件延迟计算逻辑；若后续发现显示偏早，再把 `CalculateHardwareLatency()` 纳入时钟修正。
- [x] 编译验证。

验收：
- [x] 0.5x 慢放时主时钟持续缓慢推进，不只在下一帧到来时跳变。
- [x] 2.0x 快放时主时钟推进速度约为正常速度两倍。
- [x] 视频同步仍以音频主时钟为准。

---

### 9.5 VideoRender 增加速度感知的调度频率

**目标**：新增 `SetSpeed(double)` 方法，仅用于调整自调度检查频率，不承担主同步语义。

- [x] `videorender.h` 增加线程安全速度成员，优先使用 `std::atomic<double> m_speed {1.0};`；如编译环境不适合，则使用 `QMutex` 保护。
- [x] `videorender.h` 增加方法声明 `void SetSpeed(double speed);`
- [x] `videorender.cpp` 实现 `SetSpeed()`：
  - 边界保护同 AudioPlayer
  - 仅接受 `0.5 / 1.0 / 1.5 / 2.0`
  - 更新线程安全速度成员
- [x] 在 `RenderFrame()` 中计算 `scaledInterval = qMax(1, qRound(m_frameIntervalMs / speedSnapshot))`。
- [x] 只把 `scaledInterval` 用于空队列等待、正常渲染后的下一次调度等检查频率；`diff > SYNC_THRESHOLD_MS` 的精确等待仍以音频时钟差值为准。
- [x] 编译验证。

验收：
- [x] VideoRender 不直接修改主时钟。
- [x] 视频仍通过 `videoPts - audioClock` 做同步判断。
- [x] 倍速只影响自调度检查频率，不破坏 seek/暂停/停止流程。

---

### 9.6 MediaPlayController 增加 SetPlaybackSpeed 入口

**目标**：应用层统一入口，输入验证，下发到 AudioPlayer + VideoRender 两个组件。

- [x] `MediaPlayController.h` 增加：
  ```cpp
  public:
      void SetPlaybackSpeed(double speed);
      double PlaybackSpeed() const;
  signals:
      void PlaybackSpeedChanged(double speed);
  private:
      double playbackSpeed_ = 1.0;
  ```
- [x] `MediaPlayController.cpp` 实现 `SetPlaybackSpeed()`：
  - `speed <= 0.0 || !std::isfinite(speed)` → qWarning + return
  - 仅允许 `0.5 / 1.0 / 1.5 / 2.0`
  - 输入值不在支持列表时拒绝，或显式归一化到最近档位并打印日志
  - `if (std::fabs(playbackSpeed_ - speed) < kSpeedEpsilon) return;` 去重
  - `playbackSpeed_ = speed;`
  - 若 `audioplay_` 已初始化，则 `audioplay_->SetSpeed(speed);`
  - 若 `videoRender_` 已初始化，则 `videoRender_->SetSpeed(speed);`
  - `emit PlaybackSpeedChanged(speed);`
- [x] `PlaybackSpeed() const` 直接 return `playbackSpeed_`。
- [x] `Initialize()` 构建 AudioPlayer/VideoRender 后下发当前 `playbackSpeed_`，保证 Stop/Start 或后续切换源时速度状态保持。
- [x] 编译验证。

验收：
- [x] `SetPlaybackSpeed(2.0)` 调用后 AudioPlayer 和 VideoRender 都收到速度变更。
- [x] 重复设相同速度不会重复下发（去重生效）。
- [x] 无效值（0、-1、NaN）被拒绝，不影响当前播放。
- [x] 速度状态不因 Stop/Start 或后续 SwitchSource 重置。

---

### 9.7 MainWindow 绑定速度控制快捷键

**目标**：用键盘快速切换播放速度，验证整个链路。

- [x] `keyReleaseEvent` 增加快捷键：
  - `Qt::Key_1` → `SetPlaybackSpeed(0.5)` + `NoticeWindow("速度: 0.5x")`
  - `Qt::Key_2` → `SetPlaybackSpeed(1.0)` + `NoticeWindow("速度: 1.0x")`
  - `Qt::Key_3` → `SetPlaybackSpeed(1.5)` + `NoticeWindow("速度: 1.5x")`
  - `Qt::Key_4` → `SetPlaybackSpeed(2.0)` + `NoticeWindow("速度: 2.0x")`
- [x] 连接 `PlaybackSpeedChanged` 信号，debug 输出当前速度。
- [x] 编译验证。

验收：
- [x] 按键 1/2/3/4 能实时切换速度。
- [x] 速度切换后音视频同步保持。
- [x] 切换速度不中断播放。
- [x] `MainWindow` 不直接依赖基础设施层。

---

### 9.8 边界测试与报告

**目标**：手动验证关键场景，确认无回归。

- [x] 正常速度播放 10s → 切 2.0x 播放 10s → 切回 1.0x → 音视频同步不丢。
- [x] 正常速度播放 10s → 切 0.5x 播放 10s → 切回 1.0x → 视频不长时间冻结。
- [x] 2.0x 播放中 Pause → 恢复 → 速度保持 2.0x。
- [x] 0.5x 播放中 Seek → Seek 后速度保持 0.5x。
- [x] 2.0x 播放中 Stop → Start → 速度保持 2.0x（速度是持久状态）。
- [x] 高速播放（2.0x）时拖动进度条 Seek → 不 crash。
- [x] 形成报告：`doc/Phase1_1_任务9_播放速度控制实现报告.md`

验收：
- [x] 0.5x/1.0x/1.5x/2.0x 切换正常。
- [x] 变速后音视频保持同步。
- [x] Seek/Stop/Pause 后速度状态保持。
- [x] 无效速度输入被拒绝。
- [x] `MainWindow` 不直接依赖基础设施层。

## 1.1 完成标准

- [x] 播放、暂停、停止行为边界清晰。
- [x] 快进/快退是真实 Seek。
- [x] Seek 后音视频同步。
- [x] Seek 后无旧包、旧帧、旧音频残留。
- [x] 进度信息能用于 UI 进度条。
- [x] `MainWindow` 没有新增基础设施层依赖。
- [x] 没有新增全局变量。

---

# Phase 1.2: 文件列表管理

> 目标：播放列表 + 切换文件 + 循环模式。继续遵循 `doc/架构指导.md`：`MainWindow` 只做 UI 交互和事件转发，`MediaPlayController` 只控制当前媒体源，播放列表状态放在领域层，不绕过 `IMediaSource` 抽象。

---

## 1.2.1 创建 PlaylistManager 数据类

**目标**：新建领域层类，管理播放列表条目、当前播放索引和播放模式。只做数据与选择规则，不依赖 Qt GUI，不创建 `FileMediaSource`，不接触 demuxer/decoder 等基础设施。

- [x] 新建 `src/domain/PlaylistManager.h/.cpp`。
- [x] 定义 `PlaylistItem`：
  ```cpp
  struct PlaylistItem {
      QString filePath;
      QString displayName;
      double durationSeconds = 0.0;
  };
  ```
- [x] 定义 `enum class PlayMode { Sequential, ListLoop, SingleLoop, Random };`
- [x] `PlaylistManager` 继承 `QObject`，第一版接口如下：
  ```cpp
  class PlaylistManager : public QObject {
      Q_OBJECT
  public:
      int Count() const;
      int CurrentIndex() const;
      QList<PlaylistItem> Items() const;
      std::optional<PlaylistItem> CurrentItem() const;

      void AddFile(const QString& filePath);
      void AddFiles(const QStringList& filePaths);
      void RemoveAt(int index);
      void Clear();
      bool SetCurrentIndex(int index);

      void SetPlayMode(PlayMode mode);
      PlayMode GetPlayMode() const;

  signals:
      void PlaylistChanged();
      void CurrentIndexChanged(int index);
      void PlayModeChanged(PlayMode mode);
  private:
      QList<PlaylistItem> items_;
      int currentIndex_ = -1;
      PlayMode playMode_ = PlayMode::Sequential;
  };
  ```
- [x] `AddFile()` 做路径规范化和去重，相同文件不重复加入。
- [x] `AddFile()` 自动生成 `displayName`，优先使用文件名。
- [x] `RemoveAt()` 明确 current index 规则：
  - 删除当前项前面的条目：`currentIndex_` 前移一位。
  - 删除当前项：优先指向同位置的新条目；如果删除末尾则回退到最后一项。
  - 删除后列表为空：`currentIndex_ = -1`。
- [x] `SetCurrentIndex()` 对越界 index 返回 false，不改变当前状态。
- [x] `CMakeLists.txt` 添加 `src/domain/PlaylistManager.cpp`。
- [ ] 编译验证。

验收：
- [x] AddFile/AddFiles/Clear/Count 正确。
- [x] CurrentIndex/CurrentItem 在空列表、单项列表、多项列表下正确。
- [x] RemoveAt 删除当前项、当前项之前、当前项之后的行为正确。
- [x] 去重生效。
- [x] 不引入 `MainWindow` 或基础设施层依赖。

---

## 1.2.2 上一个/下一个选择规则

**目标**：在 `PlaylistManager` 内完成播放模式和前后项选择规则。这里只改变当前条目，不触发真实播放。

- [x] 增加 `bool MoveNext()`。
- [x] 增加 `bool MovePrevious()`。
- [x] `Sequential`：
  - Next 到末尾后返回 false，current index 保持不变。
  - Previous 到开头后返回 false，current index 保持不变。
- [x] `ListLoop`：
  - Next 末尾回绕到 0。
  - Previous 开头回绕到最后一项。
- [x] `SingleLoop`：
  - Next/Previous 都保持当前项，列表非空时返回 true。
- [x] `Random`：
  - 列表数量大于 1 时尽量选择不同于当前项的 index。
  - 列表数量为 1 时保持当前项。
- [x] 空列表下 MoveNext/MovePrevious 返回 false。
- [ ] 编译验证。

验收：
- [x] 四种播放模式选择行为正确。
- [x] 空列表、单项列表、开头、末尾不越界。
- [x] Random 不会在多文件列表中固定返回同一项。
- [x] MoveNext/MovePrevious 只改变播放列表状态，不触发 Controller 播放。

---

## 1.2.3 文件切换和 Controller 初始化衔接

**目标**：建立“当前播放项变化 -> 创建 `FileMediaSource` -> 切换 `MediaPlayController` 当前媒体源”的流程，同时保持 Controller 依赖 `IMediaSource` 抽象。

- [x] `MediaPlayController.h` 增加换源入口，参数使用媒体源抽象：
  ```cpp
  bool SwitchSource(std::shared_ptr<IMediaSource> mediaSource,
                    QWidget* rendererContainer,
                    bool autoStart);
  ```
- [x] `SwitchSource()` 内部流程：
  - 切换前先验证新媒体源可以打开。
  - 停止当前管线。
  - 释放旧资源。
  - 使用新的 `IMediaSource` 重新初始化。
  - `autoStart == true` 时启动新媒体源；否则只完成初始化。
- [x] 应用层根据 `PlaylistItem::filePath` 创建 `FileMediaSource`，再传给 Controller。
- [x] 文件路径无效或媒体源打开失败时返回 false，不污染 PlaylistManager 当前状态。
- [ ] 编译验证。

验收：
- [x] 播放中切换文件：旧视频停止，新视频正常播放。
- [x] Stop 状态下切换文件：只切换当前源，不自动播放。
- [x] 切换失败不 crash，旧状态处理规则明确。
- [x] `MediaPlayController` 不新增 `QString filePath` 型换源接口。

---

## 1.2.4 MainWindow UI 绑定

**目标**：`MainWindow` 展示播放列表并转发 UI 事件，不保存播放列表业务状态，不直接创建或操作基础设施组件。

- [x] `mainwindow.ui` 改为水平分栏：
  - 左侧：`QListWidget("listWidget_files")`，用于展示播放列表。
  - 右侧：保留现有视频、进度条、控制按钮区域。
- [x] `QListWidget` 设置 `SingleSelection`。
- [x] `MainWindow::OpenFile()` 弹出文件对话框（`QFileDialog::getOpenFileNames`），批量添加 `*.mp4/*.mkv/*.avi`。
- [x] 文件选择结果只转发给 `PlaylistManager::AddFiles()`。
- [x] `PlaylistChanged()` 后刷新 `QListWidget` 展示。
- [x] 双击列表项只转发 index，调用 `PlaylistManager::SetCurrentIndex()`。
- [x] 当前项变化后，由应用层创建 `FileMediaSource` 并调用 Controller 换源。
- [x] 启动文件加入 `PlaylistManager`，并同步显示在列表中。
- [ ] 编译验证。

验收：
- [x] UI 可以添加、删除、清空文件。
- [x] 启动时当前文件出现在列表中。
- [x] 双击列表另一文件可以切换播放。
- [x] 列表连续切换不 crash。
- [x] `MainWindow` 不新增 demuxer/decoder/queue 相关 include。

---

## 1.2.5 上一个/下一个 + 播放模式 UI

**目标**：把 `PlaylistManager` 的 MoveNext/MovePrevious 和播放模式暴露到 UI，仍然只通过应用层和 Controller 抽象完成换源。

- [x] `widget_func` 增加 `pushButton_prev`（上一个）和 `pushButton_next`（下一个）。
- [x] `widget_func` 增加 `QComboBox("comboBox_mode")` 或一组互斥按钮，四个选项：顺序/单曲循环/列表循环/随机。
- [x] `pushButton_prev` 点击后调用 `PlaylistManager::MovePrevious()`，成功后切换当前媒体源并播放。
- [x] `pushButton_next` 点击后调用 `PlaylistManager::MoveNext()`，成功后切换当前媒体源并播放。
- [x] `comboBox_mode::currentIndexChanged` 只调用 `PlaylistManager::SetPlayMode()`。
- [x] `keyReleaseEvent` 增加：
  - `Ctrl+Left` -> prev。
  - `Ctrl+Right` -> next。
- [x] 播放到文件末尾时检测模式决定行为：
  - Sequential：停止并保持结束位置
  - SingleLoop：重新切换当前项 + `Start`（EOF 后重建当前媒体源）
  - ListLoop：`MoveNext()` + 换源 + `Start`
  - Random：`MoveNext()` + 换源 + `Start`
- [x] 检测 EOF 的时机：`MediaPlayController` 监听 demuxer 的 EOF 信号（如果已有），或通过 `StateChanged` + `Position ≈ Duration` 判断。
- [ ] 编译验证。

验收：
- [x] Prev/Next 切集后音视频同步正常。
- [x] 第一个文件按 Prev 不 crash。
- [x] 最后一个文件 Next 后：Sequential 模式保持原位，ListLoop 回绕到第一个。
- [x] 四种模式切换后立即生效。
- [x] 单曲循环：视频播完自动从头开始。
- [x] 列表循环：最后一集播完切回第一集。
- [x] 顺序播放：播完停止不跳。
- [x] 随机播放：每次 MoveNext 顺序不固定。

---

## 1.2.6 最近播放记录预留（本阶段跳过）

**目标**：只形成方案，不急着编码，避免 Phase1.2 主线扩大。

> 说明：本功能已按当前计划跳过，不纳入 Phase 1.2 收口验收。

- [ ] 明确最近播放记录保存字段：文件路径、显示名、上次播放位置、最后播放时间。
- [ ] 明确保存位置：优先考虑 `QSettings`，后续如列表复杂再考虑独立配置文件。
- [ ] 明确是否恢复上次播放位置。
- [ ] 明确最近播放记录与当前播放列表的关系：启动时可恢复列表，但不自动播放。

验收：
- [ ] 形成方案文档后再进入编码。
- [ ] 不影响播放列表、换源、循环模式主线。

---

## 验收：

- [x] 播放列表支持添加/删除/清空。
- [x] 双击列表项切换播放。
- [x] 上一个/下一个按钮和快捷键正常工作。
- [x] 四种循环模式行为正确。
- [x] 文件切换继续通过 `IMediaSource` 抽象完成。
- [x] 文件列表不反向侵入基础设施层。
- [x] `MainWindow` 通过 `PlaylistManager` 和 `MediaPlayController` 交互，不直接碰 demuxer/decoder。

---

# Phase 1.3: 音量控制

> 目标：把当前键盘音量增减扩展为完整音量能力。继续遵循 `doc/架构指导.md`：`MainWindow` 只做 UI 绑定，`MediaPlayController` 提供统一音量入口，`AudioPlayer` 负责实际音量应用。暂不做音量持久化。

---

## 1.3.1 梳理当前音量链路

**目标**：先确认现有实现，不直接改代码。

- [x] 检查 `MainWindow::VolumeUp()` / `VolumeDown()`。
- [x] 检查 `MediaPlayController::VolumeUp()` / `VolumeDown()`。
- [x] 检查 `AudioPlayer::VolumeUp()` / `VolumeDown()` 是否真正作用到 `QAudioSink`。
- [x] 检查 `AudioPlayer::VolumeChanged(qreal)` 是否已被 Controller 或 UI 使用。
- [x] 明确当前音量单位：UI 用 0-100，内部用 0.0-1.0。

验收：
- [x] 写清楚当前缺口。
- [x] 后续改动不需要 `MainWindow` 直接依赖 `AudioPlayer`。

---

## 1.3.2 收敛 AudioPlayer 音量能力

**目标**：让音频输出层拥有完整、单一的音量状态入口。

- [x] 新增 `SetVolume(qreal volume)`。
- [x] 新增 `qreal Volume() const`。
- [x] `VolumeUp()` / `VolumeDown()` 改为调用 `SetVolume()`。
- [x] `SetVolume()` 做 `0.0` 到 `1.0` clamp。
- [x] `SetVolume()` 调用 `QAudioSink::setVolume()` 或统一的 `ApplyOutputVolume()`。
- [x] 新增 `SetMuted(bool muted)`。
- [x] 新增 `bool IsMuted() const`。
- [x] 新增 `MuteChanged(bool muted)` 信号。
- [x] 静音不丢失用户音量，取消静音恢复原音量。
- [x] 保证音频线程和 UI 线程访问音量状态时线程安全。

验收：
- [x] `VolumeUp()` / `VolumeDown()` 真实改变输出音量。
- [x] `SetVolume(0.0)` 静音输出但不一定改变静音状态。
- [x] `SetMuted(true)` 后实际输出为 0。
- [x] `SetMuted(false)` 后恢复静音前音量。
- [x] 音量和静音信号只在状态变化时发出。

---

## 1.3.3 MediaPlayController 增加统一音量接口

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

---

## 1.3.4 UI 接入音量滑块和静音按钮

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

---

## 1.3.5 边界检查和报告

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

## 1.3 完成标准

- [ ] 音量支持 UI 滑块控制。
- [ ] 音量支持键盘增减控制。
- [ ] 支持静音和取消静音。
- [ ] 音量状态和静音状态有 Controller 级信号。
- [ ] 切换文件后音量和静音状态保持。
- [ ] UI 不直接依赖 `AudioPlayer` 或 `QAudioSink`。
- [ ] 不新增全局变量。
