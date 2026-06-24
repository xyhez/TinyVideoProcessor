# Phase 3.7 视频帧调度边界设计

## 阶段目标

本设计文档用于明确 `VideoRender::RenderFrame()` 中“帧调度”职责的边界，为后续拆分 `VideoRender` 和接入 `IVideoFrameSink` 做准备。

当前目标不是直接新增 `VideoFrameScheduler` 类，也不是修改播放代码，而是先把调度模型设计清楚：

```text
RenderFrame()
    -> 调度职责
        -> 取帧 / 看队首帧
        -> 同步判断
        -> 等待 / 丢帧 / 交出可显示帧
        -> 安排下一轮调度
    -> 绘制职责
        -> 接收当前可显示帧
        -> 上传纹理
        -> OpenGL 绘制
```

后续如果要让 `MultiViewRenderer` 接收真实视频帧，必须先保证它接收到的是“已经由单路调度器判断可显示的帧”，而不是直接读取 `VFrame_` 队列。

## 当前 RenderFrame 状态机

当前 `RenderFrame()` 是一个自调度状态机。每次进入函数时，根据运行状态、队列状态、同步类型和帧 PTS 决定下一步。

### 1. 停止或未初始化

入口条件：

```text
!m_running || !m_frameQueue || !m_syncManager
```

行为：

```text
直接返回，不调度下一轮
```

语义：

- 当前渲染器没有处于运行态。
- 或者帧队列 / 同步管理器尚未准备好。
- 这是调度停止状态。

### 2. 队列暂无可检查帧

当前逻辑：

```text
m_frameQueue->Front(nextFrame) 失败
```

行为：

```text
ScheduleNextFrame(scaledInterval)
```

语义：

- 队列暂时没有帧。
- 不应阻塞 UI 线程。
- 按当前播放速度缩放后的帧间隔重新轮询。

### 3. AUDIO sync 下帧过早

当前判断：

```text
videoPts = nextFrame->pts * videoTimeBase * 1000
audioPts = syncManager->GetClock() - 20
diff = videoPts - audioPts

diff > SYNC_THRESHOLD_MS
```

行为：

```text
waitMs = min(round(diff), 30)
ScheduleNextFrame(waitMs)
```

语义：

- 视频帧早于音频主时钟。
- 不能弹出队列。
- 需要等待后重新判断同一队首帧。

调度器输出应表达为：

```text
Wait(waitMs)
```

### 4. AUDIO sync 下帧过晚

当前判断：

```text
diff < -SYNC_THRESHOLD_MS
```

行为：

```text
TryPop(drop)
av_frame_free(&drop)
ScheduleNextFrame(0)
```

语义：

- 视频帧落后于音频主时钟。
- 该帧已经不适合显示。
- 调度器应弹出并释放该帧，然后立即进入下一轮，尝试追赶主时钟。

调度器输出应表达为：

```text
DropFrame
NextImmediately
```

### 5. 可显示帧

条件：

```text
非 AUDIO sync
或 AUDIO sync 下 diff 在同步窗口内
```

行为：

```text
TryPop(newFrame)
替换 m_displayFrame
update()
ScheduleNextFrame(scaledInterval)
```

语义：

- 该帧已经通过调度判断，可以交给显示端。
- 从队列弹出后，当前代码由 `VideoRender` 接管 `AVFrame*` 所有权。
- 后续 sink 边界建立后，这一步应变成“调度器交出可显示帧给 sink”。

调度器输出应表达为：

```text
PresentFrame(frame)
NextAfter(scaledInterval)
```

### 6. VIDEO sync 下刷新视频时钟

当前逻辑：

```text
if syncType == VIDEO && pts 有效:
    syncManager->RefreshClock(videoPts)
```

语义：

- 没有音频主时钟时，视频显示本身推动主时钟。
- 这属于同步调度职责，不属于 OpenGL 绘制职责。

后续即使 `VideoRender` 不再直接绘制，也不应该把这部分放进 `MultiViewRenderer`。

## 帧调度器输入

如果后续引入 `VideoFrameScheduler` 或等价内部调度边界，它至少需要以下输入。

