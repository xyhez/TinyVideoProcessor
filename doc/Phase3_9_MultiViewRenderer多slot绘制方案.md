# Phase 3.9 MultiViewRenderer 多 slot 绘制方案

## 背景

Phase 3.8 已经完成 `MultiViewRenderer` slot 0 单路真实帧接入。当前链路可以证明以下边界成立：

- `VideoRender` 仍负责单路帧调度、同步判断和默认回退显示。
- `IVideoFrameSink` 能承接已经允许显示的 `AVFrame*`。
- `SingleStreamFrameSinkAdapter` 能把单路 sink 调用转成带 `StreamId` 的 `MultiViewRenderer` 调用。
- `MultiViewRenderer` 能接管 `AVFrame*`，并把 YUV420P 帧上传为 Y/U/V 三个 OpenGL 纹理。

当前限制是：真实帧绘制仍停留在 slot 0。代码层面主要体现在 `MultiViewRenderer::DrawViewport()` 只在 `viewport.slotIndex == 0 && viewport.IsBound()` 时查找 `renderStates_` 并调用 `DrawStreamFrame()`。

Phase 3.9 的目标不是重写播放管线，而是把已经成立的 slot 0 绘制路径扩展为多个 slot 的统一绘制路径。

## 设计目标

- 多个 stream 可以同时进入 `MultiViewRenderer`。
- `MultiViewRenderer` 根据 `StreamId -> slot -> viewport` 映射绘制真实帧。
- slot 0 不再是唯一可以绘制真实帧的 slot。
- 空 slot、无帧 slot、无映射 stream 都有明确绘制行为。
- OpenGL 纹理创建、上传和释放仍留在 `MultiViewRenderer` 的 OpenGL 上下文内。
- `MultiViewRenderer` 不负责同步、取帧、seek 判断、队列消费和 active audio 策略。

## 不改变的架构边界

- `MediaPipeline` 继续是单路播放管线。
- `MediaPipeline` 不持有 `StreamId`。
- `MediaPipeline` 不直接依赖 `MultiViewRenderer`。
- `MultiViewRenderer` 不访问 `VFrame_`。
- `MultiViewRenderer` 不访问 `SyncManager`。
- `MultiViewRenderer` 不直接访问 demuxer、decoder、audio player、packet queue 或 frame queue。
- `VideoRender` 暂时继续承担单路帧调度和默认回退路径。
- `SingleStreamFrameSinkAdapter` 继续是 `StreamId` 从外层进入统一渲染器的适配边界。

## 当前数据结构

`MultiViewRenderer` 里已经存在三类关键状态：

```cpp
QMap<int, StreamId> viewSlots_;
QMap<StreamId, StreamRenderState> renderStates_;
StreamId activeStreamId_;
```

含义：

- `viewSlots_`：slot 索引到 `StreamId` 的绑定关系。
- `renderStates_`：`StreamId` 到该流当前帧、纹理和帧元数据的绑定关系。
- `activeStreamId_`：当前被选中的显示流，用于绘制高亮边框。

`Viewport` 已经同时包含：

```cpp
int slotIndex;
StreamId streamId;
QRect qtRect;
QRect glViewportRect;
```

这说明多 slot 绘制不需要引入新的映射模型，后续应复用现有 `Viewports()` 和 `ViewportForSlot()`。

## 映射链路

完整绘制链路应固定为：

```text
StreamId
    -> renderStates_[streamId]
        -> 当前可显示帧和纹理状态

slotIndex
    -> viewSlots_[slotIndex]
        -> StreamId
            -> renderStates_[streamId]
                -> DrawStreamFrame(viewport, state)

slotIndex
    -> ViewportForSlot(slotIndex)
        -> qtRect / glViewportRect
```

实际绘制时不要按 `renderStates_` 遍历。原因：

- `renderStates_` 只说明某路流有帧状态，不说明它当前是否可见。
- 同一路 stream 可能暂时不在当前布局或 slot 映射中。
- 按 stream 遍历容易把帧画到错误区域，或者在 layout 切换后留下不可见流的绘制副作用。

推荐按可见 slot 遍历：

