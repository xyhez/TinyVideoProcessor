# Phase 3.3 多路显示布局预研报告

## 阶段定位

Phase 3.2 已完成多路播放管理：`MultiStreamManager` 可以管理多个 `MediaPipeline`，`MediaPlayController` 作为 UI 对外入口，`MainWindow` 不直接访问 Manager 或 Pipeline。

Phase 3.3 的目标不是继续扩展多路生命周期，而是解决“多路如何显示”的问题。

当前最小验证 UI 已经能添加多路文件源，但多路渲染仍临时复用单个 `video_widget`。这只能证明多路管线生命周期可用，不能作为最终多画面方案。

## 当前渲染链路

当前单路渲染链路如下：

```text
MainWindow
    -> MediaPlayController
    -> MultiStreamManager
    -> MediaPipeline
    -> VideoRender
    -> rendererContainer(QWidget*)
```

`MediaPipeline::Initialize()` 接收 `rendererContainer`，并在内部创建 `VideoRender`：

```text
MediaPipeline::Initialize(mediaSource, rendererContainer)
    -> CreatePipelineResources()
    -> new VideoRender(rendererContainer)
    -> rendererContainer->layout()->addWidget(videoRender_)
```

这个设计对单路播放是直接有效的，但进入多路显示后有明显限制。

## 当前问题

### 1. MediaPipeline 直接创建 VideoRender

`MediaPipeline` 现在既负责播放管线，又负责创建具体 QWidget 渲染控件。

这会带来一个边界问题：

- 单路时可接受。
- 多路布局时，布局系统需要管理每一路画面所在位置。
- 如果未来引入统一 `MultiViewRenderer`，`MediaPipeline` 不应该继续直接创建 `VideoRender`。

这不是立即要改的 bug，但需要在 Phase 3.3 先设计清楚。

### 2. rendererContainer 是 QWidget 指针

当前 `rendererContainer` 是 UI 层传入的 `QWidget*`。

在多路阶段，如果继续走多个 `VideoRender`，则每一路都需要独立容器：

```text
Stream #1 -> container #1 -> VideoRender #1
Stream #2 -> container #2 -> VideoRender #2
Stream #3 -> container #3 -> VideoRender #3
```

如果走统一渲染器，则不再是每一路一个 QWidget：

```text
Stream #1 -> frame/texture slot #1
Stream #2 -> frame/texture slot #2
Stream #3 -> frame/texture slot #3
             -> MultiViewRenderer
```

这两条路线对 `MediaPipeline` 的接口要求不同。

### 3. active stream 和布局选中态还未统一

当前 active stream 表示 UI 关注的一路，进度条和时间显示跟随它。

多画面后还需要明确：

- 用户点击某个画面是否切换 active stream。
- active stream 是否需要高亮边框。
- active audio stream 是否跟随 active stream。
- 全屏查看一路时是否改变 active stream。

这些属于布局交互语义，不能混在 Manager 生命周期逻辑里。

## 两种实现路线

### 路线 A：多个 VideoRender + 多个 QWidget 容器

结构：

```text
MainWindow
    -> 多路容器布局
        -> renderContainer #1
        -> renderContainer #2
        -> renderContainer #3

MediaPlayController
    -> MultiStreamManager
        -> MediaPipeline #1 -> VideoRender #1
        -> MediaPipeline #2 -> VideoRender #2
        -> MediaPipeline #3 -> VideoRender #3
```

优点：

- 改动小。
- 符合当前 `MediaPipeline::Initialize(mediaSource, QWidget*)` 接口。
- 可以先验证 2 路、4 路显示。
- 不需要重写 OpenGL 纹理管理。

缺点：

- 多个 `QOpenGLWidget` 性能可能较差。
- 布局切换时 QWidget 生命周期和重挂载需要谨慎。
- 4 路以上可能资源占用偏高。
- 后续切到 `MultiViewRenderer` 时仍需要重构渲染边界。

适合第一步多画面验证。

### 路线 B：统一 MultiViewRenderer

结构：

