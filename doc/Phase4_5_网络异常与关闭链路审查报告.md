# Phase 4.5 网络异常与关闭链路审查报告

## 审查目标

本报告只做静态审查，不修改播放代码。目标是确认 RTSP 网络流在以下场景中的现状和风险：

- 打开失败是否能返回明确错误。
- 运行中断流是否能上报到 UI。
- Stop / Remove / Shutdown 是否存在阻塞风险。
- 异常后是否具备移除和重新添加的基础条件。

## 当前链路

```text
MainWindow
    -> MediaPlayController
        -> MultiStreamManager
            -> MediaPipeline
                -> NetworkMediaSource
                -> demuxer
                    -> av_read_frame()
```

网络流仍然遵守现有架构边界：

- `NetworkMediaSource` 只负责打开、关闭和维护媒体源信息。
- `demuxer` 只从 `IMediaSource` 取得 `AVFormatContext` 并读包。
- `MediaPipeline` 负责线程、队列、解码和渲染生命周期。
- `MultiStreamManager` 负责多路 stream 生命周期和错误转发。
- `MainWindow` 只负责 UI 提示。

## 已具备的能力

### 1. 打开失败可以向上返回

`NetworkMediaSource::Open()` 已经覆盖以下失败场景：

- URL 为空。
- 协议不支持。
- `avformat_open_input()` 失败。
- `avformat_find_stream_info()` 失败。
- 未找到视频流。

失败原因通过 `SetLastError()` 保存，再由 `MultiStreamManager::AddStream()` 读取 `LastError()` 并发出 `StreamErrorOccurred`，最后由 `MainWindow` 弹出错误提示。

涉及位置：

- `src/domain/NetworkMediaSource.cpp:55`
- `src/domain/NetworkMediaSource.cpp:211`
- `src/domain/MultiStreamManager.cpp:31`
- `src/ui/mainwindow.cpp:148`

结论：打开失败上报链路基本成立。

### 2. 打开和读取超时参数已经下发

`NetworkMediaSource::ConfigureOpenOptions()` 已经设置：

- `stimeout`
- `timeout`
- `rw_timeout`
- `buffer_size`
- `max_delay`
- `fflags=nobuffer`
- `flags=low_delay`
- `rtsp_transport=tcp`

涉及位置：

- `src/domain/NetworkMediaSource.cpp:230`

结论：现有配置可以降低网络读阻塞概率，但不能作为确定性关闭保障。

### 3. 运行期读包错误可以上报

`demuxer::StartDemuxer()` 对 `av_read_frame()` 的返回值做了处理：

- `AVERROR_EOF`：触发 `DemuxFinished`。
- `AVERROR(EAGAIN)`：短暂 sleep 后继续。
- 其他错误：累计连续错误次数。
- 连续错误达到 `kMaxConsecutiveReadErrors` 后，触发 `DemuxErrorOccurred`。

`MediaPipeline` 收到 `DemuxErrorOccurred` 后会停止 demuxer、decoder、队列、定时器和视频调度，并向上发出 `ErrorOccurred`。

涉及位置：

- `src/infrastructure/demuxer.cpp:134`
- `src/infrastructure/demuxer.cpp:171`
- `src/infrastructure/demuxer.cpp:191`
- `src/domain/MediaPipeline.cpp:503`
- `src/domain/MultiStreamManager.cpp:489`

结论：运行期错误上报链路存在，但对长期 `EAGAIN` 和网络读阻塞还不够确定。

## 主要风险

### 风险 1：Stop / Remove / Shutdown 可能等待阻塞中的 `av_read_frame()`

`MediaPipeline::ReleaseResources()` 会先请求队列停止，再调用 `demuxer_->Stop()`，然后对 demuxer 线程执行 `thread->wait()`。

问题是：`demuxer_->Stop()` 只设置 `stop_flag`，不能直接打断正在执行的 `av_read_frame()`。如果网络读卡在 FFmpeg 内部，demuxer 线程只有等 `av_read_frame()` 返回后才能检查 `stop_flag` 并退出。

涉及位置：

- `src/domain/MediaPipeline.cpp:391`
- `src/domain/MediaPipeline.cpp:435`
- `src/infrastructure/demuxer.cpp:171`
- `src/infrastructure/demuxer.cpp:397`

结论：这是当前 Phase 4.5 的最高优先级风险。

### 风险 2：Stop 实时源时也会先等待 suspend

`MediaPipeline::Stop()` 对实时源最终会走 `ResetRealtimePipelineStop()`，不会 seek，这是正确的。但在进入该逻辑前，它先调用 `SuspendPipelineAndWait()`。

