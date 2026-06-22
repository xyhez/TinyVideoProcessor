# Phase 2.4 摄像头异常与生命周期报告

## 1. 阶段目标

Phase 2.4 的目标是让摄像头在真实使用中具备可诊断、可恢复、可释放的行为。

本阶段不改变摄像头作为 `IMediaSource` 接入现有播放体系的架构路线，不把播放管线、异常策略、截图、录像或设备属性控制放入 `CameraMediaSource`。

核心原则：

- `CameraMediaSource` 只负责打开、关闭和描述摄像头媒体源。
- `demuxer` 负责识别运行期读包错误。
- `MediaPipeline` 负责单路播放管线的运行状态和资源释放。
- `MultiStreamManager` 负责按 `StreamId` 管理多路 stream。
- `MediaPlayController` 继续作为 UI 层播放入口。
- `MainWindow` 只展示错误和触发用户操作，不直接访问底层摄像头上下文。

## 2. 完成范围

### 2.1 生命周期前置修正

已完成实时源停止路径修正：

- 文件源 `Stop()` 保持 seek 回零语义。
- 实时源 `Stop()` 不再调用 demuxer seek 回零。
- 实时源停止时只清理本路队列、刷新解码器并重置同步时钟。
- 解码帧入队失败时释放 `outFrame`，避免停止或关闭过程中的帧泄漏。

该修正确保摄像头这类不可 seek 源不会误走文件源语义。

### 2.2 打开失败处理

已为媒体源增加统一错误读取接口：

```cpp
virtual QString LastError() const;
```

`CameraMediaSource::Open()` 现在会记录明确失败原因，包括：

- 摄像头设备名为空。
- dshow input format 不存在。
- 设备不存在。
- 设备被占用。
- 参数不支持。
- 流信息读取失败。
- 未找到可用视频流。

打开失败链路：

```text
CameraMediaSource
  -> MultiStreamManager::AddStream()
  -> MediaPlayController::StreamErrorOccurred
  -> MainWindow
```

说明：打开失败发生在 `MediaPipeline` 创建之前，因此不强行经过 `MediaPipeline`。这符合实际生命周期。

打开失败时的资源处理：

- `AVDictionary` 会释放。
- 如果 FFmpeg 写入了半初始化 `AVFormatContext`，会调用 `avformat_close_input()` 清理。
- `MultiStreamManager` 不会创建无效 stream。

### 2.3 运行中异常处理

运行期读包发生在 `demuxer::StartDemuxer()`。

本阶段新增：

```cpp
void DemuxErrorOccurred(QString message);
```

读包错误语义：

- `AVERROR_EOF`：自然结束，继续走 `DemuxFinished()`。
- `AVERROR(EAGAIN)`：临时无数据，短暂重试。
- 其他连续读包错误：达到阈值后判定为运行期异常。

当前阈值策略：

```text
连续非临时读包错误 >= 100 次
每次间隔 20ms
约等于连续失败 2 秒
```

运行期错误链路：

```text
demuxer::DemuxErrorOccurred
  -> MediaPipeline::HandleDemuxError()
  -> MediaPipeline::ErrorOccurred
  -> MultiStreamManager::StreamErrorOccurred
  -> MediaPlayController::StreamErrorOccurred
  -> MainWindow
```

`MediaPipeline::HandleDemuxError()` 会停止本路：

- demuxer
- decoder
- packet/frame queues
- position timer
- audio player 等待
- video render 等待

active stream 发生运行期错误时，`MediaPlayController` 会将播放状态切回 `Stopped`。

### 2.4 重复添加策略

本阶段明确第一版策略：

```text
不允许重复添加同一个摄像头设备。
```

新增媒体源通用接口：

```cpp
virtual QString SourceIdentity() const;
virtual bool AllowsDuplicateOpen() const;
```

当前实现：

- `CameraMediaSource` 使用 `camera:<设备名>` 作为身份，并返回不允许重复打开。
- `FileMediaSource` 使用 `file:<路径>` 作为身份，但保持允许重复打开，不改变现有文件多路播放行为。
- `MultiStreamManager::AddStream()` 在打开媒体源前检查不可重复资源。

重复添加同一摄像头时：

```text
MultiStreamManager -> StreamErrorOccurred("该媒体源已在使用，不能重复添加")
```

