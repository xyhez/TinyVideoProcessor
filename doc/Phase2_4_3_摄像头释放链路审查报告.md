# Phase 2.4.3 摄像头释放链路审查报告

## 1. 审查目标

本次只做静态审查，不修改运行时代码。

目标是确认摄像头 stream 在以下路径中是否会释放底层设备资源：

- remove stream
- SwitchSource
- ShutdownAll
- 关闭程序
- Controller 析构兜底

## 2. 释放链路结论

当前释放链路已经具备代码闭环。

核心释放路径是：

```text
MultiStreamManager
  -> MediaPipeline::Shutdown()
  -> MediaPipeline::ReleaseResources()
  -> mediaSource_->Close()
  -> CameraMediaSource::Close()
  -> avformat_close_input(&formatContext_)
```

也就是说，只要 stream 进入 `MediaPipeline`，释放最终都会回到媒体源自身的 `Close()`，摄像头设备资源由 `CameraMediaSource` 关闭。

## 3. 分路径审查

### 3.1 remove stream

入口：

```cpp
MediaPlayController::RemoveStream(id)
  -> MultiStreamManager::RemoveStream(id)
```

`MultiStreamManager::RemoveStream()` 当前会：

- 查找目标 stream。
- 调用 `pipeline->Shutdown()`。
- 从 `streams_` 删除该 stream。
- 发出 `StreamRemoved(id)`。
- 如果被移除的是 active stream，重新选择 fallback active stream。
- 如果被移除的是 active audio stream，重新选择 fallback audio stream。

`pipeline->Shutdown()` 最终调用 `MediaPipeline::ReleaseResources()`，其中会执行：

```cpp
if (mediaSource_) {
    mediaSource_->Close();
}
```

结论：remove stream 释放链路完整。

### 3.2 SwitchSource

入口：

```cpp
MediaPlayController::SwitchSource(mediaSource, rendererContainer, autoStart)
```

当前流程：

```text
Stop()
multiStreamManager_->ShutdownAll()
AddStream(newSource)
SetActiveStream(newStream)
```

`ShutdownAll()` 会关闭所有旧 stream，因此如果旧 stream 是摄像头，也会走到 `CameraMediaSource::Close()`。

结论：SwitchSource 释放旧摄像头链路完整。

### 3.3 ShutdownAll

入口：

```cpp
MediaPlayController::Shutdown()
  -> MultiStreamManager::ShutdownAll()
```

`MultiStreamManager::ShutdownAll()` 当前会：

- 遍历所有 stream。
- 对每个 stream 调用 `pipeline->Shutdown()`。
- 清空 `streams_`。
- 发出 `StreamRemoved`。
- 清空 active stream 和 active audio stream。

结论：ShutdownAll 会释放所有摄像头 stream。

### 3.4 关闭程序

入口：

```cpp
MainWindow::closeEvent()
  -> mediaPlayController_->Shutdown()
```

因此关闭窗口时会进入 `ShutdownAll()`，最终释放所有 pipeline 和 media source。

同时 `MainWindow::~MainWindow()` 会 reset `mediaPlayController_`，`MediaPlayController::~MediaPlayController()` 也会调用 `Shutdown()`，作为兜底路径。

结论：关闭程序释放链路完整，且有析构兜底。

## 4. 资源释放顺序

`MediaPipeline::ReleaseResources()` 当前顺序是：

1. 停止 position timer。
2. 对包队列、帧队列 `RequestStop()`。
3. 停止 demuxer、decoder、audio player、video render。
4. quit/wait 各工作线程。
5. reset demuxer、decoder、audio player，删除 video render。
6. reset 队列和 sync manager。
7. 调用 `mediaSource_->Close()`。
8. reset media source。

该顺序合理：

- 先停止线程和队列，避免后台线程继续读写 FFmpeg context。
- 再关闭 media source，避免摄像头 context 被提前释放。

## 5. 已实现的重复添加防护

当前已实现：

- `IMediaSource::SourceIdentity()`
- `IMediaSource::AllowsDuplicateOpen()`
- `CameraMediaSource` 以设备名生成 `camera:` 身份，并声明不允许重复打开。
- `MultiStreamManager::AddStream()` 在 `Open()` 前检查重复不可共享资源。

这能避免同一个摄像头被重复添加导致 dshow 设备抢占。

## 6. 待实机验证项

静态链路完整，但以下仍需要 C920 实机确认：

- 添加摄像头后 remove stream，随后再次添加同一摄像头。
- 添加摄像头后切换播放列表文件，确认旧摄像头释放。
- 添加多个 stream 后调用 ShutdownAll，确认摄像头释放。
- 播放中关闭窗口，确认程序不阻塞。
- 异常 stream 报错后 remove，确认设备可再次打开。

## 7. 结论

2.4.3 当前不需要额外运行时代码修改。

释放链路已经符合架构边界：

- `MainWindow` 只触发 Controller 行为，不直接释放摄像头上下文。
- `MediaPlayController` 只协调播放状态和管理入口。
- `MultiStreamManager` 只按 `StreamId` 管理 stream。
- `MediaPipeline` 统一释放本路播放资源。
- `CameraMediaSource` 只负责关闭自身持有的 `AVFormatContext`。

下一步应做 C920 实机验证。验证通过后，可以把 2.4.3 的释放项和验收项打勾。
