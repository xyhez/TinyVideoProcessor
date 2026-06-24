# TODO: Phase 3.10 VideoFrameScheduler 抽取

## 当前进度

- 已完成：
  - Phase 3.7 已拆清 `VideoRender` 内部帧调度边界。
  - `IVideoFrameSink` 已落地，`VideoRender` 已作为默认 sink。
  - Phase 3.8 已完成 `MultiViewRenderer` 单路真实帧接入。
  - Phase 3.9 已完成 `MultiViewRenderer` 多 slot 真实帧扩展。
  - 外部 sink 路径下旧 `VideoRender` widget 的可见职责已收口。
  - `VideoFrameScheduler` 抽取方案已完成。
  - `VideoFrameScheduler` 空壳类已新增并接入构建文件。
- 未完成：
  - `VideoRender` 仍持有 `VFrame_`、`syncmanager`、播放速度和自调度循环。
  - `MediaPipeline` 仍通过创建 `VideoRender` 间接获得视频帧调度能力。
  - `MultiViewRenderer` 尚未成为 UI 唯一视频显示区域。
- 已知风险：
  - 直接删除 `VideoRender` 会同时破坏取帧、同步判断、丢帧追赶和 VIDEO sync 时钟刷新。
  - 调度器抽取如果一次性改动 `MediaPipeline`、`VideoRender`、`MultiViewRenderer` 和 UI，回归定位会困难。
  - `AVFrame*` 所有权必须保持单一，晚帧释放、可显示帧转交和 stop / seek 清理不能混淆。
  - `QTimer::singleShot()` 调度对象迁移后，必须确认对象销毁时不会留下 queued 调用访问已释放对象。
- 本轮范围：
  - 抽出单路 `VideoFrameScheduler`。
  - 让调度器负责取帧、同步判断、丢帧、视频时钟刷新和下一轮调度。
  - 让 sink 只负责接收可显示帧和清理当前显示帧。
  - 不让 `MultiViewRenderer` 访问 `VFrame_` 或 `SyncManager`。
  - 不把 `StreamId` 放进 `MediaPipeline` 或 `VideoFrameScheduler`。
  - 不处理非 YUV420P 格式转换和性能优化。

## 阶段目标

把当前仍留在 `VideoRender` 内部的单路视频帧调度能力抽成独立 `VideoFrameScheduler`，让后续 UI 可以逐步全面转向 `MultiViewRenderer`。

目标链路：

```text
MediaPipeline
    -> VideoFrameScheduler
        -> VFrame_ / SyncManager / frameRate / videoTimeBase / playbackSpeed
        -> IVideoFrameSink::PresentFrame(frame)
            -> 默认路径：VideoRender
            -> 统一渲染路径：SingleStreamFrameSinkAdapter -> MultiViewRenderer
```

最终边界：

- `VideoFrameScheduler` 负责单路帧调度。
- `IVideoFrameSink` 负责接收可显示帧。
- `VideoRender` 降级为默认单路可见 sink。
- `MultiViewRenderer` 继续只负责多路绘制。
- `MediaPipeline` 组合调度器和 sink，不直接依赖多路渲染器。

## 架构边界

- `VideoFrameScheduler` 不继承 `QOpenGLWidget`。
- `VideoFrameScheduler` 不做 OpenGL 绘制。
- `VideoFrameScheduler` 不持有 `StreamId`。
- `VideoFrameScheduler` 不知道 `MultiViewRenderer`。
- `MultiViewRenderer` 不访问 `VFrame_`。
- `MultiViewRenderer` 不访问 `SyncManager`。
- `VideoRender` 不再负责从 `VFrame_` 取帧。
- `VideoRender` 保留默认 widget sink 能力，便于回退。

## 参考文档

- `doc/架构指导.md`
- `doc/Phase3_7_视频帧调度边界设计.md`
- `doc/Phase3_7_IVideoFrameSink接口草案.md`
- `doc/Phase3_7_VideoRender适配方案.md`
- `doc/Phase3_9_MultiViewRenderer多slot真实帧扩展验收报告.md`

## 3.10.1 VideoFrameScheduler 抽取 TODO 拆分

**目标**：建立 Phase 3.10 独立 TODO，明确调度器抽取的实施边界、任务顺序、验收标准和不做范围。

