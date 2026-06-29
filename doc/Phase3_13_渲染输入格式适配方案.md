# Phase 3.13 渲染输入格式适配方案

## 1. 背景

Phase 3.13 的目标是解决统一渲染路径中的输入格式和显示质量问题。当前主显示链路已经稳定为：

```text
MediaPipeline
    -> VideoFrameScheduler
        -> IVideoFrameSink
            -> SingleStreamFrameSinkAdapter
                -> MultiViewRenderer
```

`MultiViewRenderer` 当前只直接绘制 `AV_PIX_FMT_YUV420P`。近期硬解 4K MKV 问题已经确认并修复：硬解 transfer 出来的软件帧可能是 `yuv420p10le`，旧代码误按 `NV12` 输入给 `sws_scale()`，导致颜色和画面解释错误。

这个问题暴露出一个更大的边界问题：格式转换逻辑现在散在 `decoder` 和 renderer 诊断路径附近，需要明确长期归属。

## 2. 设计目标

本方案要解决的问题不是“某个文件怎么播”，而是建立稳定的可显示帧边界：

- 解码器可以输出不同像素格式。
- 调度器只负责取帧、同步判断、丢帧和提交，不关心像素格式转换。
- sink / renderer 只消费符合当前绘制约束的可显示帧。
- 格式转换必须集中，不在 UI、调度器和多路管理层散落。
- 转换前后 `AVFramePtr` 所有权必须清晰。

## 3. 必须保持的架构边界

- 不把 `StreamId` 放进 `MediaPipeline`。
- 不把 `StreamId` 放进 `VideoFrameScheduler`。
- 不让 `MultiViewRenderer` 访问 `VFrame_` 或 `SyncManager`。
- 不让 `MultiViewRenderer` 做取帧、同步判断、丢帧或队列消费。
- 不在 `MainWindow` 做格式判断或格式转换。
- 不为单个 4K MKV 文件写特殊分支。

## 4. 三种方案对比

### 4.1 方案 A：decoder 统一输出 YUV420P

做法：

```text
decoder
    -> 所有视频帧统一转成 YUV420P
    -> frame queue
    -> VideoFrameScheduler
    -> IVideoFrameSink
    -> MultiViewRenderer
```

优点：

- 改动小，当前代码已经接近这个方向。
- renderer 继续只处理 `YUV420P`，实现简单。
- `VideoFrameScheduler` 不受影响。

缺点：

- `decoder` 会知道 renderer 的输入约束，职责偏重。
- 后续如果 renderer 支持 `NV12` / `P010` / RGB，decoder 还要跟着改。
- 软解和硬解转换逻辑容易重复。
- 色彩空间、range、HDR 降级策略会混入 decoder。

结论：

可以作为当前过渡状态，但不建议作为长期架构终点。

### 4.2 方案 B：sink 前建立 `VideoFrameConverter`

做法：

```text
VideoFrameScheduler
    -> IVideoFrameSink
        -> VideoFrameConverter
            -> MultiViewRenderer 可绘制格式
```

其中 `VideoFrameConverter` 属于基础设施层，职责是把软件内存帧转换为当前显示后端可消费的格式。

优点：

- 格式转换集中，避免散在 decoder、renderer、UI。
- `VideoFrameScheduler` 不知道格式转换。
- `MultiViewRenderer` 可以继续先只绘制 `YUV420P`。
- 后续 renderer 支持更多格式时，只需要调整 converter / renderer 输入契约。
- `VideoRender` fallback 和 `MultiViewRenderer` 可以复用同一转换策略。

缺点：

- 需要新增基础设施类和少量接入代码。
- 硬件帧仍需要先在 decoder 中 transfer 到系统内存，converter 不应直接处理硬件上下文。
- 第一版需要定义缓存策略，避免每帧重复创建 `SwsContext`。

结论：

推荐作为 Phase 3.13 的目标方案。

### 4.3 方案 C：renderer 内部支持多格式

做法：

```text
MultiViewRenderer
    -> 按 pix_fmt 选择纹理布局和 shader
    -> NV12 shader / YUV420P shader / P010 shader / RGB shader ...
```

