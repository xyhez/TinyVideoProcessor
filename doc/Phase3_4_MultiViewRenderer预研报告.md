# Phase 3.4 MultiViewRenderer 预研报告

## 阶段目标

Phase 3.4 的目标不是立即实现 `MultiViewRenderer`，而是在 Phase 3.3 已经完成多容器多画面验证的基础上，判断后续统一渲染改造的正确边界。

当前 Phase 3.3 采用的是低风险方案：

```text
一个 stream
    -> 一个 MediaPipeline
    -> 一个 VideoRender
    -> 一个 QWidget 容器
```

该方案已经验证了多路生命周期、容器绑定、布局切换、active stream 和 active audio stream 策略。但它不是最终高性能多路渲染方案。架构指导中推荐的长期方向是：

```text
多个 stream
    -> 多个视频帧来源
    -> 一个 MultiViewRenderer
    -> 单个 OpenGL 上下文
    -> 多纹理多 viewport 绘制
```

因此本阶段先回答一个核心问题：

```text
要引入 MultiViewRenderer，当前架构必须先拆出哪些边界？
```

## 当前渲染链路复盘

当前 `MediaPipeline` 的初始化链路是：

```text
MediaPipeline::Initialize(mediaSource, rendererContainer)
    -> CreatePipelineResources()
        -> 创建视频包队列 VPacket_
        -> 创建视频帧队列 VFrame_
        -> 创建 demuxer / decoder
        -> 创建 VideoRender(rendererContainer)
        -> VideoRender::InitVideoRender(VFrame_, syncManager_, frameRate, videoTimeBase)
        -> rendererContainer->layout()->addWidget(videoRender_)
```

当前 `VideoRender` 同时承担以下职责：

- 从 `SyncQueue<AVFrame*>` 中取帧。
- 根据 `syncmanager` 做音视频同步判断。
- 根据帧率和播放速度自调度下一次渲染。
- 丢弃过晚的视频帧。
- 保存当前显示帧。
- 管理 OpenGL shader。
- 管理 Y/U/V 三个纹理。
- 将 `AVFrame` 上传为 OpenGL 纹理。
- 计算保持宽高比的绘制矩形。
- 在自己的 `QOpenGLWidget` 中绘制画面。

这说明当前 `VideoRender` 不是一个单纯的 renderer，它同时包含：

```text
帧消费 + 同步调度 + 纹理上传 + OpenGL 绘制 + QWidget 显示载体
```

如果直接新增 `MultiViewRenderer`，会和 `VideoRender` 的职责发生重叠。

## 当前方案的优势

多个 `VideoRender` + 多个 `QWidget` 容器的方案有几个明确优势：

- 对现有单路播放链路侵入小。
- 每路播放管线仍然独立。
- 每个 `MediaPipeline` 仍能按原方式创建和释放自己的 `VideoRender`。
- Phase 3.3 已经验证 2 路、4 路、无音频流、多路切换和关闭稳定性。
- 出问题时容易定位到单路 pipeline。

这也是 Phase 3.3 选择该方案的原因。

## 当前方案的限制

当前方案的主要限制也很明确：

- 每路一个 `QOpenGLWidget`，意味着多个 OpenGL widget 和上下文管理成本。
- 多路布局只是 QWidget 层面的布局，不是一次统一 OpenGL 绘制。
- active stream 高亮只能在容器层做边框样式，无法和渲染画面深度融合。
- 多路高分辨率下的纹理上传和绘制无法统一调度。
- 后续 3x3、4x4、1+N、单路放大等布局会使 QWidget 管理复杂度上升。
- 无法方便地做统一 overlay、统一截图、统一画面合成和统一图像处理入口。

因此它适合作为阶段性验证方案，不适合作为最终多路渲染架构。

## MultiViewRenderer 的推荐职责

`MultiViewRenderer` 推荐作为基础设施层的统一视频渲染组件，职责是：

- 持有单个 `QOpenGLWidget`。
- 维护多个 stream 对应的纹理状态。
- 根据布局模型计算每个 stream 的 viewport。
- 将多路视频帧上传到对应纹理。
- 在一次 `paintGL()` 中绘制多个画面。
- 处理 active stream 的视觉标记。
- 处理鼠标点击坐标到 stream 的命中映射。
- 在 stream 移除时释放对应纹理。