```text
for viewport in Viewports():
    先绘制 slot 背景
    如果 viewport 未绑定 stream：
        绘制空 slot 边框
        continue
    如果 viewport 绑定 stream 但没有 render state：
        绘制已绑定但无帧的 slot 背景和边框
        continue
    如果 render state 有可用帧：
        上传纹理并绘制真实帧
    绘制边框和 active 高亮
```

这个顺序保证“当前可见 slot”是唯一绘制入口。

## slot 绘制行为

### 空 slot

条件：

- `viewport.IsBound() == false`

行为：

- 绘制空 slot 背景。
- 不查找 `renderStates_`。
- 不上传纹理。
- 绘制普通边框。

### 已绑定但无帧 slot

条件：

- `viewport.IsBound() == true`
- `renderStates_` 中没有对应 `streamId`，或对应 state 没有有效 `currentFrame`

行为：

- 绘制已绑定 slot 背景。
- 不绘制真实帧。
- 绘制普通边框或 active 边框。

这种状态是合法的。例如刚添加 stream、seek flush 后、stop 后、无视频帧到达前。

### 正常视频 slot

条件：

- `viewport.IsBound() == true`
- `renderStates_` 中存在对应 `streamId`
- state 持有有效 `currentFrame`
- frame 格式当前支持，第一阶段为 YUV420P

行为：

- 先绘制 slot 背景。
- 调用 `DrawStreamFrame(viewport, state)` 上传纹理并绘制真实帧。
- 绘制普通边框或 active 边框。

### 不支持格式 slot

条件：

- state 持有有效帧。
- `state.pixelFormat != AV_PIX_FMT_YUV420P`

行为：

- 保留已绑定 slot 背景。
- 不绘制真实帧。
- 通过 `LogUnsupportedFrameIfNeeded()` 输出一次诊断日志。
- 不在本阶段引入格式转换。

## paintGL 遍历策略

`paintGL()` 保持现有结构：

```text
清空整块背景
启用 scissor
遍历 Viewports()
    DrawViewport(viewport)
关闭 scissor
```

调整点集中在 `DrawViewport()`：

当前逻辑：

```cpp
if (viewport.slotIndex == 0 && viewport.IsBound()) {
    QMutexLocker locker(&renderStateMutex_);
    auto it = renderStates_.find(viewport.streamId);
    if (it != renderStates_.end()) {
        DrawStreamFrame(viewport, it.value());
    }
}
```

目标逻辑：

```cpp
if (viewport.IsBound()) {
    QMutexLocker locker(&renderStateMutex_);
    auto it = renderStates_.find(viewport.streamId);
    if (it != renderStates_.end()) {
        DrawStreamFrame(viewport, it.value());
    }
}
```

核心变化只有一个：移除 `viewport.slotIndex == 0` 限制。
这不是补丁式绕路，而是把 Phase 3.8 已验证的单 slot 绘制入口泛化为可见 slot 绘制入口。

## 纹理上传时机

保持当前策略：

- `PresentFrame(streamId, frame)` 只接管 frame 所有权，写入 `renderStates_`，设置 `textureDirty = true`，然后触发 `update()`。
- `DrawStreamFrame()` 在 OpenGL 绘制阶段调用 `UploadTexturesIfNeeded()`。
- `UploadTexturesIfNeeded()` 在 `MultiViewRenderer` 的 OpenGL 上下文中创建、更新和复用纹理。
- `DetachStream()` 或析构时释放当前帧和纹理资源。

不推荐在 `PresentFrame()` 中上传纹理。原因：

- `PresentFrame()` 可能来自播放线程或 queued 调用，不能假设当前线程持有 OpenGL 上下文。
- OpenGL 资源跨线程创建和释放会引入不稳定问题。
- 当前设计已经能用 `textureDirty` 把“接帧”和“上传纹理”分开。

## slot 映射变化处理

`SetViewSlots()` 当前只更新 `viewSlots_`，不隐式删除 `renderStates_`。这个策略可以保留：

- slot 映射负责“哪路流可见”。
- render state 负责“哪路流有可显示帧和纹理”。

当某路 stream 只是暂时不可见时，不必立即释放帧和纹理。
当某路 stream 被真正移除时，应由 remove / detach 链路调用 `DetachStream(streamId)` 释放资源。