```text
MainWindow
    -> MultiViewRenderer

MediaPlayController
    -> MultiStreamManager
        -> MediaPipeline #1
        -> MediaPipeline #2
        -> MediaPipeline #3

Frame/Texture Bridge
    -> StreamId + frame/texture
    -> MultiViewRenderer
```

优点：

- 符合长期目标。
- 单个 OpenGL 上下文更适合多路。
- 布局切换、选中边框、全屏一路可以统一处理。
- 后续性能优化空间更大。

缺点：

- 当前 `VideoRender` 和 `MediaPipeline` 绑定较深。
- 需要抽象帧输出或纹理输入接口。
- 会影响同步、渲染调度和资源释放边界。
- 一次性改动大，风险高。

不建议 Phase 3.3 第一步直接实现。

## 建议路线

Phase 3.3 建议分两层推进。

第一层：先完成多路显示布局模型和多个 QWidget 容器方案。

目标：

- 用多个容器承载多个 `VideoRender`。
- 保持 `MediaPipeline` 当前创建 `VideoRender` 的方式。
- 验证 2 路和 4 路显示。
- 明确 active stream 与画面选择的关系。
- 不追求统一 OpenGL 多纹理性能。

第二层：在多路显示语义稳定后，再预研统一渲染器。

目标：

- 抽象渲染输出边界。
- 决定 `MediaPipeline` 是否继续持有 `VideoRender`。
- 设计 `MultiViewRenderer` 如何接收每路视频帧或纹理。
- 再决定是否进行大改。

## 布局模型建议

先定义布局枚举，不急于做复杂 UI：

```cpp
enum class MultiViewLayout {
    Single,
    Grid2x2,
    Grid3x3,
    Grid4x4,
    FocusPlusList
};
```

第一版只建议实现：

- `Single`
- `Grid2x2`

原因：

- 当前重点是验证布局容器生命周期。
- 2x2 足够验证 4 路文件源。
- 3x3 / 4x4 会很快碰到性能问题，不适合作为第一步。

## UI 语义建议

第一版多路显示 UI 建议：

- 点击某一路画面，设置为 active stream。
- active stream 用边框或列表标记表示。
- active audio stream 仍由 `Audio` 按钮显式选择，不自动跟随点击。
- 单路全屏暂不做。
- 多路布局切换先支持 `Single` 和 `2x2`。

## 架构边界

Phase 3.3 仍需遵守：

- `MainWindow` 不直接访问 `MultiStreamManager`。
- `MainWindow` 不直接访问 `MediaPipeline`。
- `MediaPlayController` 作为 UI 对外入口。
- `MultiStreamManager` 负责 stream 生命周期和 id。
- `MediaPipeline` 继续保持单路职责。
- 布局状态不能塞进 `MediaPipeline`。

如果第一版使用多个 QWidget 容器，布局管理可以先放在 UI 层，但业务操作仍必须通过 Controller。

如果后续引入统一 `MultiViewRenderer`，建议新增明确的渲染边界，而不是让 Manager 直接操作 OpenGL 细节。

## 已知风险

### 1. 多个 VideoRender 的资源占用

多个 `QOpenGLWidget` 可能带来额外上下文和显存开销。第一版应该限制验证路数，建议先 2 路，再 4 路。

### 2. QWidget 生命周期

布局切换时不要随意删除正在被 Pipeline 使用的容器。应该先停止或移除 stream，再释放对应容器。

### 3. 当前最小验证 UI 与最终 UI 不一致

当前 `Streams` 列表是验证入口，不是最终监控系统 UI。Phase 3.3 可以继续复用它，但不要把验证 UI 当成最终设计。

### 4. 无音频流仍要持续覆盖

多路显示阶段仍要保留无音频文件验证，因为摄像头和工业相机通常没有音频流。

## 结论

Phase 3.3 应先做多路显示布局预研和任务拆分，不应直接实现 `MultiViewRenderer`。

推荐下一步：

1. 写 `Phase3_3_多路显示布局实施计划.md`。
2. 在 `TODO.md` 中拆分 Phase 3.3 小任务。
3. 第一轮实现选择“多个 `VideoRender` + 多个 QWidget 容器”的低风险路线。
4. 在 2 路、4 路显示稳定后，再进入 `MultiViewRenderer` 统一渲染预研。
