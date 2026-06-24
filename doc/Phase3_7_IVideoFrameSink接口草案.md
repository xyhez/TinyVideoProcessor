# Phase 3.7 IVideoFrameSink 接口草案

## 阶段目标

本设计文档用于定义“可显示视频帧消费者”的接口语义，为后续从当前 `VideoRender` 路径迁移到 `MultiViewRenderer` 真实帧接入做准备。

本阶段只做接口草案，不新增代码文件，不修改 `VideoRender`，不修改 `MediaPipeline`。

核心目标是回答四个问题：

```text
1. 调度器把什么样的帧交给 sink？
2. sink 接到 AVFrame* 后是否拥有所有权？
3. seek / stop / remove stream / shutdown 时如何清理当前帧？
4. VideoRender 和 MultiViewRenderer 如何共用同一套接帧语义？
```

## 接口定位

`IVideoFrameSink` 推荐定位为“可显示帧消费者”。

它消费的不是原始解码队列，也不是 `VFrame_` 本身，而是经过单路帧调度边界判断后的可显示帧：

```text
decoder
    -> VFrame_
    -> VideoFrameScheduler
        -> 判断等待 / 丢帧 / 可显示
        -> IVideoFrameSink::PresentFrame(frame)
```

`IVideoFrameSink` 不负责：

- 从 `VFrame_` 取帧。
- 判断帧早晚。
- 丢弃过晚帧。
- 访问 `syncmanager`。
- seek / stop / pause 的业务语义。
- active stream / active audio stream 策略。
- 管理 `MediaPipeline`。
- 管理 `StreamId`。

`IVideoFrameSink` 负责：

- 接收调度器交出的可显示帧。
- 持有当前显示帧或把帧内容转移到自身渲染状态。
- 在替换当前帧时清理旧帧。
- 在 seek / stop / detach / shutdown 时清理当前帧。
- 触发自身需要的绘制更新。

## 第一版接口草案

第一版建议保持接口尽量小：

```cpp
class IVideoFrameSink {
public:
    virtual ~IVideoFrameSink() = default;

    // 接收一帧已经通过同步调度判断的可显示帧。
    // 调用成功后，frame 所有权转移给 sink。
    virtual void PresentFrame(AVFrame* frame) = 0;

    // 清理 sink 当前持有的显示帧，防止 seek / stop 后旧帧回显。
    virtual void ClearFrame() = 0;
};
```

这只是草案，不是最终代码。后续落地时需要根据 Qt 线程模型和 `VideoRender` 拆分结果决定是否调整命名、返回值和线程投递方式。

## PresentFrame 语义

`PresentFrame()` 表示：

```text
调度器已经确认该帧可以显示，现在把该帧交给 sink。
```

调用前置条件：

- `frame != nullptr`。
- 该帧已经从 `VFrame_` 弹出。
- AUDIO sync 下，该帧位于同步窗口内。
- VIDEO sync 下，该帧已被判定可以推进视频时钟。
- 调度器不再继续持有该帧。

第一版推荐所有权语义：

```text
PresentFrame(frame) 调用后，AVFrame* 所有权转移给 sink。
```

也就是说：

- 调度器不再释放该帧。
- sink 替换当前显示帧时释放旧帧。
- sink 清理当前显示帧时释放当前帧。
- sink 析构时释放仍持有的当前帧。

当前 `VideoRender` 的行为已经接近这个语义：

```text
TryPop(newFrame)
av_frame_free(&m_displayFrame)
m_displayFrame = newFrame
update()
```

后续 `VideoRender` 适配 sink 时，可以将这段逻辑收拢到 `PresentFrame()`。

## PresentFrame 是否需要返回值

第一版建议 `PresentFrame()` 暂不返回成功 / 失败。

原因：

- 当前 `VideoRender` 替换显示帧没有失败分支。
- 如果 sink 暂时无法绘制，也可以先持有帧并等待下一次 `paintGL()`。
- 引入返回值后必须立即设计失败时谁释放 `AVFrame*`，会让第一版接口复杂化。

如果后续确实需要失败返回，必须明确：

```text
PresentFrame(frame) 返回 false 时，frame 由谁释放？
```

在没有充分需求前，不建议加入失败返回。

## ClearFrame 语义

`ClearFrame()` 表示：

```text
清理 sink 当前持有的显示帧和可见画面状态。
```

它应在以下场景被调用：

- seek 前或 seek 生效时。
- stop 时。
- stream detach 前。
- pipeline shutdown 时。
- 解码器 flush 后需要清除旧画面时。

`ClearFrame()` 应负责：

