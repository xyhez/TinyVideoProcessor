# Phase 3.10 VideoFrameScheduler 抽取方案

## 目标

本方案用于确认 `VideoFrameScheduler` 的接口、输入输出、所有权、生命周期和迁移顺序。

Phase 3.9 之后，真实视频帧已经可以进入 `MultiViewRenderer` 的多个 slot，但调度能力仍然留在 `VideoRender` 内部。当前结构是：

```text
MediaPipeline
    -> VideoRender
        -> 从 VFrame_ 取帧
        -> 使用 syncmanager 判断早晚
        -> 丢弃过晚帧
        -> 刷新 VIDEO sync 时钟
        -> 把可显示帧交给自身或 external sink
```

Phase 3.10 的目标不是修改渲染效果，而是把这部分“单路视频帧调度”从 `VideoRender` 中抽离出来：

```text
MediaPipeline
    -> VideoFrameScheduler
        -> IVideoFrameSink
            -> VideoRender 或 SingleStreamFrameSinkAdapter
```

只有完成这一步，后续 UI 才能真正全面转向 `MultiViewRenderer`，而不是继续依赖一个隐藏的 `VideoRender` 来驱动视频帧。

## 当前 VideoRender 中属于调度器的内容

### 字段

以下字段应迁移到 `VideoFrameScheduler`：

```cpp
std::shared_ptr<SyncQueue<AVFrame*>> m_frameQueue;
std::shared_ptr<syncmanager> m_syncManager;
double m_videoTimeBase;
int m_frameIntervalMs;
std::atomic<double> m_speed;
std::atomic<bool> m_running;
std::atomic<int> m_dropped;
std::atomic<int> m_rendered;
```

说明：

- `m_frameQueue` 是调度器的输入队列。
- `m_syncManager` 是单路同步判断依赖。
- `m_videoTimeBase` 用于把 `AVFrame::pts` 转成毫秒，不能从 frame 自身推断。
- `m_frameIntervalMs` 和 `m_speed` 决定下一轮调度间隔。
- `m_running` 控制自调度循环是否继续。
- `m_dropped` / `m_rendered` 可先随调度器迁移，后续再决定是否对外暴露统计。

以下字段应保留在 `VideoRender`：

```cpp
AVFrame* m_displayFrame;
int m_videoW;
int m_videoH;
AVRational m_sar;
bool m_hasFrame;
QOpenGLShaderProgram m_shader;
QOpenGLTexture* m_textureY;
QOpenGLTexture* m_textureU;
QOpenGLTexture* m_textureV;
QMutex m_frameMutex;
```

说明：

- 这些字段属于默认 widget sink 的显示状态和 OpenGL 资源。
- scheduler 不应该持有 OpenGL 资源，也不应该知道 QWidget。

### 方法

以下方法应迁移到 `VideoFrameScheduler`：

```cpp
CanRenderFrame()
CalculateScaledFrameInterval()
PeekNextFrame()
EvaluateFrameTiming()
DropLateFrame()
PopDisplayableFrame()
RefreshVideoClockIfNeeded()
ScheduleNextFrame()
RenderFrame()
```

以下方法应保留在 `VideoRender`：

```cpp
PresentFrame()
ClearFrame()
ClearCurrentFrame()
UploadTexture()
paintGL()
initializeGL()
resizeGL()
```

`PresentDisplayableFrame()` 的职责需要拆开：

- “可显示帧交给当前 sink”应进入 scheduler。
- “默认路径接管帧并刷新 widget”应由 `VideoRender::PresentFrame()` 保留。

## 推荐类定位

`VideoFrameScheduler` 建议放在基础设施层：

```text
src/infrastructure/videoframescheduler.h
src/infrastructure/videoframescheduler.cpp
```

原因：

- 它直接依赖 `SyncQueue<AVFrame*>`、FFmpeg `AVFrame` 和现有 `syncmanager`。
- 它不包含业务策略，不决定 active stream / active audio。
- 它不属于 UI，也不属于多路管理。

## 推荐 public API

第一版建议接口如下：

```cpp
class VideoFrameScheduler : public QObject {
    Q_OBJECT

public:
    explicit VideoFrameScheduler(QObject* parent = nullptr);
    ~VideoFrameScheduler();

    void Initialize(std::shared_ptr<SyncQueue<AVFrame*>> frameQueue,
                    std::shared_ptr<syncmanager> syncManager,
                    double frameRate,
                    double videoTimeBase);

    void SetFrameSink(IVideoFrameSink* frameSink);

    void Start();
    void Stop();
    void Pause();
    void SetSpeed(double speed);
    void ClearOutputFrame();

private slots:
    void Tick();
};
```

语义：