如果 demuxer 正在阻塞式网络读，`SuspendAndWait()` 也要等 demuxer 从 `av_read_frame()` 返回后才能进入 suspended 状态。

涉及位置：

- `src/domain/MediaPipeline.cpp:78`
- `src/domain/MediaPipeline.cpp:539`
- `src/infrastructure/demuxer.cpp:325`

结论：实时源 Stop 在异常网络条件下仍有卡住风险。

### 风险 3：`rw_timeout` 不是完整的中断机制

静态审查时发现原实现没有使用 FFmpeg 的 `interrupt_callback`：

- 原实现未发现 `interrupt_callback`。
- 原实现未发现 `AVIOInterruptCB`。
- 原实现未发现可由 `Stop / Shutdown` 主动触发的网络读中断标志。

涉及搜索：

```powershell
rg -n "interrupt_callback|AVIOInterruptCB" src
```

结论：当前关闭依赖 FFmpeg 超时返回，而不是由播放器生命周期主动打断。

### 风险 4：长期 `EAGAIN` 不会触发错误上报

`AVERROR(EAGAIN)` 当前只是 sleep 后继续，不计入连续错误次数。如果某些网络源长期返回 `EAGAIN`，demuxer 可能持续循环，不会上报错误。

涉及位置：

- `src/infrastructure/demuxer.cpp:181`

结论：这不是最高风险，但需要在异常策略中明确是否把长期 `EAGAIN` 视为断流。

## 推荐修复方向

### 推荐方案：在 `IMediaSource` 抽象层增加可选中断能力

不要在 UI、`demuxer` 或 `MediaPipeline` 中硬编码 RTSP 特判。更符合当前架构的做法是：

1. 在 `IMediaSource` 增加默认空实现方法，例如：
   - `RequestInterrupt()`
   - `ClearInterrupt()`
2. `FileMediaSource` 和 `CameraMediaSource` 可以沿用默认空实现。
3. `NetworkMediaSource` 内部增加原子中断标志。
4. `NetworkMediaSource` 在创建 `AVFormatContext` 时设置 FFmpeg `interrupt_callback`。
5. `MediaPipeline::ReleaseResources()` 和实时源 `Stop()` 在等待线程前调用 `mediaSource_->RequestInterrupt()`。
6. 重新添加网络流或重新打开前调用 `ClearInterrupt()`。

这样做的好处：

- 保持 `demuxer` 依赖 `IMediaSource`，不感知 RTSP。
- 保持 `NetworkMediaSource` 负责网络打开和网络中断细节。
- Stop / Remove / Shutdown 可以主动打断网络读。
- 文件源和摄像头源不受影响。

### 辅助建议

- `ReleaseResources()` 中可以增加线程等待耗时日志，但不建议直接 `terminate()` 线程。
- 对长期 `EAGAIN` 增加累计时间或次数上限，避免网络假活。
- 错误信息中保留 URL 主机、协议、timeout 参数，便于区分服务端拒绝和客户端超时。

## 实施补充

本轮已按推荐方案完成第一步代码接入：

- `IMediaSource` 新增默认空实现 `RequestInterrupt()` 和 `ClearInterrupt()`。
- `NetworkMediaSource` 增加 `interruptRequested_`，并通过 FFmpeg `interrupt_callback` 打断阻塞式网络打开和读取。
- `NetworkMediaSource::Open()` 在打开前清理中断状态，避免上一次关闭影响下一次打开。
- `MediaPipeline::ReleaseResources()` 在线程等待前主动请求媒体源中断。
- `MediaPipeline::Stop()` 对实时源在进入 suspend 前请求中断，挂起完成后清理中断状态，避免影响后续恢复。
- `demuxer::StartDemuxer()` 在 `av_read_frame()` 被中断返回后优先响应 stop / suspend，避免继续按普通错误累计重试。

这一步仍需人工验证断流场景，尤其是播放中关闭 MediaMTX 后的 Remove / Close 行为。

## 验收建议

后续实现后至少验证以下场景：

- 输入错误 RTSP URL，程序不崩溃，UI 有明确错误。
- 输入未启动服务的 `rtsp://127.0.0.1:8554/not_exist`，能超时返回。
- 播放本地 MediaMTX RTSP 时关闭 MediaMTX，程序不崩溃。
- 断流后 Remove 不阻塞。
- 断流后重新 Add RTSP 可以成功。
- 播放中直接关闭主窗口不阻塞。

## 结论

当前网络异常上报链路已经具备基础能力，且已接入网络源中断回调，关闭链路的确定性比原实现更强。Phase 4.5 下一步应进行错误 URL、断流、Remove、重新添加和关闭窗口的人工验收，再决定是否补充长期 `EAGAIN` 专项处理。