**预计修改文件**：

- `doc/Phase3_10TODO.md`
- `doc/TODO.md`

**任务清单**：

- [x] 明确 Phase 3.10 阶段目标。
- [x] 明确当前已完成和未完成内容。
- [x] 明确 `VideoFrameScheduler`、`IVideoFrameSink`、`VideoRender`、`MultiViewRenderer` 的职责边界。
- [x] 拆分 3.10.2 到 3.10.8 任务。
- [x] 为每个任务补充修改文件、验收标准、测试流程和 commit message。
- [x] 在主 `doc/TODO.md` 中增加 Phase 3.10 入口。

**验收标准**：

- [x] 能说明为什么不能直接删除 `VideoRender`。
- [x] 能说明 `VideoFrameScheduler` 负责什么、不负责什么。
- [x] 能说明全面转向 `MultiViewRenderer` 的前置条件。
- [x] 后续任务可以按 TODO 顺序逐步执行。

**测试流程**：

- [x] 本任务为文档拆分，不运行程序。
- [x] 静态检查：`git diff --check -- doc\Phase3_10TODO.md doc\TODO.md`

**Commit message**：

```text
docs: 拆分 Phase 3.10 VideoFrameScheduler 抽取任务

- 新增 Phase 3.10 独立 TODO
- 明确 VideoFrameScheduler 抽取的阶段目标
- 明确调度器、sink、VideoRender 和 MultiViewRenderer 的职责边界
- 明确全面转向 MultiViewRenderer 的前置条件
- 拆分调度器方案、空壳类、调度逻辑迁移、MediaPipeline 组合和验收报告任务
- 在主 TODO 中增加 Phase 3.10 入口
```

## 3.10.2 VideoFrameScheduler 抽取方案确认

**目标**：在动代码前确认调度器接口、所有权、线程、生命周期和迁移顺序，避免直接从 `VideoRender` 中硬搬代码。

**预计修改文件**：

- `doc/Phase3_10_VideoFrameScheduler抽取方案.md`
- `doc/Phase3_10TODO.md`

**任务清单**：

- [x] 梳理当前 `VideoRender` 中仍属于调度器的字段和方法。
- [x] 设计 `VideoFrameScheduler` 的 public API。
- [x] 设计 `VideoFrameScheduler` 的输入：`VFrame_`、`syncmanager`、frameRate、videoTimeBase、playbackSpeed。
- [x] 设计 `VideoFrameScheduler` 的输出：`IVideoFrameSink::PresentFrame()` 和 `IVideoFrameSink::ClearFrame()`。
- [x] 明确 scheduler 持有 sink 指针还是由 `MediaPipeline` 注入 sink。
- [x] 明确 `Start()`、`Stop()`、`Pause()`、`SetSpeed()`、`ClearOutput()` 的语义。
- [x] 明确 queued 调用和对象销毁顺序。
- [x] 明确第一版不改变播放行为。

**验收标准**：

- [x] 能说明 scheduler 如何替代 `VideoRender::RenderFrame()`。
- [x] 能说明 stop / seek / shutdown 时谁清理当前输出帧。
- [x] 能说明 `AVFrame*` 从队列到 sink 的所有权转移。
- [x] 能说明为什么 scheduler 不持有 `StreamId`。

**测试流程**：

- [x] 本任务为设计文档，不运行程序。
- [x] 静态检查：`git diff --check -- doc\Phase3_10_VideoFrameScheduler抽取方案.md doc\Phase3_10TODO.md`

**Commit message**：

```text
docs: 设计 VideoFrameScheduler 抽取方案

- 新增 VideoFrameScheduler 抽取方案文档
- 明确调度器 API、输入输出和生命周期
- 明确 AVFrame 所有权转移与清理协议
- 明确 scheduler 不持有 StreamId、不依赖 MultiViewRenderer
- 明确 stop、seek、shutdown 下的输出清理职责
```

## 3.10.3 新增 VideoFrameScheduler 空壳

**目标**：新增独立调度器类的最小骨架，先建立接口和依赖边界，不迁移调度行为。

**预计修改文件**：

- `src/infrastructure/videoframescheduler.h`
- `src/infrastructure/videoframescheduler.cpp`
- `CMakeLists.txt`
- `doc/Phase3_10TODO.md`

