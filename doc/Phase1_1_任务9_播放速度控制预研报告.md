# Phase 1.1 任务 9: 播放速度控制预研报告

## 1. 当前链路分析

### 1.1 AudioPlayer (`src/infrastructure/audioplayer.cpp`)

音频播放流程：
```
帧队列.Pop() → swr_convert(重采样) → QAudioSink.write(PCM) → RefreshClock(audioclock)
```

时钟计算（第193行）：
```cpp
double audioclock = tmp->pts * m_audioTimeBase * 1000;  // ms
m_syncManager->RefreshClock(audioclock);
```

关键约束：`QAudioSink` 以固定采样率输出（44100Hz），无法让声卡"加速播放"。音频实际输出的速度由写数据速度决定 —— 丢弃样本 = 变快，重复样本 = 变慢。

### 1.2 VideoRender (`src/infrastructure/videorender.cpp`)

视频渲染流程：
```
QTimer(m_frameInterval) → RenderFrame() → Front(队列看预览帧)
  → 计算 diff = videoPTS - audioClock
  → diff > 40ms: 等待 (setInterval(waitMs))
  → diff < -40ms: 丢帧 (TryPop + av_frame_free)
  → 正常: TryPop + uploadTexture + update()
```

帧间隔计算（第35行）：
```cpp
m_frameInterval = qRound(1000.0 / frameRate);  // ms
```

同步阈值：`SYNC_THRESHOLD_MS = 40`（第5行）

### 1.3 syncmanager (`src/infrastructure/syncmanager.cpp`)

主时钟为音频驱动：
```cpp
RefreshClock(double clock) → mainclock = clock;  // 纯赋值，单位 ms
GetClock() → return mainclock;
SeekTo(double clockMs) → mainclock = clockMs;
```

时钟本身不引入速度概念。音频帧以什么速率被消费，时钟就以什么速率推进。

---

## 2. 方案设计

### 2.1 核心思路：缩放音频写输出 + 缩放视频帧间隔

不修改 syncmanager 时钟逻辑。利用"音频帧消费速度决定时钟推进速度"的天然特性：

```
speed=2.0: 每帧只写 50% PCM → 帧队列消耗速度翻倍 → 时钟以 2x 推进 → 视频自然跟随
speed=0.5: 每帧写 200% PCM（重复样本）→ 帧队列消耗速度减半 → 时钟以 0.5x 推进
```

### 2.2 三方改动

#### AudioPlayer

| 速度 | PCM 写策略 | 音质 |
|------|-----------|------|
| 0.5x | 每样本写两次（重复） | 降调 |
| 1.0x | 正常 | 正常 |
| 1.5x | 写 2/3，丢弃 1/3 | 升调 |
| 2.0x | 写 1/2，丢弃 1/2 | 升调（chipmunk） |

已知限制：**会变调**，这是不引入外部 DSP 库（SoundTouch/rubberband）的必然代价。如果后续需要不变调变速，需要引入 SoundTouch 或 libsamplerate。

新增成员和方法：
```cpp
// audioplayer.h
public:
    void SetSpeed(double speed);   // 设置播放速度
private:
    double m_speed = 1.0;          // 播放速度因子
```

写 PCM 循环修改（`audioplayer.cpp:199-230`）：
```cpp
int effectiveChunk = static_cast<int>(chunk / m_speed);
if (m_speed < 1.0) {
    // 慢放：重复写入补齐
    int repeats = qRound(1.0 / m_speed);
    for (int r = 0; r < repeats && written > 0; r++) {
        m_ioDevice->write(pcmPtr, effectiveChunk);
    }
} else {
    // 快放：跳过部分样本
    int skip = static_cast<int>(chunk * (1.0 - 1.0/m_speed));
    m_ioDevice->write(pcmPtr, effectiveChunk); // 只写 effectiveChunk
    pcmIndex += skip; // 跳过丢弃样本
}
```

#### VideoRender

