# Phase 4 网络流输入预研报告

## 预研结论

Phase 4 可以启动，建议第一阶段只做 `RTSP MVP`。

当前项目已经具备进入网络流输入的前置条件：

- `IMediaSource` 已经抽象出文件源、摄像头源和实时源语义。
- `MediaPipeline` 已经能接收任意 `IMediaSource`，并保持单路播放管线职责。
- `MultiStreamManager` 已经能管理多路 `MediaPipeline` 生命周期。
- `MultiViewRenderer` 已经支持统一多路绘制。
- Phase 3.15 已经完成 9/16 路真实播放扩容，网络流接入后可以复用同一套多路预览体系。

推荐实现顺序：

```text
Phase 4.1 预研和任务拆分
Phase 4.2 NetworkMediaSource 空壳与参数模型
Phase 4.3 RTSP 单路最小可播放 MVP
Phase 4.4 RTSP 接入多路预览
Phase 4.5 网络异常、超时和关闭链路
Phase 4.6 RTMP / HTTP-FLV / MJPEG 预留或扩展
Phase 4.7 Phase 4 验收报告
```

RTSP 优先的原因：

- 最贴近监控摄像头、网口摄像头、NVR 和工业现场预览场景。
- FFmpeg 原生支持，能复用当前 demux / decode / render 链路。
- 与后续真实网络摄像头验证关联最强。
- RTMP 更偏推流发布场景，HTTP-FLV / HLS 延迟模型不同，不适合作为第一步。

## 协议边界

### RTSP

RTSP 是 Phase 4 的 MVP 目标。

适用场景：

- IP Camera。
- NVR。
- 常见安防摄像头。
- 支持 RTSP 输出的网口摄像头。

预期 URL：

```text
rtsp://user:password@host:554/path
```

FFmpeg 关键参数：

```text
rtsp_transport=tcp
stimeout / timeout
buffer_size
max_delay
fflags=nobuffer
flags=low_delay
```

第一版建议默认使用 TCP。UDP 虽然延迟可能更低，但丢包、NAT 和防火墙问题更多，不适合 MVP。

### RTMP

RTMP 暂不作为第一阶段目标。

适用场景：

- OBS / FFmpeg 推流。
- 直播服务器拉流。
- 本地 SRS / MediaMTX 回归测试。

风险：

- 公开长期可用 RTMP 测试源不稳定。
- RTMP 更常见的是推流到服务器，再由播放器拉流。
- 和网口摄像头场景相关性弱于 RTSP。

### HTTP-FLV / MJPEG / HLS

这些协议建议作为后续扩展。

- HTTP-FLV：可做低延迟直播拉流，但不是大多数摄像头的首选协议。
- MJPEG：部分摄像头支持，实现简单但带宽高、编码效率低。
- HLS：兼容性好但延迟高，不适合作为实时预览 MVP。

### GigE Vision / 厂商 SDK

工业 GigE 相机不应直接塞进普通 `NetworkMediaSource`。

如果设备使用海康 MVS、GenICam、GigE Vision 等协议，后续应单独设计：

```text
IndustrialCameraMediaSource
```

它和 RTSP 摄像头都通过网口传输，但抽象边界不同：

- RTSP 是 FFmpeg 可直接打开的媒体流。
- GigE Vision / 厂商 SDK 通常是采集 SDK 输出图像帧，不一定是标准封装媒体流。

## 架构设计

### 媒体源层

新增 `NetworkMediaSource`，实现 `IMediaSource`：

```cpp
class NetworkMediaSource : public IMediaSource {
public:
    bool Open() override;
    bool Close() override;
    AVFormatContext* GetFormatContect() override;
    MediaSourceType GetType() override;
    void SetInfo(QString url) override;
    Fileinfo& GetSourceInfo() override;
    QString LastError() const override;
    QString SourceIdentity() const override;
    bool AllowsDuplicateOpen() const override;
    bool IsSeekable() const override;
    bool HasDuration() const override;
    bool IsRealtime() const override;

    void SetOpenOptions(const NetworkOpenOptions& options);
};
```

建议新增参数结构：

```cpp
enum class NetworkStreamProtocol {
    Rtsp,
    Rtmp,
    HttpFlv,
    Mjpeg,
    Auto
};

struct NetworkOpenOptions {
    QString url;
    NetworkStreamProtocol protocol = NetworkStreamProtocol::Auto;
    QString rtspTransport = "tcp";
    int openTimeoutMs = 5000;
    int readTimeoutMs = 5000;
    int bufferSizeBytes = 1024 * 1024;
    int maxDelayUs = 500000;
    bool lowLatency = true;
};
```

第一版只需要真正支持 RTSP，其他协议先保留枚举和 TODO，不要假装已经支持。

### 播放管线层

`NetworkMediaSource` 应复用现有：

```text
NetworkMediaSource
    -> demuxer
        -> decoder
            -> AudioPlayer / VideoFrameScheduler
                -> MultiViewRenderer
```

不新增网络流专用 `MediaPipeline`。

原因：

- 网络流打开后仍然是 `AVFormatContext`。
- demuxer 已经基于 `IMediaSource` 获取 format context。
- 实时源能力已经由 `IsSeekable()`、`HasDuration()`、`IsRealtime()` 描述。
- 摄像头源已经证明实时源可以复用现有管线。

