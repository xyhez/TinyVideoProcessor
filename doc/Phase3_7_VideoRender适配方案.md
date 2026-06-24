# Phase 3.7 VideoRender 适配方案

## 目标

本方案用于明确当前 `VideoRender` 如何在不破坏既有播放路径的前提下，逐步适配 Phase 3.7 已经设计好的“帧调度边界”和 `IVideoFrameSink` 语义。

本阶段只做方案设计，不修改代码。

## 当前稳定基线

当前播放链路仍然是：

```text
MediaPlayController
    -> MultiStreamManager
        -> MediaPipeline
            -> VideoRender
                -> QWidget / QOpenGLWidget
```

其中 `MediaPipeline` 在初始化时创建 `VideoRender`：

```text
MediaPipeline::Initialize(mediaSource, rendererContainer)
    -> CreatePipelineResources(...)
        -> new VideoRender(rendererContainer)
        -> VideoRender::InitVideoRender(VFrame_, syncManager_, frameRate, videoTimeBase)
```

这个路径已经支撑了：

- 单路文件播放。
- 多路文件播放。
- 无音频流文件播放。
- 摄像头源接入后的单路或多路预览路径。

因此第一阶段必须保留当前路径作为稳定回退基线。

## 当前 VideoRender 的职责

当前 `VideoRender` 同时承担四类职责：

1. 从 `VFrame_` 查看和弹出视频帧。
2. 根据 `SyncManager` 判断视频帧是等待、丢弃还是显示。
3. 持有当前显示帧 `m_displayFrame` 并负责释放。
4. 在 `QOpenGLWidget` 上完成纹理上传和绘制。

后续适配的目标不是立刻删除这些职责，而是先把职责边界显式化。

## 适配原则

### 1. 不先改 MediaPipeline 初始化签名

暂时保持：

```cpp
bool Initialize(std::shared_ptr<IMediaSource> mediaSource,
                QWidget* rendererContainer);
```

不改成传入 `IVideoFrameSink`，也不让 `MediaPipeline` 接收外部渲染器对象。

原因：

- 当前多路文件和摄像头预览都依赖这条初始化路径。
- 直接改签名会影响 `MediaPlayController`、`MultiStreamManager` 和 UI 容器绑定。
- `IVideoFrameSink` 接口还没有落地，不应提前扩大调用链影响。

### 2. VideoRender 先作为默认 sink 语义载体

第一阶段不需要让 `VideoRender` 立刻继承 `IVideoFrameSink`。

可以先在 `VideoRender` 内部形成等价语义：

```text
PresentFrame(frame)
    -> 释放旧 m_displayFrame
    -> 接管新 frame 所有权
    -> 更新宽高和 SAR
    -> 标记已有画面
    -> update()

ClearFrame()
    -> 释放 m_displayFrame
    -> 清空宽高和状态
    -> update()
```

等这两个语义在当前路径稳定后，再把它们提升为正式 `IVideoFrameSink` 接口实现。

### 3. 调度边界先用私有方法拆分

3.7.7 的第一步不建议新增独立 `VideoFrameScheduler` 类。

推荐先把 `RenderFrame()` 内部逻辑拆成私有方法：

```text
RenderFrame()
    -> CanRenderFrame()
    -> CalculateScaledFrameInterval()
    -> PeekNextFrame()
    -> EvaluateFrameTiming()
    -> DropLateFrame()
    -> PopDisplayableFrame()
    -> PresentFrame()
    -> RefreshVideoClockIfNeeded()
    -> ScheduleNextFrame()
```

这样可以先降低 `RenderFrame()` 复杂度，同时保持：

- `VideoRender` 对外接口不变。
- `MediaPipeline` 不变。
- `MultiViewRenderer` 不接真实帧。
- 当前播放行为不变。

### 4. 不让 MultiViewRenderer 直接消费 VFrame_

`MultiViewRenderer` 后续只接收已经调度好的可显示帧。

本阶段不允许出现：

```text
MultiViewRenderer
    -> VFrame_
    -> SyncManager
    -> demuxer / decoder
```

否则会把多路统一绘制器变成播放管线的一部分，破坏当前架构分层。

## 推荐拆分顺序

### 第一步：只拆 VideoRender 私有方法

修改范围：

- `src/infrastructure/videorender.h`
- `src/infrastructure/videorender.cpp`
- `doc/TODO.md`

不修改：

- `MediaPipeline`
- `MultiStreamManager`
- `MediaPlayController`
- `MultiViewRenderer`
- `CMakeLists.txt`

拆分重点：

- 保留 `RenderFrame()` 作为自调度入口。
- 把队列为空的处理拆出。
- 把 AUDIO sync 判断拆出。
- 把晚帧丢弃拆出。
- 把可显示帧弹出和接管拆出。
- 把 VIDEO sync 时钟刷新拆出。
- 把显示帧替换收敛到 `PresentFrame()` 私有方法。

### 第二步：补出 ClearFrame 私有语义

在不改外部接口的前提下，为 `VideoRender` 增加内部清理当前显示帧的语义。

它应该只负责当前显示帧，不替代：

