# TODO: Phase 3.14 MultiViewRenderer 多路性能优化

## 当前进度

- 已完成：
  - `MultiViewRenderer` 已成为主窗口常规显示区域。
  - `VideoFrameScheduler` 已承担单路取帧、同步判断、丢帧和调度职责。
  - `MultiViewRenderer` 已支持多 slot 绘制、布局切换和聚焦交互。
  - Phase 3.14.1 已完成多路性能指标定义。
  - Phase 3.14.2 已完成 MultiViewRenderer 轻量性能观测实现。
  - Phase 3.14.3 已完成纹理资源复用与上传路径静态实现，待人工运行验收。
  - Phase 3.14.4 已完成渲染刷新节流与无效重绘控制静态实现，待人工运行验收。
  - 已补充 `VideoFrameConverter` 转换日志签名去重，避免多路交替转换时刷屏。
- 前置要求：
  - Phase 3.13 应先解决非 `YUV420P` 和 4K MKV 显示正确性问题。
  - 性能优化必须建立在显示正确的基础上。
- 未完成：
  - 4 路以上扩容前的性能基线。
- 本阶段范围：
  - 优化统一渲染路径的 CPU/GPU/内存压力。
  - 为 Phase 3.15 的 9/16 路真实播放扩容提供数据依据。

## 阶段目标

让 `MultiViewRenderer` 在多路文件/摄像头预览场景下具备可观测、可解释、可调优的性能表现。

本阶段重点不是一次性做极限优化，而是先建立：

- 指标。
- 基线。
- 可复用资源。
- 低风险优化点。
- 扩容前准入标准。

## 架构边界

- 不把性能策略塞进 `MainWindow`。
- 不让 `MultiViewRenderer` 管理播放管线生命周期。
- 不让 `MultiViewRenderer` 做同步判断或队列消费。
- 不让 `VideoFrameScheduler` 感知具体 UI 布局。
- 不通过降低同步正确性换取表面 FPS。
- 不在没有指标前盲目引入 PBO 或复杂异步上传。

## 参考文档

- `doc/架构指导.md`
- `doc/TODO.md`
- `doc/Phase3_10_VideoFrameScheduler抽取验收报告.md`
- `doc/Phase3_11_UI全面转向MultiViewRenderer验收报告.md`
- `doc/Phase3_12_MultiViewRenderer布局与交互验收报告.md`
- `doc/Phase3_13TODO.md`

## 3.14.1 多路性能指标定义

**目标**：先定义性能优化看什么，不直接改核心渲染逻辑。

**预计修改文件**：

- `doc/Phase3_14_多路性能指标设计.md`
- `doc/Phase3_14TODO.md`

**任务清单**：

- [x] 定义渲染 FPS 统计口径。
- [x] 定义每路最后送帧时间统计口径。
- [x] 定义每路纹理上传次数统计口径。
- [x] 定义 dropped / late frame 是否沿用 scheduler 数据。
- [x] 定义 CPU、GPU、内存的人工观测口径。
- [x] 明确性能日志默认不开启或低频输出。
- [x] 明确哪些指标属于 debug 观测，不进入正式 UI。

**验收标准**：

- [x] 能说明 4 路、9 路、16 路验收时看哪些指标。
- [x] 能说明 renderer 指标和 scheduler 指标的边界。
- [x] 能说明日志不会在正常播放中刷屏。

**测试流程**：

- [x] 本任务为设计文档，不运行程序。
- [x] 静态检查：`git diff --check -- doc\Phase3_14_多路性能指标设计.md doc\Phase3_14TODO.md`

**Commit message**：

```text
docs: 设计 MultiViewRenderer 多路性能指标

- 新增多路渲染性能指标设计文档
- 明确 FPS、纹理上传、队列压力和资源占用观测口径
- 区分 renderer 指标和 scheduler 指标边界
- 为 9/16 路扩容建立性能验收基线
```

## 3.14.2 渲染性能轻量观测实现

**目标**：增加低侵入性能观测能力，为优化前后对比提供数据。

**预计修改文件**：

- `src/infrastructure/multiviewrenderer.h`
- `src/infrastructure/multiviewrenderer.cpp`
- `src/infrastructure/decoder.h`
- `src/infrastructure/decoder.cpp`
- 可选：`src/core/AppLog.h`
- `doc/Phase3_14TODO.md`

**任务清单**：

