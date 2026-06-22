# Phase 3.6 MultiViewRenderer OpenGL 学习路线

## 1. 文档目标

本文件用于指导 `VideoRender` 向 `MultiViewRenderer` 演进过程中的 OpenGL 学习和实现拆分。

当前目标不是一次性完成统一多路视频渲染，而是在严格遵循 `doc/架构指导.md` 的前提下，通过可验证的小步骤理解并改造现有 OpenGL 渲染链路。

核心原则：

- `MediaPipeline` 继续保持单路播放管线职责。
- `MultiStreamManager` 继续负责多路管线管理和 `StreamId` 分配。
- `MultiViewRenderer` 只负责多路画面显示、布局、点击命中和后续纹理渲染。
- `MultiViewRenderer` 不持有 `MediaPipeline`。
- `MultiViewRenderer` 不访问 demuxer、decoder、audio player。
- `MultiViewRenderer` 不参与 active audio stream 策略。
- 当前阶段保留多个 `VideoRender` 作为稳定回退路径。

## 2. 当前渲染链路理解

当前真实视频渲染仍由 `VideoRender` 完成，链路大致如下：

```text
MediaPipeline
    -> VideoRender
        -> SyncQueue<AVFrame*>
        -> SyncManager
        -> QOpenGLWidget
        -> Y/U/V 三纹理上传
        -> shader YUV 转 RGB
        -> glDrawArrays 绘制到当前 widget
```

`VideoRender` 当前承担的职责：

- 从视频帧队列取帧。
- 根据音视频同步时钟决定等待、丢帧或渲染。
- 保存当前显示帧。
- 判断输入帧格式是否支持。
- 上传 YUV420P 三个平面到 OpenGL 纹理。
- 使用 shader 完成 YUV 到 RGB 的颜色转换。
- 根据窗口尺寸和视频宽高比计算绘制矩形。
- 在单个 `QOpenGLWidget` 内绘制一路视频。

这说明 `VideoRender` 同时包含两类能力：

```text
播放调度能力：取帧、同步、丢帧、刷新时钟
OpenGL 显示能力：纹理、shader、viewport、绘制
```

后续迁移到 `MultiViewRenderer` 时，不能简单复制 `VideoRender`。更合理的方向是逐步把“显示能力”抽象出来，让统一渲染器只处理 `StreamId + frame/texture + viewport`。

## 3. 需要重点学习的 OpenGL 概念

### 3.1 QOpenGLWidget 生命周期

需要明确三个核心函数的职责：

```text
initializeGL()  初始化 OpenGL 状态、shader、纹理资源
resizeGL()      处理窗口尺寸变化
paintGL()       执行实际绘制
```

学习重点：

- OpenGL 资源必须在有效 OpenGL context 中创建和销毁。
- `QOpenGLWidget::update()` 只请求重绘，不等于立即绘制。
- 后续真实帧接入时，解码线程不能直接操作 OpenGL 纹理。

### 3.2 shader 基础

当前 `VideoRender` 使用：

```text
vertex shader    处理顶点坐标和纹理坐标
fragment shader  采样 Y/U/V 纹理并转换为 RGB
```

学习重点：

- attribute 顶点输入。
- varying 纹理坐标传递。
- sampler2D 纹理采样。
- YUV420P 三平面到 RGB 的转换逻辑。

当前阶段只需要理解现有 shader，不急于改复杂颜色空间处理。

### 3.3 纹理上传

当前 `VideoRender` 使用三张纹理：

```text
Y 纹理：width x height
U 纹理：width/2 x height/2
V 纹理：width/2 x height/2
```

学习重点：

- `QOpenGLTexture::R8_UNorm` 表示单通道 8-bit 纹理。
- `QOpenGLPixelTransferOptions::setRowLength()` 用于处理 FFmpeg linesize。
- 视频宽高变化时需要重新创建纹理。

后续 `MultiViewRenderer` 会从单路纹理变成：

```text
StreamId -> TextureSet
```

### 3.4 viewport 和绘制区域

当前 `VideoRender` 是一个 widget 只画一路视频，基本可以把整个 widget 当成一个显示区域。

`MultiViewRenderer` 需要在一个 `QOpenGLWidget` 内画多路画面：