- `Initialize(...)` 只绑定调度输入，不启动调度。
- `SetFrameSink(...)` 设置可显示帧输出目标，不转移 sink 所有权。
- `Start()` 进入运行态并触发一次 `Tick()`。
- `Stop()` 停止调度并唤醒 frame queue。
- `Pause()` 第一版等价于 `Stop()`，保留语义入口。
- `SetSpeed()` 校验并保存播放速度。
- `ClearOutputFrame()` 调用当前 sink 的 `ClearFrame()`，不清队列、不 flush decoder、不重置时钟。
- `Tick()` 替代当前 `VideoRender::RenderFrame()` 的自调度入口。

## 输入

### frameQueue

类型：

```cpp
std::shared_ptr<SyncQueue<AVFrame*>>
```

用途：

- 查看队首帧。
- 弹出过晚帧并释放。
- 弹出可显示帧并转交 sink。

约束：

- 只有 scheduler 访问视频帧队列。
- `MultiViewRenderer` 和 `VideoRender` sink 不访问队列。

### syncManager

类型：

```cpp
std::shared_ptr<syncmanager>
```

用途：

- 获取同步类型。
- 获取主时钟。
- VIDEO sync 下刷新视频时钟。

约束：

- 同步判断属于 scheduler。
- sink 不访问 `syncmanager`。

### frameRate

用途：

- 初始化 `frameIntervalMs`。
- 异常帧率继续使用默认值，避免除零。

### videoTimeBase

用途：

- 将 frame pts 转换为毫秒。

约束：

- 继续使用 `FileInfo` / demuxer 解析出的 time base。
- 不从 `AVFrame` 自身推断 time base。

### playbackSpeed

用途：

- 修正下一轮调度间隔。

约束：

- 只接受现有支持档位：`0.5`、`1.0`、`1.5`、`2.0`。
- 非法速度拒绝，不改变当前速度。

## 输出

`VideoFrameScheduler` 的唯一输出目标是 `IVideoFrameSink`。

```text
可显示帧:
    scheduler 弹出 AVFrame*
    -> 刷新 VIDEO sync 时钟
    -> frameSink->PresentFrame(frame)
    -> frame 所有权转移给 sink

清理当前显示帧:
    scheduler->ClearOutputFrame()
    -> frameSink->ClearFrame()
```

调度器不直接调用：

```text
VideoRender::UploadTexture()
MultiViewRenderer::PresentFrame(streamId, frame)
QOpenGLWidget::update()
```

这些都属于 sink 或 adapter 的职责。

## 所有权协议

### 队列队首检查

`PeekNextFrame()` 只查看队首帧，不转移所有权。

用途：

- 判断 AUDIO sync 下是否需要等待。
- 避免过早弹出帧，导致后续无法重新判断同一帧。

### 丢弃过晚帧

`DropLateFrame()` 从队列弹出帧后，scheduler 负责释放：

```cpp
AVFrame* drop = nullptr;
if (frameQueue->TryPop(drop)) {
    av_frame_free(&drop);
}
```

该帧不进入 sink。

### 可显示帧

`PopDisplayableFrame()` 从队列弹出帧后，scheduler 负责把所有权转移给 sink：

```cpp
frameSink->PresentFrame(frame);
```

调用成功后 scheduler 不再访问该指针。

如果 sink 为空：

- 第一版不应弹出可显示帧。
- 如果已经弹出但发现 sink 为空，必须立即 `av_frame_free(&frame)`，避免泄漏。

推荐实现上在弹出前检查 sink 是否存在。

### 清理当前显示帧

`ClearOutputFrame()` 只调用 sink 的 `ClearFrame()`。

它不负责：

- 清空 `VFrame_`
- 清空 packet queue
- decoder flush
- demuxer seek
- sync clock reset

这些仍由 `MediaPipeline` 负责。

## 生命周期语义

### Start

行为：

```text
running = true
rendered = 0
dropped = 0
QTimer::singleShot(0, this, &VideoFrameScheduler::Tick)
```

约束：

- 多次 `Start()` 不应创建不可控的重复调度链。第一版可以在 `running` 已经为 true 时直接返回。

### Stop

行为：

```text
running = false
frameQueue->WakeAll()
```

约束：

- `Stop()` 不隐式清空队列。
- 是否清空队列由 `MediaPipeline::ClearQueues()` 决定。
- `Stop()` 不直接释放 sink 当前帧；是否清理当前输出由 `MediaPipeline` 显式调用 `ClearOutputFrame()`。

### Pause

第一版语义：

```text
Pause() == Stop()
```

原因：

- 当前 `VideoRender::Pause()` 本质上就是 `StopRendering()`。
- 暂停应保留当前画面，不应隐式调用 `ClearOutputFrame()`。

### Seek

推荐顺序：

```text
MediaPipeline::SeekTo()
    -> scheduler->Pause()
    -> 等待 demuxer / decoder suspend
    -> scheduler->ClearOutputFrame()
    -> ClearQueues()
    -> demuxer->SeekTo(target)
    -> FlushDecoders()
    -> Vdecoder->SetDiscardBefore(target)
    -> ClearQueues()
    -> syncManager->SeekTo(targetMs)
```

