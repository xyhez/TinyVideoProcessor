# Phase 3.7 视频帧输出边界预研报告

## 阶段目标

Phase 3.7 的目标是在 Phase 3.6 `MultiViewRenderer` 空壳已经完成后，继续向真实视频帧接入推进，但本阶段仍然不直接改代码。

本阶段要回答的问题是：

```text
MediaPipeline 如何在不破坏单路职责的前提下，把“可显示视频帧”交给外部渲染器？
```

当前不能直接把 `VFrame_` 或 `AVFrame*` 塞给 `MultiViewRenderer`，因为真实视频帧接入涉及帧调度、同步判断、所有权释放、seek 清理、stop 清理和 OpenGL 线程边界。如果这些边界没有先设计清楚，`MultiViewRenderer` 很容易从“统一渲染器”变成新的播放管线中心。

Phase 3.7 的目标是形成可执行的迁移判断，为后续 TODO 拆分和代码实现提供依据。

## 当前渲染链路现状

当前单路播放链路是：

```text
MediaPipeline::Initialize(mediaSource, rendererContainer)
    -> CreatePipelineResources(mediaSource, rendererContainer)
        -> 创建 APacket_ / VPacket_
        -> 创建 AFrame_ / VFrame_
        -> 创建 demuxer
        -> 创建 decoder
        -> 创建 VideoRender(rendererContainer)
        -> 创建 syncmanager
        -> VideoRender::InitVideoRender(VFrame_, syncManager_, frameRate, videoTimeBase)
        -> rendererContainer->layout()->addWidget(videoRender_)
```

因此当前 `MediaPipeline` 与 `VideoRender` 是强绑定关系：

```text
MediaPipeline
    -> 持有 VFrame_
    -> 创建 VideoRender
    -> 把 VFrame_ 交给 VideoRender
    -> 管理 VideoRender 生命周期
```

这个设计在单路和多个 `VideoRender` 容器方案中是可运行的，但不适合最终的统一渲染器方案。

最终统一渲染方向应该变为：

```text
MediaPipeline
    -> 解码并维护单路播放状态
    -> 输出可显示视频帧
    -> 外部视频帧消费者
        -> VideoRender 或 MultiViewRenderer
```

## VideoRender 当前职责拆解

当前 `VideoRender` 并不是单纯的 OpenGL 绘制器，它包含四类职责。

### 1. 帧消费与调度

`VideoRender` 当前持有：

```cpp
std::shared_ptr<SyncQueue<AVFrame*>> m_frameQueue;
```

并在 `RenderFrame()` 中从队列取帧。它负责决定什么时候继续调度下一帧：

```text
RenderFrame()
    -> 从 m_frameQueue 取帧
    -> 根据同步策略判断
    -> 正常显示 / 等待 / 丢帧
    -> ScheduleNextFrame(...)
```

这部分不是纯渲染，它更接近“视频帧调度器”。

### 2. 音视频同步判断

`VideoRender` 当前持有：

```cpp
std::shared_ptr<syncmanager> m_syncManager;
```

它根据 `syncmanager` 判断当前视频帧与主时钟的关系：

```text
AUDIO sync:
    framePts - audioClock
    -> 太早：等待
    -> 太晚：丢帧
    -> 合适：显示

VIDEO sync:
    显示后刷新 video clock
```

这说明当前同步调度已经和 `VideoRender` 耦合。如果直接把真实帧交给 `MultiViewRenderer`，就必须决定同步逻辑是否迁移。

### 3. 帧持有与释放

`VideoRender` 当前持有当前显示帧：

```cpp
AVFrame* m_displayFrame = nullptr;
```

当新帧成为显示帧时，旧帧会被释放：

```text
av_frame_free(&m_displayFrame)
m_displayFrame = newFrame
```

析构时也会释放 `m_displayFrame`。

这意味着从 `VFrame_` 取出的帧所有权当前由 `VideoRender` 接管。

### 4. OpenGL 上传与绘制

`VideoRender` 还负责：

- 初始化 shader。
- 管理 Y / U / V 三个纹理。
- 判断帧格式是否支持。
- 上传 `AVFrame` 到纹理。
- 计算保持宽高比后的绘制矩形。
- 在 `paintGL()` 中绘制当前显示帧。

这些才是纯渲染能力。

## 当前强绑定的问题

当前设计的核心问题不是 `VideoRender` 写得不好，而是它承担了过多职责，导致后续 `MultiViewRenderer` 难以直接接入。

