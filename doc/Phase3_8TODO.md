# TODO: Phase 3.8 MultiViewRenderer 单路真实帧接入

## 当前进度

- 已完成：
  - Phase 3.7 已完成视频帧输出边界准备。
  - `VideoRender` 已拆清内部帧调度边界。
  - `IVideoFrameSink` 已落地，`VideoRender` 已作为默认实现。
  - `MultiViewRenderer` 已具备 layout、slot、viewport、点击命中能力。
- 未完成：
  - `MultiViewRenderer` 尚未持有真实视频帧。
  - `MultiViewRenderer` 尚未上传或绘制 YUV 纹理。
  - 尚未实现 `SingleStreamFrameSinkAdapter`。
  - 尚未提供实验路径把单路 pipeline 接到 `MultiViewRenderer`。
- 已知风险：
  - OpenGL 纹理必须在 `MultiViewRenderer` 上下文线程创建、释放和上传。
  - `AVFrame*` 跨线程投递时必须保证所有权不泄漏、不重复释放。
  - renderer 销毁后不能有 queued frame 访问已释放对象。
  - `StreamId` 不能进入 `MediaPipeline`。
- 本轮范围：
  - Phase 3.8 只做单路真实帧接入。
  - 第一版只验证 slot 0。
  - 当前多个 `VideoRender` 路径继续保留为稳定回退。

## 阶段目标

在不破坏当前 `VideoRender` 默认播放路径的前提下，为 `MultiViewRenderer` 建立单路真实帧接入能力。

目标链路：

```text
MediaPipeline
    -> 单路帧调度
        -> IVideoFrameSink
            -> SingleStreamFrameSinkAdapter(streamId)
                -> MultiViewRenderer::PresentFrame(streamId, frame)
                    -> slot 0
                    -> current frame / texture
                    -> paintGL()
```

## 架构边界

- `MediaPipeline` 继续保持单路职责。
- `MediaPipeline` 不持有 `StreamId`。
- `MediaPipeline` 不直接依赖 `MultiViewRenderer`。
- `MultiViewRenderer` 不访问 `VFrame_`。
- `MultiViewRenderer` 不访问 `SyncManager`。
- `MultiViewRenderer` 不管理 demuxer、decoder、audio player。
- `SingleStreamFrameSinkAdapter` 负责把单路 sink 调用转换为带 `StreamId` 的渲染调用。
- `VideoRender` 默认路径必须保留，实验路径失败时可以回退。

## 参考文档

- `doc/架构指导.md`
- `doc/Phase3_7_视频帧输出边界实现报告.md`
- `doc/Phase3_7_MultiViewRenderer单路真实帧接入计划.md`
- `doc/Phase3_7_帧所有权与清理协议.md`
- `doc/Phase3_7_IVideoFrameSink接口草案.md`

## 3.8.1 单路真实帧接入 TODO 拆分

**目标**：建立 Phase 3.8 独立 TODO，明确单路真实帧接入的实施边界、步骤、验收标准和回退策略。

**预计修改文件**：

- `doc/Phase3_8TODO.md`
- `doc/TODO.md`

**任务清单**：

- [x] 明确 Phase 3.8 阶段目标。
- [x] 明确当前已完成和未完成内容。
- [x] 明确架构边界。
- [x] 拆分 3.8.2 到 3.8.7 任务。
- [x] 为每个任务补充修改文件、验收标准、测试流程和 commit message。
- [x] 在主 `doc/TODO.md` 中增加 Phase 3.8 入口。

**验收标准**：

- [x] 能说明 Phase 3.8 只接一路真实帧。
- [x] 能说明为什么保留 `VideoRender` 回退路径。
- [x] 能说明 `StreamId` 不进入 `MediaPipeline`。
- [x] 后续任务可以按 TODO 顺序逐步执行。

**测试流程**：

- [x] 本任务为 TODO 拆分，不运行程序。
- [x] 静态检查：`git diff --check -- doc\Phase3_8TODO.md doc\TODO.md`

**Commit message**：

```text
docs: 拆分 Phase 3.8 单路真实帧接入任务

- 新增 Phase 3.8 独立 TODO
- 明确 MultiViewRenderer 单路真实帧接入目标
- 明确 VideoRender 默认路径继续作为回退
- 明确 StreamId 不进入 MediaPipeline 的架构边界
- 拆分帧状态模型、adapter、接帧接口、slot 0 绘制、实验路径和验收报告任务
- 在主 TODO 中增加 Phase 3.8 入口
```

