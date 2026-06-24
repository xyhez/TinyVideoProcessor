# Phase 3.8 MultiViewRenderer 帧状态模型设计

## 目标

本设计用于明确 `MultiViewRenderer` 后续接收真实视频帧时，内部每路 stream 需要保存哪些状态，以及这些状态的生命周期如何管理。

本阶段只做文档设计，不修改代码，不接真实帧。

## 当前前提

当前 `MultiViewRenderer` 已具备：

- layout 管理。
- slot 与 `StreamId` 绑定。
- viewport 计算。
- 空 slot 绘制。
- active stream 边框绘制。
- 点击命中并发出 `StreamClicked(streamId)` 信号。

当前 `MultiViewRenderer` 还不具备：

- current frame 状态。
- OpenGL texture 状态。
- 帧格式诊断。
- `PresentFrame(streamId, frame)`。
- `ClearFrame(streamId)`。
- `DetachStream(streamId)`。
- 真实帧上传和绘制。

因此下一步不能直接写纹理绘制，应先定义清楚渲染状态模型。

## 状态模型总览

推荐在 `MultiViewRenderer` 中维护：

```text
QMap<StreamId, StreamRenderState> renderStates_;
```

其中 `StreamId` 仍然只属于多路管理和渲染适配层，不进入 `MediaPipeline`。

`StreamRenderState` 推荐字段：

```cpp
struct StreamRenderState {
    AVFrame* currentFrame = nullptr;

    int videoWidth = 0;
    int videoHeight = 0;
    AVRational sampleAspectRatio = {1, 1};

    QOpenGLTexture* textureY = nullptr;
    QOpenGLTexture* textureU = nullptr;
    QOpenGLTexture* textureV = nullptr;

    AVPixelFormat pixelFormat = AV_PIX_FMT_NONE;
    bool textureDirty = false;
    bool hasFrame = false;
};
```

第一版只接一路到 slot 0，但状态容器不应写死单路。

原因：

- `MultiViewRenderer` 本质是多路渲染器。
- slot 0 单路只是验收方式。
- 后续扩展多路时可以复用 `streamId -> state` 结构。

## 字段说明

### currentFrame

`currentFrame` 保存当前 stream 最新可显示帧。

所有权规则：

```text
MultiViewRenderer::PresentFrame(streamId, frame)
    -> 接管 frame 所有权
    -> 释放旧 currentFrame
    -> currentFrame = frame
```

`currentFrame` 只能由该 stream 的 `StreamRenderState` 持有。

禁止：

- 调用方在 `PresentFrame(streamId, frame)` 后继续释放 frame。
- 多个 stream state 共享同一个 `AVFrame*`。
- texture 上传后立即释放 currentFrame，除非后续明确改成纹理完全持有画面数据。

第一版建议 current frame 保持到下一帧替换、`ClearFrame(streamId)`、`DetachStream(streamId)` 或 renderer 析构。

### videoWidth / videoHeight

保存当前帧尺寸。

用途：

- 判断纹理尺寸是否需要重建。
- 计算保持宽高比的绘制矩形。
- 输出格式诊断日志。

更新时机：

```text
PresentFrame(streamId, frame)
    -> videoWidth = frame->width
    -> videoHeight = frame->height
```

### sampleAspectRatio

保存当前帧 SAR。

用途：

- 后续计算显示宽高比。
- 与当前 `VideoRender::CalcRect()` 的职责对应。

更新时机：

```text
PresentFrame(streamId, frame)
    -> sampleAspectRatio = frame->sample_aspect_ratio
```

### textureY / textureU / textureV

第一版推荐使用 `QOpenGLTexture*`，而不是原生 OpenGL texture id。

理由：

- 当前 `VideoRender` 已使用 `QOpenGLTexture*`。
- 符合现有代码风格。
- Qt 会封装部分纹理生命周期细节。
- 先保证正确性，后续性能优化再考虑原生 texture id。