原因：

- scheduler 只控制视频帧调度和当前输出帧。
- seek 的队列清理、decoder flush、demuxer seek 仍属于 `MediaPipeline`。

### Stop 播放

推荐顺序：

```text
MediaPipeline::Stop()
    -> scheduler->Pause()
    -> positionTimer->stop()
    -> scheduler->ClearOutputFrame()
    -> ClearQueues()
    -> demuxer seek 或实时源 reset
    -> FlushDecoders()
    -> syncManager->SeekTo(0)
```

### Shutdown

推荐顺序：

```text
MediaPipeline::ReleaseResources()
    -> positionTimer->stop()
    -> queue RequestStop()
    -> demuxer / decoder / audio stop
    -> scheduler->Stop()
    -> scheduler->ClearOutputFrame()
    -> wait threads
    -> reset scheduler
    -> reset sink / renderer
```

约束：

- scheduler 必须早于 sink 被销毁。
- queued `Tick()` 如果在 Stop 后触发，必须因 `running == false` 立即返回。

## queued 调用和对象销毁

`QTimer::singleShot(delay, this, &VideoFrameScheduler::Tick)` 的接收者是 scheduler 自身。

要求：

- scheduler 析构前调用 `Stop()`。
- `Tick()` 第一行检查 `CanScheduleFrame()`。
- scheduler 销毁后，Qt 不应再向已销毁 QObject 投递该 slot。
- sink 指针不转移所有权；scheduler 不负责删除 sink。
- sink 可能先于 scheduler 清空时，`MediaPipeline` 必须先 `scheduler->SetFrameSink(nullptr)` 或先 reset scheduler。

推荐第一版释放顺序：

```text
scheduler->Stop()
scheduler->ClearOutputFrame()
scheduler->SetFrameSink(nullptr)
scheduler.reset()
sink.reset() 或 delete VideoRender
```

## 与 MediaPipeline 的关系

Phase 3.10 最终目标是让 `MediaPipeline` 显式持有：

```cpp
std::unique_ptr<VideoFrameScheduler> videoScheduler_;
std::unique_ptr<VideoRender> videoRender_;
std::unique_ptr<IVideoFrameSink> videoFrameSink_;
```

实际成员形态可以按现有代码微调，但边界应满足：

- `videoScheduler_` 负责启动、暂停、停止、seek 前清输出、shutdown 前停止。
- `videoRender_` 只在默认路径作为 widget sink。
- 外部 sink 路径不再需要隐藏 `VideoRender` 承担调度。
- `MediaPipeline` 不知道 `MultiViewRenderer`。
- `MediaPipeline` 不持有 `StreamId`。

## 分步迁移策略

### 第一步：新增空壳类

新增 `VideoFrameScheduler` 类，只建立 API 和空实现。

目标：

- 先接入构建系统。
- 不改变播放路径。
- 降低后续迁移风险。

### 第二步：VideoRender 内部组合 scheduler

让 `VideoRender` 内部使用 scheduler 驱动当前自身 sink。

目标：

- 先验证调度逻辑迁移本身。
- 外部 `MediaPipeline` 调用方式暂时不变。

风险控制：

- 如果播放行为变化，只需回看 `VideoRender` 和 scheduler，不牵涉 `MediaPipeline`。

### 第三步：MediaPipeline 显式组合 scheduler

把 scheduler 从 `VideoRender` 内部提升到 `MediaPipeline`。

目标：

- 默认路径：scheduler -> `VideoRender`
- 统一渲染路径：scheduler -> external sink

这是外部 sink 路径摆脱隐藏 `VideoRender` 的关键步骤。

### 第四步：VideoRender 收口为纯 sink

移除 `VideoRender` 中残留调度接口和字段，只保留：

- `PresentFrame()`
- `ClearFrame()`
- OpenGL widget 显示

完成后，旧 UI 是否可以删除才具备判断基础。

## 不做范围

- 不让 scheduler 持有 `StreamId`。
- 不让 scheduler 直接调用 `MultiViewRenderer`。
- 不让 `MultiViewRenderer` 访问 `VFrame_`。
- 不让 `VideoRender` 在本阶段被直接删除。
- 不改 active audio 策略。
- 不改音频同步管理策略。
- 不做非 YUV420P 格式转换。
- 不做性能优化和零拷贝。

## 验收判断

3.10.2 完成后，应能回答：

1. `VideoFrameScheduler` 如何替代 `VideoRender::RenderFrame()`？
2. stop、seek、shutdown 时谁清理当前输出帧？
3. `AVFrame*` 从队列到 sink 的所有权如何转移？
4. 为什么 scheduler 不持有 `StreamId`？
5. 为什么全面转向 `MultiViewRenderer` 必须等 scheduler 抽取完成？

如果这些问题不能回答清楚，不应进入 3.10.3 代码实现。