## 3.8.2 MultiViewRenderer 帧状态模型设计

**目标**：设计 `MultiViewRenderer` 内部每路渲染状态，为后续接管 `AVFrame*` 和上传纹理做准备。本步骤先做文档或最小空结构，不接真实帧。

**预计修改文件**：

- `doc/Phase3_8_MultiViewRenderer帧状态模型设计.md`
- `doc/Phase3_8TODO.md`

**任务清单**：

- [x] 设计 `StreamRenderState` 的字段。
- [x] 明确 `AVFrame* currentFrame` 所有权。
- [x] 明确 texture 状态是 `QOpenGLTexture*` 还是 OpenGL texture id。
- [x] 明确 dirty flag 的作用。
- [x] 明确 width / height / SAR 的保存位置。
- [x] 明确 `streamId -> StreamRenderState` 容器。
- [x] 明确状态释放时机。
- [x] 暂不改 `MultiViewRenderer` 代码。

**验收标准**：

- [x] 能说明一条 stream 在 `MultiViewRenderer` 中需要保存哪些状态。
- [x] 能说明 current frame 与 texture 的生命周期关系。
- [x] 能说明状态释放由 `ClearFrame(streamId)` 和 `DetachStream(streamId)` 分别负责什么。
- [x] 没有引入真实帧绘制代码。

**测试流程**：

- [x] 本任务为设计文档，不运行程序。
- [x] 静态检查：`git diff --check -- doc\Phase3_8_MultiViewRenderer帧状态模型设计.md doc\Phase3_8TODO.md`

**Commit message**：

```text
docs: 设计 MultiViewRenderer 帧状态模型

- 新增 MultiViewRenderer 帧状态模型设计文档
- 明确 StreamRenderState 的 current frame、texture、dirty、尺寸和 SAR 字段
- 明确 streamId 到渲染状态的管理方式
- 明确 ClearFrame 和 DetachStream 的状态释放边界
- 为后续 slot 0 真实帧绘制做准备
```

## 3.8.3 SingleStreamFrameSinkAdapter 空壳

**目标**：新增单路 sink adapter 空壳，实现 `IVideoFrameSink`，用于后续把单路 frame sink 调用转发到带 `StreamId` 的 `MultiViewRenderer`。本步骤暂不连接真实 pipeline。

**预计修改文件**：

- `src/infrastructure/singlestreamframesinkadapter.h`
- `src/infrastructure/singlestreamframesinkadapter.cpp`
- `CMakeLists.txt`
- `doc/Phase3_8TODO.md`

**任务清单**：

- [x] 新增 `SingleStreamFrameSinkAdapter` 类。
- [x] 继承 `IVideoFrameSink`。
- [x] 持有 `QPointer<MultiViewRenderer>` 或等价弱引用。
- [x] 持有 `MultiViewRenderer::StreamId`。
- [x] 实现 `PresentFrame(AVFrame* frame)` 空壳转发策略。
- [x] 实现 `ClearFrame()` 空壳转发策略。
- [x] 明确 renderer 为空或已销毁时释放 frame。
- [x] 暂不接 `MediaPipeline`。

**验收标准**：

- [x] adapter 可以表达单路 sink 到多路 renderer 的边界。
- [x] adapter 不让 `StreamId` 进入 `MediaPipeline`。
- [x] adapter 不创建 OpenGL 资源。
- [x] adapter 不访问 `VFrame_` 或 `SyncManager`。
- [x] 暂不改变当前播放路径。

**测试流程**：

- [x] 静态检查：`git diff --check -- src\infrastructure\singlestreamframesinkadapter.h src\infrastructure\singlestreamframesinkadapter.cpp CMakeLists.txt doc\Phase3_8TODO.md`
- [ ] 人工运行建议：当前默认 `VideoRender` 播放路径不变。

**Commit message**：

```text
feat: 增加单路帧 sink 适配器空壳

- 新增 SingleStreamFrameSinkAdapter 类
- 实现 IVideoFrameSink 单路接口
- 持有 MultiViewRenderer 弱引用和 StreamId
- 为 PresentFrame 和 ClearFrame 建立转发边界
- 保持 StreamId 不进入 MediaPipeline
- 暂不接入真实播放路径
```

## 3.8.4 MultiViewRenderer 单路接帧接口空壳

**目标**：给 `MultiViewRenderer` 增加带 `StreamId` 的接帧和清理接口，先完成 `AVFrame*` 所有权接管与释放，不做 OpenGL 纹理上传。

**预计修改文件**：