优点：

- 理论上性能潜力更高，部分格式可避免 CPU 转换。
- 长期可以支持更完整的 GPU 渲染路径。

缺点：

- OpenGL shader 和纹理布局复杂度明显上升。
- 现在还没有多格式 shader 管理能力。
- `P010`、HDR、BT.2020、range、matrix 等问题会同时涌入 renderer。
- 对当前简历项目阶段而言，容易把 Phase 3.13 变成不可控的大改。

结论：

不作为第一版方案。可以作为后续性能优化或高阶显示质量专题。

## 5. 推荐方案

推荐采用方案 B：新增基础设施层 `VideoFrameConverter`，建立“可显示帧格式边界”。

第一版目标链路：

```text
decoder
    -> 输出软件内存帧
    -> frame queue
    -> VideoFrameScheduler
    -> IVideoFrameSink
    -> VideoFrameConverter
    -> YUV420P AVFramePtr
    -> MultiViewRenderer
```

注意：硬件帧 transfer 仍属于 decoder 职责。

原因：

- 硬件帧依赖 decoder 的硬件上下文。
- `VideoFrameConverter` 第一版只处理软件内存帧。
- renderer 不应该知道硬件 decoder 的上下文和 transfer 细节。

## 6. 第一版输出格式

第一版统一输出格式选择：

```cpp
AV_PIX_FMT_YUV420P
```

理由：

- 当前 `MultiViewRenderer` 和 `VideoRender` 已经能绘制 `YUV420P`。
- 已有 shader 和三平面纹理上传逻辑可以复用。
- 能覆盖普通 H.264/H.265 SDR 视频的主要显示需求。
- 相比 RGB，保留当前渲染路径改动更小。

不选择 RGB 作为第一版目标：

- 当前 renderer 没有 RGB shader / 上传路径。
- RGB 会增加内存带宽压力。
- 多路播放场景下 CPU 转 RGB 可能更重。

## 7. 格式处理策略

### 7.1 `YUV420P`

策略：

- 原样通过。
- 不复制帧数据。
- 保持 `AVFramePtr` 所有权移动语义。

### 7.2 `YUVJ420P`

策略：

- 第一版转换到 `YUV420P`。
- 保留 `color_range` 元数据。
- 后续在 3.13.4 中处理 full range / limited range 显示差异。

风险：

- 只改 pix_fmt 而不处理 range 会导致发灰或过曝。
- 因此 3.13.3 只解决“能稳定进入可绘制格式”，3.13.4 再处理色彩准确性。

### 7.3 `NV12`

策略：

- 使用 `sws_scale()` 转换到 `YUV420P`。
- 硬解 transfer 后如果得到 `NV12`，也走同一转换逻辑。

注意：

- 不能再固定假定硬解 transfer 后一定是 `NV12`。
- 必须使用 `AVFrame::format` 作为 sws 输入格式。

### 7.4 `YUV422P`

策略：

- 第一版使用 `sws_scale()` 转换到 `YUV420P`。
- 输出日志记录降采样行为。

风险：

- 色度从 4:2:2 降到 4:2:0，会损失部分色度信息。
- 对播放器第一版可接受，但报告中需要说明。

### 7.5 `P010` / 10-bit planar

策略：

- 第一版使用 `sws_scale()` 转换到 8-bit `YUV420P`。
- 输出一次性日志，说明发生 10-bit 到 8-bit 降级。

风险：

- HDR / 10-bit 精度会损失。
- 如果源是 HDR / BT.2020 / PQ，第一版不承诺完整 tone mapping。
- 这类问题应在后续色彩和 HDR 专题处理。

### 7.6 RGB / BGR

策略：

- 第一版可以通过 `sws_scale()` 转到 `YUV420P`。
- 如果转换失败，丢弃该帧并输出明确日志。

### 7.7 硬件帧格式

策略：

- decoder 负责 `av_hwframe_transfer_data()`。
- transfer 后得到的软件帧再进入格式转换。
- `VideoFrameConverter` 不直接接收硬件帧。

## 8. 所有权语义