主要问题如下：

1. `MediaPipeline::Initialize()` 仍要求传入 `QWidget* rendererContainer`。
2. `MediaPipeline` 内部直接创建 `VideoRender`。
3. `VideoRender` 直接消费 `VFrame_`。
4. `VideoRender` 同时做同步调度和 OpenGL 绘制。
5. `AVFrame*` 的释放责任隐含在 `VideoRender` 内部。
6. `seek / stop / shutdown` 时，只清队列还不够，当前显示帧也需要明确清理策略。
7. 如果 `MultiViewRenderer` 直接消费多个 `VFrame_`，它会被迫承担多路同步调度责任。

因此真实帧接入前必须先抽象“视频帧输出边界”。

## 推荐边界：可显示帧消费者

Phase 3.7 推荐引入的不是“渲染器接口”，而是“可显示帧消费者”边界。

原因是 `MultiViewRenderer` 最终消费的不应该是原始播放管线内部细节，而应该是经过单路同步调度后，已经被判定可以显示的视频帧。

推荐抽象方向：

```text
MediaPipeline
    -> 解码队列 VFrame_
    -> 单路视频帧调度器
    -> IVideoFrameSink
        -> VideoRenderFrameSink
        -> MultiViewRendererFrameSink
```

其中：

- `MediaPipeline` 继续负责单路播放生命周期。
- 单路视频帧调度器负责从 `VFrame_` 取帧并根据 `syncmanager` 判断显示时机。
- `IVideoFrameSink` 只接收“可以显示”的帧。
- `VideoRender` 可以先作为默认 sink，保持现有路径。
- `MultiViewRenderer` 后续作为另一个 sink 接入。

这样可以避免让 `MultiViewRenderer` 直接面对解码队列和同步队列。

## 接口方向判断

当前可以考虑三种接口方向。

### 方案 A：直接暴露 VFrame_ 给外部渲染器

形式：

```text
MediaPipeline
    -> GetVideoFrameQueue()
    -> MultiViewRenderer 从队列取帧
```

优点：

- 改动看似最小。
- `MultiViewRenderer` 可以直接拿到帧。

缺点：

- 外部渲染器会接管帧调度和同步判断。
- `MultiViewRenderer` 必须知道 `SyncQueue<AVFrame*>`。
- 多路同步逻辑会集中到统一渲染器。
- `AVFrame*` 所有权更容易混乱。
- 会破坏“渲染器不管理播放管线”的边界。

结论：不推荐。

### 方案 B：MediaPipeline 发出可显示帧信号

形式：

```text
MediaPipeline
    -> emit VideoFrameReady(frame)
    -> 外部 sink 消费
```

优点：

- `MultiViewRenderer` 不需要直接访问 `VFrame_`。
- UI 或 Controller 可以连接信号。
- 符合 Qt 事件模型。

缺点：

- `AVFrame*` 跨线程传递所有权必须非常明确。
- 如果信号过快，可能造成 UI 线程堆积。
- 需要决定帧是 move、clone 还是引用计数持有。
- seek / stop 时旧信号可能已经排队，需要防止旧帧回显。

结论：可以作为候选，但第一版需要谨慎。

### 方案 C：抽象 IVideoFrameSink，由单路调度器推送可显示帧

形式：

```cpp
class IVideoFrameSink {
public:
    virtual ~IVideoFrameSink() = default;
    virtual void PresentFrame(AVFrame* frame) = 0;
    virtual void ClearFrame() = 0;
};
```

实际落地时不建议直接照这个接口写，需要先结合 Qt 生命周期和帧所有权进一步细化。

优点：

- `MediaPipeline` 不需要知道最终是 `VideoRender` 还是 `MultiViewRenderer`。
- 同步调度可以保留在单路 pipeline 附近。
- `MultiViewRenderer` 只接收可显示帧。
- 有利于先保留当前 `VideoRender` 作为默认 sink。

缺点：

- 需要新增抽象和适配层。
- 需要拆分当前 `VideoRender` 的职责。
- 实施步骤必须足够小，否则风险较高。

结论：推荐作为长期方向。

## 推荐迁移方向

推荐路线不是直接创建完整 `IVideoFrameSink`，而是分小步迁移：

