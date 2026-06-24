# Phase 3.7 视频帧输出边界实现报告

## 阶段目标

Phase 3.7 的目标不是直接把真实视频帧接入 `MultiViewRenderer`，而是在真实接入前先拆清视频帧输出边界。

本阶段要解决的问题是：

- 当前 `VideoRender` 同时负责取帧、同步判断、帧持有和 OpenGL 绘制，职责过重。
- `MultiViewRenderer` 后续要统一多路绘制，但不能直接消费 `VFrame_`。
- `AVFrame*` 所有权和清理顺序必须先明确，否则 seek / stop / remove stream / shutdown 容易出现旧帧回显、重复释放或泄漏。
- `StreamId` 必须保留在多路管理和渲染适配层，不能进入单路 `MediaPipeline`。

阶段原则：

```text
MediaPipeline
    -> 保持单路职责
    -> 不感知 StreamId
    -> 不直接依赖 MultiViewRenderer 多路语义

VideoRender
    -> 继续作为当前稳定显示路径
    -> 先拆清内部帧调度边界
    -> 作为第一版 IVideoFrameSink 默认实现

MultiViewRenderer
    -> 继续保持统一多画面渲染器定位
    -> 不取帧
    -> 不做同步
    -> 不管理 demuxer / decoder / audio player
```

## 完成内容

### 3.7.1 视频帧输出边界预研

已完成文档：

- `doc/Phase3_7_视频帧输出边界预研报告.md`

结论：

- 不应让 `MultiViewRenderer` 直接读取 `VFrame_`。
- 不应让 `MultiViewRenderer` 负责同步判断。
- 推荐通过“可显示帧消费者”边界向后迁移。
- 真实帧接入前必须先设计 `AVFrame*` 所有权和清理协议。

### 3.7.2 VideoRender 职责标注与边界确认

已完成代码注释标注：

- `src/infrastructure/videorender.h`
- `src/infrastructure/videorender.cpp`

标注内容：

- `RenderFrame()` 中的取帧、同步判断、丢帧、显示、下一轮调度阶段。
- `m_displayFrame` 的所有权语义。
- `UploadTexture()` / `DrawFrame()` 属于 OpenGL 绘制职责。
- `ScheduleNextFrame()` 属于帧调度职责。

本步骤只增加职责说明，不改变播放行为。

### 3.7.3 帧调度边界设计

已完成文档：

- `doc/Phase3_7_视频帧调度边界设计.md`

结论：

```text
帧调度职责
    -> 从 VFrame_ 查看 / 弹出帧
    -> 根据 SyncManager 判断等待、丢弃或显示
    -> 释放过晚帧
    -> 将可显示帧交给 sink
    -> 安排下一轮调度

绘制职责
    -> 持有 current frame
    -> 上传纹理
    -> 绘制 viewport
```

调度职责后续可以抽成 `VideoFrameScheduler` 或等价对象，但本阶段先在 `VideoRender` 内部拆清。

### 3.7.4 IVideoFrameSink 接口草案

已完成文档：

- `doc/Phase3_7_IVideoFrameSink接口草案.md`

确定第一版接口语义：

```cpp
class IVideoFrameSink {
public:
    virtual ~IVideoFrameSink() = default;
    virtual void PresentFrame(AVFrame* frame) = 0;
    virtual void ClearFrame() = 0;
};
```

关键约定：

- `PresentFrame(frame)` 接收的是已经完成调度判断的可显示帧。
- 调用 `PresentFrame(frame)` 后，`AVFrame*` 所有权转移给 sink。
- `ClearFrame()` 只清理当前显示帧，不清队列、不 flush decoder、不重置时钟。
- 第一版 `IVideoFrameSink` 是单路接口，不包含 `StreamId`。

### 3.7.5 帧所有权与清理协议设计

已完成文档：

- `doc/Phase3_7_帧所有权与清理协议.md`

关键规则：

```text
从 VFrame_ 弹出的 AVFrame*
    -> 如果是过晚帧，由调度逻辑释放
    -> 如果是可显示帧，通过 PresentFrame 转移给 sink

sink current frame
    -> 替换时释放旧帧
    -> ClearFrame 时释放当前帧
    -> 析构时释放当前帧
```

seek / stop / remove stream / shutdown 不能混用：

- `ClearQueues()` 只清队列。
- `FlushDecoders()` 只刷新解码器。
- `ClearFrame()` 只清当前显示帧。

三者职责不同，不能互相替代。

### 3.7.6 当前 VideoRender 路径适配方案

已完成文档：

- `doc/Phase3_7_VideoRender适配方案.md`

确定迁移路线：

```text
先保留当前 VideoRender 播放路径
    -> 在 VideoRender 内部拆清帧调度和显示帧接管
    -> 形成 PresentFrame / ClearFrame 语义
    -> 再落地 IVideoFrameSink
    -> 最后规划 MultiViewRenderer 单路真实帧接入
```