### UI / Controller 层

第一版 UI 不应复杂化。

建议 MVP 入口：

- 一个 `Add Network` 按钮或菜单。
- 一个简单输入对话框：URL + 协议 + TCP/UDP。
- Controller 负责构造 `NetworkMediaSource` 并调用现有 `AddStream()`。

不建议第一版做复杂网络参数面板。超时、缓冲、低延迟参数先由默认值承载，后续再暴露。

## 实时源语义

网络流默认按实时源处理：

```text
IsSeekable() = false
HasDuration() = false
IsRealtime() = true
```

影响：

- UI 进度条显示 `LIVE`。
- 禁用 seek。
- Stop 不执行 seek 回零。
- 自然 EOF 不应当按文件结束体验处理，应进入断流或错误处理。

注意：部分 HTTP 文件或 VOD RTSP 流可能有 duration，也可能支持 seek。但 Phase 4 MVP 不处理这类特殊情况，先按实时流统一收口。

## 异常与生命周期

网络流比文件源更容易出现运行期异常：

- URL 错误。
- 认证失败。
- 连接超时。
- 服务器断开。
- 网络抖动。
- 连续 `av_read_frame()` 错误。
- 程序关闭时阻塞在 FFmpeg 网络读取。

第一版必须优先保证：

- 打开失败能返回 `LastError()`。
- 运行中读流失败能通过 `DemuxErrorOccurred()` 上报。
- Stop / Remove / Close 不阻塞。
- 网络流断开后不会访问已释放资源。

现有 `demuxer` 已经有连续读错误阈值和 `DemuxErrorOccurred()`，Phase 4 可以先复用这条链路。但需要特别关注 FFmpeg 网络超时设置，否则关闭时可能卡在阻塞读。

## 测试策略

### 公网 RTSP 冒烟测试

用于验证 `NetworkMediaSource` 能否打开标准 RTSP URL。

验收目的：

- 能打开 URL。
- 能发现视频流。
- 能进入现有解码和渲染链路。
- Stop / Remove / Close 不阻塞。

### 本地 MediaMTX / SRS 回归测试

用于稳定复现测试环境。

建议：

- MediaMTX：同时覆盖 RTSP / RTMP。
- SRS：用于 RTMP 后续验证。
- 使用 FFmpeg 把本地 mp4 推成 RTSP 或 RTMP。

本地回归比公网流更重要，因为公网流可能变化、不可控、延迟大。

### 真实摄像头验证

等有实际设备后再做。

RTSP 网口摄像头验证项：

- 正确 URL。
- 用户名密码认证。
- 主码流 / 子码流。
- TCP / UDP。
- 单路播放。
- 文件 + RTSP 混合多路播放。
- 关闭程序不阻塞。

## MVP 验收标准

Phase 4 的最小可验收目标：

- 能通过 URL 添加 1 路 RTSP。
- RTSP 能进入 `MediaPipeline` 并显示画面。
- RTSP 无音频时保持静音，不破坏 active audio 策略。
- RTSP 有音频时仍然最多一路音频输出。
- RTSP 显示为实时源，不支持 seek。
- RTSP 可以 Stop、Remove、Close，不阻塞。
- 文件源、摄像头源、RTSP 源可以共存于多路预览。

非 MVP 范围：

- 不做断线自动重连。
- 不做复杂延迟控制 UI。
- 不做 RTMP 正式能力。
- 不做 HTTP-FLV / HLS 正式能力。
- 不做 ONVIF 设备发现。
- 不做工业 GigE 相机 SDK。

## 对简历项目的价值

Phase 4 完成 RTSP MVP 后，项目可以从“多路文件/摄像头预览”升级为“多路视频源预览平台”。

可展示点：

- 基于 `IMediaSource` 扩展网络媒体源，体现依赖倒置和开放封闭。
- 复用 `MediaPipeline`，没有为网络流复制播放管线。
- 通过 `IsRealtime()` 统一处理摄像头和网络流实时源语义。
- 支持文件、摄像头、RTSP 混合多路预览。
- 对网络超时、断流、关闭阻塞有明确设计边界。

简历表述可以升级为：

```text
基于 Qt6 + FFmpeg + OpenGL 实现多路视频预览平台，支持本地文件、USB 摄像头和 RTSP 网络流统一接入，基于 IMediaSource 抽象复用同一套解复用、解码、同步和多视口渲染管线。
```

## 风险与决策点

- RTSP 流关闭阻塞是最高风险，必须在 `NetworkMediaSource` 打开时设置合理超时。
- RTSP over TCP 作为 MVP 默认策略，UDP 后续再补。
- RTMP 不作为第一阶段承诺能力。
- 工业 GigE 相机不归入普通 `NetworkMediaSource`。
- 多路 RTSP 性能验收应放到后续量化阶段，不阻塞单路 RTSP MVP。

## 结论

Phase 4 应从 RTSP MVP 开始。

新增模块应围绕 `NetworkMediaSource` 展开，播放链路继续复用现有 `MediaPipeline`，不要引入网络流专用管线。这样符合当前架构指导，也能最小化对文件播放、摄像头播放和多路渲染的影响。
