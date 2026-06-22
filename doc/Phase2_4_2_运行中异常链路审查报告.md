# Phase 2.4.2 运行中异常链路审查报告

## 1. 审查目标

本次只审查摄像头运行中异常链路，不直接修改运行时代码。

目标是判断当前系统在摄像头已经成功打开并进入播放后，遇到读帧失败、断流、运行中拔出设备时，能否做到：

- 异常能被发现。
- 异常能通过既有播放链路转发到 UI。
- 异常 stream 可被移除。
- 异常 stream 不影响其他文件或摄像头 stream。
- 不把摄像头异常处理逻辑塞进 `CameraMediaSource`。

## 2. 当前链路现状

### 2.1 打开期错误链路已经具备

当前打开失败链路已经补齐：

```text
CameraMediaSource -> MultiStreamManager -> MediaPlayController -> MainWindow
```

该链路只覆盖 `CameraMediaSource::Open()` 阶段。也就是说，设备不存在、设备被占用、参数不支持等打开前错误已经可以向 UI 转发。

### 2.2 运行期错误链路尚未具备

运行期读包发生在 `demuxer::StartDemuxer()`：

```cpp
ret = av_read_frame(ic, packet);
```

当前处理逻辑是：

- `ret == AVERROR_EOF`：认为自然结束，停止队列并发出 `DemuxFinished()`。
- `ret < 0` 且不是 EOF：输出 `"读取错误，重试..."`，等待 20ms 后继续。

问题是：非 EOF 错误没有错误信号，没有退出策略，也没有区分临时无数据和真实设备异常。

## 3. 关键问题

### 3.1 摄像头拔出或断流可能进入无限重试

如果 dshow 在摄像头运行中断流或拔出设备时持续返回错误，当前 `demuxer` 会无限执行：

```text
读取错误，重试...
sleep 20ms
continue
```

结果是：

- UI 不知道 stream 已经异常。
- `MediaPipeline` 不知道 demuxer 已经不可恢复。
- 视频画面大概率停在最后一帧或卡住。
- demuxer 线程仍然存活并持续重试。

### 3.2 没有 demuxer 运行期错误信号

`demuxer` 当前只有：

```cpp
void DemuxFinished();
```

缺少类似：

```cpp
void DemuxErrorOccurred(QString message);
```

因此运行期错误无法进入既有的：

```text
MediaPipeline -> MultiStreamManager -> MediaPlayController -> MainWindow
```

### 3.3 EOF 和异常结束语义需要分开

文件播放读到 EOF 是正常结束，应继续走 `PlaybackFinished`。

摄像头或实时源运行中读包持续失败不是正常结束，不应伪装成播放完成。它应该是 stream error。

建议语义：

- `AVERROR_EOF`：自然结束，只适用于文件源或真正结束的输入。
- `AVERROR(EAGAIN)`：临时无数据，可短暂重试。
- 其他连续错误：运行期异常，转发错误并停止当前 demuxer。

### 3.4 多路隔离基础是存在的

当前每一路 stream 都有独立的：

- `MediaPipeline`
- `demuxer`
- decoder
- queues
- threads
- renderer

`MultiStreamManager` 只按 `StreamId` 管理这些单路管线。

所以只要运行期错误被限制在当前 `MediaPipeline` 内部处理，并通过 `StreamErrorOccurred(id, message)` 转发，理论上不会拖垮其他 stream。

### 3.5 异常 stream 可移除能力基本存在

`MultiStreamManager::RemoveStream(id)` 当前会：

- 调用该 stream 的 `pipeline->Shutdown()`。
- 从 `streams_` 删除该 stream。
- 重新选择 active stream。
- 重新选择 active audio stream。
- 发出 `StreamRemoved`。

因此异常 stream 的移除入口已经具备。缺口是 UI 尚未知道哪一路进入异常状态，也没有把异常 stream 标成可处理对象。

## 4. 架构边界判断

本阶段不应该把运行期异常策略放进 `CameraMediaSource`。

原因：

- `CameraMediaSource` 的职责是打开和关闭摄像头媒体源，并提供 `AVFormatContext`。
- 运行期读包发生在 `demuxer`，错误首先由 `av_read_frame()` 返回。
- 是否重试、何时判定 fatal、如何停止队列，属于播放管线运行策略。