- 释放当前显示帧。
- 将 current frame 指针置空。
- 清除 hasFrame 状态。
- 请求重绘空画面，避免旧帧继续停留。

对当前 `VideoRender` 来说，后续可以对应为：

```text
av_frame_free(&m_displayFrame)
m_hasFrame = false
update()
```

对后续 `MultiViewRenderer` 来说，`ClearFrame()` 可能对应：

```text
释放某一路 current frame
标记该路纹理无效
请求重绘对应 viewport
```

但 `ClearFrame()` 本身不应该执行 seek，也不应该 flush decoder。

## DetachStream 是否属于第一版接口

这里需要区分单路 sink 和多路 sink。

### 单路 VideoRender

当前 `VideoRender` 是单路显示对象，不需要 `StreamId`：

```text
一个 VideoRender
    -> 一个 current frame
    -> 一个 QWidget / QOpenGLWidget
```

对它来说，`ClearFrame()` 已经足够表达清理当前显示帧。

### 多路 MultiViewRenderer

后续 `MultiViewRenderer` 会同时持有多路状态：

```text
StreamId / slot
    -> current frame
    -> texture state
    -> viewport
```

它需要能够清理某一路：

```text
DetachStream(streamId)
```

但如果把 `DetachStream(StreamId)` 放进第一版 `IVideoFrameSink`，会让单路 `VideoRender` 被迫感知多路语义。

第一版建议：

```text
IVideoFrameSink 只定义单路 sink 语义。
MultiViewRenderer 后续通过适配层或扩展接口处理多路 stream detach。
```

也就是说，`DetachStream()` 暂不进入第一版 `IVideoFrameSink`。

后续可选方向：

```cpp
class IMultiVideoFrameSink {
public:
    virtual ~IMultiVideoFrameSink() = default;
    virtual void PresentFrame(StreamId streamId, AVFrame* frame) = 0;
    virtual void ClearFrame(StreamId streamId) = 0;
    virtual void DetachStream(StreamId streamId) = 0;
};
```

但该接口不应让 `MediaPipeline` 直接感知 `StreamId`。`StreamId` 应由 `MultiStreamManager`、Controller 或渲染适配层绑定。

## 调用线程要求

调用线程是接口落地的关键风险点。

当前 `VideoRender::RenderFrame()` 通过 `QTimer::singleShot()` 在 `VideoRender` 所属线程内自调度，通常也就是 UI 线程。因此当前替换 `m_displayFrame` 和调用 `update()` 与 `QOpenGLWidget` 使用方式一致。

后续拆分后需要明确：

```text
PresentFrame() 必须在 sink 所属 QObject 线程调用，
或者通过 Qt queued connection 投递到 sink 所属线程执行。
```

原因：

- `QOpenGLWidget::update()` 应在 UI 对象所属线程调用。
- OpenGL 纹理上传必须发生在拥有 OpenGL context 的绘制阶段或正确的上下文线程。
- 如果调度线程直接操作 sink 内部状态，需要额外同步和生命周期保护。

第一版推荐：

```text
调度器与 sink 保持同线程调用。
不在后台解码线程直接调用 PresentFrame()。
```

后续如果要跨线程，应单独设计：

- 帧投递方式。
- 队列积压策略。
- sink 已销毁后的投递取消。
- 帧所有权在 queued connection 中的转移语义。

## OpenGL 上传边界

`IVideoFrameSink::PresentFrame()` 不应该要求立即上传 OpenGL 纹理。

推荐语义：

```text
PresentFrame()
    -> 接收并保存 current frame
    -> 标记需要重绘
    -> 触发 update()

paintGL()
    -> 在 OpenGL 上下文中上传纹理
    -> 绘制当前帧
```

原因：

- `PresentFrame()` 可能不在 OpenGL context 当前状态下。
- `QOpenGLWidget` 的标准绘制入口是 `paintGL()`。
- 多路 `MultiViewRenderer` 后续也需要在一次 `paintGL()` 中统一上传和绘制多个 viewport。

因此接口只表达“帧交付”，不表达“立即绘制完成”。

## 所有权规则

第一版推荐硬性规则：

```text
谁从 VFrame_ 弹出帧，谁必须保证该帧最终被释放。
如果该帧交给 sink，则 PresentFrame() 后释放责任转移给 sink。
```

具体规则：

- 过晚帧：调度器弹出后立即释放。
- 可显示帧：调度器弹出后调用 `PresentFrame(frame)`。
- `PresentFrame()` 后：sink 负责释放。
- sink 替换当前帧：释放旧 current frame。
- sink `ClearFrame()`：释放 current frame。
- sink 析构：释放 current frame。

