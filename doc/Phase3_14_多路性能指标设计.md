# Phase 3.14 多路性能指标设计

## 1. 目标

Phase 3.14 的目标不是直接追求最高帧率，而是先让 `MultiViewRenderer` 的多路渲染性能可观测、可解释、可对比。

后续优化必须能回答三个问题：

- 当前是否真的慢。
- 慢在渲染刷新、纹理上传、格式转换、调度丢帧还是系统资源。
- 优化前后是否有可比较的数据。

因此 3.14.1 只定义指标，不修改核心渲染逻辑。

## 2. 指标分层

性能指标按职责分为三层。

```text
VideoFrameScheduler
    -> 调度指标：晚帧、丢帧、帧队列压力

MultiViewRenderer
    -> 渲染指标：paint 次数、纹理上传次数、活跃 stream 数量、帧尺寸

人工观测
    -> 系统指标：CPU、GPU、内存、显存、UI 响应
```

边界要求：

- `MultiViewRenderer` 不读取帧队列，不判断同步，不直接统计 scheduler 内部决策。
- `VideoFrameScheduler` 不感知 UI 布局，不统计 slot、viewport 或纹理上传。
- `MainWindow` 不承担性能统计逻辑。
- 性能日志默认低频或可控，不能影响正常播放。

## 3. Renderer 指标

### 3.1 Paint FPS

定义：

```text
paintFPS = 统计窗口内 paintGL() 调用次数 / 统计窗口秒数
```

建议窗口：

- 默认 5 秒。
- Debug 观察时可以临时降到 2 秒。

用途：

- 判断 `MultiViewRenderer` 实际刷新频率。
- 判断多路播放时是否出现无效高频刷新。
- 判断 `PresentFrame()` 到 `update()` 是否造成过度 repaint。

注意：

- paint FPS 不是视频帧率。
- 多路播放时，一个 `paintGL()` 可能绘制多个 stream。
- paint FPS 高不代表播放流畅，仍需要结合 scheduler 的晚帧/丢帧指标。

### 3.2 Active Stream Count

定义：

```text
activeStreamCount = 当前持有 currentFrame 且绑定到可见 slot 的 stream 数量
```

用途：

- 区分 1 路、4 路、9 路、16 路压力。
- 判断布局中有多少路真正参与绘制。

注意：

- 已添加但不可见的 stream 可以单独记录为 `attachedStreamCount`。
- `Single` 布局下不应误判为只有一个 pipeline 存在。

### 3.3 Texture Upload Count

定义：

```text
textureUploadCount = 统计窗口内 UploadTexturesIfNeeded() 实际执行 setData 的次数
```

建议拆分：

- 总上传次数。
- 每路上传次数。
- 每路上传像素量估算。

YUV420P 单帧上传像素量估算：

```text
uploadPixels = Y.width * Y.height + U.width * U.height + V.width * V.height
```

用途：

- 判断是否存在无效重复上传。
- 判断 4K 视频对上传压力的影响。
- 判断布局切换是否触发不必要纹理重建或上传。

### 3.4 Texture Rebuild Count

定义：

```text
textureRebuildCount = 纹理对象因尺寸变化被释放并重新创建的次数
```

用途：

- 验证同一路同尺寸播放时纹理是否稳定复用。
- 验证布局切换和 active stream 切换不会重建纹理。
- 验证 stream detach 时只释放该路资源。

### 3.5 Last Presented Time

定义：

```text
lastPresentedTime[streamId] = MultiViewRenderer 最近一次接收该 stream 帧的时间
```

用途：

- 判断某一路是否长时间没有新帧进入 renderer。
- 区分“没有送帧”和“送帧后渲染慢”。
- 多路场景下定位单路卡住还是整体渲染压力。

注意：

- 这个时间只表示 renderer 收到帧，不表示该帧已经绘制到屏幕。

### 3.6 Frame Signature

每路记录最近一次帧签名：

```text
streamId
width
height
pixelFormat
linesize[0..2]
sampleAspectRatio
colorRange
colorSpace
convertedToDisplayFormat
```

用途：

- 判断是否存在 4K、高位深或异常 stride 的输入。
- 判断是否发生格式转换。
- 为性能日志解释纹理上传压力。

## 4. Scheduler 指标

`VideoFrameScheduler` 当前已经承担单路取帧、同步判断、丢帧和调度职责，因此晚帧和丢帧指标原则上属于 scheduler。

建议后续指标：