- `src/infrastructure/multiviewrenderer.h`
- `src/infrastructure/multiviewrenderer.cpp`
- `doc/Phase3_8TODO.md`

**任务清单**：

- [x] 增加 `PresentFrame(StreamId streamId, AVFrame* frame)`。
- [x] 增加 `ClearFrame(StreamId streamId)`。
- [x] 增加 `DetachStream(StreamId streamId)`。
- [x] 建立 `streamId -> currentFrame` 最小状态。
- [x] `PresentFrame` 替换旧帧并接管新帧所有权。
- [x] `ClearFrame` 清理当前帧但保留 stream 状态。
- [x] `DetachStream` 清理当前帧并移除 stream 状态。
- [x] 调用 `update()` 刷新空壳画面。
- [x] 暂不上传纹理。

**验收标准**：

- [x] `MultiViewRenderer` 可以安全接管和释放一路 `AVFrame*`。
- [x] `PresentFrame(streamId, frame)` 所有权语义清晰。
- [x] `ClearFrame` 与 `DetachStream` 职责不同。
- [x] 未修改 `MediaPipeline`。
- [x] 未接真实播放路径。

**测试流程**：

- [x] 静态检查：`git diff --check -- src\infrastructure\multiviewrenderer.h src\infrastructure\multiviewrenderer.cpp doc\Phase3_8TODO.md`
- [ ] 人工运行建议：当前多画面空壳显示和点击行为不变。

**Commit message**：

```text
feat: 增加 MultiViewRenderer 单路接帧接口空壳

- 增加 PresentFrame(streamId, frame) 接口
- 增加 ClearFrame(streamId) 和 DetachStream(streamId) 接口
- 建立 streamId 到 current frame 的最小状态
- 明确 AVFrame 所有权接管和释放语义
- 暂不实现 OpenGL 纹理上传
- 保持当前播放路径不变
```

## 3.8.5 slot 0 单路帧绘制

**目标**：在 `MultiViewRenderer` 中将一路真实 `AVFrame*` 上传为纹理并绘制到 slot 0，先只验证一路文件视频。

**预计修改文件**：

- `src/infrastructure/multiviewrenderer.h`
- `src/infrastructure/multiviewrenderer.cpp`
- `doc/Phase3_8TODO.md`

**任务清单**：

- [x] 为 `StreamRenderState` 增加 Y/U/V 纹理状态。
- [x] 在 OpenGL 上下文线程创建和释放纹理。
- [x] 支持 YUV420P 帧上传。
- [x] 对不支持格式输出诊断日志。
- [x] 在 `paintGL()` 中找到 slot 0 对应 stream。
- [x] 将该 stream 的纹理绘制到 slot 0 viewport。
- [x] 保留空 slot 背景绘制。
- [x] 暂不接多路绘制。

**验收标准**：

- [x] 一路 YUV420P 视频帧可以显示到 slot 0。
- [x] 空 slot 仍正常显示。
- [x] 不支持格式不会崩溃。
- [x] OpenGL 资源释放边界清晰。
- [x] 当前 `VideoRender` 默认路径仍可回退。

**测试流程**：

- [x] 静态检查：`git diff --check -- src\infrastructure\multiviewrenderer.h src\infrastructure\multiviewrenderer.cpp doc\Phase3_8TODO.md`
- [ ] 人工运行建议：单路普通 MP4。
- [ ] 人工运行建议：无音频视频。
- [ ] 人工运行建议：关闭程序不崩溃。

**Commit message**：

```text
feat: 支持 MultiViewRenderer slot 0 单路帧绘制

- 为 MultiViewRenderer 增加单路纹理状态
- 支持 YUV420P 帧上传到 OpenGL 纹理
- 将一路真实帧绘制到 slot 0 viewport
- 保留空 slot 背景和边框绘制
- 对不支持格式输出诊断日志
- 保持 VideoRender 默认路径可回退
```

## 3.8.6 实验路径接入

**目标**：提供一条可控实验路径，让一条 `MediaPipeline` 可选择通过 `SingleStreamFrameSinkAdapter` 输出到 `MultiViewRenderer`，同时保留 `VideoRender` 默认路径。

**预计修改文件**：

- `src/domain/MediaPipeline.h`
- `src/domain/MediaPipeline.cpp`
- `src/domain/MultiStreamManager.h`
- `src/domain/MultiStreamManager.cpp`
- `src/application/MediaPlayController.h`
- `src/application/MediaPlayController.cpp`
- `src/ui/mainwindow.h`
- `src/ui/mainwindow.cpp`
- `doc/Phase3_8TODO.md`