它不应该负责：

- 打开媒体源。
- demux。
- decode。
- 音频播放。
- seek。
- stop / pause 的业务语义。
- active audio stream 策略。
- 播放列表。
- 创建或管理 `MediaPipeline`。

原因是这些职责已经属于 `IMediaSource`、`MediaPipeline`、`MultiStreamManager`、`MediaPlayController` 和 `MainWindow` 的边界。

## MultiViewRenderer 所属层级判断

`MultiViewRenderer` 应属于基础设施层，原因是它直接依赖 Qt OpenGL 和具体渲染实现。

推荐层级关系：

```text
MainWindow
    -> MediaPlayController
    -> MultiStreamManager
        -> MediaPipeline
            -> 视频帧输出接口

MainWindow
    -> MultiViewRendererWidget
        -> 多路纹理渲染
```

需要注意：`MainWindow` 可以持有渲染控件，因为它是 UI 展示载体；但 `MainWindow` 不应该直接持有或调度 `MediaPipeline`。渲染控件和 pipeline 之间的连接应通过 Controller 或专门的渲染适配层完成。

## 关键边界：帧输出接口

引入 `MultiViewRenderer` 前，最关键的改造不是写新的 OpenGL 绘制代码，而是让 `MediaPipeline` 不再强绑定 `VideoRender`。

当前问题：

```text
MediaPipeline
    -> new VideoRender(rendererContainer)
    -> VideoRender 直接消费 VFrame_
```

后续目标：

```text
MediaPipeline
    -> 输出视频帧
    -> 外部渲染器消费视频帧
```

建议预留一个视频帧输出边界，例如：

```cpp
class IVideoFrameSink {
public:
    virtual ~IVideoFrameSink() = default;
    virtual void AttachStream(StreamId id, std::shared_ptr<SyncQueue<AVFrame*>> frameQueue) = 0;
    virtual void DetachStream(StreamId id) = 0;
};
```

这只是方向示例，当前不建议立刻按这个接口实现。真正落地前还要解决 `StreamId` 是否能进入接口、接口归属层级、帧释放责任和线程调度方式。

## VideoRender 可拆分职责

当前 `VideoRender` 后续可以拆成几类职责：

1. 帧调度器
   - 从帧队列取帧。
   - 根据同步时钟判断等待、渲染或丢帧。
   - 根据速度和帧率安排下一次取帧。

2. 纹理上传器
   - 接收 `AVFrame`。
   - 管理 Y/U/V 纹理。
   - 处理尺寸变化和纹理重建。

3. OpenGL 绘制器
   - 管理 shader。
   - 根据 viewport 绘制纹理。
   - 支持多个画面区域。

4. QWidget 显示载体
   - 当前由 `QOpenGLWidget` 承担。
   - 多路统一渲染后应只保留一个 widget。

如果直接把 `VideoRender` 改成 `MultiViewRenderer`，会同时动这四类职责，风险过大。

## 帧所有权与释放责任

当前 `VideoRender` 从 `VFrame_` 取出 `AVFrame*` 后，负责释放旧显示帧：

```text
av_frame_free(&m_displayFrame)
```

统一渲染后必须明确：

- 帧从队列取出后由谁释放。
- 每路当前显示帧由谁持有。
- stream 移除时当前显示帧和待渲染帧如何释放。
- seek / stop 时旧帧如何丢弃，避免旧画面回显。
- 渲染线程和 UI 线程之间是否需要复制帧或转移所有权。

建议后续在抽象接口里显式约定所有权，不要让多个对象共享裸 `AVFrame*` 的释放责任。

## 同步策略影响

当前视频同步逻辑在 `VideoRender::RenderFrame()` 内部：

- AUDIO sync 时按音频时钟判断视频帧早晚。
- VIDEO sync 时由渲染帧刷新视频时钟。
- 过晚视频帧会被丢弃。

如果引入统一渲染器，有两种选择：

1. 每路保留独立帧调度器，统一渲染器只接收“可显示帧”。
2. 统一渲染器同时承担所有路的帧调度。

推荐第一种作为迁移路线。

原因：

- 改动更小。
- 每路音视频同步语义仍由单路 pipeline 附近维护。
- `MultiViewRenderer` 不会变成播放控制中心。
- 更符合单一职责原则。