**任务清单**：

- [x] 新增 `VideoFrameScheduler` 类。
- [x] 让 scheduler 使用 `QObject`，便于沿用 Qt 自调度模型。
- [x] 增加 `Initialize(...)` 接收 frame queue、sync manager、frameRate、videoTimeBase。
- [x] 增加 `SetFrameSink(IVideoFrameSink*)`。
- [x] 增加 `Start()`、`Stop()`、`Pause()`、`SetSpeed()`。
- [x] 增加空实现或最小保护实现，不改变现有播放路径。
- [x] 加入 CMake 构建文件。

**验收标准**：

- [x] 新类职责只包含调度，不包含 OpenGL 绘制。
- [x] 新类不持有 `StreamId`。
- [x] 现有 `VideoRender` 播放路径不变。
- [x] CMake 能找到新增源文件。

**测试流程**：

- [x] 静态检查：`git diff --check -- src\infrastructure\videoframescheduler.h src\infrastructure\videoframescheduler.cpp CMakeLists.txt doc\Phase3_10TODO.md`
- [ ] 人工运行建议：普通文件播放、暂停、关闭程序。

**Commit message**：

```text
feat: 新增 VideoFrameScheduler 空壳

- 新增独立视频帧调度器类
- 定义调度器初始化、sink 注入、启动停止和速度控制入口
- 保持调度器不依赖 StreamId 和 MultiViewRenderer
- 暂不迁移 VideoRender 现有调度行为
- 将新增源文件接入 CMake
```

## 3.10.4 迁移 VideoRender 内部调度逻辑到 scheduler

**目标**：把 `VideoRender` 中已经拆清的调度方法迁移到 `VideoFrameScheduler`，先让 `VideoRender` 内部组合 scheduler，保持外部行为不变。

**预计修改文件**：

- `src/infrastructure/videorender.h`
- `src/infrastructure/videorender.cpp`
- `src/infrastructure/videoframescheduler.h`
- `src/infrastructure/videoframescheduler.cpp`
- `doc/Phase3_10TODO.md`

**任务清单**：

- [ ] 将 `CanRenderFrame()` 迁移到 scheduler。
- [ ] 将 `CalculateScaledFrameInterval()` 迁移到 scheduler。
- [ ] 将 `PeekNextFrame()` 迁移到 scheduler。
- [ ] 将 `EvaluateFrameTiming()` 迁移到 scheduler。
- [ ] 将 `DropLateFrame()` 迁移到 scheduler。
- [ ] 将 `PopDisplayableFrame()` 迁移到 scheduler。
- [ ] 将 `RefreshVideoClockIfNeeded()` 迁移到 scheduler。
- [ ] 将自调度循环迁移到 scheduler。
- [ ] `VideoRender` 作为 sink 接收 scheduler 输出帧。

**验收标准**：

- [ ] `VideoRender` 不再直接访问 `VFrame_`。
- [ ] `VideoRender` 不再直接访问 `syncmanager`。
- [ ] 默认单路播放路径行为不变。
- [ ] 外部 sink 路径仍可把帧送到 `MultiViewRenderer`。

**测试流程**：

- [ ] 静态检查：`git diff --check -- src\infrastructure\videorender.h src\infrastructure\videorender.cpp src\infrastructure\videoframescheduler.h src\infrastructure\videoframescheduler.cpp doc\Phase3_10TODO.md`
- [ ] 人工运行建议：普通 MP4 播放、暂停、继续、seek、stop、关闭。
- [ ] 人工运行建议：无音频视频播放和关闭。
- [ ] 人工运行建议：两路文件同时进入 `MultiViewRenderer`。

**Commit message**：

```text
refactor: 将 VideoRender 帧调度迁移到 VideoFrameScheduler

- 将取帧、同步判断、丢帧和视频时钟刷新迁移到 VideoFrameScheduler
- 让 VideoRender 作为默认 IVideoFrameSink 接收可显示帧
- 保持 VideoRender 的 OpenGL 绘制职责不变
- 保持外部 frame sink 路径不变
- 保持 MultiViewRenderer 不访问 VFrame 和 SyncManager
```

## 3.10.5 MediaPipeline 组合 scheduler 与 sink