`VideoFrameConverter` 建议接口：

```cpp
class VideoFrameConverter {
public:
    AVFramePtr ConvertToDisplayFrame(AVFramePtr frame);
};
```

所有权规则：

- 输入 `AVFramePtr frame` 表示 converter 接管原帧所有权。
- 如果无需转换，直接返回原 `frame`。
- 如果需要转换，创建新的 `AVFramePtr convertedFrame`。
- 转换成功时返回 `convertedFrame`，原帧自动释放。
- 转换失败时返回空指针，原帧自动释放或由局部 RAII 清理。

禁止规则：

- 不使用裸 `AVFrame*` 表达转换结果所有权。
- 不使用 `release()` 逃逸所有权。
- 不把转换后帧交给调用方手动 `av_frame_free()`。

## 9. 色彩和元数据策略

转换后必须保留：

- `pts`
- `pkt_dts`
- `time_base`
- `best_effort_timestamp`
- `duration`
- `sample_aspect_ratio`
- `color_range`
- `colorspace`
- `color_primaries`
- `color_trc`

实现上优先使用：

```cpp
av_frame_copy_props(convertedFrame.get(), sourceFrame.get());
```

然后再检查 `time_base` 是否有效，必要时使用流 time_base 补齐。

## 10. 转换失败策略

转换失败时：

- 返回空 `AVFramePtr`。
- 输出日志，包含输入 `pix_fmt`、尺寸、linesize、色彩信息。
- 当前帧丢弃。
- 不停止播放管线。
- 不影响下一帧继续尝试。

日志频率：

- 同一种失败签名只低频输出或输出一次。
- 避免每帧刷屏。

## 11. 3.13.3 建议实施顺序

第一步：

- 新增 `src/infrastructure/videoframeconverter.h`
- 新增 `src/infrastructure/videoframeconverter.cpp`
- 实现 `ConvertToDisplayFrame(AVFramePtr frame)`
- 支持 `YUV420P` 原样通过。
- 支持常见软件帧转 `YUV420P`。

第二步：

- 收口 `decoder` 中软解和硬解重复的 `sws_scale()` 逻辑。
- decoder 仍负责硬件帧 transfer。
- transfer 后的软件帧交给 converter 做显示格式归一化。

第三步：

- 将 converter 接入统一显示路径。
- 推荐优先接在 `MultiViewRenderer::PresentFrame()` 或其内部提交当前帧前。
- 保持 `VideoFrameScheduler` 和 `MainWindow` 不变。

第四步：

- `MultiViewRenderer` 继续只绘制 `YUV420P`。
- 不在 3.13.3 引入多 shader。

## 12. 不做范围

- 不在 3.13.3 做 HDR tone mapping。
- 不在 3.13.3 支持 BT.2020 / PQ 完整还原。
- 不在 3.13.3 做 GPU 多格式 shader。
- 不在 3.13.3 做 PBO / 异步上传性能优化。
- 不在 3.13.3 扩展 9/16 路真实播放容量。

## 13. 验收标准

方案层面验收：

- 能说明格式转换不属于 UI。
- 能说明为什么不进入 `VideoFrameScheduler`。
- 能说明硬件 transfer 和软件格式转换的边界。
- 能说明转换前后 `AVFramePtr` 所有权。
- 能说明第一版支持和降级策略。

实现层面后续验收：

- 普通 `YUV420P` 文件不退化。
- 硬解 4K MKV 不再误按 `NV12` 处理。
- `NV12` / `YUVJ420P` / `YUV422P` / `P010` 有明确转换或降级日志。
- 不支持格式不会导致崩溃。

## 14. 结论

Phase 3.13 推荐建立 `VideoFrameConverter` 作为可显示帧格式边界。

短期目标不是让 `MultiViewRenderer` 支持所有像素格式，而是让进入 renderer 的帧满足明确契约：

```text
软件内存帧 -> VideoFrameConverter -> YUV420P 可显示帧 -> MultiViewRenderer
```

这样能保持当前架构边界稳定，同时为后续色彩空间处理、HDR 降级说明和性能优化留出清晰位置。
