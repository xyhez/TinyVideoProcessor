# Phase 4 RTMP / HTTP-FLV / MJPEG 扩展评估报告

## 评估目标

Phase 4 已经完成 RTSP MVP、异常关闭链路和本地 MediaMTX 验收。本报告用于判断是否继续把 RTMP、HTTP-FLV、MJPEG 纳入当前阶段正式能力。

本轮只做文档评估，不修改播放代码。

## 当前结论

当前阶段不建议继续实现 RTMP、HTTP-FLV、MJPEG。

正式能力边界建议保持为：

```text
文件源 + USB 摄像头源 + RTSP 网络流
```

原因：

- RTSP 已覆盖网口摄像头、NVR、安防预览等核心场景。
- RTMP / HTTP-FLV 更偏直播平台或转发服务，不是摄像头接入主路径。
- MJPEG 有一定摄像头兼容价值，但带宽高、性能差，不适合作为当前多路预览主能力。
- Phase 4 继续扩展协议会拉长测试矩阵，降低当前稳定版本的确定性。

## 协议评估

### RTMP

**典型场景**

- OBS / FFmpeg 推流到 SRS、MediaMTX、Nginx-RTMP。
- 播放直播服务器上的 RTMP 流。
- 旧直播系统兼容。

**优势**

- FFmpeg 原生支持。
- 本地可以通过 SRS 或 MediaMTX 搭建测试环境。
- 和“网络流输入”概念相关。

**问题**

- 与摄像头、NVR、工业现场预览关系弱于 RTSP。
- 很多 RTMP 场景是推流发布，不是终端播放器主动接入摄像头。
- 公网可长期使用的 RTMP 测试源不稳定。
- 如果正式支持，需要补协议选择、URL 校验、测试服务、断流验收和 README 能力说明。

**结论**

不进入当前 Phase 4 正式实现。保留枚举和后续扩展空间即可。

### HTTP-FLV

**典型场景**

- Web 直播低延迟拉流。
- SRS / flv.js 生态。
- HTTP 方式穿透代理和防火墙。

**优势**

- 延迟通常低于 HLS。
- HTTP 传输部署简单。
- FFmpeg 可以打开 HTTP-FLV URL。

**问题**

- 摄像头和 NVR 默认输出 HTTP-FLV 的情况少于 RTSP。
- 与当前 Qt 桌面多路预览场景关联有限。
- 需要处理 HTTP 状态码、重连、长连接断开、缓存和延迟策略。
- 对简历项目而言，收益不如把 RTSP 和多路稳定性讲清楚。

**结论**

暂不实现。只有当后续明确需要接入 SRS / Web 直播源时，再作为独立小阶段实现。

### MJPEG

**典型场景**

- 老式 IP Camera。
- 简单 HTTP 摄像头。
- 部分嵌入式设备输出连续 JPEG。

**优势**

- 解码简单。
- 部分低端或老设备支持。
- 可作为某些摄像头兼容路径。

**问题**

- 带宽高，压缩效率低。
- 多路预览下性能压力明显。
- 音频通常缺失。
- 色彩、帧率、边界断帧处理需要额外验收。
- 不是 Logitech C920 的验证路径，也不是工业 GigE 相机路径。

**结论**

有兼容价值，但不进入当前 Phase 4。后续如果真实设备只提供 MJPEG，再作为“摄像头兼容输入”单独规划。

## 是否需要协议选择 UI

当前不需要。

现阶段只正式支持 RTSP，UI 继续保留 `Add RTSP` 比 `Add Network` 更准确。

如果未来扩展多协议，推荐策略是：

- URL 输入框继续保留。
- 协议默认 `Auto`。
- 高级选项里再提供 `RTSP / RTMP / HTTP-FLV / MJPEG`。
- README 只标注已经人工验收过的协议。

当前如果提前做协议选择 UI，会制造未实现能力的错觉，不符合项目架构指导里的渐进式演进原则。

## 是否需要本地回归环境

当前 RTSP 回归已经可以使用 MediaMTX + FFmpeg。

后续如果做 RTMP，需要新增：

- MediaMTX 或 SRS RTMP 服务。
- FFmpeg RTMP 推流命令。
- RTMP 拉流播放验收。
- 服务端关闭后的断流验收。

后续如果做 HTTP-FLV，更适合使用 SRS：

- SRS 提供 HTTP-FLV 拉流。
- FFmpeg 推 RTMP 到 SRS，再用 HTTP-FLV 拉流。

当前 Phase 4 不需要新增 SRS 依赖。

## 对架构的影响

如果未来实现 RTMP / HTTP-FLV / MJPEG，仍应沿用当前边界：

```text
NetworkMediaSource
    -> IMediaSource
        -> MediaPipeline
            -> demuxer
            -> decoder
            -> VideoFrameScheduler
            -> MultiViewRenderer
```

不应新增协议专用 `MediaPipeline`，也不应让 `demuxer` 感知具体协议。

协议差异应收敛在：

- `NetworkStreamProtocol`
- `NetworkOpenOptions`
- `NetworkMediaSource::IsProtocolSupported()`
- `NetworkMediaSource::ConfigureOpenOptions()`
- URL 校验与错误提示

## README 能力边界建议

README 当前不应写：

```text
支持 RTMP / HTTP-FLV / MJPEG
```

建议写：

```text
支持 RTSP 网络流接入，并预留 RTMP / HTTP-FLV / MJPEG 扩展枚举。
```

或者更严格：

```text
网络流当前正式支持 RTSP，RTMP / HTTP-FLV / MJPEG 暂未作为稳定能力开放。
```

## 决策结果

- RTMP：暂不实现，保留后续扩展。
- HTTP-FLV：暂不实现，等待明确直播源需求。
- MJPEG：暂不实现，等待真实设备兼容需求。
- 协议选择 UI：暂不实现，保留 `Add RTSP`。
- 本地回归环境：继续使用 MediaMTX + FFmpeg；暂不引入 SRS。
- README：只承诺 RTSP。

## 下一步建议

进入 Phase 4.7，编写网络流支持验收报告，收口 Phase 4。

Phase 4.7 应明确：

- 当前正式支持 RTSP。
- RTSP 已通过本地 MediaMTX 验收。
- 异常关闭链路已接入 FFmpeg 中断回调。
- RTMP / HTTP-FLV / MJPEG 作为后续扩展，不纳入当前稳定能力。
- 工业 GigE 相机仍应走独立 `IndustrialCameraMediaSource` 或厂商 SDK 路线。