推荐职责边界：

```text
demuxer
  负责识别 av_read_frame() 运行期错误，并发出错误信号。

MediaPipeline
  负责接收 demuxer 错误，停止本路队列/计时器，并向外转发 ErrorOccurred。

MultiStreamManager
  负责把本路 Pipeline 错误绑定 StreamId 后转发。

MediaPlayController
  继续作为 UI 层入口，转发 StreamErrorOccurred。

MainWindow
  只展示错误，不直接访问 demuxer、MediaPipeline 或摄像头上下文。
```

## 5. 建议实施方案

### 5.1 demuxer 增加运行期错误信号

在 `demuxer` 中增加：

```cpp
void DemuxErrorOccurred(QString message);
```

触发条件：

- `av_read_frame()` 返回非 EOF 错误。
- 排除明确可重试的 `AVERROR(EAGAIN)`。
- 对实时源采用连续错误阈值，避免短暂无帧被误判。

### 5.2 增加连续错误阈值

建议不要第一次读包失败就判定 fatal。

第一版可采用简单阈值：

```text
连续读包错误 >= 100 次
```

当前每次错误 sleep 20ms，因此约等于连续失败 2 秒。

这个值足够粗但可控：

- 避免短暂抖动误报。
- 摄像头拔出或断流时能在可接受时间内上报。
- 不引入复杂状态机。

后续如果需要更准确，可以改为基于 `QElapsedTimer` 的时间阈值。

### 5.3 错误消息应包含 FFmpeg 错误文本

`demuxer` 应使用 `av_strerror()` 把错误码转成可读文本。

建议消息格式：

```text
读取媒体流失败：<ffmpeg error text>
```

如果 `m_mediaSource->IsRealtime()` 为 true，可以更具体：

```text
实时媒体源读取失败：<ffmpeg error text>。设备可能已断开或输入流已中断。
```

### 5.4 MediaPipeline 转发 demuxer 错误

`MediaPipeline::CreateConnects()` 中增加：

```text
demuxer::DemuxErrorOccurred -> MediaPipeline::HandleDemuxError
```

`HandleDemuxError` 建议做：

- 停止 `positionTimer_`。
- 请求 packet/frame queues stop，唤醒 decoder/render 等等待点。
- 暂停或停止 renderer。
- `emit ErrorOccurred(message)`。

注意：不要在 demuxer 线程里直接析构 Pipeline 资源。资源释放仍应由外层 remove/shutdown 流程完成。

### 5.5 UI 层保持现有错误展示方式

2.4.1 已经让 `MainWindow` 监听：

```cpp
MediaPlayController::StreamErrorOccurred
```

2.4.2 只需要复用该链路。运行期错误会带有真实 `StreamId`，UI 能提示具体哪一路异常。

## 6. 验收建议

### 6.1 单路摄像头

- 打开摄像头并开始预览。
- 运行中拔出摄像头。
- 预期：约 2 秒内出现错误提示。
- 预期：程序不崩溃。
- 预期：窗口仍可关闭。
- 预期：异常 stream 可移除。

### 6.2 文件 + 摄像头混合多路

- 添加一路文件视频。
- 添加一路摄像头。
- 播放中拔出摄像头。
- 预期：文件视频不受影响。
- 预期：错误提示指向摄像头 stream。
- 预期：移除摄像头 stream 后文件 stream 继续存在。

### 6.3 临时读包错误

- 摄像头正常运行时不应因为偶发 `av_read_frame()` 失败立即报错。
- 如果只是短暂无数据，应继续运行。

## 7. 结论

当前 2.4.2 的主要缺口不是资源释放，而是运行期错误识别和信号转发。

最小合理改法是：

1. `demuxer` 增加运行期错误信号。
2. `demuxer::StartDemuxer()` 区分 EOF、临时无数据和连续 fatal 错误。
3. `MediaPipeline` 接收 demuxer 错误，停止本路运行状态并转发。
4. `MultiStreamManager / MediaPlayController / MainWindow` 复用现有 `StreamErrorOccurred` 链路。

该方案符合当前架构路线：异常处理留在播放管线和管理层，不污染 `CameraMediaSource`，也不要求 `MainWindow` 直接访问底层资源。