- `MediaPipeline::ClearQueues()`
- decoder flush
- demuxer seek
- sync clock reset

后续 seek / stop / shutdown 是否调用该语义，应在单独任务中根据调用链再处理。

### 第三步：落地 IVideoFrameSink

只有当 `PresentFrame()` 和 `ClearFrame()` 在当前路径内稳定后，才新增：

```text
src/infrastructure/ivideoframesink.h
```

并让 `VideoRender` 适配该接口。

这一阶段仍然不需要把 `MultiViewRenderer` 接入真实帧。

### 第四步：规划 MultiViewRenderer 单路真实帧接入

`MultiViewRenderer` 真实帧接入应在 `VideoRender` 适配完成后单独规划。

推荐方向是增加单路 sink adapter：

```text
SingleStreamFrameSinkAdapter
    -> 持有 StreamId
    -> 实现 IVideoFrameSink
    -> PresentFrame(frame)
        -> MultiViewRenderer::PresentFrame(streamId, frame)
```

这样可以保证：

- `MediaPipeline` 不知道 `StreamId`。
- 单路调度逻辑不关心多路布局。
- `MultiViewRenderer` 只负责多路绘制状态。

## 3.7.7 首次代码修改建议

3.7.7 应该只做最小结构拆分。

建议新增或整理的 `VideoRender` 私有方法：

```cpp
bool CanRenderFrame() const;
int CalculateScaledFrameInterval(double speed) const;
bool PeekNextFrame(AVFrame*& frame);
FrameTimingDecision EvaluateFrameTiming(const AVFrame* frame) const;
bool DropLateFrame();
bool PopDisplayableFrame(AVFrame*& frame);
void PresentFrame(AVFrame* frame);
void RefreshVideoClockIfNeeded(const AVFrame* frame, SYNCTYPE syncType);
```

其中 `FrameTimingDecision` 可以先不做成复杂类。第一版可以使用私有枚举表达：

```cpp
enum class FrameTimingDecision {
    Display,
    Wait,
    Drop
};
```

如果为了减少改动，也可以先不引入枚举，直接拆出更小的布尔判断函数。但从可读性看，私有枚举更利于表达调度结果。

## 暂时不能动的接口

3.7.7 前后暂时不应修改：

- `MediaPipeline::Initialize(...)`
- `VideoRender::InitVideoRender(...)`
- `MediaPlayController::AddStream(...)`
- `MultiStreamManager::AddStream(...)`
- `MultiViewRenderer` 的真实帧入口

原因是这些接口处在多路播放、UI 容器绑定和摄像头接入路径的交界处。当前阶段目标只是拆清 `VideoRender` 内部边界，不能把影响扩大到跨模块调用链。

## 回归验证范围

虽然本方案文档不运行程序，但后续 3.7.7 代码拆分完成后，建议至少人工验证：

1. 普通有音频 MP4 播放、暂停、继续、seek、stop、关闭。
2. 无音频流视频播放、切换 active stream、关闭。
3. 多路 2x2 文件播放、切换 active stream、切换 active audio stream、关闭。
4. 摄像头源预览、暂停、stop all、关闭。
5. 播放结束后关闭窗口。

如果这些场景有任意一个退化，应优先回退 3.7.7 的代码拆分，而不是继续推进 `IVideoFrameSink`。

## 风险与控制

### 风险 1：拆分 RenderFrame 后改变调度时序

控制方式：

- 保持 `QTimer::singleShot()` 调用位置不变。
- 保持 early frame wait、late frame drop、display frame 的分支顺序不变。
- 第一版不改变 `SYNC_THRESHOLD_MS`、frame interval 和 speed 计算。

### 风险 2：AVFrame 所有权转移不清

控制方式：

- 从队列 `TryPop()` 成功后必须只有一个释放责任方。
- late frame 由丢帧逻辑释放。
- displayable frame 交给 `PresentFrame()` 后由 `VideoRender` current frame 语义释放。
- `PresentFrame()` 内部替换旧帧时释放旧帧。

### 风险 3：提前引入接口导致改动过大

控制方式：

- 3.7.7 不新增 `IVideoFrameSink`。
- 3.7.7 不改 `CMakeLists.txt`。
- 3.7.7 只拆 `VideoRender` 内部结构。

### 风险 4：误把 MultiViewRenderer 做成播放管线

控制方式：

- `MultiViewRenderer` 不接 `VFrame_`。
- `MultiViewRenderer` 不接 `SyncManager`。
- `MultiViewRenderer` 不管理 decoder/demuxer。
- `MultiViewRenderer` 后续只接可显示帧。

## 结论

当前最稳妥的适配路线是：

```text
先保留当前 VideoRender 播放路径
    -> 在 VideoRender 内部拆清帧调度和显示帧接管
    -> 形成 PresentFrame / ClearFrame 私有语义
    -> 再落地 IVideoFrameSink
    -> 最后规划 MultiViewRenderer 单路真实帧接入
```

这条路线符合当前架构指导：`MediaPipeline` 继续保持单路职责，`StreamId` 留在多路管理层，`MultiViewRenderer` 不进入播放管线，只作为后续统一绘制器。