长期如果要优化性能，可以再评估是否将多路调度统一到渲染器。

## Active Stream 与点击命中

当前 active stream 点击逻辑在 `MainWindow`：

```text
点击 QWidget 容器
    -> 找到容器 index
    -> 找到 StreamId
    -> SetActiveStream()
    -> SetActiveAudioStream()
```

统一渲染后，点击命中应从“QWidget 容器命中”变成“viewport 命中”：

```text
点击 MultiViewRenderer 坐标
    -> MultiViewRenderer 根据布局计算命中的 viewport
    -> 返回 StreamId
    -> MainWindow / Controller 切换 active stream 和 active audio stream
```

这里建议 `MultiViewRenderer` 只负责命中检测和发出类似 `StreamClicked(StreamId)` 的信号，不负责调用 `SetActiveStream()`。状态切换仍由 `MainWindow -> MediaPlayController` 完成。

## 推荐迁移路线

不建议下一步直接实现完整 `MultiViewRenderer`。推荐分阶段迁移：

### 步骤一：抽象视频帧输出边界

目标：

- 让 `MediaPipeline` 不再强依赖 `QWidget* rendererContainer`。
- 明确单路 pipeline 如何把视频帧交给外部消费者。
- 保留当前 `VideoRender` 作为默认消费者。

验收：

- 单路播放不退化。
- 多路 QWidget 方案仍能工作。
- `MediaPipeline` 不直接知道最终布局。

### 步骤二：拆分 VideoRender 职责

目标：

- 将帧调度、纹理上传、绘制逻辑分清。
- 先不改变 UI 行为。

验收：

- 当前单路和多路播放行为不变。
- seek、stop、pause、remove stream 不退化。

### 步骤三：引入 MultiViewRenderer 空壳

目标：

- 新增统一渲染器类。
- 先只实现布局、viewport、点击命中和空画面绘制。
- 不接真实视频帧。

验收：

- 能渲染空的 1x1 / 2x2 画面区域。
- 点击区域能返回正确 stream。
- 不影响现有播放链路。

### 步骤四：接入一路真实视频帧

目标：

- 先让 `MultiViewRenderer` 渲染一路视频。
- 与现有 `VideoRender` 路径并存，作为可切换实验路径。

验收：

- 单路视频画面正常。
- 音视频同步正常。
- seek / stop / shutdown 正常。

### 步骤五：接入多路真实视频帧

目标：

- 支持 2 路和 4 路纹理绘制。
- 替代多个 `QOpenGLWidget` 的显示方案。

验收：

- 2 路和 4 路显示通过。
- 布局切换流畅。
- active stream 高亮和点击切换通过。
- 无音频流策略不退化。
- 关闭程序不阻塞。

## 不建议的做法

不建议直接做以下事情：

- 直接让 `MultiViewRenderer` 创建或持有 `MediaPipeline`。
- 直接让 `MultiViewRenderer` 管理 demuxer / decoder。
- 直接把 `StreamId` 塞进 `MediaPipeline`。
- 直接删除当前 `VideoRender` 路径。
- 直接把多路布局逻辑放进 `MediaPipeline`。
- 在没有帧所有权约定前共享裸 `AVFrame*`。
- 一次性同时改播放管线、渲染器、布局和点击逻辑。

这些做法都会破坏当前已经稳定的分层边界。

## 预研结论

当前项目已经具备进入 `MultiViewRenderer` 后续工作的条件，但下一步应先做边界抽象，不应直接实现完整统一渲染器。

推荐结论：

```text
先抽象视频帧输出边界
再拆分 VideoRender 职责
再引入 MultiViewRenderer 空壳
最后逐步接真实视频帧
```

阶段判断：

- `MultiViewRenderer` 应属于基础设施层。
- `MediaPipeline` 应继续保持单路播放管线职责。
- `MultiStreamManager` 继续管理多路生命周期和 `StreamId`。
- `MediaPlayController` 继续作为 UI 对外入口。
- `MainWindow` 可以承载渲染控件，但不直接管理 pipeline。
- active audio stream 策略不应进入渲染器。

下一步建议先进入 Phase 3.5：音视频格式归一化预研。先明确音频输出格式和视频帧格式边界，再进入视频帧输出边界抽象设计与最小实现计划。