**目标**：让 `MediaPipeline` 显式组合 `VideoFrameScheduler` 和 `IVideoFrameSink`，不再依赖 `VideoRender` 获得调度能力。

**预计修改文件**：

- `src/domain/MediaPipeline.h`
- `src/domain/MediaPipeline.cpp`
- `src/infrastructure/videorender.h`
- `src/infrastructure/videorender.cpp`
- `src/infrastructure/videoframescheduler.h`
- `src/infrastructure/videoframescheduler.cpp`
- `doc/Phase3_10TODO.md`

**任务清单**：

- [ ] 在 `MediaPipeline` 中持有 `VideoFrameScheduler`。
- [ ] 默认路径创建 `VideoRender` 作为 sink。
- [ ] 外部 sink 路径使用传入的 `IVideoFrameSink`。
- [ ] scheduler 统一接收当前 sink。
- [ ] `Start()` / `Pause()` / `Stop()` / `SeekTo()` / `ReleaseResources()` 改为控制 scheduler。
- [ ] `VideoRender` 不再暴露调度启动停止语义，或仅保留兼容壳。

**验收标准**：

- [ ] `MediaPipeline` 的视频调度入口变为 scheduler。
- [ ] `VideoRender` 只作为默认显示 sink。
- [ ] 外部 sink 路径不需要创建隐藏的 `VideoRender` 来调度。
- [ ] `StreamId` 仍不进入 `MediaPipeline`。

**测试流程**：

- [ ] 静态检查：`git diff --check -- src\domain\MediaPipeline.h src\domain\MediaPipeline.cpp src\infrastructure\videorender.h src\infrastructure\videorender.cpp src\infrastructure\videoframescheduler.h src\infrastructure\videoframescheduler.cpp doc\Phase3_10TODO.md`
- [ ] 人工运行建议：默认单路播放仍能显示。
- [ ] 人工运行建议：多路统一渲染仍能显示多路画面。
- [ ] 人工运行建议：seek / stop / pause / shutdown 不阻塞、不残留旧帧。

**Commit message**：

```text
refactor: 让 MediaPipeline 显式组合 VideoFrameScheduler

- 在 MediaPipeline 中创建和控制 VideoFrameScheduler
- 默认路径使用 VideoRender 作为 IVideoFrameSink
- 外部统一渲染路径直接使用注入的 frame sink
- 移除外部 sink 路径对隐藏 VideoRender 调度能力的依赖
- 保持 StreamId 不进入 MediaPipeline
```

## 3.10.6 收口 VideoRender 为纯默认 sink

**目标**：清理 `VideoRender` 中残留的调度接口和状态，让它只承担默认 widget 显示职责。

**预计修改文件**：

- `src/infrastructure/videorender.h`
- `src/infrastructure/videorender.cpp`
- `doc/Phase3_10TODO.md`

**任务清单**：

- [ ] 移除或废弃 `StartRendering()`、`StopRendering()`、`Pause()` 的调度语义。
- [ ] 移除 frame queue、sync manager、frameRate、videoTimeBase、speed 等调度字段。
- [ ] 保留 `PresentFrame()` 和 `ClearFrame()`。
- [ ] 保留 OpenGL 初始化、纹理上传和绘制。
- [ ] 保留默认 widget sink 回退能力。

**验收标准**：

- [ ] `VideoRender` 不再包含调度职责。
- [ ] `VideoRender` 仍可作为默认单路显示 sink。
- [ ] `MultiViewRenderer` 统一渲染路径不受影响。

**测试流程**：

- [ ] 静态检查：`git diff --check -- src\infrastructure\videorender.h src\infrastructure\videorender.cpp doc\Phase3_10TODO.md`
- [ ] 人工运行建议：默认单路播放。
- [ ] 人工运行建议：多路统一渲染。

**Commit message**：

```text
refactor: 收口 VideoRender 为默认视频帧 sink

- 移除 VideoRender 中残留的视频帧调度状态
- 保留 PresentFrame 和 ClearFrame sink 语义
- 保留 OpenGL widget 默认显示能力
- 让调度职责完全归属 VideoFrameScheduler
```

## 3.10.7 UI 全面转向 MultiViewRenderer 条件复核

**目标**：在调度器抽取完成后，复核是否可以移除旧多 `VideoRender` 容器路径，让 UI 以 `MultiViewRenderer` 作为唯一视频显示区域。