```text
Single:
    slot 0 -> 整个窗口

Grid2x2:
    slot 0 -> 左上
    slot 1 -> 右上
    slot 2 -> 左下
    slot 3 -> 右下
```

学习重点：

- `glViewport(x, y, w, h)` 指定当前绘制区域。
- Qt 坐标通常左上为原点，OpenGL viewport 坐标左下为原点。
- 点击命中使用 Qt 坐标，绘制使用 OpenGL 坐标，两者要明确转换。

### 3.5 宽高比和画面适配

当前 `VideoRender::CalcRect()` 在归一化设备坐标中计算保持宽高比后的绘制矩形。

后续 `MultiViewRenderer` 中需要把这个问题拆成两层：

```text
第一层：slot viewport 在 widget 中的位置和大小
第二层：视频内容在 viewport 内如何保持宽高比显示
```

当前 3.6 空壳阶段只做第一层，不接真实视频帧。

### 3.6 叠加绘制

`MultiViewRenderer` 后续除了画视频，还要画：

- 空 slot 背景。
- slot 边界。
- active stream 高亮边框。
- 后续可能的 stream 标签、无信号状态、FPS 信息。

学习重点：

- 先用简单清屏和矩形边界验证绘制流程。
- 不要在空壳阶段引入复杂 UI 控件堆叠。
- 叠加信息属于渲染器显示职责，不属于播放控制职责。

## 4. 分阶段学习与实现路线

### 4.1 阶段 A：读懂现有 VideoRender

目标：能解释当前单路 OpenGL 渲染全过程。

检查点：

- 能说明 `initializeGL()` 初始化了哪些内容。
- 能说明 `paintGL()` 为什么先判断帧格式再上传纹理。
- 能说明 Y/U/V 三纹理分别对应 AVFrame 哪些数据。
- 能说明 `linesize` 为什么不能简单等于视频宽度。
- 能说明 `CalcRect()` 如何保持画面比例。
- 能说明当前只支持 `AV_PIX_FMT_YUV420P` 的影响。

产出建议：

- 在个人笔记中画出 `AVFrame -> Texture -> Shader -> Screen` 流程。
- 暂不修改代码。

### 4.2 阶段 B：MultiViewRenderer 空壳

目标：确认统一渲染器类存在，并且不影响当前播放路径。

当前应保持：

```text
MultiViewRenderer
    -> QOpenGLWidget
    -> initializeGL()
    -> paintGL()
    -> glClear()
```

验收点：

- 类可以加入构建。
- 当前多个 `VideoRender` 真实播放路径不变。
- `MultiViewRenderer` 不接真实视频帧。
- `MultiViewRenderer` 不创建或持有任何播放管线。

### 4.3 阶段 C：viewport 布局计算

目标：先用纯数据计算出每个 slot 的显示区域。

建议接口方向：

```text
layout + widget size + viewSlots -> slot rectangles
```

需要支持：

- `Single`
- `Grid2x2`
- 最多 4 个 slot
- 空 slot

重点不是 OpenGL 绘制，而是把布局结果算清楚。

验收点：

- `Single` 返回一个完整区域。
- `Grid2x2` 返回四个等分区域。
- widget 尺寸变化后布局重新计算。
- 空 slot 不影响整体布局。

### 4.4 阶段 D：空画面绘制

目标：在一个 `QOpenGLWidget` 中看到多个区域。

实现顺序：

1. 清空背景。
2. 按 viewport 绘制每个 slot 的背景色。
3. 绘制 slot 边界。
4. 切换 `Single` / `Grid2x2` 验证布局。

当前阶段不做：

- 不上传视频纹理。
- 不接收 `AVFrame`。
- 不实现 YUV shader。
- 不替换 `VideoRender`。

学习重点：

- `glViewport()` 的作用范围。
- 多次绘制时 OpenGL 状态如何影响结果。
- Qt 坐标和 OpenGL viewport 坐标的差异。

### 4.5 阶段 E：active stream 高亮

目标：让 `MultiViewRenderer` 根据 active stream 绘制选中态。

输入关系：

```text
slot index -> StreamId
active StreamId
```

渲染器只负责显示高亮，不负责切换播放状态。