新增方法和成员：
```cpp
// videorender.h
public:
    void SetSpeed(double speed);

private:
    double m_speed = 1.0;
    int baseFrameInterval_; // 替代 m_frameInterval，存原始值

// videorender.cpp
void VideoRender::SetSpeed(double speed) {
    m_speed = speed;
    m_frameInterval = qMax(1, qRound(baseFrameInterval_ / speed));
    if (m_timer && m_timer->isActive()) {
        m_timer->setInterval(m_frameInterval);
    }
}
```

同步阈值不缩放，保留 `SYNC_THRESHOLD_MS = 40`。速度为 2x 时音频时钟推进快，`diff < -40` 自然触发丢帧，无需特殊处理。

#### MediaPlayController

新增接口：
```cpp
// MediaPlayController.h
public:
    void SetPlaybackSpeed(double speed);
    double PlaybackSpeed() const;

signals:
    void PlaybackSpeedChanged(double speed);

private:
    double playbackSpeed_ = 1.0;
```

实现：
```cpp
void MediaPlayController::SetPlaybackSpeed(double speed) {
    // 边界保护
    if (speed <= 0.0 || !std::isfinite(speed)) {
        qWarning() << "Invalid speed:" << speed;
        return;
    }
    // 精度截断到 0.05
    speed = qRound(speed * 20.0) / 20.0;

    if (qFuzzyCompare(playbackSpeed_, speed)) return;

    playbackSpeed_ = speed;
    audioplay_->SetSpeed(speed);
    videoRender_->SetSpeed(speed);

    emit PlaybackSpeedChanged(speed);
}
```

---

## 3. 速度与现有操作的交互

| 操作 | 交互行为 | 需要改动? |
|------|---------|:---:|
| Seek | 先暂停管线 → seek → 恢复播放，速度不变 | 否 |
| Stop | 停止整个管线，下次 Start 速度重新生效 | 否 |
| Pause | 暂停，恢复后速度不变 | 否 |
| Start | `SetSpeed()` 已下发到组件，启动时生效 | 否 |
| Rewind/Forward | 调用 `SeekRelative`，不涉及速度 | 否 |

速度是持久状态，不受播放控制操作影响。只在 `SetPlaybackSpeed()` 被调用时才改变。

---

## 4. 支持范围

| 速度 | 支持 | 策略 |
|------|:---:|------|
| 0.5x | ✅ | 每样本写两次，降调 |
| 1.0x | ✅ | 正常（默认） |
| 1.5x | ✅ | 写 2/3 PCM，降采样升调 |
| 2.0x | ✅ | 写 1/2 PCM，降采样升调 |
| 0.25x | ⚠️ | 可选但 PCM 重复量极大，体验差 |
| 4.0x | ⚠️ | 可选但丢 3/4 样本，音质极差 |
| ≤0 | ❌ | 无效输入，SetPlaybackSpeed 拒收 |
| NaN/Inf | ❌ | `std::isfinite` 过滤 |

---

## 5. 编码顺序

```
Step 1 — syncmanager: 无改动
Step 2 — AudioPlayer: 加 m_speed + SetSpeed() + PCM 写缩放
Step 3 — VideoRender: 加 m_speed + SetSpeed() + 帧间隔缩放
Step 4 — MediaPlayController: SetPlaybackSpeed() + signal
Step 5 — MainWindow: 绑定快捷键（如数字键 0.5/1/2）
```

## 6. 未来升级路径（不变调变速）

当前方案缺点：变速会改变音调。如需不变调变速，可引入：

1. **SoundTouch** — 轻量级，MIT 协议，支持 ±50% 变速 + 变调独立控制
2. **Rubber Band** — 专业级，GPL，质量更高
3. **FFmpeg atempo filter** — 软实现，需要额外 AVFilterGraph

这些库需要额外集成，当前 Phase 1 暂不引入，保持依赖干净。

---

## 7. 验收标准

- [ ] 0.5x/1.0x/1.5x/2.0x 切换正常，播放不 crash
- [ ] 变速后音视频保持同步
- [ ] Seek/Stop/Pause 后速度状态保持
- [ ] 无效速度输入被拒绝
- [ ] `MainWindow` 不直接依赖基础设施层