### VFrame_

类型：

```cpp
std::shared_ptr<SyncQueue<AVFrame*>>
```

用途：

- 查看队首帧。
- 弹出过晚帧。
- 弹出可显示帧。

边界要求：

- 调度器可以访问队列。
- `MultiViewRenderer` 不应直接访问队列。

### syncmanager

类型：

```cpp
std::shared_ptr<syncmanager>
```

用途：

- 获取当前同步类型。
- 获取音频或视频主时钟。
- VIDEO sync 下刷新视频时钟。

边界要求：

- 同步判断属于单路播放语义。
- 不应进入 `MultiViewRenderer`。

### frameRate

用途：

- 计算基础帧间隔。

当前逻辑：

```text
frameIntervalMs = round(1000 / frameRate)
```

边界要求：

- 帧率异常时仍需要兜底默认值。

### videoTimeBase

用途：

- 将 `AVFrame::pts` 转换成毫秒。

当前逻辑：

```text
videoPtsMs = frame->pts * videoTimeBase * 1000
```

边界要求：

- 不应直接从 `AVFrame` 自身推断 time_base。
- 继续使用媒体源解析出的 `videoTimeBase`。

### playbackSpeed

用途：

- 影响下一轮调度间隔。

当前逻辑：

```text
scaledInterval = max(1, round(frameIntervalMs / speed))
```

边界要求：

- 调度器只消费已经校验过的速度值。
- 非法速度仍由上层或设置入口拒绝。

## 帧调度器输出

帧调度器的输出不应该是 OpenGL 操作，而应该是可被上层执行的调度结果。

推荐抽象为以下几类结果。

### Stop

含义：

```text
调度停止，不安排下一轮
```

触发条件：

- `m_running == false`
- 缺少必要输入

### Wait

含义：

```text
当前帧过早，等待指定毫秒后重试
```

携带数据：

```text
delayMs
```

注意：

- 不弹出队列。
- 不转移 `AVFrame*` 所有权。

### DropFrame

含义：

```text
当前帧过晚，丢弃后立即重试
```

职责：

- 调度器弹出过晚帧。
- 由弹出方释放该帧。

注意：

- 不交给 sink。
- 不触发 OpenGL 绘制。

### PresentFrame

含义：

```text
当前帧可显示，弹出并交给 sink
```

携带数据：

```text
AVFrame* frame
int nextDelayMs
```

所有权要求：

- 后续接口必须明确 `PresentFrame` 是否转移 `AVFrame*` 所有权。
- 当前推荐方向是：调度器弹出可显示帧后，把所有权转移给 sink。

### NextAfter

含义：

```text
本轮处理完成，按指定延迟进入下一轮
```

用途：

- 正常显示后按帧间隔调度。
- 队列为空时按帧间隔轮询。

## AUDIO sync 职责归属

AUDIO sync 下，音频时钟是主时钟。

调度器负责：

- 读取视频帧 PTS。
- 读取音频主时钟。
- 计算 diff。
- 判断等待、丢帧或显示。
- 丢弃过晚帧。
- 安排下一轮调度。

sink 负责：

- 接收可显示帧。
- 保存当前显示帧。
- 上传纹理。
- 绘制。

`MultiViewRenderer` 后续只应位于 sink 侧，不应知道 audio clock。

## VIDEO sync 职责归属

VIDEO sync 下，视频显示推进主时钟。

调度器负责：

- 判断当前帧可显示。
- 在帧被接受显示后刷新 video clock。
- 安排下一轮调度。

sink 负责：

- 接收可显示帧。
- 显示该帧。

需要注意：如果后续 sink 的实际绘制是异步的，必须重新确认“交给 sink”与“真正绘制完成”之间的时钟刷新时机。第一阶段可以保持当前语义：帧被调度器接受并交给 sink 后刷新视频时钟。

## 调度器不做的事情

帧调度边界必须明确排除以下职责。

### 不做 OpenGL 上传

调度器不应该调用：

```text
UploadTexture()
DrawFrame()
glDrawArrays()
QOpenGLTexture::setData()
```

原因：

