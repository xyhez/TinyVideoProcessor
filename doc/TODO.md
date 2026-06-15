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
- [ ] 编译验证。

验收：
- [x] `Duration()` 在 Initialize 成功后返回 >0 的秒数。
- [ ] `Position()` 在播放中返回递增的秒数。（需运行时验证）
- [x] 未初始化时两个方法都返回 0.0，不 crash。

---

### 7.3 增加 `PositionChanged` / `DurationChanged` 信号

**目标**：声明两个信号，让 UI 层可以绑定。

- [x] `MediaPlayController.h` 增加 `void PositionChanged(double seconds);`
- [x] `MediaPlayController.h` 增加 `void DurationChanged(double seconds);`
- [ ] 编译验证（信号只声明不定义，moc 自动生成实现）。

验收：
- [ ] 编译通过，无 moc 相关链接错误。

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
- [ ] 编译验证。

验收：
- [ ] 播放时 `PositionChanged` 持续发射，间隔约 200ms。（需运行时验证）
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
- [ ] 编译验证。

验收：
- [x] Stop 后 `PositionChanged` 发射值为 0.0。
- [x] Seek 后 `PositionChanged` 发射值为目标秒数（不等到 200ms 定时器）。
- [x] 初始化成功后 `DurationChanged` 发射总时长。

---

### 7.6 暂停时不推进位置

**目标**：Pause 状态 Position 冻结，不继续增长。

- [x] `Pause()`：`positionTimer_->stop();`，然后 emit `PositionChanged(Position())`（冻结当前值）。
- [x] `Start()`：已经在 7.4 中处理了 `positionTimer_->start();`，覆盖从 Pause 恢复的场景。
- [ ] 编译验证。
- [ ] 手动测试：播放 → 暂停 → 等 3 秒 → PositionChanged 不再发射新值。

验收：
- [x] 暂停期间定时器停止，Position 冻结。
- [ ] 恢复播放后 Position 从冻结值继续增长。（需运行时验证）
- [x] 暂停时 emit 了一次当前 Position（UI 可以据此显示精确的暂停位置）。

---

### 7.7 边界情况防御

**目标**：处理异常状态，不 crash、不发射荒谬值。

- [x] `Duration()` null guard（已在 7.2 中处理）。
- [x] `Position()` null guard（已在 7.2 中处理）。
- [x] 总时长为 0（直播流或未 Open）：`Duration()` 返回 0.0，UI 据此禁用进度条或显示不确定状态。
- [x] `positionTimer_` 回调通过 `Position()` 间接检查 `syncManager_` 非空。
- [x] `SeekTo()` 空值检查维持现有逻辑不变（已有 null guard）。
- [ ] 编译验证。

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

- [ ] 播放过程中位置持续更新。（需运行时验证）
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
- [ ] 编译验证。

验收：
- [ ] 窗口启动后看到进度条和两个时间标签。（需运行时验证）
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
- [ ] 编译验证。

验收：
- [ ] 播放开始后，总时长标签显示正确 MM:SS。（需运行时验证）
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
- [ ] 编译验证。

验收：
- [ ] 播放时滑块平滑前进。（需运行时验证）
- [ ] 当前时间标签持续更新。（需运行时验证）
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
- [ ] 编译验证。

验收：
- [x] 拖动滑块时当前时间标签跟随预览值变化。
- [x] 拖动过程中不调用 SeekTo（不会触发视频跳转）。
- [ ] 释放滑块后视频跳到目标位置。（需运行时验证）
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
- [ ] 用户拖动进度条后能跳到对应位置。（需运行时验证）
- [ ] 拖动进度条不会破坏音视频同步。（需运行时验证）
- [x] 拖动过程中不会触发高频 seek。
- [x] `MainWindow` 没有新增基础设施层依赖。

---

验收：

- [x] 进度条显示不反向驱动播放状态混乱。
- [ ] 用户拖动进度条后能跳到对应位置。（需运行时验证）
- [ ] 拖动进度条不会破坏音视频同步。（需运行时验证）
- [x] 拖动过程中不会触发高频 seek。
- [x] `MainWindow` 没有新增基础设施层依赖。

## 任务 9: 播放速度控制

> 预研已完成，方案见 `doc/Phase1_1_任务9_播放速度控制预研报告.md`。
> 核心思路：缩放音频 PCM 写输出 → 帧消费速度改变 → 主时钟推进速度自然跟随 → 视频同步逻辑无需改动。
> 每步编译验证。

---

### 9.1 AudioPlayer 增加速度控制

**目标**：新增 `SetSpeed(double)` 方法，缩放每次写入 `QAudioSink` 的 PCM 数据量。