阶段约束：

- 暂不修改 `MediaPipeline::Initialize(...)`。
- 暂不让 `MediaPipeline` 接收 `StreamId`。
- 暂不让 `MultiViewRenderer` 接真实帧。
- 当前多 `VideoRender` 路径继续作为稳定回退。

### 3.7.7 VideoRender 内部帧调度边界最小拆分

已完成代码修改：

- `src/infrastructure/videorender.h`
- `src/infrastructure/videorender.cpp`

拆出的私有边界：

```cpp
bool CanRenderFrame() const;
int CalculateScaledFrameInterval(double speed) const;
bool PeekNextFrame(AVFrame*& frame);
FrameTimingDecision EvaluateFrameTiming(const AVFrame* frame, SYNCTYPE syncType, int& waitMs) const;
bool DropLateFrame();
bool PopDisplayableFrame(AVFrame*& frame);
void PresentFrame(AVFrame* frame);
void RefreshVideoClockIfNeeded(const AVFrame* frame, SYNCTYPE syncType);
```

实际效果：

- `RenderFrame()` 仍是自调度入口。
- 取帧、同步判断、过晚帧释放、可显示帧弹出、显示帧接管、视频时钟刷新已经拆成独立方法。
- `UploadTexture()` / `DrawFrame()` 未变。
- 未修改 `MediaPipeline`。
- 未修改 `MultiViewRenderer`。

### 3.7.8 IVideoFrameSink 最小接口落地

已完成代码修改：

- `src/infrastructure/ivideoframesink.h`
- `src/infrastructure/videorender.h`
- `src/infrastructure/videorender.cpp`
- `CMakeLists.txt`

当前代码状态：

```cpp
class VideoRender : public QOpenGLWidget,
                    public IVideoFrameSink,
                    protected QOpenGLFunctions
```

`VideoRender` 现在是第一版默认 frame sink：

- `PresentFrame(AVFrame* frame)`：接管可显示帧并请求重绘。
- `ClearFrame()`：清理当前显示帧。

接口边界保持：

- `VideoRender::InitVideoRender(...)` 不变。
- `MediaPipeline` 不感知 `IVideoFrameSink`。
- `MultiViewRenderer` 未接真实帧。
- `StreamId` 未进入 `MediaPipeline`。

### 3.7.9 MultiViewRenderer 单路真实帧接入计划

已完成文档：

- `doc/Phase3_7_MultiViewRenderer单路真实帧接入计划.md`

确定后续接入方向：

```text
MediaPipeline
    -> 单路帧调度
        -> IVideoFrameSink
            -> SingleStreamFrameSinkAdapter(streamId)
                -> MultiViewRenderer::PresentFrame(streamId, frame)
```

关键结论：

- `MultiViewRenderer` 不直接实现第一版单路 `IVideoFrameSink`。
- 后续通过 `SingleStreamFrameSinkAdapter` 保持单路 sink 语义。
- `MultiViewRenderer` 需要多路入口：`PresentFrame(streamId, frame)`、`ClearFrame(streamId)`、`DetachStream(streamId)`。
- 第一版真实帧接入只接一路到 slot 0。
- OpenGL 纹理创建、释放、上传必须在 `MultiViewRenderer` 上下文线程执行。

## 当前架构状态

### 当前可运行路径

当前默认播放路径仍然是：

```text
MediaPlayController
    -> MultiStreamManager
        -> MediaPipeline
            -> VideoRender
                -> IVideoFrameSink::PresentFrame(frame)
                -> QOpenGLWidget paintGL()
```

这条路径仍是当前稳定基线。

### 后续目标路径

后续目标路径是：

```text
MediaPlayController
    -> MultiStreamManager
        -> MediaPipeline #1
            -> VideoFrameScheduler #1
                -> IVideoFrameSink
                    -> SingleStreamFrameSinkAdapter(streamId=1)
                        -> MultiViewRenderer::PresentFrame(streamId=1, frame)

        -> MediaPipeline #2
            -> VideoFrameScheduler #2
                -> IVideoFrameSink
                    -> SingleStreamFrameSinkAdapter(streamId=2)
                        -> MultiViewRenderer::PresentFrame(streamId=2, frame)
```

这个目标路径还没有实现，只是 Phase 3.8 的设计方向。

## 明确保留的边界

### MediaPipeline

`MediaPipeline` 保持单路职责：

- 管理单路 demuxer。
- 管理单路 decoder。
- 管理单路 audio player。
- 管理单路 sync manager。
- 管理单路 video frame queue。

不应承担：

- 多路 `StreamId`。
- slot / viewport。
- active stream。
- active audio stream。
- 多路统一渲染器状态。

### IVideoFrameSink

`IVideoFrameSink` 是单路可显示帧出口。

它只负责：

- 接收可显示帧。
- 接管帧所有权。
- 清理当前显示帧。

它不负责：