验收点：

- active stream 对应 slot 有高亮边框。
- 切换 active stream 后高亮跟随变化。
- active stream 无效时不显示高亮。
- 空 slot 不显示高亮。

### 4.6 阶段 F：点击命中

目标：点击某个显示区域时，渲染器只发出 `StreamClicked(StreamId)`。

职责边界：

```text
MultiViewRenderer:
    判断点击命中哪个 slot
    如果 slot 有绑定 stream，发出 StreamClicked(streamId)

MainWindow / MediaPlayController:
    决定是否 SetActiveStream()
    决定是否 SetActiveAudioStream()
```

验收点：

- 点击不同 slot 能得到正确 `StreamId`。
- 点击空 slot 不误触发有效 stream。
- `MultiViewRenderer` 不直接调用播放控制接口。

### 4.7 阶段 G：一路真实帧试接入

该阶段不属于当前 3.6 空壳范围，只作为后续预研方向。

目标：先让 `MultiViewRenderer` 接一路真实视频帧，不急于多路。

需要先解决的问题：

- `MediaPipeline` 如何对外输出视频帧。
- `AVFrame` 所有权由谁释放。
- 解码线程如何把帧投递到 UI/OpenGL 线程。
- seek、stop、remove stream 时如何清理旧帧。
- 纹理资源如何按 `StreamId` 管理。

建议只有在 3.6 空壳、viewport、active 高亮、点击命中全部验收后再进入。

### 4.8 阶段 H：多路真实帧接入

该阶段属于统一渲染器正式替代多个 `VideoRender` 的后续目标。

目标形态：

```text
MediaPipeline #1 -> frameReady(streamId, frame)
MediaPipeline #2 -> frameReady(streamId, frame)
MediaPipeline #3 -> frameReady(streamId, frame)

MultiViewRenderer
    -> StreamId -> TextureSet
    -> layout -> viewport
    -> paintGL() 循环绘制多路
```

该阶段必须重新审查性能、内存、帧释放、线程和同步策略。

## 5. 与架构路线的对应关系

`doc/架构指导.md` 中 Phase 3 推荐方向是“单个 OpenGL 上下文 + 多纹理”。`MultiViewRenderer` 正是该方向的基础设施组件。

但是架构路线也要求渐进式演进，每个 Phase 都要可运行、可测试。因此当前不应直接替换稳定播放链路。

推荐迁移顺序：

```text
多个 VideoRender 稳定播放
    -> MultiViewRenderer 空壳
    -> viewport 布局
    -> 空画面绘制
    -> active 高亮
    -> 点击命中
    -> 一路真实帧接入
    -> 多路真实帧接入
    -> 替换多个 VideoRender
```

这样符合：

- 单一职责原则：渲染器只负责显示。
- 开放封闭原则：新增统一渲染器，不直接破坏旧路径。
- 依赖倒置原则：后续通过帧输出抽象连接 pipeline 和 renderer。
- 渐进式演进：每一步都可以独立验证。

## 6. 当前阶段不做范围

为了避免变成补丁式大改，Phase 3.6 暂不做以下内容：

- 不迁移真实视频帧。
- 不删除 `VideoRender`。
- 不重写音视频同步。
- 不修改 `MediaPipeline` 的播放调度。
- 不引入 HDR tone mapping。
- 不做 10-bit / NV12 / P010 等格式统一转换。
- 不做多路渲染性能优化。
- 不做 3x3 / 4x4 / 1+N 布局。

## 7. 学习验收标准

完成 3.6 后，应能回答以下问题：

- `QOpenGLWidget` 的三个核心生命周期函数分别负责什么。
- 当前 `VideoRender` 为什么使用三张纹理。
- `AVFrame::linesize` 对纹理上传有什么影响。
- `glViewport()` 和顶点坐标裁剪空间分别解决什么问题。
- 为什么点击命中要用 Qt 坐标，而绘制 viewport 要注意 OpenGL 坐标。
- 为什么 `MultiViewRenderer` 不应该持有 `MediaPipeline`。
- 为什么真实帧接入前必须先明确帧所有权和线程边界。

如果这些问题都能讲清楚，再进入真实帧接入会更稳。