这样避免同一个 dshow 设备被多个 stream 抢占。

### 2.5 释放链路审查

已形成：

```text
doc/Phase2_4_3_摄像头释放链路审查报告.md
```

核心释放路径：

```text
MultiStreamManager
  -> MediaPipeline::Shutdown()
  -> MediaPipeline::ReleaseResources()
  -> mediaSource_->Close()
  -> CameraMediaSource::Close()
  -> avformat_close_input(&formatContext_)
```

已静态确认以下路径都会进入释放链路：

- remove stream
- SwitchSource
- ShutdownAll
- closeEvent
- MediaPlayController 析构兜底

## 3. 架构边界检查

### 3.1 MainWindow

`MainWindow` 只负责：

- 触发添加、移除、关闭等用户操作。
- 监听 `MediaPlayController::StreamErrorOccurred`。
- 展示错误提示。

它没有直接访问：

- `AVFormatContext`
- `MediaPipeline`
- `demuxer`
- 摄像头底层资源

### 3.2 CameraMediaSource

`CameraMediaSource` 只负责：

- 根据参数打开摄像头。
- 关闭摄像头。
- 保存媒体源基础信息。
- 暴露自身错误原因和身份能力。

它不创建：

- decoder
- renderer
- audio player
- MediaPipeline

它不参与：

- active stream 策略
- active audio stream 策略
- UI 交互
- 运行期异常判定策略

### 3.3 MediaPipeline

`MediaPipeline` 继续保持单路播放管线职责。

本阶段新增的逻辑仍属于单路管线范畴：

- 实时源 Stop 不 seek。
- demuxer 运行期错误处理。
- 本路队列、线程和渲染等待状态停止。
- 本路资源释放。

没有在 `MediaPipeline` 中引入摄像头专用播放管线。

### 3.4 MultiStreamManager

`MultiStreamManager` 仍只按 `StreamId` 管理 stream。

重复添加防护通过 `IMediaSource` 通用能力完成：

- `SourceIdentity()`
- `AllowsDuplicateOpen()`

因此 `MultiStreamManager` 不需要知道具体源是否是摄像头。

## 4. 实机验证状态

根据当前 TODO，以下代码链路已经实现或静态确认，但仍需要 C920 实机最终验收：

### 4.1 打开失败

待验证：

- 添加不存在或被占用设备时程序不崩溃。
- UI 能看到可理解错误。
- 失败后资源释放完整。

### 4.2 运行中异常

待验证：

- 单路摄像头运行中拔出不拖垮程序。
- 多路中摄像头异常不影响其他 stream。
- 异常 stream 可移除。

### 4.3 释放链路

待验证：

- remove stream 后摄像头可再次添加。
- 关闭程序不阻塞。
- 异常 stream remove 后设备可再次打开。

## 5. 当前限制

### 5.1 运行期异常阈值是固定值

当前使用连续错误次数阈值：

```text
100 次 * 20ms ≈ 2 秒
```

这是第一版可控策略。后续如需更精确，可改为基于时间窗口的策略，例如 `QElapsedTimer`。

### 5.2 摄像头身份仍基于设备名

当前 `CameraMediaSource::SourceIdentity()` 使用设备名。

这对单个 C920 足够，但后续如果支持多个同名 USB 摄像头，需要引入更稳定的设备唯一标识。

该问题应放入后续多摄像头预研，不在当前 C920 MVP 中提前复杂化。

### 5.3 真实设备能力枚举仍未完全动态化

Phase 2.2 中设备格式能力仍存在 C920 硬编码列表。

这不属于 Phase 2.4 生命周期问题，但会影响非 C920 摄像头的参数选择准确性。

## 6. 相关文档

- `doc/Phase2TODO.md`
- `doc/Phase2_4_2_运行中异常链路审查报告.md`
- `doc/Phase2_4_3_摄像头释放链路审查报告.md`
- `doc/架构指导.md`

## 7. 结论

Phase 2.4 已完成摄像头异常与生命周期的主要代码链路：

- 打开失败可诊断。
- 运行中异常可上报。
- 重复添加同一摄像头可防护。
- 释放链路已静态确认闭环。
- 架构边界没有被破坏。

当前剩余工作是 C920 实机验收。验收通过后，Phase 2.4 可以正式收口，进入 Phase 2.5 摄像头配置持久化。