- [x] 在 `MultiViewRenderer` 内部记录 paint 次数。
- [x] 记录每路纹理上传次数。
- [x] 记录每路当前帧尺寸、像素格式和是否发生转换。
- [x] 低频输出多路渲染统计日志。
- [x] 日志低频输出，降低热路径阻塞风险。
- [x] 默认日志频率可控，避免刷屏。
- [x] 完全空闲状态不输出性能日志。
- [x] 帧格式诊断日志只在格式变化时输出。
- [x] 硬解 transfer 格式日志只在格式变化时输出。
- [x] 不把统计逻辑传到 `MediaPipeline`。

**验收标准**：

- [x] 可以判断 4 路播放时渲染器实际刷新频率。
- [x] 可以判断是否存在无效重复纹理上传。
- [x] 可以判断 4K 视频对上传压力的影响。
- [x] 正常播放日志量可控。
- [x] 未绑定 stream 且无帧无上传时不输出空性能日志。
- [x] 同一路同格式连续播放时不重复输出 accepted frame format。
- [x] 硬解连续输出同一软件帧格式时不重复输出 transfer frame format。

**测试流程**：

- [x] 静态检查：`git diff --check -- src\infrastructure\multiviewrenderer.h src\infrastructure\multiviewrenderer.cpp src\infrastructure\decoder.h src\infrastructure\decoder.cpp doc\Phase3_14TODO.md`
- [x] 定向搜索：`rg -n "paintFps|textureUploads|textureRebuilds|lastPresentedElapsedMs|LogPerformanceStatsIfNeeded|MultiViewRenderer perf|Hardware transfer frame format|hasLoggedFrameFormat" src\infrastructure\multiviewrenderer.* src\infrastructure\decoder.* doc\Phase3_14TODO.md`
- [ ] 人工运行建议：1 路、4 路、4K 单路分别观察日志。

**Commit message**：

```text
feat: 增加 MultiViewRenderer 性能观测

- 在统一渲染器中记录绘制和纹理上传统计
- 低频输出多路渲染性能诊断日志
- 保持性能统计不进入 MediaPipeline 和 VideoFrameScheduler
- 为后续多路渲染优化提供对比基线
```

## 3.14.3 纹理资源复用与上传路径优化

**目标**：减少每帧重复创建/销毁纹理和无效上传。

**预计修改文件**：

- `src/infrastructure/multiviewrenderer.h`
- `src/infrastructure/multiviewrenderer.cpp`
- `doc/Phase3_14TODO.md`

**任务清单**：

- [x] 确认每路纹理只在尺寸或格式变化时重建。
- [x] 当前帧尺寸不变时复用已有纹理对象。
- [x] 仅当 `textureDirty` 时上传纹理。
- [x] stream detach 时释放该路纹理资源。
- [x] 布局切换不重建纹理。
- [x] active stream 切换不重建纹理。
- [x] 避免在 paint 路径中做不必要内存分配。

**验收标准**：

- [x] 同一路同尺寸播放时纹理不会反复创建。
- [x] 布局切换不会导致所有纹理重建。
- [x] 移除 stream 后对应纹理释放。
- [ ] 4 路播放时 CPU 占用和卡顿不增加。

**测试流程**：

- [x] 静态检查：`git diff --check -- src\infrastructure\multiviewrenderer.h src\infrastructure\multiviewrenderer.cpp doc\Phase3_14TODO.md`
- [x] 定向搜索：`rg -n "textureDirty|FreeTextures|QOpenGLTexture|UploadTexturesIfNeeded|DetachStream|SetLayout|TextureSignatureMatches|UpdateTextureSignature|CreateYuvTexture" src\infrastructure\multiviewrenderer.h src\infrastructure\multiviewrenderer.cpp doc\Phase3_14TODO.md`
- [ ] 人工运行建议：4 路播放、布局切换、移除一路、重新添加一路。

**Commit message**：

```text
perf: 优化 MultiViewRenderer 纹理复用

- 仅在帧尺寸或格式变化时重建纹理
- 减少布局切换和 active stream 切换时的无效资源操作
- 保持 stream detach 时释放对应纹理资源
- 降低多路播放下的纹理上传和资源管理开销
```

## 3.14.4 渲染刷新节流与无效重绘控制

**目标**：避免没有新帧时持续触发无意义重绘。

**预计修改文件**：