- OpenGL 操作依赖 `QOpenGLWidget` 上下文。
- 调度器应该可被 `VideoRender` 和 `MultiViewRenderer` 复用。
- 如果调度器持有 OpenGL 资源，会重新把播放调度和渲染实现耦合在一起。

### 不持有 QWidget

调度器不应该依赖：

```text
QWidget*
QOpenGLWidget*
rendererContainer
```

原因：

- QWidget 属于 UI 显示载体。
- 帧调度属于单路播放时序。
- 两者耦合会阻碍后续统一渲染。

### 不参与 active audio 策略

调度器只关注当前 pipeline 的同步类型和主时钟。

它不应该决定：

- 哪一路是 active stream。
- 哪一路是 active audio stream。
- 多路中哪一路有声音。

这些仍属于 `MultiStreamManager` / `MediaPlayController` 的策略。

### 不管理 StreamId

调度器属于单路 pipeline 附近的职责，不应该持有 `StreamId`。

如果后续 `MultiViewRenderer` 需要知道某帧属于哪一路，应由 Manager / Controller / 渲染适配层在外部建立绑定关系。

## 调度器与 sink 的调用关系

推荐关系：

```text
VideoFrameScheduler
    -> 从 VFrame_ 取帧
    -> 根据 syncmanager 判断
    -> 输出 PresentFrame(frame)

IVideoFrameSink
    -> 接收可显示帧
    -> 持有 current frame
    -> 清理旧 frame
    -> 请求绘制
```

当前 `VideoRender` 中这两部分还在同一个类里：

```text
RenderFrame()
    -> 调度
    -> 替换 m_displayFrame
    -> update()

paintGL()
    -> UploadTexture()
    -> DrawFrame()
```

后续拆分时，第一步可以不立刻新增独立类，而是先把 `RenderFrame()` 内部拆成更清晰的小函数：

```text
RenderFrame()
    -> ComputeNextFrameAction()
    -> HandleDropFrame()
    -> PresentFrameToCurrentRenderer()
    -> ScheduleNextFrame()
```

等行为稳定后，再判断是否值得抽成独立 `VideoFrameScheduler`。

## 后续拆分建议

后续 3.7.7 进行代码拆分时，建议按以下顺序。

### 第一步：只拆函数，不新增类

目标：

- 降低风险。
- 保持 `VideoRender` 对外接口不变。
- 保持 `MediaPipeline` 不变。

建议拆出：

- 队列空时的调度处理。
- AUDIO sync 判断。
- 过晚帧丢弃。
- 可显示帧接管。
- VIDEO sync 刷新时钟。

### 第二步：明确可显示帧交付点

把当前逻辑：

```text
m_displayFrame = newFrame
update()
```

抽象成一个内部函数：

```text
PresentFrame(newFrame)
```

该函数后续可以自然对齐 `IVideoFrameSink::PresentFrame()`。

### 第三步：再引入接口

只有当内部函数边界稳定后，再新增 `IVideoFrameSink`。

这样可以避免一次性同时改：

- 调度逻辑
- 接口
- 所有权
- OpenGL 绘制
- `MediaPipeline`

## 风险与约束

需要重点控制以下风险：

- 过早弹出队首帧会导致帧过早时无法重新判断。
- 丢帧后未释放会造成内存泄漏。
- 可显示帧所有权不清会导致重复释放。
- VIDEO sync 下刷新时钟时机变化会影响无音频视频同步。
- 如果调度器依赖 QWidget，后续无法复用到 `MultiViewRenderer`。
- 如果 `MultiViewRenderer` 直接调度多路帧，会破坏播放管线边界。

## 设计结论

视频帧调度边界应定义为：

```text
读取 VFrame_
根据 syncmanager 和 videoTimeBase 判断帧早晚
决定等待、丢弃或交出可显示帧
安排下一次调度
```

它不应该负责：

```text
OpenGL 上传
OpenGL 绘制
QWidget 管理
active stream / active audio stream 策略
StreamId 管理
```

下一步可以进入 `IVideoFrameSink` 接口草案设计，明确可显示帧交给 sink 后的所有权、清理和调用线程语义。