- `droppedFrameCount`：调度器主动丢弃的晚帧数量。
- `lateFrameCount`：被判定为已经明显落后的帧数量。
- `presentedFrameCount`：调度器送到 sink 的帧数量。
- `queueEmptyCount`：尝试取帧时队列为空的次数。

Phase 3.14 第一版可以先不修改 scheduler，只在文档中明确边界。若后续需要对比渲染和调度，应由 `VideoFrameScheduler` 暴露只读统计快照，而不是让 `MultiViewRenderer` 访问队列或同步器。

## 5. 人工观测指标

这些指标不建议第一版写入正式 UI。

### 5.1 CPU

观察方式：

- Windows 任务管理器。
- Qt Creator Analyzer。
- Visual Studio 性能分析器。

记录口径：

```text
1 路 1080p CPU 占用
4 路 1080p CPU 占用
1 路 4K CPU 占用
4 路混合分辨率 CPU 占用
```

### 5.2 GPU

观察方式：

- Windows 任务管理器 GPU 页。
- GPUView / Nsight 可作为后续工具。

记录口径：

- 3D/Copy 使用率。
- 是否随纹理上传明显上升。
- 多路播放时是否出现持续高占用。

### 5.3 内存和显存

记录口径：

- 添加 stream 前后进程内存。
- 播放 5 分钟后内存是否持续增长。
- 移除 stream 后内存是否回落。
- 布局切换是否造成异常增长。

## 6. 日志策略

性能日志必须低频、可控、可关闭。

建议：

- 默认每 5 秒输出一次统计摘要。
- 只在 debug 或显式开启性能诊断时输出。
- 不在每帧输出。
- 不在 `paintGL()` 中做复杂字符串拼接。
- 不在渲染热路径做阻塞 I/O。

推荐日志格式：

```text
MultiViewRenderer perf:
paintFps=58.8
visibleStreams=4
attachedStreams=4
uploads=240
textureRebuilds=0
maxFrame=3840x2160
```

每路摘要可以低频输出：

```text
stream=3 size=1920x1080 fmt=yuv420p uploads=60 lastFrameAgeMs=12 converted=true
```

## 7. 4 路 / 9 路 / 16 路验收口径

### 7.1 4 路

目标：

- 建立稳定基线。
- 验证日志不会刷屏。
- 验证纹理不会无效重建。

建议样本：

- 4 路 720p 或 1080p SDR。
- 1 路 4K + 3 路 720p 作为压力样本。

关注指标：

- paint FPS。
- 每路 upload count。
- texture rebuild count。
- CPU/GPU/内存。

### 7.2 9 路

目标：

- 验证 3x3 布局下 renderer 是否还能稳定刷新。
- 判断是否需要限制分辨率或帧率。

建议样本：

- 9 路 720p 优先。
- 不建议第一轮直接 9 路 4K。

关注指标：

- paint FPS 是否明显下降。
- 每路是否持续有帧进入 renderer。
- CPU/GPU 是否接近瓶颈。
- 是否出现严重掉帧或 UI 卡顿。

### 7.3 16 路

目标：

- 验证 4x4 扩容上限的基本可用性。
- 为 Phase 3.15 判断是否允许真实 16 路播放。

建议样本：

- 16 路 480p/720p 优先。
- 16 路 1080p 作为高压样本。
- 不承诺 16 路 4K。

关注指标：

- UI 是否可操作。
- 是否有明显资源泄漏。
- active stream / active audio stream 是否保持正确。
- 是否需要引入降级策略。

## 8. Phase 3.14.2 实现建议

第一版实现应保持低侵入：

- 在 `MultiViewRenderer` 内部新增统计结构。
- 在 `paintGL()` 入口累计 paint 次数。
- 在实际纹理上传成功后累计 upload 次数。
- 在纹理重建处累计 rebuild 次数。
- 在 `PresentFrame()` 记录每路最后送帧时间和帧签名。
- 用一个低频时间窗口输出摘要。

不建议第一版做：

- PBO 异步上传。
- GPU timer query。
- 正式 UI 性能面板。
- 跨线程复杂统计对象。
- 修改 `VideoFrameScheduler` 公共接口。

## 9. 进入下一步的标准

完成本设计后，可以进入 Phase 3.14.2：渲染性能轻量观测实现。

3.14.2 的验收重点是：

- 能看到 renderer 实际刷新频率。
- 能看到每路纹理上传次数。
- 能判断是否存在无效重建。
- 正常播放日志量可控。