- `src/infrastructure/multiviewrenderer.h`
- `src/infrastructure/multiviewrenderer.cpp`
- `src/infrastructure/singlestreamframesinkadapter.cpp`
- 可选：`src/infrastructure/singlestreamframesinkadapter.h`
- `doc/Phase3_14TODO.md`

**任务清单**：

- [x] 复核 `PresentFrame()` 调用 `update()` 的频率。
- [x] 避免同一路短时间内重复排队过多 UI 更新。
- [x] 没有新帧时不主动刷新视频区域。
- [x] Clear / Detach / Layout 变化时仍然能及时刷新。
- [x] 多路同时送帧时合并 UI update 请求。
- [x] 不改变 scheduler 的调度节奏。

**验收标准**：

- [x] 暂停状态下渲染器不会主动持续高频刷新。
- [x] 4 路播放时 UI update 请求具备合并机制。
- [ ] seek / stop / remove stream 后画面清理仍及时。
- [ ] 不影响音视频同步。

**测试流程**：

- [x] 静态检查：`git diff --check -- src\infrastructure\multiviewrenderer.h src\infrastructure\multiviewrenderer.cpp doc\Phase3_14TODO.md`
- [x] 定向搜索：`rg -n "update\\(|PresentFrame|ClearFrame|DetachStream|RequestRenderUpdate|invokeMethod|QueuedConnection|renderUpdatePending" src\infrastructure\multiviewrenderer.h src\infrastructure\multiviewrenderer.cpp doc\Phase3_14TODO.md`
- [ ] 人工运行建议：播放、暂停、seek、stop、移除 stream、关闭程序。

**Commit message**：

```text
perf: 控制 MultiViewRenderer 无效重绘

- 优化 PresentFrame 到 update 的刷新触发路径
- 减少多路同时送帧时的重复 UI 更新请求
- 保持 Clear、Detach 和布局变化时的及时刷新
- 不改变 VideoFrameScheduler 的同步调度职责
```

## 3.14.5 多路性能基线验收

**目标**：形成 4 路播放性能基线，并给出 9/16 路扩容准入判断。

**预计修改文件**：

- `doc/Phase3_14_多路性能优化验收报告.md`
- `doc/Phase3_14TODO.md`

**任务清单**：

- [ ] 记录 1 路、4 路普通视频播放表现。
- [ ] 记录 1 路 4K 视频播放表现。
- [ ] 记录 4 路混合分辨率播放表现。
- [ ] 记录暂停、seek、stop、关闭时的性能和生命周期表现。
- [ ] 记录是否具备进入 9 路扩容的条件。
- [ ] 记录是否具备进入 16 路扩容的条件。
- [ ] 明确 PBO / 异步上传是否需要进入后续专题。

**验收标准**：

- [ ] 能说明当前 4 路播放是否稳定。
- [ ] 能说明性能瓶颈主要在解码、转换、上传还是绘制。
- [ ] 能说明扩容到 9/16 路前还缺什么。

**测试流程**：

- [ ] 本任务为验收报告，不运行程序。
- [ ] 静态检查：`git diff --check -- doc\Phase3_14_多路性能优化验收报告.md doc\Phase3_14TODO.md`

**Commit message**：

```text
docs: 增加多路渲染性能优化验收报告

- 记录 Phase 3.14 多路渲染性能优化结果
- 汇总 1 路、4 路和 4K 视频的性能基线
- 分析解码、格式转换、纹理上传和绘制瓶颈
- 明确进入 9/16 路真实播放扩容的准入条件
```

## 不做范围

- 不扩展真实播放容量到 9/16 路。
- 不修复显示正确性问题；该问题应在 Phase 3.13 完成。
- 不引入复杂 GPU 异步上传作为第一选择。
- 不改变 active audio 策略。
- 不改变 `MediaPipeline` 单路职责。
- 不删除 `VideoRender` fallback。

## 风险与沟通点

- 如果性能瓶颈主要来自解码和格式转换，单纯优化 `MultiViewRenderer` 不会显著改善多路播放。
- 如果 4K 转换成本过高，Phase 3.15 扩容时需要限制 4K 多路数量或引入降级策略。
- 如果后续确实需要 PBO / 异步上传，应单独设计，不应在本阶段临时塞入 paint 路径。
- `VideoFrameConverter` 日志只用于确认转换类型，不作为逐帧转换次数统计来源。