**任务清单**：

- [ ] 设计外部 sink 注入方式。
- [ ] 确认是否修改 `MediaPipeline::Initialize(...)`。
- [ ] 保持默认路径仍创建 `VideoRender`。
- [ ] 实验路径使用 `SingleStreamFrameSinkAdapter`。
- [ ] 第一版只接一路 stream 到 slot 0。
- [ ] 保持 active audio 策略不变。
- [ ] 保持 `StreamId` 不进入 `MediaPipeline`。
- [ ] 提供失败回退到 `VideoRender` 路径的方式。

**验收标准**：

- [ ] 默认播放路径不变。
- [ ] 实验路径可以显示一路到 `MultiViewRenderer` slot 0。
- [ ] `MediaPipeline` 不保存 `StreamId`。
- [ ] active audio 不退化。
- [ ] stop / remove / shutdown 不访问已释放对象。

**测试流程**：

- [ ] 静态检查：`git diff --check -- <本任务修改文件>`
- [ ] 人工运行建议：默认路径普通 MP4。
- [ ] 人工运行建议：实验路径普通 MP4。
- [ ] 人工运行建议：实验路径无音频视频。
- [ ] 人工运行建议：seek、stop、remove stream、关闭程序。

**Commit message**：

```text
feat: 接入 MultiViewRenderer 单路真实帧实验路径

- 增加单路 pipeline 到 MultiViewRenderer 的实验路径
- 通过 SingleStreamFrameSinkAdapter 转发可显示帧
- 保持 VideoRender 默认播放路径不变
- 保持 StreamId 不进入 MediaPipeline
- 保持 active audio 策略不变
- 支持一路视频显示到 slot 0
```

## 3.8.7 单路接入验收报告

**目标**：收尾 Phase 3.8，记录单路真实帧接入结果、限制和是否可以进入多路真实帧扩展。

**预计修改文件**：

- `doc/Phase3_8_MultiViewRenderer单路真实帧接入验收报告.md`
- `doc/Phase3_8TODO.md`

**任务清单**：

- [ ] 记录默认 `VideoRender` 路径是否可回退。
- [ ] 记录实验路径普通 MP4 验证结果。
- [ ] 记录实验路径无音频视频验证结果。
- [ ] 记录 seek / stop / remove stream / shutdown 验证结果。
- [ ] 记录 OpenGL 线程和 `AVFrame*` 所有权风险是否解决。
- [ ] 记录当前不支持格式和限制。
- [ ] 判断是否进入多路真实帧扩展。

**验收标准**：

- [ ] 能说明单路真实帧接入是否成功。
- [ ] 能说明是否保留稳定回退路径。
- [ ] 能说明后续扩展多路前还要解决的问题。
- [ ] 能说明是否可以进入下一阶段。

**测试流程**：

- [ ] 本任务为报告文档，不运行程序。
- [ ] 静态检查：`git diff --check -- doc\Phase3_8_MultiViewRenderer单路真实帧接入验收报告.md doc\Phase3_8TODO.md`

**Commit message**：

```text
docs: 增加 MultiViewRenderer 单路真实帧接入验收报告

- 新增 Phase 3.8 单路真实帧接入验收报告
- 记录默认 VideoRender 路径和实验路径验证结果
- 记录普通 MP4、无音频视频、seek、stop、remove stream 和 shutdown 验收情况
- 记录 OpenGL 线程和 AVFrame 所有权风险
- 明确是否可以进入多路真实帧扩展
```

## 不做范围

- 不直接接多路真实帧。
- 不把 `StreamId` 放进 `MediaPipeline`。
- 不让 `MultiViewRenderer` 访问 `VFrame_`。
- 不让 `MultiViewRenderer` 做音视频同步。
- 不替换默认 `VideoRender` 路径。
- 不处理非 YUV420P 格式转换。
- 不做性能优化。
- 不做多路纹理调度优化。

## 风险与沟通点

- 如果 `MediaPipeline::Initialize(...)` 需要修改，必须先确认签名变化影响范围。
- 如果 queued `PresentFrame` 涉及跨线程所有权，必须先明确投递失败时的释放责任。
- 如果 `MultiViewRenderer` 绘制需要复用 `VideoRender` 的 shader 逻辑，应优先小范围复制必要逻辑，避免提前抽公共渲染基类。
- 如果 slot 0 单路绘制不稳定，应回退到当前 `VideoRender` 默认路径定位问题。