限制：

- `QOpenGLTexture` 仍然必须在有效 OpenGL 上下文中创建和释放。
- 不能在非 renderer OpenGL 上下文线程创建或销毁。

### pixelFormat

保存当前帧格式。

第一版只支持：

```text
AV_PIX_FMT_YUV420P
```

用途：

- 判断是否支持上传。
- 避免遇到非 YUV420P 时静默黑屏。

如果后续遇到 NV12、P010、YUVJ420P 等格式，应输出诊断日志，不在本阶段做格式转换。

### textureDirty

`textureDirty` 表示 current frame 已更新，纹理需要在 OpenGL 上下文中重新上传。

状态变化：

```text
PresentFrame(streamId, frame)
    -> textureDirty = true
    -> update()

paintGL()
    -> 如果 textureDirty:
        -> 上传 currentFrame 到 texture
        -> textureDirty = false
```

为什么需要 dirty flag：

- `PresentFrame()` 可能只是接管 `AVFrame*`，不一定处在 OpenGL 上下文中。
- 纹理上传必须延迟到 `paintGL()` 或其他上下文有效位置。
- 避免在接帧阶段直接调用 OpenGL API。

### hasFrame

`hasFrame` 表示当前 state 是否存在可显示帧。

状态变化：

```text
PresentFrame(streamId, frame)
    -> hasFrame = true

ClearFrame(streamId)
    -> hasFrame = false

DetachStream(streamId)
    -> 移除 state
```

`hasFrame` 与 `currentFrame != nullptr` 应保持一致。

## 状态生命周期

### 创建

推荐懒创建：

```text
PresentFrame(streamId, frame)
    -> 如果 renderStates_ 不存在 streamId:
        -> 创建 StreamRenderState
```

不建议在 `SetViewSlots()` 时创建 frame state。

原因：

- slot 绑定只表示显示位置。
- frame state 表示该 stream 已有真实帧状态。
- 二者生命周期不同。

### 替换帧

```text
PresentFrame(streamId, frame)
    -> 找到或创建 state
    -> 释放 state.currentFrame
    -> state.currentFrame = frame
    -> 更新尺寸 / SAR / pixelFormat
    -> state.textureDirty = true
    -> state.hasFrame = true
    -> update()
```

### 清理当前帧

```text
ClearFrame(streamId)
    -> 找到 state
    -> 释放 state.currentFrame
    -> state.currentFrame = nullptr
    -> state.hasFrame = false
    -> state.textureDirty = false
    -> 保留 texture
    -> 保留 state
    -> update()
```

`ClearFrame(streamId)` 用于 seek / stop。

它不移除 stream 状态，也不移除 slot 绑定。

### 移除 stream

```text
DetachStream(streamId)
    -> 找到 state
    -> 释放 state.currentFrame
    -> 在 OpenGL 上下文中释放 textureY/U/V
    -> 从 renderStates_ 移除 state
    -> update()
```

`DetachStream(streamId)` 用于 remove stream / shutdown。

它表示该 stream 不再需要 renderer 保存任何资源。

### 析构

`MultiViewRenderer` 析构时必须释放所有 state：

```text
for state in renderStates_:
    av_frame_free(&state.currentFrame)
    delete textureY
    delete textureU
    delete textureV
renderStates_.clear()
```

注意：texture 释放必须考虑 OpenGL 上下文是否有效。

## current frame 与 texture 的关系

第一版推荐关系：

```text
currentFrame 是 CPU 侧当前帧数据
textureY/U/V 是 GPU 侧最近一次上传结果
textureDirty 表示 CPU 帧比 GPU 纹理新
```

流程：

```text
PresentFrame()
    -> 替换 currentFrame
    -> textureDirty = true
    -> update()

paintGL()
    -> 如果 textureDirty:
        -> currentFrame 上传到 texture
        -> textureDirty = false
    -> 绘制 texture
```