- [ ] `audioplayer.h` 增加成员 `double m_speed = 1.0;`
- [ ] `audioplayer.h` 增加方法声明 `void SetSpeed(double speed);`
- [ ] `audioplayer.cpp` 实现 `SetSpeed()`：
  - 边界保护：speed ≤ 0 或非有限数直接 return
  - 精度截断到 0.05：`qRound(speed * 20.0) / 20.0`
  - 赋值 `m_speed = speed;`
- [ ] `audioplayer.cpp` 修改 `Run()` 中写 PCM 循环（第199-230行）：
  - 计算有效 chunk：`effectiveChunk = chunk / m_speed`
  - speed > 1.0：只写 `effectiveChunk` 字节，跳过 `chunk - effectiveChunk` 字节（快放采样丢弃）
  - speed < 1.0：写 `effectiveChunk` 字节，但循环重复写 `qRound(1.0/m_speed)` 次（慢放采样重复）
  - speed == 1.0：行为不变
  - 同步推进 `totalBytesWritten` 逻辑（保持实际写入量，以维护延迟计算准确）
- [ ] 编译验证。

验收：
- [ ] `SetSpeed(2.0)` 后音频播放速度翻倍（音调升高，可接受已知限制）。
- [ ] `SetSpeed(0.5)` 后音频播放速度减半（音调降低）。
- [ ] 切换速度不 crash，不破坏重采样链路。

---

### 9.2 VideoRender 增加速度控制

**目标**：新增 `SetSpeed(double)` 方法，按速度缩放帧间隔。

- [ ] `videorender.h` 增加成员 `double m_speed = 1.0;`。
- [ ] `videorender.h` 增加方法声明 `void SetSpeed(double speed);`
- [ ] `videorender.cpp` 实现 `SetSpeed()`：
  - 边界保护同 9.1。
  - 更新 `m_speed = speed;`
- [ ] `videorender.cpp` 修改 `RenderFrame()` 中 `ScheduleNextFrame(m_frameIntervalMs)` 改为 `ScheduleNextFrame(scaledInterval)`，其中 `scaledInterval = qMax(1, qRound(m_frameIntervalMs / m_speed))`。
- [ ] 编译验证。

验收：
- [ ] 2.0x 时帧间隔减半（`m_frameIntervalMs/2`），视频渲染频率翻倍。
- [ ] 0.5x 时帧间隔加倍，视频渲染频率减半。
- [ ] 音视频同步保持（主时钟由音频驱动，自然跟随）。

---

### 9.3 MediaPlayController 增加 SetPlaybackSpeed 入口

**目标**：应用层统一入口，输入验证，下发到 AudioPlayer + VideoRender 两个组件。

- [ ] `MediaPlayController.h` 增加：
  ```cpp
  public:
      void SetPlaybackSpeed(double speed);
      double PlaybackSpeed() const;
  signals:
      void PlaybackSpeedChanged(double speed);
  private:
      double playbackSpeed_ = 1.0;
  ```
- [ ] `MediaPlayController.cpp` 实现 `SetPlaybackSpeed()`：
  - `speed <= 0.0 || !std::isfinite(speed)` → qWarning + return
  - 精度截断到 0.05
  - `if (qFuzzyCompare(playbackSpeed_, speed)) return;` 去重
  - `playbackSpeed_ = speed;`
  - `audioplay_->SetSpeed(speed);`
  - `videoRender_->SetSpeed(speed);`
  - `emit PlaybackSpeedChanged(speed);`
- [ ] `PlaybackSpeed() const` 直接 return `playbackSpeed_`。
- [ ] 编译验证。

验收：
- [ ] `SetPlaybackSpeed(2.0)` 调用后 AudioPlayer 和 VideoRender 都收到速度变更。
- [ ] 重复设相同速度不会重复下发（去重生效）。
- [ ] 无效值（0、-1、NaN）被拒绝，不影响当前播放。

---

### 9.4 MainWindow 绑定速度控制快捷键（可选）

**目标**：用键盘快速切换播放速度，验证整个链路。

- [ ] `keyReleaseEvent` 增加快捷键：
  - `Qt::Key_1` → `SetPlaybackSpeed(0.5)` + `NoticeWindow("速度: 0.5x")`
  - `Qt::Key_2` → `SetPlaybackSpeed(1.0)` + `NoticeWindow("速度: 1.0x")`
  - `Qt::Key_3` → `SetPlaybackSpeed(1.5)` + `NoticeWindow("速度: 1.5x")`
  - `Qt::Key_4` → `SetPlaybackSpeed(2.0)` + `NoticeWindow("速度: 2.0x")`
- [ ] 连接 `PlaybackSpeedChanged` 信号，debug 输出当前速度。
- [ ] 编译验证。

验收：
- [ ] 按键 1/2/3/4 能实时切换速度。
- [ ] 速度切换后音视频同步保持。
- [ ] 切换速度不中断播放。

---

### 9.5 边界测试