```text
步骤 1：先拆出 VideoRender 中的帧调度边界
步骤 2：让当前 VideoRender 仍作为默认显示 sink
步骤 3：让 MediaPipeline 不直接依赖 QWidget 细节
步骤 4：新增可显示帧输出接口
步骤 5：先让 MultiViewRenderer 接一路真实帧
步骤 6：再扩展到多路真实帧
```

第一步的重点不是改 OpenGL，而是把“取帧、同步判断、丢帧、等待、显示”这条链路讲清楚。

## 帧所有权建议

真实帧接入前必须明确 `AVFrame*` 的所有权规则。

当前规则可以概括为：

```text
decoder
    -> 创建 AVFrame*
    -> push 到 VFrame_
VideoRender
    -> pop AVFrame*
    -> 决定丢弃或显示
    -> 丢弃时 av_frame_free()
    -> 替换显示帧时 av_frame_free(old)
    -> 析构时 av_frame_free(current)
```

后续建议保持一个原则：

```text
谁从队列取出 AVFrame*，谁负责最终释放。
```

如果要把帧交给外部 sink，有两种安全方向：

1. 调度器转移所有权给 sink。
2. 调度器保留所有权，sink 只在调用期间使用或复制帧内容。

对 `MultiViewRenderer` 来说，第一版更建议：

```text
调度器转移所有权给 sink
sink 持有每路 currentFrame
sink 在替换、清理、detach stream、析构时释放
```

但这要求接口名字和文档必须明确“PresentFrame 后所有权转移”。

如果后续引入 `std::shared_ptr<AVFrame>` 或自定义 RAII 包装，也应在单独任务中完成，避免和渲染接入混在一起。

## 同步调度建议

同步调度不建议进入 `MultiViewRenderer`。

理由：

- 每路 stream 的音视频同步仍属于单路播放语义。
- `MultiViewRenderer` 不应该知道音频时钟、视频时钟和 seek 状态。
- 多路中最多一路 active audio，但每路视频是否该显示仍应由自己的单路调度判断。
- 如果统一渲染器负责所有路调度，它会逐步变成播放调度中心。

推荐结构：

```text
MediaPipeline
    -> VideoFrameScheduler
        -> 读取 VFrame_
        -> 访问 syncmanager
        -> 判断显示 / 等待 / 丢帧
        -> 调用 IVideoFrameSink::PresentFrame()

MultiViewRenderer
    -> 接收可显示帧
    -> 上传纹理
    -> 绘制多 viewport
```

`VideoFrameScheduler` 可以先不作为独立类立即出现，但设计上要把这块职责从 `VideoRender` 中识别出来。

## seek / stop / remove stream / shutdown 清理要求

真实视频帧接入后，最容易出问题的是旧帧回显和释放顺序。

后续实现必须满足以下要求。

### seek

seek 时需要：

- 停止当前帧调度。
- 清空 `VFrame_` 队列。
- flush decoder。
- 清理 sink 当前显示帧。
- 更新 sync clock 到 seek 目标。
- 恢复解码与调度。

否则容易出现 seek 后旧画面短暂回显，或者 H264 reorder 缓存旧帧继续输出。

### stop

stop 时需要：

- 停止帧调度。
- 清空音视频队列。
- flush decoder。
- 清理 sink 当前显示帧。
- 重置 sync clock。
- 保持 UI 可再次 Start。

### remove stream

remove stream 时需要：

- 先停止对应 pipeline。
- 停止该路帧调度。
- 通知 sink detach stream。
- 释放该路当前显示帧和纹理。
- 再释放 pipeline 资源。

### shutdown

shutdown 时需要：

- 停止所有 pipeline。
- 停止所有帧调度。
- 通知 sink 清理全部 stream。
- 确保没有排队的 UI 更新继续访问已释放对象。

这部分必须作为真实帧接入前的硬性验收项。

## MainWindow / Controller / Manager 边界

Phase 3.7 仍然保持当前架构边界：

- `MainWindow` 只负责 UI 交互和渲染控件承载。
- `MainWindow` 不直接访问 `MediaPipeline`。
- `MediaPlayController` 仍是 UI 对外入口。
- `MultiStreamManager` 仍管理多路 `MediaPipeline` 生命周期和 `StreamId`。
- `MediaPipeline` 仍保持单路播放管线职责。
- `MediaPipeline` 不引入 `StreamId` 字段。
- `MultiViewRenderer` 不创建、不持有、不控制 `MediaPipeline`。

需要特别注意：如果后续 `MultiViewRenderer` 要区分多路 stream，它可以在 UI 或适配层接收 `StreamId`，但 `MediaPipeline` 仍不应该知道自己的 id。