但有一个约束必须明确：

- `paintGL()` 只能按 `Viewports()` 绘制可见 slot。
- 不允许遍历 `renderStates_` 主动画帧。

这样即使 `renderStates_` 中还保留不可见 stream 的状态，也不会画到错误区域。

## 清理行为

### ClearFrame(streamId)

语义：

- 清除某一路当前显示帧。
- 保留该 stream 的渲染状态对象和 slot 绑定关系。
- 适合 stop、seek flush、临时无帧场景。

绘制结果：

- 如果 slot 仍绑定该 stream，显示已绑定背景，不显示最后一帧。

### DetachStream(streamId)

语义：

- 释放某一路当前帧。
- 释放该路 OpenGL 纹理。
- 从 `renderStates_` 移除该 stream。
- 不隐式修改 `viewSlots_`，slot 解绑仍由上层布局或 stream 管理流程负责。

绘制结果：

- 如果 slot 仍短暂绑定该 stream，但 render state 已不存在，显示已绑定但无帧背景。
- 后续上层更新 `SetViewSlots()` 后，该 slot 变为空 slot 或绑定新 stream。

## 日志边界

本阶段不增加大量周期日志。保留必要诊断：

- shader 初始化失败。
- 不支持的像素格式，每个 stream 每种连续状态只输出一次。
- 如后续定位生命周期问题，可临时增加清理日志，但不作为常规功能提交。

不应在每帧绘制、每次 `paintGL()`、每次纹理上传时输出日志。多路渲染下这类日志会快速放大，影响判断和性能。

## 后续代码改动点

### 3.9.3 UI 层 sink 接入

目标：

- 取消只给 slot 0 创建 `SingleStreamFrameSinkAdapter` 的限制。
- 多路文件和摄像头都按自己的 `StreamId` 创建 adapter。

重点：

- `StreamId` 仍由 UI / controller / manager 外层绑定。
- 不修改 `MediaPipeline` 存储 `StreamId`。
- 不改变 active audio 策略。

### 3.9.4 MultiViewRenderer 多 slot 绘制

目标：

- 移除 `DrawViewport()` 里的 `viewport.slotIndex == 0` 条件。
- 让每个已绑定 viewport 都尝试按 `streamId` 查找 `renderStates_` 并绘制。

重点：

- 绘制入口按 slot，不按 render state。
- 空 slot 和无帧 slot 必须继续显示背景。
- `DrawViewportBorder()` 保持在最后执行，保证边框不被视频帧覆盖。

### 3.9.5 旧 VideoRender widget 过渡

目标：

- 明确外部 sink 路径下旧 `VideoRender` widget 是否继续作为可见控件。
- 避免同一路视频同时出现在旧区域和统一渲染区域。

重点：

- 默认无外部 sink 路径不能退化。
- 不在本阶段提前抽离 `VideoFrameScheduler`。

### 3.9.6 生命周期复核

目标：

- 检查 remove、stop、pause、seek、layout 切换和 shutdown。

重点：

- stream 真正移除时调用 `DetachStream(streamId)`。
- stop 或 seek flush 时至少清除当前帧，避免旧帧残留。
- renderer 销毁后 adapter 不能访问悬空对象。

## 验收判断

完成 3.9.4 后，至少应满足：

- 两路文件同时进入 `MultiViewRenderer` 的不同 slot。
- 第二路不再停留在旧 `QOpenGLWidget` 区域。
- 空 slot 仍显示为空背景。
- remove stream 后对应 slot 不残留最后一帧。
- 不支持像素格式只输出诊断，不导致崩溃。
- `MediaPipeline` 仍不感知 `StreamId`。

## 结论

多 slot 绘制的正确扩展方向是复用当前已有的 `Viewports()`、`viewSlots_` 和 `renderStates_`，按可见 slot 遍历并移除 slot 0 绘制限制。

本阶段不需要新增新的渲染抽象，也不需要把 `StreamId` 下沉到 `MediaPipeline`。后续真正需要拆的是 `VideoRender` 内部的帧调度能力，也就是独立 `VideoFrameScheduler`，但这应放在多 slot 真实帧路径稳定之后。