**目标**：手动验证关键场景，确认无回归。

- [ ] 正常速度播放 10s → 切 2.0x 播放 10s → 切回 1.0x → 音视频同步不丢。
- [ ] 2.0x 播放中 Pause → 恢复 → 速度保持 2.0x。
- [ ] 0.5x 播放中 Seek → Seek 后速度保持 0.5x。
- [ ] 2.0x 播放中 Stop → Start → 速度保持 2.0x（速度是持久状态）。
- [ ] 高速播放（2.0x）时拖动进度条 Seek → 不 crash。

验收：
- [ ] Speed 不因 Pause/Seek/Stop 而意外重置。
- [ ] 所有速度切换不 crash。
- [ ] `MainWindow` 不直接依赖基础设施层。

---

验收：

- [ ] 0.5x/1.0x/1.5x/2.0x 切换正常。
- [ ] 变速后音视频保持同步。
- [ ] Seek/Stop/Pause 后速度状态保持。
- [ ] 无效速度输入被拒绝。
- [ ] `MainWindow` 不直接依赖基础设施层。

## 1.1 完成标准

- [ ] 播放、暂停、停止行为边界清晰。
- [ ] 快进/快退是真实 Seek。
- [ ] Seek 后音视频同步。
- [ ] Seek 后无旧包、旧帧、旧音频残留。
- [ ] 进度信息能用于 UI 进度条。
- [ ] `MainWindow` 没有新增基础设施层依赖。
- [ ] 没有新增全局变量。

---

# Phase 1.2: 文件列表管理

> 目标：播放列表 + 切换文件 + 循环模式。

---

## 1.2.1 创建 PlaylistManager 数据类

**目标**：新建领域层类，管理文件路径列表、当前播放索引、播放模式。纯数据逻辑，不依赖 Qt GUI。

- [ ] 新建 `src/domain/PlaylistManager.h/.cpp`。
- [ ] 定义 `enum class PlayMode { Sequential, SingleLoop, ListLoop, Random };`
- [ ] `PlaylistManager` 继承 `QObject`：
  ```cpp
  class PlaylistManager : public QObject {
      Q_OBJECT
  public:
      void AddFile(const QString& filePath);
      void RemoveFile(int index);
      void Clear();
      int Count() const;
      int CurrentIndex() const;
      QString CurrentFile() const;
      QString NextFile();       // 根据 PlayMode 计算
      QString PreviousFile();   // 根据 PlayMode 计算
      void SetPlayMode(PlayMode mode);
      PlayMode GetPlayMode() const;
      void SetCurrentIndex(int index);
  signals:
      void PlaylistChanged();
      void CurrentIndexChanged(int index);
  private:
      QStringList files_;
      int currentIndex_ = -1;
      PlayMode playMode_ = PlayMode::Sequential;
  };
  ```
- [ ] `NextFile()` 逻辑：
  - Sequential：`index+1`，到末尾停
  - SingleLoop：`index` 不变（当前文件循环）
  - ListLoop：`index+1`，到末尾回绕到 0
  - Random：随机选一个不同于当前的 index
- [ ] `PreviousFile()` 逻辑：`index-1`，非负数；random 模式同 next 随机选。
- [ ] `AddFile` 去重（相同路径不重复加入）。
- [ ] `CMakeLists.txt` 添加 `src/domain/PlaylistManager.cpp`。
- [ ] 编译验证。

验收：
- [ ] AddFile/Clear/Count 正确。
- [ ] NextFile/PreviousFile 四种模式行为正确。
- [ ] Random 不会连续两次返回同一首（至少尝试避免）。
- [ ] 去重生效。

---

## 1.2.2 UI 布局改造：左侧加文件列表

**目标**：`mainwindow.ui` 改为水平分栏：左侧 `QListWidget`，右侧保持现有垂直三块。

- [ ] 修改 `mainwindow.ui` 的 `centralwidget` 布局：
  - 外层从 `QVBoxLayout` 改为 `QHBoxLayout`
  - 左侧：`QListWidget("listWidget_files")` — 文件列表，固定宽度 ~200px
  - 右侧：`QWidget` 包裹原有 `QVBoxLayout`（视频+进度条+按钮），stretch=1
- [ ] `QListWidget` 设置：
  - `selectionMode`: SingleSelection
  - `dragDropMode`: InternalMove（后续 1.2.7 启用拖拽）
- [ ] 编译验证，窗口左侧出现空白列表。

验收：
- [ ] 窗口启动后左侧有 `QListWidget`，右侧视频区域正常显示。
- [ ] 拖拽窗口边缘，列表和视频区域按比例缩放。

---

## 1.2.3 MediaPlayController 支持切换媒体源

**目标**：`Stop() → 换源 → Start()` 目前分三步调用，加一个 `SwitchSource()` 一步完成。