这样可以保证：

- 接帧阶段不需要 OpenGL 上下文。
- 上传阶段固定在 OpenGL 上下文中。
- `AVFrame*` 所有权清晰。

## ClearFrame 与 DetachStream 区别

### ClearFrame(streamId)

用于 stream 还存在，但当前画面要清空。

典型场景：

- seek 前后避免旧帧回显。
- stop 后清空画面。
- 暂时没有可显示帧。

保留：

- `StreamRenderState`
- texture 对象
- slot 绑定

释放：

- `currentFrame`

### DetachStream(streamId)

用于 stream 被移除或 renderer 释放该路资源。

典型场景：

- remove stream。
- shutdown。
- 切换渲染路径。

释放：

- `currentFrame`
- texture 对象
- `StreamRenderState`

slot 绑定是否移除由外层调用 `SetViewSlots()` 决定，不建议 `DetachStream()` 隐式修改布局绑定。

## OpenGL 上下文边界

只能在 OpenGL 上下文有效时执行：

- 创建 `QOpenGLTexture`。
- 调用 `QOpenGLTexture::allocateStorage()`。
- 调用 `QOpenGLTexture::setData()`。
- delete `QOpenGLTexture`。

不要求 OpenGL 上下文的操作：

- 替换 `AVFrame* currentFrame`。
- 更新 width / height / SAR。
- 设置 `textureDirty`。
- 设置 `hasFrame`。

因此后续代码应避免：

```text
PresentFrame(streamId, frame)
    -> 直接上传纹理
```

推荐：

```text
PresentFrame(streamId, frame)
    -> 只接管 frame
    -> 标记 textureDirty
    -> update()

paintGL()
    -> 检查 textureDirty
    -> 上传纹理
    -> 绘制
```

## 线程边界

第一版实现时应默认 `MultiViewRenderer` 属于 UI / OpenGL 线程。

如果 `PresentFrame(streamId, frame)` 从非 renderer 线程调用：

- adapter 应通过 queued invocation 投递到 renderer 线程。
- 投递成功后 frame 所有权随调用转移。
- renderer 已销毁或投递失败时，adapter 必须释放 frame。

本设计不要求在 3.8.2 解决 queued 调用代码，只要求后续实现不能忽略该风险。

## 第一版建议

3.8.4 接帧接口空壳阶段：

- 可以先只实现 `currentFrame`、尺寸、SAR、pixelFormat、dirty、hasFrame。
- 暂不加入 texture 字段，或先保留字段但不创建。

3.8.5 slot 0 绘制阶段：

- 再加入 `QOpenGLTexture* textureY/U/V`。
- 在 `paintGL()` 中按 dirty flag 上传。
- 只支持 `AV_PIX_FMT_YUV420P`。

这样可以把所有权管理和 OpenGL 上传风险拆开。

## 不做范围

- 不新增 `StreamRenderState` 代码。
- 不修改 `MultiViewRenderer`。
- 不接真实 `AVFrame*`。
- 不实现 texture 上传。
- 不修改 `MediaPipeline`。
- 不修改 `SingleStreamFrameSinkAdapter`。
- 不处理非 YUV420P 格式转换。

## 结论

`MultiViewRenderer` 的帧状态模型应围绕 `streamId -> StreamRenderState` 建立。

第一版状态设计重点是：

```text
AVFrame* currentFrame
    -> CPU 侧当前帧，sink 负责释放

QOpenGLTexture* textureY/U/V
    -> GPU 侧纹理，只能在 OpenGL 上下文线程操作

textureDirty
    -> 连接 currentFrame 和 texture 的状态标记

ClearFrame(streamId)
    -> 清 current frame，保留 stream state

DetachStream(streamId)
    -> 清 current frame + texture + stream state
```

下一步可以进入 3.8.3，新增 `SingleStreamFrameSinkAdapter` 空壳。
