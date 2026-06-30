# Phase 4 网络流支持验收报告

## 阶段结论

Phase 4 网络流支持阶段可以收口。

当前正式支持能力：

```text
RTSP 网络流接入
```

当前不作为正式能力开放：

```text
RTMP / HTTP-FLV / MJPEG / ONVIF / 工业 GigE 相机 SDK
```

Phase 4 的核心成果是：在不新增网络流专用播放管线的前提下，通过 `NetworkMediaSource` 把 RTSP 网络流接入现有 `IMediaSource -> MediaPipeline -> MultiViewRenderer` 架构，并补齐网络流打开失败、断流、移除、重连和关闭窗口的关键生命周期处理。

## 架构结果

网络流接入后的主链路：

```text
MainWindow
    -> MediaPlayController
        -> MultiStreamManager
            -> MediaPipeline
                -> NetworkMediaSource
                -> demuxer
                -> decoder
                -> VideoFrameScheduler
                -> MultiViewRenderer
```

本阶段遵守的架构边界：

- `NetworkMediaSource` 只负责网络媒体源打开、关闭、参数和中断回调。
- `demuxer` 仍只依赖 `IMediaSource`，不感知 RTSP、RTMP 或 HTTP-FLV。
- `MediaPipeline` 继续负责单路播放管线生命周期。
- `MultiStreamManager` 继续负责多路 stream 管理。
- `MultiViewRenderer` 继续只处理多路帧显示，不感知 URL 或协议。
- UI 当前只暴露 `Add RTSP`，不展示未完成的多协议入口。

## 已完成能力

### 1. RTSP 单路播放

验收结果：通过。

已验证：

- 能通过 URL 添加 RTSP。
- 能打开本地 MediaMTX RTSP 流。
- 能进入现有解复用、解码和渲染链路。
- UI 显示 `LIVE`。
- RTSP 作为实时源禁用 seek。
- Stop / Remove / Close 不阻塞。
- 文件播放不回归。

本地验收地址：

```text
rtsp://127.0.0.1:8554/test
```

### 2. 文件源 + RTSP 混合预览

验收结果：基础链路通过。

已验证或静态确认：

- 文件源、摄像头源、RTSP 源都复用 `AddStreamWithFrameSinkFactory()`。
- stream 到 slot 的绑定统一由 `BindStreamToVideoSlot()` 维护。
- 活动显示流仍由 `active stream` 控制。
- 活动音频流仍由 `active audio stream` 控制。
- RTSP 的实时源语义不会误伤文件源 seek。

未完成项：

- 摄像头 + RTSP 混合预览尚未使用真实摄像头验收。
- 多路不同 RTSP URL 尚未做完整人工验收。

这些未完成项不影响 Phase 4 的 RTSP MVP 收口，但需要在真实摄像头到货后补验。

### 3. 网络异常和关闭链路

验收结果：通过。

已完成：

- 打开失败通过 `LastError()` 返回。
- 错误 URL 不导致程序崩溃。
- 断流不导致程序崩溃。
- 断流后 Remove 成功。
- 服务端恢复后重新 Add RTSP 成功。
- 播放中关闭窗口正常释放资源并退出。
- `NetworkMediaSource` 已接入 FFmpeg `interrupt_callback`。
- `IMediaSource` 已提供可选中断能力：`RequestInterrupt()` / `ClearInterrupt()`。
- `MediaPipeline` 在释放资源前主动请求媒体源中断。
- `demuxer` 在 `av_read_frame()` 被中断返回后优先响应 stop / suspend。

用户验收记录：

- RTSP 播放中关闭 MediaMTX 后，程序未崩溃，画面停留在最后一帧。
- 断流后点击 Remove 成功。
- 播放中直接关闭窗口，资源释放日志完整输出，程序正常退出。
- 服务端恢复后重新添加 RTSP 通过。

说明：