- 从队列取帧。
- 音视频同步。
- 丢帧策略。
- 多路布局。
- OpenGL 具体绘制策略。

### MultiViewRenderer

`MultiViewRenderer` 后续只负责统一绘制：

- 根据 `streamId` 找到 slot。
- 根据 slot 找到 viewport。
- 持有每路 current frame / texture 状态。
- 在 OpenGL 上下文线程上传纹理。
- 在 `paintGL()` 中绘制到对应 viewport。

它不负责：

- 取帧。
- 同步判断。
- 解码。
- demux。
- 音频策略。
- `MediaPipeline` 生命周期。

## 未完成内容

Phase 3.7 没有做以下内容：

- 没有实现 `VideoFrameScheduler` 独立类。
- 没有让 `MediaPipeline` 接收外部 `IVideoFrameSink`。
- 没有实现 `SingleStreamFrameSinkAdapter`。
- 没有让 `MultiViewRenderer` 接收真实 `AVFrame*`。
- 没有实现 `MultiViewRenderer` 的 YUV 纹理上传。
- 没有实现 `MultiViewRenderer::PresentFrame(streamId, frame)`。
- 没有实现 `MultiViewRenderer::ClearFrame(streamId)`。
- 没有实现 `MultiViewRenderer::DetachStream(streamId)`。
- 没有替换当前多个 `VideoRender` 播放路径。

这些内容应进入 Phase 3.8 继续拆分。

## 风险记录

### 1. OpenGL 线程风险

`QOpenGLWidget` 的纹理创建、释放和上传必须在 OpenGL 上下文线程执行。

后续如果 `SingleStreamFrameSinkAdapter::PresentFrame(frame)` 从非 UI 线程调用，必须通过 queued invocation 转到 `MultiViewRenderer` 所在线程。

同时必须解决 queued 调用失败或 renderer 已销毁时 `AVFrame*` 的释放问题。

### 2. 所有权转移风险

`PresentFrame(frame)` 后 frame 所有权转移给 sink。

后续如果跨线程投递 frame，需要明确：

- 投递成功时谁负责释放。
- 投递失败时谁负责释放。
- renderer 已销毁时谁负责释放。
- shutdown 时如何禁止旧 queued frame 访问已释放对象。

### 3. 回退路径风险

当前 `VideoRender` 路径必须继续保留，直到 `MultiViewRenderer` 单路真实帧接入稳定。

如果后续实验路径出现黑屏、seek 旧帧回显、关闭崩溃、纹理泄漏，应优先切回 `VideoRender` 路径定位。

### 4. 架构边界风险

真实接入时最容易出错的是把 `StreamId` 塞进 `MediaPipeline`，或者让 `MultiViewRenderer` 直接访问 `VFrame_`。

这两种方式都会破坏 Phase 3 的核心架构边界。

## Phase 3.8 建议拆分

建议下一阶段命名为：

```text
Phase 3.8 MultiViewRenderer 单路真实帧接入
```

推荐任务拆分：

1. **3.8.1 单路真实帧接入 TODO 拆分**
   - 新增 Phase 3.8 任务计划。
   - 明确每一步修改文件、验收标准和回退方式。

2. **3.8.2 MultiViewRenderer 帧状态模型设计**
   - 设计 `StreamRenderState`。
   - 明确 current frame、texture、dirty flag、尺寸和 SAR。
   - 只写文档或最小空结构，不接帧。

3. **3.8.3 SingleStreamFrameSinkAdapter 空壳**
   - 新增 adapter 类。
   - 实现 `IVideoFrameSink`。
   - 暂不连接真实 pipeline。

4. **3.8.4 MultiViewRenderer 单路接帧接口空壳**
   - 增加 `PresentFrame(streamId, frame)`、`ClearFrame(streamId)`、`DetachStream(streamId)`。
   - 先只接管和释放 frame，不做纹理上传。

5. **3.8.5 slot 0 单路帧绘制**
   - 把一路 frame 上传为纹理。
   - 绘制到 slot 0。
   - 只验证一路文件视频。

6. **3.8.6 实验路径接入**
   - 让一条 pipeline 可选择使用 adapter + `MultiViewRenderer`。
   - 保留当前 `VideoRender` 默认路径。

7. **3.8.7 单路接入验收报告**
   - 记录普通文件、无音频文件、seek、stop、关闭程序的验证结果。
   - 决定是否进入多路真实帧扩展。

## 结论

Phase 3.7 已经完成“真实帧接入前的边界准备”。

当前最重要的成果不是新增渲染能力，而是建立了稳定的架构约束：

```text
调度归调度
帧消费归 sink
统一绘制归 MultiViewRenderer
多路身份归 StreamId / adapter
单路播放管线不感知多路渲染细节
```

下一阶段可以进入 Phase 3.8，但必须先拆 TODO，再实现单路真实帧接入，不能直接把多路真实帧一次性塞进 `MultiViewRenderer`。