暂不使用 shared ownership：

- 不使用 `std::shared_ptr<AVFrame>`。
- 不让多个对象同时拥有同一个裸 `AVFrame*`。
- 不让 `MultiViewRenderer` 和调度器共享同一个 current frame。

后续如果引入 RAII 包装，应作为独立任务处理，不能和 sink 接口落地混在一起。

## VideoRender 作为默认 sink

`VideoRender` 后续可以作为第一版默认 sink：

```text
VideoFrameScheduler
    -> IVideoFrameSink::PresentFrame(frame)
        -> VideoRender
            -> 替换 m_displayFrame
            -> update()
            -> paintGL()
```

这样可以先保证：

- 当前单路播放路径不变。
- 当前多路多个 `VideoRender` 路径不变。
- `MediaPipeline` 仍可按现有方式创建 `VideoRender`。
- `MultiViewRenderer` 空壳路径不受影响。

第一阶段不建议立刻让 `MediaPipeline` 接收外部 sink。可以先在 `VideoRender` 内部把 `PresentFrame()` 语义稳定下来，再决定是否把接口提升到 pipeline 初始化边界。

## MultiViewRenderer 后续接入方式

`MultiViewRenderer` 后续不应直接实现单路 `IVideoFrameSink` 并暴露给每个 `MediaPipeline`，因为它是多路统一渲染器，需要知道某帧属于哪一路。

推荐增加适配层：

```text
MultiStreamManager / Controller
    -> 建立 StreamId 与 pipeline 的绑定
    -> 为每路 pipeline 提供一个单路 sink adapter

SingleStreamFrameSinkAdapter
    -> 实现 IVideoFrameSink
    -> 内部持有 StreamId
    -> PresentFrame(frame)
        -> MultiViewRenderer::PresentFrame(streamId, frame)
```

这样可以保持：

- `MediaPipeline` 不知道 `StreamId`。
- 单路调度器只面对单路 `IVideoFrameSink`。
- `MultiViewRenderer` 可以用 `StreamId` 管理多路纹理状态。

该适配层不是 3.7.4 的实现范围，只是后续接入方向。

## 接口归属层级

`IVideoFrameSink` 应属于基础设施边界还是领域边界，需要按依赖方向判断。

建议第一版放在基础设施层或基础设施抽象附近：

```text
src/infrastructure/ivideoframesink.h
```

原因：

- 接口直接使用 `AVFrame*`。
- `AVFrame` 属于 FFmpeg 具体类型。
- sink 最终服务于渲染基础设施。
- 当前 `VideoRender` 和 `MultiViewRenderer` 都属于基础设施层。

如果未来要让领域层完全隔离 FFmpeg 类型，可以再引入更高层的视频帧封装。但当前阶段不建议增加这层复杂度。

## 暂不做的设计

第一版接口草案暂不处理：

- `std::shared_ptr<AVFrame>`。
- 自定义 `VideoFrame` RAII 包装。
- 跨线程帧队列。
- 多路 sink 接口正式落地。
- `MediaPipeline::Initialize()` 签名修改。
- `MultiViewRenderer` 真实帧接入。
- OpenGL 纹理上传接口。
- HDR / 10-bit / 色彩空间转换。

这些属于后续任务，不能和第一版 sink 语义混在一起。

## 风险点

后续落地时需要重点控制：

- `PresentFrame()` 所有权不明确导致重复释放或泄漏。
- `ClearFrame()` 未触发重绘导致旧帧停留。
- queued connection 投递后 sink 已销毁导致悬空访问。
- `MultiViewRenderer` 多路接入时把 `StreamId` 传回 `MediaPipeline`。
- 在非 OpenGL 上下文线程执行纹理上传。
- 接口过早支持多路，导致单路 `VideoRender` 被迫感知多路概念。

## 草案结论

第一版 `IVideoFrameSink` 推荐保持单路、最小、所有权明确：

```cpp
class IVideoFrameSink {
public:
    virtual ~IVideoFrameSink() = default;
    virtual void PresentFrame(AVFrame* frame) = 0;
    virtual void ClearFrame() = 0;
};
```

核心语义：

```text
PresentFrame 接收已经调度为可显示的帧，并接管 AVFrame* 所有权。
ClearFrame 清理当前显示帧，用于 seek / stop / detach / shutdown。
```

下一步应进入帧所有权与清理协议设计，把 seek、stop、remove stream 和 shutdown 的调用顺序写清楚，再进入代码层最小拆分。