- 断流后停留最后一帧是当前可接受行为。后续如果需要更完整体验，可以增加“离线状态覆盖层”或清空画面。
- 退出阶段出现 H264 reference picture 警告属于断流或不完整 GOP 的解码器日志，不等价于程序崩溃。

## 协议边界

### 当前正式支持

- RTSP。

### 当前只预留，不开放为稳定能力

- RTMP。
- HTTP-FLV。
- MJPEG。

决策原因：

- RTSP 最贴近网口摄像头、NVR、安防预览和后续真实摄像头验证。
- RTMP 更偏推流发布和直播服务器，不是摄像头接入主路径。
- HTTP-FLV 更偏 Web 直播低延迟拉流，当前桌面多路预览收益有限。
- MJPEG 有兼容价值，但带宽高，多路性能压力大，应等待真实设备需求。
- 当前阶段不应在 README 中承诺未人工验收的协议能力。

## 工业相机边界

RTSP 摄像头和工业 GigE 相机都可能通过网口连接，但不应混为同一个抽象。

当前边界：

- RTSP 摄像头：属于 `NetworkMediaSource`，由 FFmpeg 打开标准媒体流。
- 工业 GigE / GenICam / 海康 MVS 相机：后续应通过独立媒体源接入，例如 `IndustrialCameraMediaSource`。

原因：

- 工业相机通常通过厂商 SDK 或 GenICam 获取图像帧，不一定提供标准封装媒体流。
- 它更接近“采集设备源”，不是普通 URL 网络流。
- 强行塞入 `NetworkMediaSource` 会破坏职责边界。

## README 建议表述

推荐写：

```text
支持 RTSP 网络流接入，并可与本地文件源、摄像头源复用同一套播放管线和多视口渲染体系。
```

不要写：

```text
支持 RTMP / HTTP-FLV / MJPEG
```

如果需要提及扩展性，可以写：

```text
网络流模块基于 IMediaSource 抽象设计，当前稳定支持 RTSP，并预留 RTMP / HTTP-FLV / MJPEG 的扩展枚举。
```

## 简历展示表述

推荐表述：

```text
基于 Qt6 + FFmpeg + OpenGL 实现多路视频预览平台，支持本地文件、USB 摄像头和 RTSP 网络流统一接入；通过 IMediaSource 抽象复用同一套解复用、解码、同步和多视口渲染管线，并为 RTSP 断流、关闭阻塞和资源释放接入 FFmpeg interrupt callback。
```

可以展开的技术点：

- 使用 `IMediaSource` 抽象统一文件、摄像头和 RTSP 网络流。
- 使用 `MediaPipeline` 复用解复用、解码、同步和调度链路。
- 使用 `MultiStreamManager` 管理多路源生命周期。
- 使用 `MultiViewRenderer` 承担多视口渲染。
- 对 RTSP 网络流实现实时源语义：无时长、不可 seek、显示 LIVE。
- 使用 FFmpeg `interrupt_callback` 解决网络读阻塞导致的关闭风险。

## 剩余风险

- 真实摄像头 + RTSP 混合预览尚需设备到货后补验。
- 多路不同 RTSP URL 尚需更完整压力验证。
- 公网 RTSP 源不稳定，不能作为唯一验收依据。
- 多路 RTSP 性能不在 Phase 4 承诺范围内，应留给量化性能阶段。
- 断流后当前停留最后一帧，后续可增强为离线状态显示。
- 不做自动重连，断流恢复后由用户重新添加或后续阶段设计重连策略。

## 最终结论

Phase 4 达成预期目标：

- RTSP 网络流已正式接入。
- 网络流复用了既有架构，没有新增专用播放管线。
- 单路 RTSP 播放和基础混合预览链路成立。
- 网络异常和关闭链路已完成关键修复与人工验收。
- RTMP / HTTP-FLV / MJPEG 已明确后续边界，当前不纳入稳定能力。

下一阶段可以转入 README 更新、阶段提交，或继续推进真实摄像头到货后的补充验收。