推荐关系是：

```text
MultiStreamManager
    -> StreamId -> MediaPipeline
    -> StreamId -> frame sink binding

MediaPipeline
    -> 不知道 StreamId
    -> 只知道自己的 frame sink
```

也就是说，`StreamId` 仍属于 Manager 外层管理语义，不进入单路 pipeline 内部。

## MultiViewRenderer 接真实帧的最小路线

后续真正接帧时，建议严格按以下顺序推进。

### 1. 单路 VideoRender 路径不动

先保持当前 `VideoRender` 播放路径作为可运行基线。任何抽象都必须证明它不破坏当前单路、多路文件和摄像头路径。

### 2. 抽出帧调度职责

先让 `VideoRender` 的“取帧 + 同步 + 丢帧 + 调度”边界清晰。可以先通过内部私有方法或轻量类拆分，不急于改公共接口。

### 3. 明确 sink 接口语义

在接口文档中明确：

- `PresentFrame()` 是否转移帧所有权。
- `ClearFrame()` 何时调用。
- `DetachStream()` 是否负责释放 current frame。
- 调用线程是 UI 线程还是调度线程。

### 4. 让当前 VideoRender 适配 sink

先让现有 `VideoRender` 继续作为 sink 工作，验证单路和多路 `VideoRender` 路径不退化。

### 5. MultiViewRenderer 接一路真实帧

只接一路，不接多路。验证：

- 普通 MP4 显示正常。
- 音视频同步正常。
- seek 正常。
- stop 正常。
- shutdown 正常。
- 无音频视频正常。

### 6. MultiViewRenderer 接多路真实帧

在一路通过后，再扩展到 2 路和 4 路：

- 每路有独立帧状态。
- 每路有独立纹理状态。
- remove stream 释放对应纹理和帧。
- active stream 高亮和点击仍由现有 UI 链路控制。
- active audio stream 不进入渲染器。

## 不做范围

Phase 3.7 不做以下内容：

- 不修改代码。
- 不新增接口文件。
- 不改 `MediaPipeline` 初始化签名。
- 不拆分 `VideoRender`。
- 不接真实视频帧到 `MultiViewRenderer`。
- 不做多纹理上传。
- 不做 HDR、BT.2020、10-bit、色彩管理。
- 不做性能优化。
- 不替换当前多个 `VideoRender` 路径。

## 风险点

后续实现真实帧接入时，需要重点关注以下风险：

- `AVFrame*` 所有权不清导致重复释放或泄漏。
- seek 后旧帧回显。
- stop / shutdown 时 UI 线程仍访问已释放帧。
- `QOpenGLWidget` 只能在拥有上下文的线程进行 OpenGL 操作。
- 多路帧调度如果集中到 `MultiViewRenderer`，会破坏播放管线边界。
- `StreamId` 如果进入 `MediaPipeline`，会破坏单路无 id 设计。
- 直接删除当前 `VideoRender` 路径会失去稳定回退基线。
- 多路高分辨率下纹理上传频率可能成为瓶颈。

## 预研结论

Phase 3.7 的结论是：下一步不应该直接把真实视频帧接入 `MultiViewRenderer`。

推荐路线是：

```text
先拆清 VideoRender 中的帧调度职责
再定义可显示帧 sink 边界
再让当前 VideoRender 适配新边界
再让 MultiViewRenderer 接一路真实帧
最后扩展到多路真实帧
```

最关键的设计原则是：

```text
同步调度仍靠近单路 MediaPipeline
MultiViewRenderer 只消费可显示帧并负责统一绘制
```

这样可以保持 `MediaPipeline` 单路职责、`MultiStreamManager` 多路管理职责和 `MultiViewRenderer` 统一渲染职责三者边界清晰。

## 下一步建议

建议下一步创建 Phase 3.7 TODO，拆成可执行的小步：

1. 梳理并标注 `VideoRender` 当前职责。
2. 设计 `VideoFrameScheduler` 或等价内部调度边界。
3. 设计 `IVideoFrameSink` 接口草案。
4. 设计帧所有权规则。
5. 设计 seek / stop / remove stream / shutdown 的 sink 清理协议。
6. 先让当前 `VideoRender` 路径适配新边界。
7. 再进入 `MultiViewRenderer` 单路真实帧接入。

在 TODO 拆分完成前，不建议进入代码实现。