**预计修改文件**：

- `doc/Phase3_10_UI全面转向MultiViewRenderer复核报告.md`
- `doc/Phase3_10TODO.md`

**任务清单**：

- [ ] 检查默认路径是否仍需要旧 `VideoRender` widget 容器。
- [ ] 检查播放列表、文件多路、摄像头多路是否都能通过统一 sink 进入 `MultiViewRenderer`。
- [ ] 检查无音频流、seek、stop all、pause all、shutdown 是否不依赖旧容器。
- [ ] 检查 `MainWindow` 中旧视频容器是否只剩布局兼容用途。
- [ ] 给出是否进入 UI 替换任务的判断。

**验收标准**：

- [ ] 能说明旧 UI 容器是否可以删除。
- [ ] 能说明删除旧容器会影响哪些文件。
- [ ] 能说明全面转向 `MultiViewRenderer` 的剩余风险。

**测试流程**：

- [ ] 本任务为复核报告，不运行程序。
- [ ] 静态检查：`git diff --check -- doc\Phase3_10_UI全面转向MultiViewRenderer复核报告.md doc\Phase3_10TODO.md`

**Commit message**：

```text
docs: 复核 UI 全面转向 MultiViewRenderer 条件

- 新增 UI 全面转向 MultiViewRenderer 复核报告
- 检查旧 VideoRender 容器路径是否仍有必要
- 检查文件、多路、摄像头、无音频和生命周期场景
- 判断是否可以进入旧 UI 容器移除任务
```

## 3.10.8 Phase 3.10 验收报告

**目标**：收尾 Phase 3.10，记录 `VideoFrameScheduler` 抽取结果、默认路径、多路统一渲染路径和 UI 全面迁移条件。

**预计修改文件**：

- `doc/Phase3_10_VideoFrameScheduler抽取验收报告.md`
- `doc/Phase3_10TODO.md`

**任务清单**：

- [ ] 记录 scheduler 抽取结果。
- [ ] 记录 `VideoRender` 是否已降级为默认 sink。
- [ ] 记录 `MultiViewRenderer` 多路路径是否仍正常。
- [ ] 记录 stop / seek / shutdown 生命周期验证结果。
- [ ] 记录是否可以进入 UI 全面替换任务。
- [ ] 记录剩余限制。

**验收标准**：

- [ ] 能说明 Phase 3.10 是否完成。
- [ ] 能说明当前调度、sink、绘制边界。
- [ ] 能说明下一阶段是否全面替换旧 UI。

**测试流程**：

- [ ] 本任务为验收报告，不运行程序。
- [ ] 静态检查：`git diff --check -- doc\Phase3_10_VideoFrameScheduler抽取验收报告.md doc\Phase3_10TODO.md`

**Commit message**：

```text
docs: 增加 VideoFrameScheduler 抽取验收报告

- 新增 Phase 3.10 验收报告
- 记录 VideoFrameScheduler 抽取结果
- 记录 VideoRender 默认 sink 收口结果
- 记录 MultiViewRenderer 多路统一渲染路径验证情况
- 判断是否具备 UI 全面转向 MultiViewRenderer 的条件
```

## 不做范围

- 不让 `MultiViewRenderer` 访问 `VFrame_`。
- 不让 `MultiViewRenderer` 访问 `SyncManager`。
- 不让 `VideoFrameScheduler` 持有 `StreamId`。
- 不在第一步直接删除旧 UI 容器。
- 不处理非 YUV420P 格式转换。
- 不做零拷贝和性能优化。
- 不改变 active audio 策略。

## 风险与沟通点

- 如果 scheduler 抽取后播放时序变化，应优先对比 `VideoRender::RenderFrame()` 原始分支顺序，而不是继续推进 UI 替换。
- 如果外部 sink 路径仍依赖隐藏 `VideoRender`，说明 3.10.5 未完成，不能进入 UI 全面替换。
- 如果 `QTimer::singleShot()` queued 调用在对象销毁后仍触发，应先修正 scheduler 生命周期。
- 如果默认 `VideoRender` sink 和 `MultiViewRenderer` sink 出现行为不一致，应优先检查 `IVideoFrameSink` 所有权和清理语义。