- [ ] `MediaPlayController.h` 增加 `bool SwitchSource(const QString& filePath);`
- [ ] `MediaPlayController.cpp` 实现：
  ```cpp
  bool SwitchSource(const QString& filePath) {
      // 1. 停止当前管线
      Stop();
      // 2. 释放旧资源（ReleaseResources 现有逻辑）
      // 3. 创建新 FileMediaSource
      // 4. 重新 Initialize
      // 5. 如果之前是 Playing 状态，自动 Start
  }
  ```
- [ ] 区分"全新初始化"和"切换源"：`SwitchSource` 可复用 `Initialize` 的逻辑，但需先 `ReleaseResources`。
- [ ] 编译验证。

验收：
- [ ] 播放中调用 `SwitchSource(newFile)` → 停止旧视频，播新视频。
- [ ] Stop 状态下调用 `SwitchSource` → 切换源但不自动播放。
- [ ] 新文件路径无效时返回 false，不 crash。

---

## 1.2.4 MainWindow 连接列表交互

**目标**：双击列表项 → 切换播放文件。启动时添加到列表。

- [ ] `MainWindow::OpenFile()` 弹出文件对话框（`QFileDialog::getOpenFileNames`），批量添加 `*.mp4/*.mkv/*.avi`。
- [ ] `MainWindow::CreateConnects()` 增加：
  - `listWidget_files::itemDoubleClicked` → 取 `item->data(FilePathRole)` → `SwitchSource` + `Start`
  - 或通过 `PlaylistManager::SetCurrentIndex` → `CurrentIndexChanged` → 切换
- [ ] 构造函数中：初始化时把启动文件也加入 `PlaylistManager` + `listWidget`。
- [ ] `listWidget` 中每项存文件路径（用 `Qt::UserRole`）。
- [ ] 编译验证。

验收：
- [ ] 启动时当前文件出现在列表中。
- [ ] 双击列表另一文件 → 切换播放。
- [ ] 列表连续切换不 crash。

---

## 1.2.5 上一个/下一个 + 快捷键

**目标**：UI 按钮 + 键盘快捷键实现切集。

- [ ] `widget_func` 增加 `pushButton_prev`（上一个）和 `pushButton_next`（下一个）。
- [ ] `on_pushButton_prev_clicked` → `PlaylistManager::PreviousFile()` → `SwitchSource` + `Start`
- [ ] `on_pushButton_next_clicked` → `PlaylistManager::NextFile()` → `SwitchSource` + `Start`
- [ ] 播放到末尾（Sequential 模式最后一个文件结束）→ 自动停，发信号通知 UI。
- [ ] `keyReleaseEvent` 增加：
  - `Ctrl+Left` → prev
  - `Ctrl+Right` → next
- [ ] 编译验证。

验收：
- [ ] Prev/Next 切集后音视频同步正常。
- [ ] 第一个文件按 Prev 不 crash（留在原位）。
- [ ] 最后一个文件 Next 后：Sequential 模式停止，ListLoop 回绕到第一个。

---

## 1.2.6 播放模式切换

**目标**：右键菜单或按钮切换四种播放模式。

- [ ] `widget_func` 增加 `QComboBox("comboBox_mode")` 或一组互斥按钮，四个选项：顺序/单曲循环/列表循环/随机。
- [ ] `MainWindow::CreateConnects()` 增加：
  - `comboBox_mode::currentIndexChanged` → `PlaylistManager::SetPlayMode()`
- [ ] 播放到文件末尾时检测模式决定行为：
  - Sequential：停止，`NoticeWindow("播放完毕")`
  - SingleLoop：`SeekTo(0)` + `Start`（从头循环当前文件）
  - ListLoop：`NextFile()` + `SwitchSource` + `Start`
  - Random：`NextFile()`（随机） + `SwitchSource` + `Start`
- [ ] 检测 EOF 的时机：`MediaPlayController` 监听 demuxer 的 EOF 信号（如果已有），或通过 `StateChanged` + `Position ≈ Duration` 判断。
- [ ] 编译验证。

验收：
- [ ] 四种模式切换后立即生效。
- [ ] 单曲循环：视频播完自动从头开始。
- [ ] 列表循环：最后一集播完切回第一集。
- [ ] 顺序播放：播完停止不跳。
- [ ] 随机播放：每次 NextFile 顺序不固定。

---

## 验收：

- [ ] 播放列表支持添加/删除/清空。
- [ ] 双击列表项切换播放。
- [ ] 上一个/下一个按钮和快捷键正常工作。
- [ ] 四种循环模式行为正确。
- [ ] 文件列表不反向侵入基础设施层。
- [ ] `MainWindow` 通过 `PlaylistManager` 和 `MediaPlayController` 交互，不直接碰 demuxer/decoder。
